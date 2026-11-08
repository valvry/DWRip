/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "SpotMaskDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QPixmap>
#include <QTimer>

SpotMaskDialog::SpotMaskDialog(const QImage &artwork, const QString &artworkPath,
                               int embossLayers, QWidget *parent)
    : QDialog(parent), m_art(artwork), m_artPath(artworkPath),
      m_layers(embossLayers)
{
    setWindowTitle("Generate Spot Mask");

    // The preview is a few hundred pixels wide, so there is nothing to gain
    // from running every slider move over a full-resolution photograph — and
    // plenty to lose: on a 9 MP image that is ~170 ms per keystroke, and it
    // grows with the pixel count. Scaling an image down on the canvas does not
    // help, because that changes the print size, not the source pixels.
    const int kProxyMax = 900;
    const int side = qMax(m_art.width(), m_art.height());
    m_proxyScale = side > kProxyMax ? double(kProxyMax) / side : 1.0;
    m_proxy = m_proxyScale < 1.0
        ? m_art.scaled(m_art.size() * m_proxyScale, Qt::KeepAspectRatio,
                       Qt::SmoothTransformation)
        : m_art;
    auto *root = new QHBoxLayout(this);

    // ── Controls ──
    auto *form = new QFormLayout;
    m_method = new QComboBox(this);
    m_method->addItem("Luminance — texture (brick, wood, cloth)",
                      int(SpotMethod::Luminance));
    m_method->addItem("Shape — logo / text, raised flat", int(SpotMethod::Shape));
    m_method->addItem("Shape + bevel — carved edge", int(SpotMethod::Bevel));
    m_method->addItem("Outline — raised edge only", int(SpotMethod::Outline));
    form->addRow("Method:", m_method);

    m_invert = new QCheckBox("Invert (dark becomes high)", this);
    form->addRow(QString(), m_invert);

    // Seed the levels from this image, so the first preview is already useful
    // rather than a flat grey slab the user has to rescue by hand.
    int autoLo = 0, autoHi = 255;
    suggestSpotLevels(m_proxy, &autoLo, &autoHi);   // percentiles are scale-free
    // Slider + number for every setting: the slider is for hunting the shape,
    // the number for returning to one that worked.
    auto sliderRow = [this](QSlider *&sl, QSpinBox *&sp, int lo, int hi, int val,
                            const char *suffix, const QString &tip) {
        auto *w = new QWidget(this);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        sl = new QSlider(Qt::Horizontal, w);
        sl->setRange(lo, hi); sl->setValue(val);
        sp = new QSpinBox(w);
        sp->setRange(lo, hi); sp->setValue(val);
        if (suffix) sp->setSuffix(suffix);
        sp->setMinimumWidth(76);
        sl->setToolTip(tip); sp->setToolTip(tip);
        h->addWidget(sl, 1);
        h->addWidget(sp, 0);
        // Keep the pair in step without the two echoing each other forever.
        connect(sl, &QSlider::valueChanged, this, [this, sp](int v) {
            if (m_syncing) return;
            m_syncing = true; sp->setValue(v); m_syncing = false;
            scheduleRefresh();
        });
        connect(sp, &QSpinBox::valueChanged, this, [this, sl](int v) {
            if (m_syncing) return;
            m_syncing = true; sl->setValue(v); m_syncing = false;
            scheduleRefresh();
        });
        return w;
    };

    form->addRow("Low (flat):", sliderRow(m_blackSl, m_black, 0, 254, autoLo,
        nullptr, "Anything this dark or darker becomes flat"));
    form->addRow("High (full):", sliderRow(m_whiteSl, m_white, 1, 255, autoHi,
        nullptr, "Anything this bright or brighter becomes full height"));
    form->addRow("Smooth:", sliderRow(m_smoothSl, m_smooth, 0, 64, 0, " px",
        "Smooth the height map — keeps image noise from turning into jagged relief"));
    form->addRow("Bevel width:", sliderRow(m_bevelSl, m_bevel, 1, 200, 12, " px",
        "How far the rim ramps in from the edge"));
    form->addRow("Outline width:", sliderRow(m_outlineSl, m_outline, 1, 200, 6, " px",
        "Width of the raised band along the edge"));

    m_quantise = new QCheckBox(QString("Match the %1 emboss layers")
                                   .arg(m_layers > 0 ? m_layers : 8), this);
    m_quantise->setChecked(m_layers > 0);
    m_quantise->setToolTip(
        "Round the height to the number of layers the printer actually stacks, "
        "so the preview shows the steps that will really be there.");
    form->addRow(QString(), m_quantise);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet("color: palette(mid);");
    form->addRow(m_hint);

    auto *left = new QVBoxLayout;
    left->addLayout(form);
    left->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *save = buttons->addButton("Save mask as PNG…", QDialogButtonBox::AcceptRole);
    connect(save, &QPushButton::clicked, this, &SpotMaskDialog::saveMask);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    left->addWidget(buttons);
    root->addLayout(left);

    // ── Preview ──
    m_preview = new QLabel(this);
    m_preview->setMinimumSize(320, 320);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet("background:#202020; border:1px solid palette(mid);");
    root->addWidget(m_preview, 1);

    // Coalesce edits: holding a spin box's arrow fires many changes, and each
    // one would otherwise queue its own rebuild.
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(80);
    connect(m_debounce, &QTimer::timeout, this, &SpotMaskDialog::refresh);

    connect(m_method, &QComboBox::currentIndexChanged, this,
            &SpotMaskDialog::scheduleRefresh);
    connect(m_invert, &QCheckBox::toggled, this, &SpotMaskDialog::scheduleRefresh);
    connect(m_quantise, &QCheckBox::toggled, this, &SpotMaskDialog::scheduleRefresh);

    resize(880, 480);
    refresh();
}

void SpotMaskDialog::scheduleRefresh()
{
    m_debounce->start();
}

SpotParams SpotMaskDialog::paramsFor(double scale) const
{
    SpotParams p = params();
    if (scale >= 1.0)
        return p;
    // Only the distance-based settings are in pixels; levels and quantisation
    // are scale-free. Never round a requested radius down to nothing, or a thin
    // bevel would vanish from the preview but appear in the saved file.
    auto s = [scale](int v) { return v > 0 ? qMax(1, int(v * scale + 0.5)) : v; };
    p.smoothPx = s(p.smoothPx);
    p.bevelPx = s(p.bevelPx);
    p.outlinePx = s(p.outlinePx);
    return p;
}

SpotParams SpotMaskDialog::params() const
{
    SpotParams p;
    p.method = SpotMethod(m_method->currentData().toInt());
    p.invert = m_invert->isChecked();
    p.blackPoint = m_black->value();
    p.whitePoint = m_white->value();
    p.smoothPx = m_smooth->value();
    p.bevelPx = m_bevel->value();
    p.outlinePx = m_outline->value();
    p.levels = (m_quantise->isChecked() && m_layers > 0) ? m_layers : 0;
    return p;
}

void SpotMaskDialog::refresh()
{
    const SpotParams p = params();

    // Only show the widths that the chosen method uses.
    const bool bevel = (p.method == SpotMethod::Bevel);
    const bool outline = (p.method == SpotMethod::Outline);
    const bool levelsUseful = (p.method == SpotMethod::Luminance);
    m_bevel->setEnabled(bevel);      m_bevelSl->setEnabled(bevel);
    m_outline->setEnabled(outline);  m_outlineSl->setEnabled(outline);
    m_black->setEnabled(levelsUseful);  m_blackSl->setEnabled(levelsUseful);
    m_white->setEnabled(levelsUseful);  m_whiteSl->setEnabled(levelsUseful);

    switch (p.method) {
    case SpotMethod::Luminance:
        m_hint->setText("Brightness is height. Levels set how deep the dark "
                        "parts sit — on a brick photo, that is the mortar.");
        break;
    case SpotMethod::Shape:
        m_hint->setText("Everything opaque is raised to full height, flat on "
                        "top.");
        break;
    case SpotMethod::Bevel:
        m_hint->setText("Flat on top with a sloped rim, so the edge reads as "
                        "carved rather than stuck on.");
        break;
    case SpotMethod::Outline:
        m_hint->setText("Only a band along the edge is raised.");
        break;
    }

    m_mask = buildSpotMask(m_proxy, paramsFor(m_proxyScale));
    if (!m_mask.isNull())
        m_preview->setPixmap(QPixmap::fromImage(m_mask).scaled(
            m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void SpotMaskDialog::saveMask()
{
    if (m_proxy.isNull())
        return;
    // Default next to the artwork, named after it — the same convention the
    // hand-made masks already follow (foo.png → foo_spot.png).
    QFileInfo fi(m_artPath);
    const QString suggested = fi.absolutePath().isEmpty()
        ? QString("spot.png")
        : fi.absolutePath() + "/" + fi.completeBaseName() + "_spot.png";
    const QString path = QFileDialog::getSaveFileName(
        this, "Save spot mask", suggested, "PNG (*.png)");
    if (path.isEmpty())
        return;
    // Saved at full resolution — the preview ran on a proxy for speed, but the
    // file the printer gets must carry every pixel of the original.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QImage full = buildSpotMask(m_art, params());
    QApplication::restoreOverrideCursor();
    if (!full.save(path, "PNG")) {
        QMessageBox::warning(this, "Generate Spot Mask",
                             "Could not write:\n" + path);
        return;
    }
    m_savedPath = path;
    accept();
}
