/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "InkSettingsDialog.h"
#include "SettingsDialog.h"          // applyPrinterSettings()
#include "CurveEditor.h"
#include "dwrip/JobConfig.h"
#include "PrinterProfile.h"
#include "dwrip/DwripRun.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QButtonGroup>
#include <QComboBox>
#include <QGroupBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QSettings>
#include <QSignalBlocker>
#include <QStringList>
#include <QDir>

namespace {
constexpr int kAllCurveButtonId = 4;
}

InkSettingsPage::InkSettingsPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // Two columns side by side so the dialog stays short (fits on screen):
    //   left  = trim + registration,  right = correction curves.
    auto *cols = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    auto *rightCol = new QVBoxLayout;
    cols->addLayout(left);
    cols->addLayout(rightCol);
    cols->setAlignment(Qt::AlignTop);
    root->addLayout(cols);

    auto *box = new QGroupBox("Per-channel ink trim (calibration)", this);
    auto *form = new QFormLayout(box);

    auto makeSpin = [box]() {
        auto *s = new QSpinBox(box);
        s->setRange(-90, 90);   // engine clamps factor to [0,2] (delta ±100)
        s->setSuffix(" %");
        return s;
    };
    m_c = makeSpin();
    m_m = makeSpin();
    m_y = makeSpin();
    m_k = makeSpin();

    // Restore saved deltas.
    m_c->setValue(printerValue("trimC", 0).toInt());
    m_m->setValue(printerValue("trimM", 0).toInt());
    m_y->setValue(printerValue("trimY", 0).toInt());
    m_k->setValue(printerValue("trimK", 0).toInt());

    form->addRow("Cyan:", m_c);
    form->addRow("Magenta:", m_m);
    form->addRow("Yellow:", m_y);
    form->addRow("Black:", m_k);
    left->addWidget(box);

    auto *hint = new QLabel(
        "− lowers a channel's density (down to −90%), + raises it. Applied per\n"
        "channel BEFORE the total-ink limit (−l), so it corrects hue/over-ink that\n"
        "−l cannot. Calibrate by eye: adjust, print the chart, compare, repeat.", this);
    hint->setStyleSheet("color:#64748b; font-size:12px;");
    left->addWidget(hint);

    auto *chartBtn = new QPushButton("Test print chart (--chart)…", this);
    chartBtn->setToolTip("Print a per-channel density wedge with the current trim");
    connect(chartBtn, &QPushButton::clicked, this, &InkSettingsPage::testChart);
    left->addWidget(chartBtn);

    // ── Total-ink cap (TAC) — advanced ceiling on C+M+Y+K ──
    auto *tacBox = new QGroupBox("Total ink cap (TAC)", this);
    auto *tacForm = new QFormLayout(tacBox);
    m_tac = new QSpinBox(tacBox);
    m_tac->setRange(0, 100);
    m_tac->setSuffix(" %");
    m_tac->setValue(printerValue("colorLimit", 100).toInt());
    m_tac->setToolTip(
        "Ceiling on total per-pixel C+M+Y+K (100 = off, 70 = cap 280%). Only "
        "pulls back over-inked pixels — a safety cap, NOT a density control "
        "(use Ink % in the dwrip panel for density). Flag -l.");
    tacForm->addRow("Cap:", m_tac);
    left->addWidget(tacBox);

    // ── Correction curves (linearization / grey balance) ──
    auto *curveBox = new QGroupBox("Correction curves (single point)", this);
    auto *cv = new QVBoxLayout(curveBox);

    // Load persisted curve points, migrating legacy 25/50/75 anchors if needed.
    {
        const char chs[4] = {'C', 'M', 'Y', 'K'};
        for (int r = 0; r < 4; r++)
            curvePointFromSettings(chs[r], &m_curvePointPct[r],
                                   &m_curvePointDelta[r]);
    }

    // Channel selector: All / C / M / Y / K, coloured like the inks.
    auto *chRow = new QHBoxLayout;
    m_chGroup = new QButtonGroup(curveBox);
    {
        auto *all = new QPushButton("All", curveBox);
        all->setCheckable(true);
        all->setCursor(Qt::PointingHandCursor);
        all->setStyleSheet(
            "QPushButton { padding:4px 10px; border:1px solid #d8dee7;"
            "  border-radius:6px; background:white; }"
            "QPushButton:checked { border:2px solid #64748b; color:#334155;"
            "  font-weight:bold; background:#f8fafc; }");
        m_chGroup->addButton(all, kAllCurveButtonId);
        chRow->addWidget(all);

        const char *names[4] = {"Cyan", "Magenta", "Yellow", "Black"};
        const char *cols[4] = {"#00A0D2", "#D2199B", "#C9B400", "#222831"};
        for (int i = 0; i < 4; i++) {
            auto *b = new QPushButton(names[i], curveBox);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QString(
                "QPushButton { padding:4px 10px; border:1px solid #d8dee7;"
                "  border-radius:6px; background:white; }"
                "QPushButton:checked { border:2px solid %1; color:%1;"
                "  font-weight:bold; }").arg(cols[i]));
            m_chGroup->addButton(b, i);
            chRow->addWidget(b);
        }
        chRow->addStretch(1);
        auto *resetBtn = new QPushButton("Reset", curveBox);
        resetBtn->setToolTip("Reset the selected curve to zero. In All mode, "
                             "resets C/M/Y/K together.");
        resetBtn->setCursor(Qt::PointingHandCursor);
        connect(resetBtn, &QPushButton::clicked, this, [this]() {
            setCurvePointForSelection(50, 0);
            m_curveEd->setPoint(50, 0);
            if (m_curveTone && m_curveDelta) {
                const QSignalBlocker toneBlock(m_curveTone);
                const QSignalBlocker deltaBlock(m_curveDelta);
                m_curveTone->setValue(50);
                m_curveDelta->setValue(0);
            }
        });
        chRow->addWidget(resetBtn);
    }
    cv->addLayout(chRow);

    // The drag-point curve editor (selected channel, or all channels together).
    m_curveEd = new CurveEditor(curveBox);
    cv->addWidget(m_curveEd);

    auto *curveInputRow = new QHBoxLayout;
    curveInputRow->addWidget(new QLabel("Tone:", curveBox));
    m_curveTone = new QSpinBox(curveBox);
    m_curveTone->setRange(1, 99);
    m_curveTone->setSuffix(" %");
    m_curveTone->setToolTip("Tone position of the curve point");
    curveInputRow->addWidget(m_curveTone);
    curveInputRow->addSpacing(12);
    curveInputRow->addWidget(new QLabel("Delta:", curveBox));
    m_curveDelta = new QSpinBox(curveBox);
    m_curveDelta->setRange(-90, 90);
    m_curveDelta->setSuffix(" %");
    m_curveDelta->setToolTip("Signed density correction at the selected tone");
    curveInputRow->addWidget(m_curveDelta);
    curveInputRow->addStretch(1);
    cv->addLayout(curveInputRow);

    connect(m_chGroup, &QButtonGroup::idClicked, this, [this](int id) {
        selectCurveChannel(id == kAllCurveButtonId ? -1 : id);
    });
    connect(m_curveEd, &CurveEditor::changed, this, [this]() {
        int tone = 50, delta = 0;
        m_curveEd->point(&tone, &delta);
        setCurvePointForSelection(tone, delta);
        const QSignalBlocker toneBlock(m_curveTone);
        const QSignalBlocker deltaBlock(m_curveDelta);
        m_curveTone->setValue(tone);
        m_curveDelta->setValue(delta);
    });
    connect(m_curveTone, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
        setCurvePointForSelection(v, m_curveDelta->value());
        m_curveEd->setPoint(v, m_curveDelta->value());
    });
    connect(m_curveDelta, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
        setCurvePointForSelection(m_curveTone->value(), v);
        m_curveEd->setPoint(m_curveTone->value(), v);
    });
    if (auto *b0 = m_chGroup->button(0)) b0->setChecked(true);
    selectCurveChannel(0);
    rightCol->addWidget(curveBox);

    auto *curveHint = new QLabel(
        "Drag the point left/right to choose the tone, up/down to bend density.\n"
        "All edits C/M/Y/K together; channel buttons edit one ink only.\n"
        "The surrounding tones follow smoothly; endpoints stay fixed.\n"
        "Print the grey-balance chart and adjust C/M/Y until the CMY grey looks\n"
        "as neutral as the K grey.", this);
    curveHint->setStyleSheet("color:#64748b; font-size:12px;");
    rightCol->addWidget(curveHint);

    auto *grayBtn = new QPushButton("Test print gray balance (--chart-gray)…", this);
    grayBtn->setToolTip("Print K-only vs C+M+Y grey patches with the current curves");
    connect(grayBtn, &QPushButton::clicked, this, &InkSettingsPage::testGrayChart);
    rightCol->addWidget(grayBtn);

    // ── Dot dispersion: the only remaining halftone choice ──
    auto *matchBox = new QGroupBox("Dot pattern (photos)", this);
    auto *mForm = new QFormLayout(matchBox);
    m_structPat = new QComboBox(matchBox);
    m_structPat->addItems(structPatternLabels());
    m_structPat->setToolTip("Dot dispersion, ordered best-first by print test. "
                            "Jarvis wide kernel = smoothest and shows no grid; "
                            "the others are kept for comparison. The old "
                            "mask-based Blue-noise modes all gridded on film and "
                            "are now CLI-only.");
    {
        QSettings s;
        m_structPat->setCurrentIndex(
            structPatternIndex(printerValue("structPattern", "serpjarvis").toString()));
    }
    mForm->addRow("Dot pattern:", m_structPat);
    auto *matchHint = new QLabel(
        "The structured dither (AcroRIP-style medium/large ladder) is always on — "
        "the only choice is how its dots are dispersed.", matchBox);
    matchHint->setStyleSheet("color:#64748b; font-size:12px;");
    matchHint->setWordWrap(true);
    mForm->addRow(matchHint);
    rightCol->addWidget(matchBox);

    rightCol->addStretch(1);

    // ── Layer registration (2/3-zone white↔colour) ──
    auto *regBox = new QGroupBox("Layer registration (2/3-zone)", this);
    auto *regForm = new QFormLayout(regBox);
    m_regV = new QSpinBox(regBox);
    m_regV->setRange(-48, 48);
    m_regV->setSuffix(" /1440\"");
    {
        QSettings s;
        m_regV->setValue(printerValue("regV", 0).toInt());
    }
    regForm->addRow("Colour vs white (V):", m_regV);
    auto *regHint = new QLabel(
        "Shifts the colour layer vertically to align with white in both-mode.\n"
        "0 = off. Use the test print below to find the value:", regBox);
    regHint->setStyleSheet("color:#64748b; font-size:12px;");
    regForm->addRow(regHint);

    auto *regBtn = new QPushButton("Test print registration (--reg-test)…", regBox);
    regBtn->setToolTip("Print a white↔colour alignment ladder; the mark whose "
                       "colour line is centred on the white bar gives the value");
    connect(regBtn, &QPushButton::clicked, this, &InkSettingsPage::testRegistration);
    regForm->addRow(regBtn);
    auto *regUse = new QLabel(
        "Print → find the labelled mark where the colour line sits centred on the "
        "white bar → enter that number above.", regBox);
    regUse->setStyleSheet("color:#64748b; font-size:12px;");
    regUse->setWordWrap(true);
    regForm->addRow(regUse);
    left->addWidget(regBox);
    left->addStretch(1);

}

QString InkSettingsPage::liveTrimSpec() const
{
    QStringList terms;
    const struct { QSpinBox *sp; char ch; } chans[4] = {
        {m_c, 'C'}, {m_m, 'M'}, {m_y, 'Y'}, {m_k, 'K'},
    };
    for (const auto &c : chans)
        if (c.sp->value() != 0)
            terms << QString("%1:%2").arg(c.ch).arg(c.sp->value());
    return terms.join(',');
}

void InkSettingsPage::selectCurveChannel(int ch)
{
    static const QColor inkCols[4] = {
        QColor(0, 160, 210), QColor(210, 25, 155),
        QColor(201, 180, 0), QColor(34, 40, 49)};
    m_curveCh = ch < 0 ? -1 : qBound(0, ch, 3);
    const int viewCh = m_curveCh < 0 ? 0 : m_curveCh;
    m_curveEd->setPoint(m_curvePointPct[viewCh],
                        m_curvePointDelta[viewCh]);
    m_curveEd->setCurveColor(m_curveCh < 0 ? QColor(100, 116, 139)
                                            : inkCols[m_curveCh]);
    if (m_curveTone && m_curveDelta) {
        const QSignalBlocker toneBlock(m_curveTone);
        const QSignalBlocker deltaBlock(m_curveDelta);
        m_curveTone->setValue(m_curvePointPct[viewCh]);
        m_curveDelta->setValue(m_curvePointDelta[viewCh]);
    }
}

void InkSettingsPage::setCurvePointForSelection(int tonePct, int deltaPct)
{
    tonePct = qBound(1, tonePct, 99);
    deltaPct = qBound(-90, deltaPct, 90);
    if (m_curveCh < 0) {
        for (int i = 0; i < 4; i++) {
            m_curvePointPct[i] = tonePct;
            m_curvePointDelta[i] = deltaPct;
        }
        return;
    }
    const int ch = qBound(0, m_curveCh, 3);
    m_curvePointPct[ch] = tonePct;
    m_curvePointDelta[ch] = deltaPct;
}

QString InkSettingsPage::liveCurvePointSpec(int row) const
{
    const int x = qBound(1, m_curvePointPct[row], 99);
    const int d = qBound(-90, m_curvePointDelta[row], 90);
    if (d == 0)
        return {};
    return QString("%1,%2").arg(x).arg(d);
}

void InkSettingsPage::applyLiveCurves(JobConfig &cfg) const
{
    cfg.curveC.clear();
    cfg.curveM.clear();
    cfg.curveY.clear();
    cfg.curveK.clear();
    cfg.curvePointC = liveCurvePointSpec(0);
    cfg.curvePointM = liveCurvePointSpec(1);
    cfg.curvePointY = liveCurvePointSpec(2);
    cfg.curvePointK = liveCurvePointSpec(3);
    cfg.structDitherMethod = structPatternKey(m_structPat->currentIndex());
}

void InkSettingsPage::testChart()
{
    // Calibration wedge: Tier-1 device settings (channels/bidi/send/margin) but
    // ICC bypassed (--chart) and the LIVE trim + curves from the spinboxes above.
    JobConfig cfg;
    applyPrinterSettings(cfg);
    cfg.iccPath.clear();
    cfg.intent.clear();
    cfg.chart = true;
    cfg.trim = liveTrimSpec();
    applyLiveCurves(cfg);
    {
        QSettings s;
        cfg.dropMode = printerValue("dropMode", "ML").toString();
    }

    if (cfg.cupsQueue.isEmpty()) {
        const QString p = QFileDialog::getSaveFileName(
            this, "Save chart PRN", QDir::home().filePath("dwrip_chart.prn"),
            "PRN (*.prn)");
        if (p.isEmpty())
            return;
        cfg.outputPath = p;
    } else {
        cfg.outputPath = QDir::temp().filePath("dwrip_chart.prn");
    }

    runDwripWithLog(this, cfg);
}

void InkSettingsPage::testGrayChart()
{
    // Grey-balance chart: K vs C+M+Y patches, colour-only, ICC bypassed,
    // with the LIVE curves + trim so adjustments show on the next print.
    JobConfig cfg;
    applyPrinterSettings(cfg);
    cfg.iccPath.clear();
    cfg.intent.clear();
    cfg.chartGray = true;
    cfg.trim = liveTrimSpec();
    applyLiveCurves(cfg);
    {
        QSettings s;
        cfg.dropMode = printerValue("dropMode", "ML").toString();
    }

    if (cfg.cupsQueue.isEmpty()) {
        const QString p = QFileDialog::getSaveFileName(
            this, "Save gray-balance PRN", QDir::home().filePath("dwrip_gray.prn"),
            "PRN (*.prn)");
        if (p.isEmpty())
            return;
        cfg.outputPath = p;
    } else {
        cfg.outputPath = QDir::temp().filePath("dwrip_gray.prn");
    }

    runDwripWithLog(this, cfg);
}

void InkSettingsPage::testRegistration()
{
    // Registration ladder: Tier-1 device settings (channels/resolution/white-first
    // etc.) but the pattern is synthesised (--reg-test), ICC/trim irrelevant.
    JobConfig cfg;
    applyPrinterSettings(cfg);
    cfg.iccPath.clear();
    cfg.intent.clear();
    cfg.regTest = true;

    if (cfg.cupsQueue.isEmpty()) {
        const QString p = QFileDialog::getSaveFileName(
            this, "Save registration PRN", QDir::home().filePath("dwrip_regtest.prn"),
            "PRN (*.prn)");
        if (p.isEmpty())
            return;
        cfg.outputPath = p;
    } else {
        cfg.outputPath = QDir::temp().filePath("dwrip_regtest.prn");
    }

    runDwripWithLog(this, cfg);
}

// Pull every control from the ACTIVE profile. Called once at construction and
// again whenever the host switches printers, so the page always shows the
// calibration of the machine currently selected.
void InkSettingsPage::load()
{
    const QSignalBlocker b1(m_curveTone), b2(m_curveDelta);
    m_c->setValue(printerValue("trimC", 0).toInt());
    m_m->setValue(printerValue("trimM", 0).toInt());
    m_y->setValue(printerValue("trimY", 0).toInt());
    m_k->setValue(printerValue("trimK", 0).toInt());
    m_tac->setValue(printerValue("colorLimit", 100).toInt());
    m_structPat->setCurrentIndex(
        structPatternIndex(printerValue("structPattern", "serpjarvis").toString()));
    m_regV->setValue(printerValue("regV", 0).toInt());
    const char chs[4] = {'C', 'M', 'Y', 'K'};
    for (int r = 0; r < 4; r++)
        curvePointFromSettings(chs[r], &m_curvePointPct[r], &m_curvePointDelta[r]);
    selectCurveChannel(m_curveCh);
}

void InkSettingsPage::save()
{
    printerSetValue("trimC", m_c->value());
    printerSetValue("trimM", m_m->value());
    printerSetValue("trimY", m_y->value());
    printerSetValue("trimK", m_k->value());
    {
        QSettings s;
        const char chs[4] = {'C', 'M', 'Y', 'K'};
        static const int anchors[3] = {25, 50, 75};
        for (int r = 0; r < 4; r++) {
            printerSetValue(QString("curve%1PointX").arg(chs[r]),
                            qBound(1, m_curvePointPct[r], 99));
            printerSetValue(QString("curve%1PointDelta").arg(chs[r]),
                            qBound(-90, m_curvePointDelta[r], 90));
            // Drop the legacy 3-anchor keys once they have been migrated to a
            // single control point (both in the profile and in the old group).
            for (int c = 0; c < 3; c++) {
                s.remove(activePrinterGroup() + QString("/curve%1%2").arg(chs[r]).arg(anchors[c]));
                s.remove(QString("dwrip/curve%1%2").arg(chs[r]).arg(anchors[c]));
            }
        }
    }
    printerSetValue("structPattern", structPatternKey(m_structPat->currentIndex()));
    printerSetValue("regV", m_regV->value());
    printerSetValue("colorLimit", m_tac->value());   // total-ink (TAC) cap → -l
}

// ── Stand-alone wrapper ─────────────────────────────────────────────────────
InkSettingsDialog::InkSettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Ink Settings");
    setModal(true);
    auto *root = new QVBoxLayout(this);
    m_page = new InkSettingsPage(this);
    root->addWidget(m_page);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &InkSettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &InkSettingsDialog::reject);
    root->addWidget(buttons);
    root->setSizeConstraint(QLayout::SetFixedSize);
}

void InkSettingsDialog::accept()
{
    m_page->save();
    QDialog::accept();
}
