/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "SettingsDialog.h"
#include "dwrip/JobConfig.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QAbstractItemView>
#include <QRadioButton>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QInputDialog>
#include <QTabWidget>
#include "dwrip/DwripPanel.h"   // fillResolutionCombo / fillDropModeCombo
#include <QLabel>
#include <QStringList>
#include <QFileInfo>
#include <cstdlib>
#include "InkSettingsDialog.h"
#include "dwrip/DwripRun.h"   // locateDwrip()
#include "CupsSpooler.h"
#include "PrinterProfile.h"

QString trimSpecFromSettings()
{
    const struct { const char *key; char ch; } chans[4] = {
        {"trimC", 'C'}, {"trimM", 'M'}, {"trimY", 'Y'}, {"trimK", 'K'},
    };
    QStringList terms;
    for (const auto &c : chans) {
        const int d = printerValue(c.key, 0).toInt();
        if (d != 0)
            terms << QString("%1:%2").arg(c.ch).arg(d);   // %2 keeps the - sign
    }
    return terms.join(',');
}

QString curveSpecFromSettings(char ch)
{
    // curve<CH><anchor> → "d25,d50,d75"; empty when all zero.
    const int d25 = printerValue(QString("curve%1%2").arg(ch).arg(25), 0).toInt();
    const int d50 = printerValue(QString("curve%1%2").arg(ch).arg(50), 0).toInt();
    const int d75 = printerValue(QString("curve%1%2").arg(ch).arg(75), 0).toInt();
    if (d25 == 0 && d50 == 0 && d75 == 0)
        return {};
    return QString("%1,%2,%3").arg(d25).arg(d50).arg(d75);
}

void curvePointFromSettings(char ch, int *tonePct, int *deltaPct)
{
    int x = 50;
    int d = 0;
    const QString xKey = QString("curve%1PointX").arg(ch);
    const QString dKey = QString("curve%1PointDelta").arg(ch);
    if (printerContains(xKey) || printerContains(dKey)) {
        x = printerValue(xKey, 50).toInt();
        d = printerValue(dKey, 0).toInt();
    } else {
        const int anchors[3] = {25, 50, 75};
        int old[3] = {
            printerValue(QString("curve%1%2").arg(ch).arg(25), 0).toInt(),
            printerValue(QString("curve%1%2").arg(ch).arg(50), 0).toInt(),
            printerValue(QString("curve%1%2").arg(ch).arg(75), 0).toInt(),
        };
        int best = 1;
        for (int i = 0; i < 3; i++) {
            if (std::abs(old[i]) > std::abs(old[best]))
                best = i;
        }
        x = anchors[best];
        d = old[best];
    }

    if (tonePct)
        *tonePct = qBound(1, x, 99);
    if (deltaPct)
        *deltaPct = qBound(-90, d, 90);
}

QString curvePointSpecFromSettings(char ch)
{
    int x = 50, d = 0;
    curvePointFromSettings(ch, &x, &d);
    if (d == 0)
        return {};
    return QString("%1,%2").arg(x).arg(d);
}

void applyPrinterSettings(JobConfig &cfg)
{
    // Everything dwrip needs that belongs to the MACHINE, read from the active
    // printer profile. This is deliberately the single reader: the Export
    // dialog re-runs it after a profile switch, so it must cover every printer
    // key — including the four the dwrip panel edits (resolution, dropMode,
    // inkColor, whiteLimit). That is safe because DwripPanel auto-saves on every
    // change, so QSettings is always current.
    cfg.channels = printerValue("channelOrder", cfg.channels).toString();
    cfg.bidi = printerValue("bidi", cfg.bidi).toBool();
    const double margin = printerValue("margin", 0.0).toDouble();
    if (margin > 0) cfg.marginMm = margin;
    cfg.uvTailMm = printerValue("uvTail", 5.0).toDouble();
    const int uvSpeed = printerValue("uvTailSpeed", 1).toInt();
    if (uvSpeed > 1) cfg.uvTailSpeed = uvSpeed;
    const int leadInFeed = printerValue("leadInFeedStep", 45).toInt();
    if (leadInFeed != 45) cfg.leadInFeedStep = leadInFeed;
    cfg.leadInSweep = printerValue("leadInSweep", QStringLiteral("off")).toString();
    cfg.colorLimitPct = printerValue("colorLimit", 100).toInt();  // TAC cap → -l
    const QString icc = printerValue("icc").toString().trimmed();
    if (!icc.isEmpty()) cfg.iccPath = icc;
    cfg.intent = printerValue("intent", "perceptual").toString();
    cfg.cupsQueue = printerValue("cupsQueue").toString().trimmed();
    const int regV = printerValue("regV", 0).toInt();
    if (regV != 0) cfg.regV = regV;

    // Quality — stored per printer, but also editable in the Export dialog.
    cfg.resolution = printerValue("resolution", cfg.resolution).toString();
    cfg.dropMode   = printerValue("dropMode", cfg.dropMode).toString();
    cfg.inkColorPct  = printerValue("inkColor", 100).toInt();
    cfg.whiteLimitPct = printerValue("whiteLimit", 100).toInt();
    cfg.whiteMode = printerValue("whiteMode", "alpha").toString();
    cfg.whiteDarkPct = printerValue("whiteDarkPct", 100).toInt();
    cfg.whiteCurveX = printerValue("whiteCurveX", 50).toInt();
    cfg.whiteCurveDelta = printerValue("whiteCurveDelta", 0).toInt();
    cfg.whiteChokeMm = printerValue("whiteChoke", 0).toInt() / 100.0;

    cfg.trim = trimSpecFromSettings();   // per-channel calibration (--trim)
    cfg.curveC.clear();
    cfg.curveM.clear();
    cfg.curveY.clear();
    cfg.curveK.clear();
    cfg.curvePointC = curvePointSpecFromSettings('C'); // correction curves
    cfg.curvePointM = curvePointSpecFromSettings('M');
    cfg.curvePointY = curvePointSpecFromSettings('Y');
    cfg.curvePointK = curvePointSpecFromSettings('K');

    // Round-trip through the offered list so a stored key that has been retired
    // from the UI (the mask modes) resolves to the mode the combo now shows,
    // instead of silently still being sent to dwrip.
    cfg.structDitherMethod = structPatternKey(
        structPatternIndex(printerValue("structPattern", "serpjarvis").toString()));
}

namespace {
struct StructPattern { const char *key, *label; };
// Only the mask-free error-diffusion modes are offered here, ordered best-first
// by PRINT test (not by theory). The mask/ordered modes (bluenoise, bluenoisehq,
// bluejitter) and plain fs all showed a grid or worms on film and were retired
// from the UI — they remain reachable via `dwrip --struct-dither` for reference
// and regression baselines. See CLAUDE.md §structmode.
// Settings persist the key string (not the index), so this list may be
// reordered freely; a stored key that is no longer listed falls back to index 0.
const StructPattern kStructPat[] = {
    {"serpjarvis",       "Jarvis wide kernel — best (default)"},
    {"serpjarvisrandom", "Jarvis + random — 2nd"},
    {"serprandom",       "Serpentine FS + random — 3rd"},
    {"serpentine",       "Serpentine FS"},
    {"jitter",           "Jitter (old default, grainy)"},
};
const int kStructPatN = int(sizeof(kStructPat) / sizeof(kStructPat[0]));
}  // namespace

QStringList structPatternLabels()
{
    QStringList out;
    for (int i = 0; i < kStructPatN; i++) out << QString::fromUtf8(kStructPat[i].label);
    return out;
}
QString structPatternKey(int i)
{
    return (i >= 0 && i < kStructPatN) ? QString::fromUtf8(kStructPat[i].key)
                                       : QStringLiteral("serpjarvis");
}
int structPatternIndex(const QString &key)
{
    for (int i = 0; i < kStructPatN; i++) if (key == kStructPat[i].key) return i;
    return 0;
}

SettingsDialog::SettingsDialog(const AppSettings &current, QWidget *parent)
    : QDialog(parent), m_curUnit(current.unit)
{
    setWindowTitle("App Settings");
    setModal(true);

    auto *root = new QVBoxLayout(this);

    // ── Kanvas ──
    auto *canvasBox = new QGroupBox("Canvas", this);
    auto *cForm = new QFormLayout(canvasBox);
    m_bedW = new QDoubleSpinBox(canvasBox);
    m_bedH = new QDoubleSpinBox(canvasBox);
    for (QDoubleSpinBox *b : {m_bedW, m_bedH}) {
        b->setRange(1.0, 100000.0);
        b->setDecimals(2);
    }
    m_unit = new QComboBox(canvasBox);
    m_unit->addItem("cm", int(Unit::Cm));
    m_unit->addItem("mm", int(Unit::Mm));
    m_unit->setCurrentIndex(current.unit == Unit::Mm ? 1 : 0);
    cForm->addRow("Width:", m_bedW);
    cForm->addRow("Height:", m_bedH);
    cForm->addRow("Unit:", m_unit);
    auto *canvasHint = new QLabel(
        "Bed size and unit are APPLICATION settings — shared by every printer\n"
        "profile, so switching the target printer never re-flows your layout.", canvasBox);
    canvasHint->setStyleSheet("color:#666;");
    cForm->addRow(canvasHint);

    // Initialise bed fields in the current unit.
    const double um0 = unitMm(current.unit);
    m_bedW->setValue(current.bedWmm / um0);
    m_bedH->setValue(current.bedHmm / um0);
    {
        const QString suf = QString(" ") + unitSuffix(current.unit);
        m_bedW->setSuffix(suf);
        m_bedH->setSuffix(suf);
    }
    connect(m_unit, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onUnitChanged);

    // ── Printer profile: selector (always visible) + tabbed editor ──
    auto *printBox = new QWidget(this);
    auto *pForm = new QFormLayout(printBox);
    pForm->setContentsMargins(0, 0, 0, 0);

    // Printer profile selector: every field below edits the SELECTED profile.
    // Switching here also switches the app-wide active printer, so the dwrip
    // panel and the Export dialog follow along.
    auto *chRow = new QWidget(printBox);
    auto *chLay = new QHBoxLayout(chRow);
    chLay->setContentsMargins(0, 0, 0, 0);
    m_profileCombo = new QComboBox(chRow);
    m_profileCombo->setObjectName("profileCombo");
    m_profileCombo->setMaxVisibleItems(20);
    auto *newBtn = new QPushButton("New…", chRow);
    auto *dupBtn = new QPushButton("Duplicate…", chRow);
    auto *renBtn = new QPushButton("Rename…", chRow);
    auto *delBtn = new QPushButton("Delete", chRow);
    dupBtn->setToolTip("Copy every setting of this profile — including the whole "
                       "density calibration — into a new one. The fastest way to "
                       "set up a second, near-identical machine.");
    chLay->addWidget(m_profileCombo, 1);
    chLay->addWidget(newBtn, 0);
    chLay->addWidget(dupBtn, 0);
    chLay->addWidget(renBtn, 0);
    chLay->addWidget(delBtn, 0);
    connect(newBtn, &QPushButton::clicked, this, &SettingsDialog::onNewProfile);
    connect(dupBtn, &QPushButton::clicked, this, &SettingsDialog::onDuplicateProfile);
    connect(renBtn, &QPushButton::clicked, this, &SettingsDialog::onRenameProfile);
    connect(delBtn, &QPushButton::clicked, this, &SettingsDialog::onDeleteProfile);
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onProfileChanged);

    // Ink slot layout, stored per printer (was a separate "channel profile").
    auto *ordRow = new QWidget(printBox);
    auto *ordLay = new QHBoxLayout(ordRow);
    ordLay->setContentsMargins(0, 0, 0, 0);
    m_orderLabel = new QLabel(ordRow);
    auto *ordBtn = new QPushButton("Edit…", ordRow);
    ordLay->addWidget(m_orderLabel, 1);
    ordLay->addWidget(ordBtn, 0);
    connect(ordBtn, &QPushButton::clicked, this, &SettingsDialog::onEditChannelOrder);

    auto *dirRow = new QWidget(printBox);
    auto *dirLay = new QHBoxLayout(dirRow);
    dirLay->setContentsMargins(0, 0, 0, 0);
    m_bidi = new QRadioButton("Bidirectional", dirRow);
    m_unidi = new QRadioButton("Unidirectional", dirRow);
    dirLay->addWidget(m_bidi);
    dirLay->addWidget(m_unidi);

    m_margin = new QDoubleSpinBox(printBox);
    m_margin->setRange(0, 1000); m_margin->setSuffix(" mm");
    m_uvTail = new QDoubleSpinBox(printBox);
    m_uvTail->setRange(0, 1000); m_uvTail->setSuffix(" mm");
    m_uvSpeed = new QSpinBox(printBox);
    m_uvSpeed->setRange(1, 16);
    m_leadInFeed = new QSpinBox(printBox);
    m_leadInFeed->setRange(0, 1000);
    m_leadInFeed->setSuffix(" units");
    m_leadInFeed->setSpecialValueText("Off");
    m_leadInFeed->setToolTip("Transparent top/canvas feed before the first ink band. Try 15 or 30 for smoother lead-in; 0 disables splitting.");
    m_leadInSweep = new QComboBox(printBox);
    m_leadInSweep->addItem("Off", "off");
    m_leadInSweep->addItem("Short", "short");
    m_leadInSweep->addItem("Full", "full");
    m_leadInSweep->setToolTip("Experimental: Short marks only the left/capping side; Full marks left and right to force a full sweep. Both may leave small edge dots.");

    auto *iccRow = new QWidget(printBox);
    auto *iccLay = new QHBoxLayout(iccRow);
    iccLay->setContentsMargins(0, 0, 0, 0);
    m_icc = new QLineEdit(iccRow);
    m_icc->setPlaceholderText("(no ICC)");
    auto *iccBtn = new QPushButton("…", iccRow);
    iccBtn->setMaximumWidth(32);
    iccLay->addWidget(m_icc, 1);
    iccLay->addWidget(iccBtn, 0);
    connect(iccBtn, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getOpenFileName(this, "Choose ICC profile",
                                                       QString(), "ICC (*.icc *.icm)");
        if (!p.isEmpty()) m_icc->setText(p);
    });

    m_intent = new QComboBox(printBox);
    m_intent->addItems({"perceptual", "relative", "relative-bpc", "saturation", "absolute"});

    // Printer picker: CUPS queue to spool the PRN to (empty = generate file only).
    m_printer = new QComboBox(printBox);
    m_printer->setEditable(true);
    m_printer->setInsertPolicy(QComboBox::NoInsert);
    m_printer->lineEdit()->setPlaceholderText("(none — generate file only)");
    auto *refreshBtn = new QPushButton("Refresh", printBox);
    refreshBtn->setToolTip("Re-scan CUPS printers (lpstat)");
    connect(refreshBtn, &QPushButton::clicked, this, &SettingsDialog::reloadPrinters);
    auto *sendRow = new QHBoxLayout;
    sendRow->setContentsMargins(0, 0, 0, 0);
    sendRow->addWidget(m_printer, 1);
    sendRow->addWidget(refreshBtn);

    // Profile selector sits ABOVE the tabs: it says which machine every tab is
    // editing, so it must never scroll out of view with a tab.
    pForm->addRow("Printer profile:", chRow);
    root->addWidget(printBox);

    m_tabs = new QTabWidget(this);

    // Device — how the job reaches the machine and which ink sits where.
    {
        auto *w = new QWidget(m_tabs);
        auto *f = new QFormLayout(w);
        f->addRow("Printer (CUPS queue):", sendRow);
        f->addRow("Ink channel order:", ordRow);
        f->addRow("Direction:", dirRow);
        m_tabs->addTab(w, "Device");
    }

    // Colour — the ICC profile is only valid for this machine + ink + film.
    {
        auto *w = new QWidget(m_tabs);
        auto *f = new QFormLayout(w);
        f->addRow("ICC profile:", iccRow);
        f->addRow("Rendering intent:", m_intent);
        m_tabs->addTab(w, "Colour");
    }

    // Quality — also reachable from the dwrip panel and the Export dialog;
    // all three write the same per-printer settings.
    {
        auto *w = new QWidget(m_tabs);
        auto *f = new QFormLayout(w);
        m_resolution = new QComboBox(w);
        fillResolutionCombo(m_resolution);
        m_dropMode = new QComboBox(w);
        fillDropModeCombo(m_dropMode);
        m_inkColor = new QSpinBox(w);
        m_inkColor->setRange(0, 200); m_inkColor->setSuffix(" %");
        m_inkColor->setToolTip("Colour ink density — scales all CMYK on every "
                               "pixel (100 = off, flag --ink-color).");
        m_whiteLimit = new QSpinBox(w);
        m_whiteLimit->setRange(0, 100); m_whiteLimit->setSuffix(" %");
        m_whiteLimit->setToolTip("White ink density (flag -L).");
        f->addRow("Resolution:", m_resolution);
        f->addRow("Drop mode:", m_dropMode);
        f->addRow("Colour ink:", m_inkColor);
        f->addRow("White ink:", m_whiteLimit);
        m_tabs->addTab(w, "Quality");
    }

    // Calibration — the expensive part of a profile (trim, TAC, curves, dot
    // pattern, registration), hosted as a page instead of a separate dialog.
    m_inkPage = new InkSettingsPage(m_tabs);
    m_tabs->addTab(m_inkPage, "Calibration");

    // UV & feed — lamp geometry and bed mechanics of this machine.
    {
        auto *w = new QWidget(m_tabs);
        auto *f = new QFormLayout(w);
        f->addRow("Left margin (UV):", m_margin);
        f->addRow("UV-tail:", m_uvTail);
        f->addRow("UV-tail speed:", m_uvSpeed);
        f->addRow("Lead-in feed step:", m_leadInFeed);
        f->addRow("Lead-in sweep:", m_leadInSweep);
        m_tabs->addTab(w, "UV && feed");
    }

    // Application — global, deliberately last so it reads as "not the printer".
    m_tabs->addTab(canvasBox, "Application");

    root->addWidget(m_tabs);

    reloadPrinters();
    reloadProfileCombo();     // selects the active printer
    loadProfileFields();      // fills every tab from it

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("OK");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancel");
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    root->addWidget(buttons);

    // Resizable: with tabs a fixed size would grow to the tallest page and
    // leave the short ones mostly empty.
    resize(720, 470);
}

void SettingsDialog::onUnitChanged()
{
    const Unit newUnit = (m_unit->currentIndex() == 1) ? Unit::Mm : Unit::Cm;
    if (newUnit == m_curUnit)
        return;
    // Convert bed field values so the physical size stays the same.
    const double factor = unitMm(m_curUnit) / unitMm(newUnit);   // cm->mm = 10, mm->cm = 0.1
    m_bedW->setValue(m_bedW->value() * factor);
    m_bedH->setValue(m_bedH->value() * factor);
    const QString suf = QString(" ") + unitSuffix(newUnit);
    m_bedW->setSuffix(suf);
    m_bedH->setSuffix(suf);
    m_curUnit = newUnit;
}

AppSettings SettingsDialog::settings() const
{
    AppSettings s;
    s.unit = m_curUnit;
    const double um = unitMm(m_curUnit);
    s.bedWmm = m_bedW->value() * um;
    s.bedHmm = m_bedH->value() * um;
    return s;
}

void SettingsDialog::applyTo(JobConfig &cfg) const
{
    cfg.channels = m_channelOrder;
    cfg.bidi = m_bidi->isChecked();
    if (m_margin->value() > 0) cfg.marginMm = m_margin->value();
    cfg.uvTailMm = m_uvTail->value();
    if (m_uvSpeed->value() > 1) cfg.uvTailSpeed = m_uvSpeed->value();
    if (m_leadInFeed->value() != 45) cfg.leadInFeedStep = m_leadInFeed->value();
    cfg.leadInSweep = m_leadInSweep->currentData().toString();
    cfg.iccPath = m_icc->text().trimmed();
    cfg.intent = m_intent->currentText();
    cfg.cupsQueue = m_printer->currentText().trimmed();
}

void SettingsDialog::save() const
{
    settings().save();        // canvas group "app/" — bed + unit stay global
    saveProfileFields();      // everything else belongs to the active printer
    QSettings().remove("dwrip/leadInDummySweep");   // retired key
}

void SettingsDialog::reloadPrinters()
{
    const QString cur = m_printer->currentText();
    m_printer->clear();
    m_printer->addItem(QString());               // blank = file only
    m_printer->addItems(cupsListPrinters());
    m_printer->setCurrentText(cur);              // keep prior selection/text
}

QString SettingsDialog::currentProfileId() const
{
    const int i = m_profileCombo->currentIndex();
    return (i >= 0 && i < m_profileIds.size()) ? m_profileIds[i] : QString();
}

void SettingsDialog::reloadProfileCombo()
{
    m_switching = true;   // repopulating fires currentIndexChanged; ignore it
    m_profileCombo->clear();
    m_profileIds.clear();
    const QString active = activePrinterId();
    int sel = 0;
    for (const PrinterMeta &m : printersLoad()) {
        if (m.id == active) sel = m_profileIds.size();
        m_profileIds.append(m.id);
        m_profileCombo->addItem(m.name);
    }
    if (!m_profileIds.isEmpty())
        m_profileCombo->setCurrentIndex(sel);
    m_switching = false;
}

// Fields <-> active profile. Both directions list the same controls; keep them
// symmetric or a setting will silently stop following its printer.
void SettingsDialog::loadProfileFields()
{
    const JobConfig def;
    m_channelOrder = printerValue("channelOrder", def.channels).toString();
    updateOrderLabel();
    const bool bidi = printerValue("bidi", def.bidi).toBool();
    m_bidi->setChecked(bidi);
    m_unidi->setChecked(!bidi);
    m_margin->setValue(printerValue("margin", 0.0).toDouble());
    m_uvTail->setValue(printerValue("uvTail", 5.0).toDouble());
    m_uvSpeed->setValue(printerValue("uvTailSpeed", 1).toInt());
    m_leadInFeed->setValue(printerValue("leadInFeedStep", 45).toInt());
    const int leadIdx = m_leadInSweep->findData(
        printerValue("leadInSweep", QStringLiteral("off")).toString());
    m_leadInSweep->setCurrentIndex(leadIdx >= 0 ? leadIdx : 0);
    m_icc->setText(printerValue("icc", QString()).toString());
    m_intent->setCurrentText(printerValue("intent", "perceptual").toString());
    m_printer->setCurrentText(printerValue("cupsQueue", QString()).toString());
    const int ri = m_resolution->findData(printerValue("resolution", "1440x1440").toString());
    m_resolution->setCurrentIndex(ri >= 0 ? ri : 0);
    const int di = m_dropMode->findData(printerValue("dropMode", "ML").toString());
    m_dropMode->setCurrentIndex(di >= 0 ? di : 1);
    m_inkColor->setValue(printerValue("inkColor", 100).toInt());
    m_whiteLimit->setValue(printerValue("whiteLimit", 100).toInt());
    m_inkPage->load();          // calibration tab follows the printer too
}

void SettingsDialog::saveProfileFields() const
{
    printerSetValue("channelOrder", m_channelOrder);
    printerSetValue("bidi", m_bidi->isChecked());
    printerSetValue("margin", m_margin->value());
    printerSetValue("uvTail", m_uvTail->value());
    printerSetValue("uvTailSpeed", m_uvSpeed->value());
    printerSetValue("leadInFeedStep", m_leadInFeed->value());
    printerSetValue("leadInSweep", m_leadInSweep->currentData().toString());
    printerSetValue("icc", m_icc->text().trimmed());
    printerSetValue("intent", m_intent->currentText());
    printerSetValue("cupsQueue", m_printer->currentText().trimmed());
    printerSetValue("resolution", m_resolution->currentData().toString());
    printerSetValue("dropMode", m_dropMode->currentData().toString());
    printerSetValue("inkColor", m_inkColor->value());
    printerSetValue("whiteLimit", m_whiteLimit->value());
    m_inkPage->save();
}

void SettingsDialog::updateOrderLabel()
{
    m_orderLabel->setPixmap(channelOrderIcon(m_channelOrder).pixmap(88, 18));
    m_orderLabel->setToolTip(m_channelOrder);
}

void SettingsDialog::onEditChannelOrder()
{
    ChannelProfile p;
    p.name = printerName(currentProfileId());
    p.order = m_channelOrder;
    ChannelProfileDialog dlg(p, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_channelOrder = dlg.result().order;
    updateOrderLabel();
}

void SettingsDialog::onProfileChanged(int)
{
    if (m_switching)
        return;
    const QString id = currentProfileId();
    if (id.isEmpty() || id == activePrinterId())
        return;
    saveProfileFields();      // commit edits to the profile being left
    setActivePrinter(id);
    loadProfileFields();
}

void SettingsDialog::onNewProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New printer profile",
                                               "Name:", QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    saveProfileFields();
    setActivePrinter(printerCreate(name));
    reloadProfileCombo();
    loadProfileFields();
}

void SettingsDialog::onDuplicateProfile()
{
    const QString src = currentProfileId();
    if (src.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Duplicate printer profile", "Name for the copy:",
        QLineEdit::Normal, printerName(src) + " (copy)", &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    saveProfileFields();      // duplicate what is on screen, not stale values
    setActivePrinter(printerDuplicate(src, name));
    reloadProfileCombo();
    loadProfileFields();
}

void SettingsDialog::onRenameProfile()
{
    const QString id = currentProfileId();
    if (id.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Rename printer profile",
                                               "Name:", QLineEdit::Normal,
                                               printerName(id), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    printerRename(id, name);
    reloadProfileCombo();
}

void SettingsDialog::onDeleteProfile()
{
    const QString id = currentProfileId();
    if (id.isEmpty())
        return;
    if (QMessageBox::question(
            this, "Delete printer profile",
            "Delete \"" + printerName(id) + "\"?\n\n"
            "Its calibration (ink density, trim, correction curves) is lost.")
        != QMessageBox::Yes)
        return;
    if (!printerRemove(id)) {
        QMessageBox::information(this, "Delete printer profile",
                                 "At least one printer profile must remain.");
        return;
    }
    reloadProfileCombo();
    loadProfileFields();
}

void SettingsDialog::accept()
{
    save();
    QDialog::accept();
}
