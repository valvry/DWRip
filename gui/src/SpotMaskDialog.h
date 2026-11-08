/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QDialog>
#include <QImage>
#include "SpotMask.h"

class QComboBox;
class QCheckBox;
class QSpinBox;
class QSlider;
class QLabel;
class QTimer;

// Build an emboss spot mask from the active artwork, with a live preview.
//
// The generated mask is saved as a PNG next to the artwork rather than kept
// inside the app: it is a starting point, and the last 10% of a relief is
// usually still worth a pass by hand in an editor — which is exactly how these
// masks were made before.
class SpotMaskDialog : public QDialog {
    Q_OBJECT
public:
    SpotMaskDialog(const QImage &artwork, const QString &artworkPath,
                   int embossLayers, QWidget *parent = nullptr);

    // Path the mask was written to, empty if nothing was saved.
    QString savedPath() const { return m_savedPath; }

private slots:
    void refresh();          // regenerate + repaint the preview
    void scheduleRefresh();  // coalesce rapid edits into one rebuild
    void saveMask();

private:
    SpotParams params() const;
    // Same settings with the pixel radii scaled, so a preview built from the
    // proxy looks like what saving at full resolution will produce.
    SpotParams paramsFor(double scale) const;

    QImage m_art;        // full resolution — what gets saved
    QImage m_proxy;      // downscaled copy — what the live preview works from
    double m_proxyScale = 1.0;
    QImage m_mask;
    QString m_artPath;
    QString m_savedPath;
    int m_layers = 0;

    QComboBox *m_method = nullptr;
    QCheckBox *m_invert = nullptr;
    // Each setting is a slider paired with a spin box: the slider to feel the
    // shape change, the number to come back to a value that worked.
    QSlider   *m_blackSl = nullptr;   QSpinBox *m_black = nullptr;
    QSlider   *m_whiteSl = nullptr;   QSpinBox *m_white = nullptr;
    QSlider   *m_smoothSl = nullptr;  QSpinBox *m_smooth = nullptr;
    QSlider   *m_bevelSl = nullptr;   QSpinBox *m_bevel = nullptr;
    QSlider   *m_outlineSl = nullptr; QSpinBox *m_outline = nullptr;
    bool m_syncing = false;           // guards slider <-> spin echo
    QCheckBox *m_quantise = nullptr;
    QLabel    *m_preview = nullptr;
    QLabel    *m_hint = nullptr;
    QTimer    *m_debounce = nullptr;
};
