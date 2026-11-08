/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>
#include "dwrip/JobConfig.h"

class QButtonGroup;
class QCheckBox;
class QLineEdit;
class QLabel;
class QComboBox;
class QWidget;
class QFormLayout;
class QSpinBox;

// Active-image facts shown in the export summary (read-only confirmation).
struct ExportImageInfo {
    QString name;
    QString path;               // source file, for naming a generated spot mask
    QImage  image;              // source pixels, to build a spot mask from
    int wpx = 0, hpx = 0;       // source pixels
    double wmm = 0, hmm = 0;    // print size (mm)
    double dpi = 0;
};

// Tier 2 — per-job export choices: layer mode, white-first, head zones, output
// path, optional send-to-device. Classic Export/Cancel. Also shows a read-only
// summary of the active image + effective settings (Tier 1/3 from `base`).
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    // base: Tier-1+Tier-3 settings (for the summary). sendConfigured: Tier-1 has
    // a send device set (enables the checkbox).
    explicit ExportDialog(const QString &defaultOut, bool sendConfigured,
                          const JobConfig &base, const ExportImageInfo &img,
                          QWidget *parent = nullptr);

    void applyTo(JobConfig &cfg) const;   // mode/whiteFirst/zones/output/send
    QString outputPath() const;

private slots:
    void onPrinterChanged(int);      // switch the active profile, reload m_base
    void browse();
    void browseZ3();                // pick the zone-3 custom image (image mode)
    void browseSpot();              // pick the emboss spot mask PNG
    void generateSpot();            // build a spot mask from the artwork
    void previewEmboss();           // show a 2.5D shaded-relief preview
    void runEstimate();             // run dwrip --estimate → show ink mL + cost
    void updateModeZoneState();     // couples mode↔zones: 3-zone is Both-only,
                                    // single-layer forces 2 zones; also white-first
    void rebuildSummary();          // refresh the read-only summary text
    void accept() override;

private:
    void reloadPrinterCombo();
    void loadQualityFromProfile();   // active profile → resolution/drop combos

    QString modeValue() const;         // layer-mode preset → "both"/"color"/"white"
    int zonesValue() const;            // layer-mode preset → 2 / 3
    bool whiteFirstValue() const;      // layer-mode preset → white printed first

    JobConfig m_base;                  // Tier-1+Tier-3 settings (summary)
    ExportImageInfo m_img;             // active-image facts (summary)
    QLabel *m_summary = nullptr;
    QFormLayout *m_form = nullptr;        // to toggle the Zone-3 row (label+field)
    QComboBox *m_printer = nullptr;       // target printer profile
    QComboBox *m_resolution = nullptr;    // -R  (per-printer, editable here too)
    QComboBox *m_dropMode = nullptr;      // -D  (per-printer, editable here too)
    QStringList m_printerIds;             // parallel to m_printer rows
    bool m_switchingPrinter = false;      // suppress signals while repopulating
    QComboBox *m_layerMode = nullptr;     // layer-order preset (mode+order+zones)
    QComboBox *m_singleWeave = nullptr;   // single-layer sweep density (UV dose)
    QWidget   *m_z3Row = nullptr;         // zone-3 source row (visible in 3-zone)
    QComboBox *m_z3Mode = nullptr;        // same / mirror / image
    QLineEdit *m_z3Image = nullptr;       // custom PNG path (image mode)
    QWidget   *m_embossRow = nullptr;     // emboss controls (visible in emboss mode)
    QComboBox *m_embossPreset = nullptr;  // 8 / 16 / 32 layers
    QLineEdit *m_spotPath = nullptr;      // spot mask PNG
    QCheckBox *m_underbase = nullptr;     // thin white under colour
    QCheckBox *m_embossGray = nullptr;    // variable height (grayscale mask)
    QComboBox *m_embossRuns = nullptr;    // Single (Cara B) / Split (Cara A)
    QWidget   *m_embossRow2 = nullptr;    // Cara A extras (white-runs, colour-reg)
    QSpinBox  *m_embossWhiteRuns = nullptr;
    QSpinBox  *m_embossColorReg = nullptr;
    QLineEdit *m_output = nullptr;
    QCheckBox *m_send = nullptr;
    class QPushButton *m_estimateBtn = nullptr;  // "Estimate ink & cost"
    QLabel *m_estimateResult = nullptr;          // per-channel mL + cost table
};
