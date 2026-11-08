/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "dwrip/DwripPanel.h"
#include "PrinterProfile.h"
#include "dwrip/JobConfig.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QSettings>

// Build a synced slider(0..maxVal) + spinbox(%) pair inside a horizontal row.
static QWidget *makeLimitRow(QSlider *&slider, QSpinBox *&spin, QWidget *parent,
                             int maxVal = 100)
{
    auto *w = new QWidget(parent);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    slider = new QSlider(Qt::Horizontal, w);
    slider->setRange(0, maxVal);
    spin = new QSpinBox(w);
    spin->setRange(0, maxVal);
    spin->setSuffix(" %");
    spin->setMinimumWidth(84);
    h->addWidget(slider, 1);
    h->addWidget(spin, 0);
    return w;
}

// ── Shared quality choice lists ──────────────────────────────────────────────

void fillDropModeCombo(QComboBox *c)
{
    // Ordered by ceiling: full-density modes first, then the capped ones.
    c->addItem("SML — small+medium+large (finest highlights)", "SML");
    c->addItem("ML — medium+large (default)", "ML");
    c->addItem("L — large only (coarse, best UV cure)", "L");
    c->addItem("SM — small+medium (caps density ~42%)", "SM");
    c->addItem("M — medium only (caps density ~42%)", "M");
    c->addItem("S — small only (caps density ~13%)", "S");
    c->setCurrentIndex(1);   // ML
    c->setToolTip(
        "Which dot sizes the halftone ladder may stack (flag -D).\n\n"
        "Tone stays linear in physical ink volume, so:\n"
        "  • the LARGEST size in the mode sets the density ceiling —\n"
        "    SML, ML and L all reach the same maximum;\n"
        "  • the SMALLEST size sets how fine a light tint can be —\n"
        "    SML places ~2.4x more, smaller dots than ML at the same ink.\n\n"
        "L goes the other way: same ink in ~2.4x fewer, fatter drops.\n"
        "Less exposed ink surface cures more completely, which helps\n"
        "thin/soft tints, at the cost of visibly coarser highlights.");
}

void fillResolutionCombo(QComboBox *c)
{
    c->addItem("1440 × 1440 (default)", "1440x1440");
    c->addItem("1440 × 720 (faster)", "1440x720");
    c->addItem("720 × 720 (draft)", "720x720");
    c->addItem("720 × 360 (draft fast)", "720x360");
    c->addItem("2880 × 2880 (slow, huge)", "2880x2880");
    c->setToolTip("Print resolution (flag -R).\n"
                  "Note: the ultra/max cure sweeps need 1440 × 1440.");
}

DwripPanel::DwripPanel(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *box = new QGroupBox("Print quality", this);
    auto *form = new QFormLayout(box);

    // Drop mode (-D)
    m_drop = new QComboBox(box);
    fillDropModeCombo(m_drop);
    form->addRow("Drop mode:", m_drop);

    // Resolution (-R)
    m_resolution = new QComboBox(box);
    fillResolutionCombo(m_resolution);
    form->addRow("Resolution:", m_resolution);

    // Ink % (Color) — global CMYK density scale (--ink-color), 0..200 (100=off).
    auto *inkRow = makeLimitRow(m_inkColorSlider, m_inkColorSpin, box, 200);
    m_inkColorSlider->setValue(100);
    m_inkColorSpin->setValue(100);
    m_inkColorSpin->setToolTip(
        "Colour ink density — scales all CMYK by this % on every pixel "
        "(100 = off). Like trim for all four channels (flag --ink-color).");
    form->addRow("Ink % (Colour):", inkRow);

    // (Total-ink cap / TAC lives in Ink Settings — it's an advanced safety cap.)

    // White ink density (-L) — a plain scale of the white layer.
    auto *whiteRow = makeLimitRow(m_whiteSlider, m_whiteSpin, box);
    m_whiteSlider->setValue(100);
    m_whiteSpin->setValue(100);
    m_whiteSpin->setToolTip("White ink density — scales the white layer % (flag -L)");
    form->addRow("Ink % (White):", whiteRow);

    // How the white base is derived. Sits with -L because the three belong to
    // one subject: how much white goes down, and where.
    m_whiteMode = new QComboBox(box);
    m_whiteMode->addItem("Alpha (flat)", "alpha");
    m_whiteMode->addItem("By colour brightness", "luminance");
    m_whiteMode->setToolTip(
        "How the white base is built (flag --white-mode).\n"
        "Alpha: white follows the image's transparency — one flat level under "
        "opaque artwork, fading out with soft edges.\n"
        "By colour brightness: the same, then thinned under dark colours, which "
        "hide the media on their own.");
    form->addRow("White base:", m_whiteMode);

    m_whiteDark = new QSpinBox(box);
    m_whiteDark->setRange(0, 100);
    m_whiteDark->setSuffix(" %");
    m_whiteDark->setValue(100);
    m_whiteDark->setToolTip(
        "White density under the DARKEST colour (flag --white-dark-pct).\n"
        "100 = no thinning. 50–60 is a sensible first try.");
    form->addRow("White under dark:", m_whiteDark);

    // The alpha→white ramp is linear, but white ink does not cover linearly
    // with coverage. This bends it, which -L cannot do — that scales everything
    // evenly. Only visible on artwork that has soft edges or real transparency;
    // on fully opaque artwork the endpoints are pinned and nothing changes.
    auto *curveRow = new QWidget(box);
    auto *curveLay = new QHBoxLayout(curveRow);
    curveLay->setContentsMargins(0, 0, 0, 0);
    m_whiteCurveX = new QSpinBox(curveRow);
    m_whiteCurveX->setRange(1, 99);
    m_whiteCurveX->setValue(50);
    m_whiteCurveX->setSuffix(" %");
    m_whiteCurveX->setToolTip("Where on the fade the curve is pulled (tone position)");
    m_whiteCurveDelta = new QSpinBox(curveRow);
    m_whiteCurveDelta->setRange(-90, 90);
    m_whiteCurveDelta->setValue(0);
    m_whiteCurveDelta->setPrefix("");
    m_whiteCurveDelta->setSuffix(" %");
    m_whiteCurveDelta->setToolTip(
        "How far it is pulled (flag --white-curve).\n"
        "0 = off. Positive holds white longer towards a soft edge, so the fade "
        "runs further; negative ends it sooner.");
    curveLay->addWidget(m_whiteCurveX);
    curveLay->addWidget(m_whiteCurveDelta);
    form->addRow("Fade curve:", curveRow);

    // Choke: pull the white in so the colour overhangs it. White and colour are
    // laid down half an inch of paper travel apart, so a small registration
    // slip shows as a white halo on dark media — this hides it under the ink.
    auto *chokeRow = new QWidget(box);
    auto *chokeLay = new QHBoxLayout(chokeRow);
    chokeLay->setContentsMargins(0, 0, 0, 0);
    m_whiteChokeSl = new QSlider(Qt::Horizontal, chokeRow);
    m_whiteChokeSl->setRange(0, 100);          // hundredths of a mm
    m_whiteChoke = new QDoubleSpinBox(chokeRow);
    m_whiteChoke->setRange(0.0, 1.0);
    m_whiteChoke->setSingleStep(0.05);
    m_whiteChoke->setDecimals(2);
    m_whiteChoke->setSuffix(" mm");
    m_whiteChoke->setMinimumWidth(84);
    const QString chokeTip =
        "Shrink the white base inward by this much so the colour overhangs it "
        "(flag --white-choke).\n"
        "Hides a white halo when the colour and white layers register slightly "
        "off. 0.1–0.3 mm is typical; 0 = off.";
    m_whiteChokeSl->setToolTip(chokeTip);
    m_whiteChoke->setToolTip(chokeTip);
    chokeLay->addWidget(m_whiteChokeSl, 1);
    chokeLay->addWidget(m_whiteChoke, 0);
    form->addRow("White choke:", chokeRow);

    root->addWidget(box);
    root->addStretch(1);

    // The dark% only means anything in luminance mode.
    auto syncWhiteMode = [this]() {
        m_whiteDark->setEnabled(m_whiteMode->currentData().toString() == "luminance");
    };
    syncWhiteMode();
    // m_loading guard, same as every other control here: load() sets the
    // widgets one after another, so an unguarded save() fires part-way through
    // and writes back the values that have not been loaded yet — which is how
    // a saved "luminance" could end up back at "alpha".
    connect(m_whiteMode, &QComboBox::currentIndexChanged, this,
            [this, syncWhiteMode]() {
        syncWhiteMode();
        if (m_loading) return;
        save(); emit changed();
    });
    connect(m_whiteDark, &QSpinBox::valueChanged, this, [this]() {
        if (m_loading) return;
        save(); emit changed();
    });
    for (QSpinBox *b : {m_whiteCurveX, m_whiteCurveDelta})
        connect(b, &QSpinBox::valueChanged, this, [this]() {
            if (m_loading) return;
            save(); emit changed();
        });
    connect(m_whiteChokeSl, &QSlider::valueChanged, this, [this](int v) {
        if (m_loading) return;
        m_loading = true; m_whiteChoke->setValue(v / 100.0); m_loading = false;
        updateChokeTooltip();
        save(); emit changed();
    });
    connect(m_whiteChoke, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_loading) return;
        m_loading = true; m_whiteChokeSl->setValue(int(v * 100 + 0.5)); m_loading = false;
        updateChokeTooltip();
        save(); emit changed();
    });

    // Keep slider ↔ spin in sync, emit changed(), and auto-save.
    auto link = [this](QSlider *s, QSpinBox *b) {
        connect(s, &QSlider::valueChanged, this, [this, b](int v) {
            if (m_loading) return;
            m_loading = true; b->setValue(v); m_loading = false;
            save(); emit changed();
        });
        connect(b, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, s](int v) {
            if (m_loading) return;
            m_loading = true; s->setValue(v); m_loading = false;
            save(); emit changed();
        });
    };
    link(m_inkColorSlider, m_inkColorSpin);
    link(m_whiteSlider, m_whiteSpin);
    auto onCombo = [this](int) {
        if (m_loading) return;
        save(); emit changed();
    };
    connect(m_drop, QOverload<int>::of(&QComboBox::currentIndexChanged), this, onCombo);
    connect(m_resolution, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, onCombo](int i) { updateChokeTooltip(); onCombo(i); });

    load();   // restore saved Tier-3 settings (auto, no button)
}

QString DwripPanel::dropCode() const
{
    return m_drop->currentData().toString();
}

void DwripPanel::applyTo(JobConfig &cfg) const
{
    cfg.dropMode = dropCode();
    cfg.resolution = m_resolution->currentData().toString();
    cfg.inkColorPct = m_inkColorSpin->value();
    cfg.whiteLimitPct = m_whiteSpin->value();
    cfg.whiteMode = m_whiteMode->currentData().toString();
    cfg.whiteDarkPct = m_whiteDark->value();
    cfg.whiteCurveX = m_whiteCurveX->value();
    cfg.whiteCurveDelta = m_whiteCurveDelta->value();
    cfg.whiteChokeMm = m_whiteChoke->value();
    // colorLimitPct (TAC) comes from Ink Settings via applyPrinterSettings().
}

// These four live in the ACTIVE printer profile: the Export dialog edits the
// same two quality combos, so reload() must be called after it closes (the
// target printer may have changed under us).
void DwripPanel::load()
{
    m_loading = true;
    const QString drop = printerValue("dropMode", "ML").toString();
    int di = m_drop->findData(drop);
    m_drop->setCurrentIndex(di >= 0 ? di : 1);
    const QString resn = printerValue("resolution", "1440x1440").toString();
    int rdi = m_resolution->findData(resn);
    m_resolution->setCurrentIndex(rdi >= 0 ? rdi : 0);
    const int inkc = printerValue("inkColor", 100).toInt();
    const int white = printerValue("whiteLimit", 100).toInt();
    m_inkColorSlider->setValue(inkc); m_inkColorSpin->setValue(inkc);
    m_whiteSlider->setValue(white); m_whiteSpin->setValue(white);
    const QString wmode = printerValue("whiteMode", "alpha").toString();
    const int wmi = m_whiteMode->findData(wmode);
    m_whiteMode->setCurrentIndex(wmi >= 0 ? wmi : 0);
    m_whiteDark->setValue(printerValue("whiteDarkPct", 100).toInt());
    m_whiteDark->setEnabled(wmode == "luminance");
    m_whiteCurveX->setValue(printerValue("whiteCurveX", 50).toInt());
    m_whiteCurveDelta->setValue(printerValue("whiteCurveDelta", 0).toInt());
    const int choke = printerValue("whiteChoke", 0).toInt();
    m_whiteChokeSl->setValue(choke);
    m_whiteChoke->setValue(choke / 100.0);
    m_loading = false;
    updateChokeTooltip();
}

void DwripPanel::save()
{
    printerSetValue("dropMode", dropCode());
    printerSetValue("resolution", m_resolution->currentData().toString());
    printerSetValue("inkColor", m_inkColorSpin->value());
    printerSetValue("whiteLimit", m_whiteSpin->value());
    printerSetValue("whiteMode", m_whiteMode->currentData().toString());
    printerSetValue("whiteDarkPct", m_whiteDark->value());
    printerSetValue("whiteCurveX", m_whiteCurveX->value());
    printerSetValue("whiteCurveDelta", m_whiteCurveDelta->value());
    printerSetValue("whiteChoke", int(m_whiteChoke->value() * 100 + 0.5));
}

void DwripPanel::updateChokeTooltip()
{
    // The pixel equivalent lives in the tooltip, not on the panel: worth knowing
    // when you want it, not worth a permanent line of text in a narrow column.
    struct { const char *name; int h, v; } grid[] = {
        {"720x360", 720, 720}, {"720x720", 720, 720}, {"1440x720", 360, 720},
        {"1440x1440", 360, 1440}, {"2880x2880", 720, 2880},
    };
    const QString res = m_resolution->currentData().toString();
    int h = 360, v = 1440;   // source of truth is src/resolution.c
    for (const auto &g : grid)
        if (res == g.name) { h = g.h; v = g.v; break; }

    QString tip =
        "Shrink the white base inward by this much so the colour overhangs it "
        "(flag --white-choke).\n"
        "Hides a white halo when the colour and white layers register slightly "
        "off. 0.1\u2013""0.3 mm is typical; 0 = off.";
    const double mm = m_whiteChoke->value();
    if (mm > 0)
        tip += QString("\n\nOn %1 that is about %2 \u00d7 %3 raster pixels \u2014 "
                       "the same distance either way, but the grid is not square.")
                   .arg(res)
                   .arg(qRound(mm / (25.4 / h)))
                   .arg(qRound(mm / (25.4 / v)));
    m_whiteChoke->setToolTip(tip);
    m_whiteChokeSl->setToolTip(tip);
}
