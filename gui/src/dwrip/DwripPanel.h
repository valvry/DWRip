/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QWidget>

struct JobConfig;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

// Populate a combo with the canonical quality choices (label + data code).
// Shared so the Export dialog, which edits the SAME per-printer settings, can
// never drift out of sync with this panel's item list.
void fillDropModeCombo(QComboBox *c);
void fillResolutionCombo(QComboBox *c);

// Right-panel "dwrip" tab — the quality flags:
//   -D drop mode, -R resolution, --ink-color colour density, -L white density.
//   (The total-ink cap / TAC, -l, lives in Ink Settings.)
// Auto-saved (no Save button) into the ACTIVE PRINTER PROFILE on every change
// and restored on construction. Drop mode and resolution are also editable in
// the Export dialog, so call reload() after that dialog closes.
class DwripPanel : public QWidget {
    Q_OBJECT
public:
    explicit DwripPanel(QWidget *parent = nullptr);

    // Write the Tier-3 fields into cfg (used at export time).
    void applyTo(JobConfig &cfg) const;

    // Re-read the widgets from QSettings (e.g. after Import Settings).
    void reload() { load(); }

signals:
    void changed();

private:
    void load();    // from QSettings
    void updateChokeTooltip();
    void save();    // to QSettings
    QString dropCode() const;

    QComboBox *m_drop = nullptr;
    QComboBox *m_resolution = nullptr;
    QSlider   *m_inkColorSlider = nullptr;   // --ink-color global CMYK density
    QSpinBox  *m_inkColorSpin = nullptr;
    QSlider   *m_whiteSlider = nullptr;       // -L white density (TAC moved to Ink Settings)
    QSpinBox  *m_whiteSpin = nullptr;
    QComboBox *m_whiteMode = nullptr;   // --white-mode alpha | luminance
    QSpinBox  *m_whiteDark = nullptr;   // --white-dark-pct (luminance only)
    QSpinBox  *m_whiteCurveX = nullptr;     // --white-curve X,D
    QSpinBox  *m_whiteCurveDelta = nullptr;
    QSlider   *m_whiteChokeSl = nullptr;   // --white-choke, hundredths of a mm
    class QDoubleSpinBox *m_whiteChoke = nullptr;
    bool m_loading = false;   // suppress save() during initial load / sync
};
