/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "ExportDialog.h"
#include "SpotMaskDialog.h"
#include "dwrip/JobConfig.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QButtonGroup>
#include <QRadioButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QStringList>
#include <QMessageBox>
#include <QSettings>
#include <QProcess>
#include <QApplication>
#include <QLabel>
#include <QVBoxLayout>
#include <QSpinBox>
#include "Compositor.h"
#include "InkCost.h"
#include "dwrip/DwripRunner.h"
#include "dwrip/DwripRun.h"   // locateDwrip()
#include "dwrip/DwripPanel.h" // fillDropModeCombo/fillResolutionCombo
#include "PrinterProfile.h"
#include "SettingsDialog.h"   // applyPrinterSettings()

// Layer-mode presets — one dropdown encoding -m mode + white-first + zones.
// The "›" shows print/stack order (left is laid down first). CMYK›White›CMYK is
// the 3-zone sandwich; the others are 2-zone / single-layer.
namespace {
struct LayerMode { const char *key, *label, *mode; bool whiteFirst; int zones; };
const LayerMode kLayerModes[] = {
    {"white",           "White",                "white", false, 2},
    {"white_cmyk",      "White › CMYK",         "both",  true,  2},
    {"cmyk",            "CMYK",                 "color", false, 2},
    {"cmyk_white",      "CMYK › White",         "both",  false, 2},
    {"cmyk_white_cmyk", "CMYK › White › CMYK",  "both",  false, 3},
    {"emboss",          "Emboss (spot 3D)",     "both",  false, 2},
};
constexpr int kLayerModeN = int(sizeof(kLayerModes) / sizeof(kLayerModes[0]));
const LayerMode &layerModeByKey(const QString &key) {
    for (const auto &m : kLayerModes)
        if (key == m.key) return m;
    return kLayerModes[3];   // default: CMYK › White (both, colour-first, 2-zone)
}
}  // namespace

ExportDialog::ExportDialog(const QString &defaultOut, bool sendConfigured,
                           const JobConfig &base, const ExportImageInfo &img,
                           QWidget *parent)
    : QDialog(parent), m_base(base), m_img(img)
{
    setWindowTitle("Export PRN");
    setModal(true);

    auto *root = new QVBoxLayout(this);

    // Read-only summary (active image + effective Tier-1/Tier-3 settings).
    auto *summaryBox = new QGroupBox("Summary", this);
    auto *summaryLay = new QVBoxLayout(summaryBox);
    m_summary = new QLabel(summaryBox);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setWordWrap(false);
    summaryLay->addWidget(m_summary);
    root->addWidget(summaryBox);

    auto *form = new QFormLayout;
    m_form = form;   // kept so updateModeZoneState() can hide the whole Zone-3 row

    // ── Target printer ──
    // Switching here switches the app-wide active profile, so the whole machine
    // config (device, ink layout, ICC, calibration, UV) moves with it. Bed size
    // is deliberately NOT part of a profile, so the canvas layout is unaffected
    // and it is safe to change the target at export time.
    m_printer = new QComboBox(this);
    m_printer->setToolTip(
        "Which machine this job is for. Selecting a printer applies its whole\n"
        "profile: CUPS queue, ink channel order, ICC profile + intent, density\n"
        "calibration (ink %, TAC, trim, correction curves), UV margin/tail and\n"
        "the mechanical offsets.\n\n"
        "Edit profiles in App Settings. Bed size and ink prices stay global.");
    reloadPrinterCombo();
    connect(m_printer, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onPrinterChanged);

    // ── Quality: stored in the profile, but changed often enough per job that
    // it belongs here too. Both views write the same setting. ──
    auto *qRow = new QWidget(this);
    auto *qLay = new QHBoxLayout(qRow);
    qLay->setContentsMargins(0, 0, 0, 0);
    m_resolution = new QComboBox(qRow);
    fillResolutionCombo(m_resolution);
    m_dropMode = new QComboBox(qRow);
    fillDropModeCombo(m_dropMode);
    qLay->addWidget(m_resolution, 1);
    qLay->addWidget(m_dropMode, 1);
    loadQualityFromProfile();
    connect(m_resolution, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::rebuildSummary);
    connect(m_dropMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::rebuildSummary);

    // Layer mode: one dropdown of print-order presets (mode + white-first +
    // zones). Replaces the old mode radios / white-first checkbox / zone radios.
    m_layerMode = new QComboBox(this);
    for (const auto &m : kLayerModes)
        m_layerMode->addItem(QString::fromUtf8(m.label), QString::fromUtf8(m.key));
    m_layerMode->setToolTip(
        "Which layers to print and in what order (‹ left is laid down first ›):\n"
        "  White — white only\n"
        "  White › CMYK — white underbase, then colour\n"
        "  CMYK — colour only\n"
        "  CMYK › White — colour, then white backing (default)\n"
        "  CMYK › White › CMYK — 3-zone sandwich (two-sided possible)");

    // Single-layer sweep density. The UV lamp rides the carriage, so sweeps per
    // row == cure dose. Single-layer has always doubled the advance to print 2x
    // faster, which silently halves that dose — the reason thin/soft tints stay
    // tacky in CMYK-only. Only meaningful when one layer prints (see
    // docs/known_issues.md §2).
    m_singleWeave = new QComboBox(this);
    m_singleWeave->addItem("Fast — 1× dose, fastest (720 dpi vertical)", "fast");
    m_singleWeave->addItem("Full cure — 2× dose, 2× slower", "dense");
    m_singleWeave->addItem("Extra cure — 3× dose, 3× slower", "ultra");
    m_singleWeave->addItem("Maximum cure — 5× dose, 5× slower", "max");
    m_singleWeave->setToolTip(
        "How many times the carriage sweeps over each row, which sets how much\n"
        "UV the ink receives (the lamp is mounted on the carriage).\n\n"
        "The ink laid on each row is IDENTICAL at every level — only the number\n"
        "of sweeps changes, so dose and print time rise together:\n\n"
        "  Fast (default)  90 nozzles, advance 22/54 — 1× dose. Also prints only\n"
        "                  every other row, i.e. 720 dpi vertical, not 1440.\n"
        "  Full cure       45 nozzles, advance 11/27 — 2× dose, every row prints\n"
        "                  (full 1440 dpi vertical). Same weave as both-mode.\n"
        "  Extra cure      30 nozzles, advance 7/8   — 3× dose.\n"
        "  Maximum cure    18 nozzles, advance 4/5   — 5× dose.\n\n"
        "Step up until soft/thin tints stop feeling tacky, then stop — every\n"
        "level costs print time in direct proportion.\n\n"
        "Extra and Maximum need 1440×1440 resolution. Both-mode already runs the\n"
        "2× weave, so this only applies to the single-layer presets (White, CMYK).");

    auto *outRow = new QWidget(this);
    auto *outLay = new QHBoxLayout(outRow);
    outLay->setContentsMargins(0, 0, 0, 0);
    m_output = new QLineEdit(outRow);
    m_output->setText(defaultOut);
    auto *browseBtn = new QPushButton("…", outRow);
    browseBtn->setMaximumWidth(32);
    outLay->addWidget(m_output, 1);
    outLay->addWidget(browseBtn, 0);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::browse);

    m_send = new QCheckBox(sendConfigured
                 ? QString("Spool to '%1' after generating").arg(base.cupsQueue)
                 : QString("Spool to printer after generating"), this);
    m_send->setEnabled(sendConfigured);
    m_send->setChecked(sendConfigured);   // default on when a queue is set
    m_send->setToolTip(sendConfigured
        ? "Queue the PRN to CUPS (non-blocking) after it is generated"
        : "Set a printer (CUPS queue) in App Settings first");

    // Zone-3 source (3-zone only): same / mirror / custom image + file picker.
    m_z3Row = new QWidget(this);
    auto *z3Lay = new QHBoxLayout(m_z3Row);
    z3Lay->setContentsMargins(0, 0, 0, 0);
    m_z3Mode = new QComboBox(m_z3Row);
    m_z3Mode->addItem("Same as front", "same");
    m_z3Mode->addItem("Mirror front", "mirror");
    m_z3Mode->addItem("Custom image…", "image");
    m_z3Mode->setToolTip(
        "Trailing colour zone: Same = 2x sandwich (default); Mirror/Custom = "
        "two-sided print (front / white / back), each side normal density.");
    m_z3Image = new QLineEdit(m_z3Row);
    m_z3Image->setPlaceholderText("zone-3 PNG…");
    auto *z3Browse = new QPushButton("…", m_z3Row);
    z3Browse->setMaximumWidth(32);
    z3Lay->addWidget(m_z3Mode, 0);
    z3Lay->addWidget(m_z3Image, 1);
    z3Lay->addWidget(z3Browse, 0);
    connect(z3Browse, &QPushButton::clicked, this, &ExportDialog::browseZ3);

    // Emboss controls (spot 3D): preset + spot mask + underbase.
    m_embossRow = new QWidget(this);
    auto *emLay = new QHBoxLayout(m_embossRow);
    emLay->setContentsMargins(0, 0, 0, 0);
    m_embossPreset = new QComboBox(m_embossRow);
    m_embossPreset->addItem("8 layers", 8);
    m_embossPreset->addItem("16 layers", 16);
    m_embossPreset->addItem("32 layers", 32);
    m_embossPreset->setToolTip("White relief height preset (more layers = "
                               "taller, slower). Needs 1440×1440.");
    m_spotPath = new QLineEdit(m_embossRow);
    m_spotPath->setPlaceholderText("spot mask PNG (white = raised)…");
    auto *spotBrowse = new QPushButton("…", m_embossRow);
    auto *spotGen = new QPushButton("Generate…", m_embossRow);
    spotGen->setToolTip("Build a spot mask from this artwork — texture, shape, bevel or outline");
    spotBrowse->setMaximumWidth(32);
    m_underbase = new QCheckBox("Underbase", m_embossRow);
    m_underbase->setToolTip("Thin white under colour in flat areas "
                            "(for dark/clear media)");
    m_embossGray = new QCheckBox("Grayscale", m_embossRow);
    m_embossGray->setToolTip("Variable height: mask level → per-pixel height "
                             "(white/opaque = tallest). Off = binary on/off.");
    auto *previewBtn = new QPushButton("Preview 3D", m_embossRow);
    previewBtn->setToolTip("Shaded-relief preview of the emboss (colour on relief)");
    connect(previewBtn, &QPushButton::clicked, this, &ExportDialog::previewEmboss);
    m_embossRuns = new QComboBox(m_embossRow);
    m_embossRuns->addItem("1 run (Cara B)", false);
    m_embossRuns->addItem("Split (Cara A)", true);
    m_embossRuns->setToolTip("Cara B: one run (colour shares few nozzles). "
        "Cara A (split): white relief run(s) + a separate full-quality colour "
        "run — better colour, taller relief, printed in sequence.");
    emLay->addWidget(m_embossPreset, 0);
    emLay->addWidget(m_spotPath, 1);
    emLay->addWidget(spotBrowse, 0);
    emLay->addWidget(spotGen, 0);
    emLay->addWidget(m_underbase, 0);
    emLay->addWidget(m_embossGray, 0);
    emLay->addWidget(m_embossRuns, 0);
    emLay->addWidget(previewBtn, 0);

    // Cara A extras (shown only in split): white-run count + colour registration.
    m_embossRow2 = new QWidget(this);
    auto *em2 = new QHBoxLayout(m_embossRow2);
    em2->setContentsMargins(0, 0, 0, 0);
    m_embossWhiteRuns = new QSpinBox(m_embossRow2);
    m_embossWhiteRuns->setRange(1, 20);
    m_embossWhiteRuns->setPrefix("white runs: ");
    m_embossWhiteRuns->setToolTip("Stack this many white runs for taller relief "
                                  "(total layers = preset × runs).");
    m_embossColorReg = new QSpinBox(m_embossRow2);
    m_embossColorReg->setRange(-500, 500);
    m_embossColorReg->setPrefix("colour reg: ");
    m_embossColorReg->setSuffix(" /1440\"");
    m_embossColorReg->setToolTip("Feed offset to register the colour run onto "
                                 "the relief (calibrate by eye).");
    em2->addWidget(m_embossWhiteRuns, 0);
    em2->addWidget(m_embossColorReg, 0);
    em2->addStretch(1);

    // Preset options depend on the run mode (Cara B: 8/16/32; Cara A: 15/30/45/90).
    auto repopEmboss = [this]() {
        const bool split = m_embossRuns->currentData().toBool();
        const int cur = m_embossPreset->currentData().toInt();
        const QSignalBlocker blk(m_embossPreset);
        m_embossPreset->clear();
        const QList<int> vals = split ? QList<int>{15, 30, 45, 90}
                                      : QList<int>{8, 16, 32};
        for (int v : vals)
            m_embossPreset->addItem(QString("%1 layers").arg(v), v);
        int idx = m_embossPreset->findData(cur);
        if (idx < 0) idx = split ? 2 : 1;   // default 45 (split) / 16 (single)
        m_embossPreset->setCurrentIndex(idx);
    };
    connect(m_embossRuns, &QComboBox::currentIndexChanged, this,
            [this, repopEmboss]() {
        repopEmboss(); updateModeZoneState(); rebuildSummary();
    });
    connect(spotBrowse, &QPushButton::clicked, this, &ExportDialog::browseSpot);
    connect(spotGen, &QPushButton::clicked, this, &ExportDialog::generateSpot);

    form->addRow("Printer:", m_printer);
    form->addRow("Quality:", qRow);
    form->addRow("Layer mode:", m_layerMode);
    form->addRow("Cure sweeps:", m_singleWeave);
    form->addRow("Zone 3:", m_z3Row);
    form->addRow("Emboss:", m_embossRow);
    form->addRow("", m_embossRow2);
    form->addRow("Output:", outRow);
    form->addRow("", m_send);
    root->addLayout(form);

    // Ink & cost estimate (before printing): runs dwrip --estimate on demand.
    m_estimateBtn = new QPushButton("Estimate ink && cost", this);
    m_estimateBtn->setToolTip("Run a quick ink tally for these settings "
                              "(no PRN written). Set prices in Edit → Ink Cost.");
    connect(m_estimateBtn, &QPushButton::clicked, this, &ExportDialog::runEstimate);
    root->addWidget(m_estimateBtn);
    m_estimateResult = new QLabel(this);
    m_estimateResult->setTextFormat(Qt::RichText);
    m_estimateResult->setWordWrap(false);
    m_estimateResult->hide();
    root->addWidget(m_estimateResult);

    // Restore last choices.
    {
        QSettings s;
        s.beginGroup("export");
        int lmi = m_layerMode->findData(s.value("layerMode", "cmyk_white").toString());
        m_layerMode->setCurrentIndex(lmi >= 0 ? lmi : 3);
        // Per-JOB: how much cure a sheet needs depends on the artwork (soft
        // tints vs solid coverage), not on the machine, so this stays in the
        // export group and does not follow the printer profile.
        const int swi = m_singleWeave->findData(s.value("singleWeave", "fast").toString());
        m_singleWeave->setCurrentIndex(swi >= 0 ? swi : 0);
        const int z3i = m_z3Mode->findData(s.value("z3Mode", "same").toString());
        m_z3Mode->setCurrentIndex(z3i >= 0 ? z3i : 0);
        m_z3Image->setText(s.value("z3Image").toString());
        m_embossRuns->setCurrentIndex(s.value("embossSplit", false).toBool() ? 1 : 0);
        repopEmboss();   // populate preset list for the chosen run mode
        const int emi = m_embossPreset->findData(s.value("embossLayers", 16).toInt());
        if (emi >= 0) m_embossPreset->setCurrentIndex(emi);
        m_embossWhiteRuns->setValue(s.value("embossWhiteRuns", 1).toInt());
        m_embossColorReg->setValue(s.value("embossColorReg", 0).toInt());
        m_spotPath->setText(s.value("spotPath").toString());
        m_underbase->setChecked(s.value("underbase", false).toBool());
        m_embossGray->setChecked(s.value("embossGray", false).toBool());
        s.endGroup();
    }

    // Layer mode drives Zone-3 row visibility (only for the 3-zone sandwich).
    connect(m_layerMode, &QComboBox::currentIndexChanged, this, &ExportDialog::updateModeZoneState);
    connect(m_z3Mode, &QComboBox::currentIndexChanged, this, &ExportDialog::updateModeZoneState);
    updateModeZoneState();

    // Keep the summary in sync with the Tier-2 choices.
    connect(m_layerMode, &QComboBox::currentIndexChanged, this, &ExportDialog::rebuildSummary);
    connect(m_singleWeave, &QComboBox::currentIndexChanged, this, &ExportDialog::rebuildSummary);
    connect(m_z3Mode, &QComboBox::currentIndexChanged, this, &ExportDialog::rebuildSummary);
    connect(m_z3Image, &QLineEdit::textChanged, this, &ExportDialog::rebuildSummary);
    connect(m_embossPreset, &QComboBox::currentIndexChanged, this, &ExportDialog::rebuildSummary);
    connect(m_underbase, &QCheckBox::toggled, this, &ExportDialog::rebuildSummary);
    connect(m_send, &QCheckBox::toggled, this, &ExportDialog::rebuildSummary);
    connect(m_output, &QLineEdit::textChanged, this, &ExportDialog::rebuildSummary);
    rebuildSummary();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Export");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancel");
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ExportDialog::reject);
    root->addWidget(buttons);

    // Fixed size: lock to content, no resize handles. Re-fits if the summary
    // grows/shrinks (SetFixedSize tracks the layout's sizeHint).
    root->setSizeConstraint(QLayout::SetFixedSize);
}

QString ExportDialog::modeValue() const
{
    return layerModeByKey(m_layerMode->currentData().toString()).mode;
}

int ExportDialog::zonesValue() const
{
    return layerModeByKey(m_layerMode->currentData().toString()).zones;
}

bool ExportDialog::whiteFirstValue() const
{
    return layerModeByKey(m_layerMode->currentData().toString()).whiteFirst;
}

void ExportDialog::rebuildSummary()
{
    if (!m_summary)
        return;

    // One aligned label/value row. Label muted + fixed-width gutter so every
    // value lines up in a column — far easier to scan than "·"-joined runs.
    auto row = [](const QString &label, const QString &value) {
        return QString("<tr>"
                       "<td width='78' valign='top'>"
                       "<span style='color:#64748b'>%1</span></td>"
                       "<td valign='top'>%2</td></tr>")
            .arg(label, value);
    };

    const QString name = m_img.name.isEmpty() ? QStringLiteral("—") : m_img.name;
    const QString header =
        QString("<div style='margin-bottom:7px'><b>%1</b><br>"
                "<span style='color:#64748b'>%2×%3 px &nbsp;·&nbsp; "
                "%4×%5 mm &nbsp;·&nbsp; %6 dpi</span></div>")
            .arg(name).arg(m_img.wpx).arg(m_img.hpx)
            .arg(m_img.wmm, 0, 'f', 1).arg(m_img.hmm, 0, 'f', 1)
            .arg(m_img.dpi, 0, 'f', 0);

    const QString inkc   = m_base.inkColorPct ? QString::number(*m_base.inkColorPct)
                                              : QStringLiteral("100");
    const QString colour = m_base.colorLimitPct ? QString::number(*m_base.colorLimitPct)
                                                : QStringLiteral("100");
    const QString white  = m_base.whiteLimitPct ? QString::number(*m_base.whiteLimitPct)
                                                : QStringLiteral("100");
    const QString icc = m_base.iccPath.isEmpty()
        ? QStringLiteral("none <span style='color:#64748b'>(built-in UCR)</span>")
        : QFileInfo(m_base.iccPath).fileName() + " <span style='color:#64748b'>(" +
              (m_base.intent.isEmpty() ? QStringLiteral("perceptual") : m_base.intent) + ")</span>";

    const bool sending = m_send->isEnabled() && m_send->isChecked() &&
                         !m_base.cupsQueue.isEmpty();
    const QString outName = outputPath().isEmpty()
        ? (sending ? QStringLiteral("temp (print only, not saved)")
                   : QStringLiteral("(none)"))
        : QFileInfo(outputPath()).fileName();

    QString rows;
    rows += row("Resolution", "<b>" + m_resolution->currentData().toString() + "</b>");
    rows += row("Printer",    "<b>" + (m_printer->currentIndex() >= 0
                                       ? m_printer->currentText() : QString("—")) + "</b>");
    rows += row("Channels",   "<b>" + m_base.channels + "</b> &nbsp;·&nbsp; drop <b>"
                              + m_dropMode->currentData().toString() + "</b>");
    rows += row("Ink",        QString("colour <b>%1%</b> &nbsp;·&nbsp; TAC <b>%2%</b> "
                                       "&nbsp;·&nbsp; white <b>%3%</b>")
                                  .arg(inkc, colour, white));
    rows += row("ICC", icc);
    QString printVal = "<b>" + m_layerMode->currentText() + "</b>";
    if (m_layerMode->currentData().toString() == "emboss") {
        const bool split = m_embossRuns->currentData().toBool();
        const int lyr = m_embossPreset->currentData().toInt();
        if (split) {
            const int k = m_embossWhiteRuns->value();
            printVal += QString(" &nbsp;·&nbsp; <b>split</b> &nbsp;·&nbsp; "
                                "<b>%1</b> layers (%2×%3)")
                            .arg(lyr * k).arg(lyr).arg(k);
        } else {
            printVal += QString(" &nbsp;·&nbsp; <b>%1</b> layers").arg(lyr);
        }
        printVal += QString("%1%2")
                        .arg(m_embossGray->isChecked() ? " &nbsp;·&nbsp; grayscale" : "")
                        .arg(m_underbase->isChecked() ? " &nbsp;·&nbsp; underbase" : "");
    } else if (zonesValue() == 3) {
        const QString z3 = m_z3Mode->currentData().toString();
        if (z3 != "same")
            printVal += " &nbsp;·&nbsp; zone-3 <b>" + z3 + "</b>";
    }
    if (modeValue() != "both") {
        const QString sw = m_singleWeave->currentData().toString();
        if (sw == "dense")      printVal += " &nbsp;·&nbsp; <b>2× cure</b> (2× slower)";
        else if (sw == "ultra") printVal += " &nbsp;·&nbsp; <b>3× cure</b> (3× slower)";
        else if (sw == "max")   printVal += " &nbsp;·&nbsp; <b>5× cure</b> (5× slower)";
        // ultra/max are tuned for the 1440 nozzle spacing; the engine silently
        // falls back to dense elsewhere, so say so before the user waits for a
        // print that cured less than they asked for.
        if ((sw == "ultra" || sw == "max") &&
            m_resolution->currentData().toString() != "1440x1440")
            printVal += " &nbsp;·&nbsp; <span style='color:#b06000'>needs 1440×1440,"
                        " will fall back to 2×</span>";
    }
    rows += row("Layer mode", printVal);
    rows += row("Output", "<b>" + outName + "</b>" +
                (sending ? " &nbsp;·&nbsp; spool → <b>" + m_base.cupsQueue + "</b>" : QString()));

    m_summary->setText(header + "<table cellspacing='0' cellpadding='2'>" + rows + "</table>");
}

void ExportDialog::updateModeZoneState()
{
    // Every layer-mode preset is a valid combo, so no cross-enabling is needed.
    // Zone-3 row only in the 3-zone sandwich; emboss row only in emboss mode.
    const bool emboss = m_layerMode->currentData().toString() == "emboss";
    const bool z3 = !emboss && zonesValue() == 3;
    // Cure sweeps only bite when a single layer prints; both-mode is already dense.
    m_form->setRowVisible(m_singleWeave, modeValue() != "both");
    m_form->setRowVisible(m_z3Row, z3);      // hides the "Zone 3:" label too
    m_form->setRowVisible(m_embossRow, emboss);
    m_form->setRowVisible(m_embossRow2,
                          emboss && m_embossRuns->currentData().toBool());
    const bool custom = m_z3Mode->currentData().toString() == "image";
    m_z3Image->setEnabled(z3 && custom);
}

void ExportDialog::browse()
{
    const QString p = QFileDialog::getSaveFileName(this, "Save PRN", m_output->text(),
                                                   "PRN (*.prn)");
    if (!p.isEmpty())
        m_output->setText(p);
}

void ExportDialog::browseZ3()
{
    const QString p = QFileDialog::getOpenFileName(this, "Zone-3 image",
                                                   m_z3Image->text(), "PNG (*.png)");
    if (!p.isEmpty())
        m_z3Image->setText(p);
}

void ExportDialog::browseSpot()
{
    const QString p = QFileDialog::getOpenFileName(this, "Spot mask (white = raised)",
                                                   m_spotPath->text(), "PNG (*.png)");
    if (!p.isEmpty())
        m_spotPath->setText(p);
}

void ExportDialog::previewEmboss()
{
    if (m_spotPath->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Preview 3D", "Choose a spot mask PNG first.");
        return;
    }
    QImage color(m_base.inputPath);   // composited colour artwork (set at open)
    QImage spot(m_spotPath->text().trimmed());
    if (spot.isNull()) {
        QMessageBox::warning(this, "Preview 3D", "Could not load the spot mask.");
        return;
    }
    if (color.isNull()) {             // colour optional — preview relief alone
        color = QImage(spot.size(), QImage::Format_ARGB32);
        color.fill(QColor(205, 205, 205, 255));  // opaque so the relief shows
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QImage preview = buildEmbossPreview(
        color, spot, m_embossPreset->currentData().toInt(),
        m_embossGray->isChecked(), m_underbase->isChecked());
    QApplication::restoreOverrideCursor();
    if (preview.isNull()) {
        QMessageBox::warning(this, "Preview 3D", "Preview failed.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Emboss preview (2.5D shaded relief)");
    auto *lay = new QVBoxLayout(&dlg);
    auto *lbl = new QLabel(&dlg);
    QPixmap pm = QPixmap::fromImage(preview);
    if (pm.width() > 760 || pm.height() > 760)
        pm = pm.scaled(760, 760, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    lbl->setPixmap(pm);
    lay->addWidget(lbl);
    auto *hint = new QLabel("Shaded relief with the colour on top (light from "
                            "top-left). Approximate — not the real cured height.",
                            &dlg);
    hint->setStyleSheet("color:#64748b; font-size:12px;");
    lay->addWidget(hint);
    lay->setSizeConstraint(QLayout::SetFixedSize);
    dlg.exec();
}

void ExportDialog::runEstimate()
{
    if (m_base.inputPath.isEmpty()) {
        m_estimateResult->setText("<i>No image to estimate.</i>");
        m_estimateResult->show();
        return;
    }
    // Same settings as this export, but --estimate (no PRN, no spool).
    JobConfig cfg = m_base;
    applyTo(cfg);
    cfg.estimate = true;
    cfg.outputPath.clear();
    cfg.cupsQueue.clear();

    m_estimateResult->setText("<i>Calculating…</i>");
    m_estimateResult->show();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QProcess proc;
    proc.start(locateDwrip(), DwripRunner::buildArgs(cfg));
    const bool ok = proc.waitForFinished(120000);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        m_estimateResult->setText("<i>Estimate failed (dwrip did not finish).</i>");
        return;
    }
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput())
                      + QString::fromLocal8Bit(proc.readAllStandardError());

    const InkCostParams p = InkCostParams::load();
    double total = 0;
    const QVector<InkUse> rows = inkCostCompute(out, p, &total);

    bool anyPrice = false;
    for (int i = 0; i < 5; i++) if (p.price[i] > 0) anyPrice = true;

    QString html = "<table cellspacing='0' cellpadding='3'>"
        "<tr><td style='color:#64748b'>Ink</td>"
        "<td style='color:#64748b' align='right'>mL</td>"
        "<td style='color:#64748b' align='right'>Cost</td></tr>";
    for (const InkUse &u : rows) {
        if (u.ml <= 0)
            continue;
        html += QString("<tr><td><b>%1</b></td>"
                        "<td align='right'>%2</td>"
                        "<td align='right'>%3</td></tr>")
                    .arg(u.ink)
                    .arg(u.ml, 0, 'f', 3)
                    .arg(u.cost > 0 ? p.currency + " " + QString::number(u.cost, 'f', 2)
                                    : QStringLiteral("—"));
    }
    html += QString("<tr><td colspan='2' align='right'><b>Total</b></td>"
                    "<td align='right'><b>%1</b></td></tr></table>")
                .arg(anyPrice ? p.currency + " " + QString::number(total, 'f', 2)
                              : QStringLiteral("—"));
    if (!anyPrice)
        html += "<div style='color:#64748b;font-size:12px'>"
                "Set prices in Edit → Ink Cost for a cost total.</div>";
    m_estimateResult->setText(html);
}

QString ExportDialog::outputPath() const
{
    return m_output->text().trimmed();
}

void ExportDialog::reloadPrinterCombo()
{
    m_switchingPrinter = true;   // repopulating fires currentIndexChanged
    m_printer->clear();
    m_printerIds.clear();
    const QString active = activePrinterId();
    int sel = 0;
    for (const PrinterMeta &m : printersLoad()) {
        if (m.id == active) sel = m_printerIds.size();
        m_printerIds.append(m.id);
        m_printer->addItem(m.name);
    }
    if (!m_printerIds.isEmpty())
        m_printer->setCurrentIndex(sel);
    m_switchingPrinter = false;
}

void ExportDialog::loadQualityFromProfile()
{
    m_switchingPrinter = true;
    const int ri = m_resolution->findData(
        printerValue("resolution", "1440x1440").toString());
    m_resolution->setCurrentIndex(ri >= 0 ? ri : 0);
    const int di = m_dropMode->findData(printerValue("dropMode", "ML").toString());
    m_dropMode->setCurrentIndex(di >= 0 ? di : 1);
    m_switchingPrinter = false;
}

void ExportDialog::onPrinterChanged(int)
{
    if (m_switchingPrinter)
        return;
    const int i = m_printer->currentIndex();
    if (i < 0 || i >= m_printerIds.size())
        return;
    setActivePrinter(m_printerIds[i]);
    // Re-read the whole machine config so the summary (and the spool target)
    // describe the printer that is now selected, not the previous one.
    m_base = JobConfig();
    applyPrinterSettings(m_base);
    loadQualityFromProfile();
    m_send->setEnabled(!m_base.cupsQueue.isEmpty());
    if (m_base.cupsQueue.isEmpty())
        m_send->setChecked(false);
    m_send->setText(m_base.cupsQueue.isEmpty()
                        ? QString("Spool to printer after generating")
                        : QString("Spool to '%1' after generating").arg(m_base.cupsQueue));
    rebuildSummary();
}

void ExportDialog::applyTo(JobConfig &cfg) const
{
    cfg.mode = modeValue();
    cfg.whiteFirst = whiteFirstValue();
    cfg.zones = zonesValue();
    // Quality lives in the printer profile; these two are simply also editable
    // here, so pass the on-screen values through (accept() persists them).
    cfg.resolution = m_resolution->currentData().toString();
    cfg.dropMode   = m_dropMode->currentData().toString();
    // Only send --single-weave when it can act (single layer, non-default value):
    // both-mode would just warn, and "fast" is the engine default.
    cfg.singleWeave = (modeValue() != "both")
                          ? m_singleWeave->currentData().toString() : QString();
    // Emboss (spot 3D): overrides zones/mode in the engine.
    if (m_layerMode->currentData().toString() == "emboss") {
        cfg.embossLayers = m_embossPreset->currentData().toInt();
        cfg.spotPath = m_spotPath->text().trimmed();
        cfg.underbase = m_underbase->isChecked();
        cfg.embossGrayscale = m_embossGray->isChecked();
        cfg.embossSplit = m_embossRuns->currentData().toBool();
        cfg.embossWhiteRuns = m_embossWhiteRuns->value();
        cfg.embossColorReg = m_embossColorReg->value();
    } else {
        cfg.embossLayers = 0;
        cfg.spotPath.clear();
    }
    // Zone-3 source only when the 3-zone sandwich is active (else stays "same").
    if (zonesValue() == 3) {
        cfg.z3Mode = m_z3Mode->currentData().toString();
        cfg.z3ImagePath = m_z3Image->text().trimmed();
    } else {
        cfg.z3Mode = "same";
        cfg.z3ImagePath.clear();
    }
    cfg.outputPath = outputPath();
    if (!(m_send->isEnabled() && m_send->isChecked()))
        cfg.cupsQueue.clear();   // don't spool unless requested (overrides Tier-1)
}

void ExportDialog::accept()
{
    const bool sending = m_send->isEnabled() && m_send->isChecked();
    if (outputPath().isEmpty() && !sending) {
        QMessageBox::warning(this, "Export",
            "Choose an output path (.prn) — or enable “Send to printer” to print "
            "without saving a file.");
        return;
    }
    if (zonesValue() == 3 &&
        m_z3Mode->currentData().toString() == "image" &&
        m_z3Image->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Export",
            "Zone 3 is set to Custom image — choose a PNG first.");
        return;
    }
    if (m_layerMode->currentData().toString() == "emboss") {
        if (m_spotPath->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Export",
                "Emboss needs a spot mask PNG (white = raised area).");
            return;
        }
        if (m_resolution->currentData().toString() != "1440x1440") {
            QMessageBox::warning(this, "Export",
                "Emboss needs resolution 1440×1440 — set it in the Quality row above.");
            return;
        }
    }
    // Quality belongs to the printer, not to the job history: write it back to
    // the active profile so it sticks per machine (and the dwrip panel agrees).
    printerSetValue("resolution", m_resolution->currentData().toString());
    printerSetValue("dropMode", m_dropMode->currentData().toString());

    QSettings s;
    s.beginGroup("export");
    s.setValue("layerMode", m_layerMode->currentData().toString());
    s.setValue("singleWeave", m_singleWeave->currentData().toString());
    s.setValue("z3Mode", m_z3Mode->currentData().toString());
    s.setValue("z3Image", m_z3Image->text().trimmed());
    s.setValue("embossLayers", m_embossPreset->currentData().toInt());
    s.setValue("spotPath", m_spotPath->text().trimmed());
    s.setValue("underbase", m_underbase->isChecked());
    s.setValue("embossGray", m_embossGray->isChecked());
    s.setValue("embossSplit", m_embossRuns->currentData().toBool());
    s.setValue("embossWhiteRuns", m_embossWhiteRuns->value());
    s.setValue("embossColorReg", m_embossColorReg->value());
    s.endGroup();
    QDialog::accept();
}

void ExportDialog::generateSpot()
{
    if (m_img.image.isNull()) {
        QMessageBox::information(this, "Generate Spot Mask",
                                 "No artwork available to build a mask from.");
        return;
    }
    SpotMaskDialog dlg(m_img.image, m_img.path,
                       m_embossPreset->currentData().toInt(), this);
    if (dlg.exec() == QDialog::Accepted && !dlg.savedPath().isEmpty()) {
        m_spotPath->setText(dlg.savedPath());
        rebuildSummary();
    }
}
