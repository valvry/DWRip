/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QDialog>
#include <QList>
#include "AppSettings.h"
#include "ChannelProfileDialog.h"
#include "PrinterProfile.h"

struct JobConfig;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QRadioButton;
class QLineEdit;
class QLabel;
class QTabWidget;
class InkSettingsPage;

// Fill every MACHINE-owned field into cfg by reading the active printer profile
// (see PrinterProfile.h). Used by export without opening any dialog, and re-run
// by the Export dialog when the target printer is switched.
void applyPrinterSettings(JobConfig &cfg);

// Assemble the --trim spec (e.g. "C:-12,Y:-5") from the persisted per-channel
// deltas (QSettings dwrip/trimC..trimK). Empty when all deltas are 0.
QString trimSpecFromSettings();

// Legacy 3-anchor curve ("d25,d50,d75"), kept for old CLI compatibility.
QString curveSpecFromSettings(char ch);

// Assemble one channel's --curve-point-* spec ("X,D") from persisted point
// settings. If only legacy 25/50/75 anchors exist, migrate by choosing the
// strongest anchor as the single control point. Empty when delta is 0.
void curvePointFromSettings(char ch, int *tonePct, int *deltaPct);
QString curvePointSpecFromSettings(char ch);

// The structured dither is the only halftone path, so the only remaining
// choice is its dot dispersion. Mask-free error-diffusion modes only,
// best-first by print test; the mask modes are CLI-only (see kStructPat).
#include <QStringList>
QStringList structPatternLabels();                 // Jarvis / Jarvis+random / Serp+random / Serpentine / Jitter
QString     structPatternKey(int index);           // serpjarvis / serpjarvisrandom / serprandom / serpentine / jitter
int         structPatternIndex(const QString &key);

// App Settings: the canvas (bed size + unit — GLOBAL, shared by all printers)
// plus the editor for PRINTER PROFILES. The profile combo picks which machine
// every field below it belongs to, and switching it also switches the app-wide
// active printer (see PrinterProfile.h). Classic OK/Cancel modal.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const AppSettings &current, QWidget *parent = nullptr);

    AppSettings settings() const;        // canvas tier (bed + unit)
    void applyTo(JobConfig &cfg) const;  // Tier-1 dwrip flags

private slots:
    void onUnitChanged();
    void onProfileChanged(int);          // switch the active printer, swap fields
    void onNewProfile();
    void onDuplicateProfile();           // clone incl. the whole calibration
    void onRenameProfile();
    void onDeleteProfile();
    void onEditChannelOrder();           // 6-slot ink layout of this printer
    void reloadPrinters();               // re-scan CUPS queues into m_printer
    void accept() override;              // save() then close

private:
    void save() const;
    void reloadProfileCombo();           // repopulate from printersLoad()
    void loadProfileFields();            // active profile → widgets
    void saveProfileFields() const;      // widgets → active profile
    void updateOrderLabel();
    QString currentProfileId() const;

    // Canvas
    QDoubleSpinBox *m_bedW = nullptr;
    QDoubleSpinBox *m_bedH = nullptr;
    QComboBox      *m_unit = nullptr;    // 0=cm, 1=mm

    // Printer profiles
    QComboBox   *m_profileCombo = nullptr;
    QStringList  m_profileIds;           // parallel to the combo rows
    bool         m_switching = false;    // suppress signals while repopulating
    QString      m_channelOrder;         // this printer's 6-slot ink layout
    QLabel      *m_orderLabel = nullptr; // colour chips for m_channelOrder

    // Printer profile editor (tabbed)
    QTabWidget      *m_tabs = nullptr;
    InkSettingsPage *m_inkPage = nullptr;   // Calibration tab
    QComboBox *m_resolution = nullptr;
    QComboBox *m_dropMode = nullptr;
    QSpinBox  *m_inkColor = nullptr;
    QSpinBox  *m_whiteLimit = nullptr;
    QRadioButton *m_bidi = nullptr;
    QRadioButton *m_unidi = nullptr;
    QDoubleSpinBox *m_margin = nullptr;
    QDoubleSpinBox *m_uvTail = nullptr;
    QSpinBox     *m_uvSpeed = nullptr;
    QSpinBox     *m_leadInFeed = nullptr;
    QComboBox    *m_leadInSweep = nullptr;
    QLineEdit    *m_icc = nullptr;
    QComboBox    *m_intent = nullptr;
    QComboBox    *m_printer = nullptr;   // CUPS queue picker (editable)

    Unit m_curUnit = Unit::Cm;           // tracks unit for field conversion
};
