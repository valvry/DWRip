/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QDialog>
#include <QWidget>

class QSpinBox;
class QComboBox;
class QButtonGroup;
class CurveEditor;

// Calibration page — per-channel trim (--trim), total-ink cap (-l), correction
// curves (--curve-point-*), dot pattern and layer registration for the ACTIVE
// printer profile. Built as a plain widget so App Settings can host it as a tab
// (one window per printer, rather than a chain of dialogs); InkSettingsDialog
// below is the stand-alone wrapper kept for the --ink-dialog entry point.
//
// The "Test print …" buttons run dwrip with the CURRENT (on-screen) values, so
// the user can calibrate by eye without committing or closing anything.
class InkSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit InkSettingsPage(QWidget *parent = nullptr);

    void load();   // active profile → widgets (call after switching printers)
    void save();   // widgets → active profile (call from the host's OK)

private slots:
    void testChart();
    void testGrayChart();     // run dwrip --chart-gray to calibrate grey balance
    void testRegistration();  // run dwrip --reg-test to calibrate reg-v by eye
private:
    QString liveTrimSpec() const;   // "C:-12,Y:-5" from the current spinboxes
    QString liveCurvePointSpec(int row) const;  // "X,D" (empty when delta is zero)
    void applyLiveCurves(struct JobConfig &cfg) const;

    void selectCurveChannel(int ch);   // -1 = all, 0..3 = C/M/Y/K
    void setCurvePointForSelection(int tonePct, int deltaPct);

    QSpinBox *m_c = nullptr;
    QSpinBox *m_m = nullptr;
    QSpinBox *m_y = nullptr;
    QSpinBox *m_k = nullptr;
    QSpinBox *m_tac = nullptr;          // -l total-ink (TAC) cap, 0..100 (100=off)
    int m_curvePointPct[4] = {50, 50, 50, 50};
    int m_curvePointDelta[4] = {};
    int m_curveCh = 0;                 // -1 = all, 0..3 = channel shown
    CurveEditor  *m_curveEd = nullptr; // drag-point curve (Photoshop-style)
    QSpinBox *m_curveTone = nullptr;   // numeric tone position 1..99%
    QSpinBox *m_curveDelta = nullptr;  // numeric point delta +/-90%
    QButtonGroup *m_chGroup = nullptr; // C/M/Y/K selector
    QComboBox *m_structPat = nullptr;   // --struct-dither dot dispersion
    QSpinBox *m_regV = nullptr;   // colour-vs-white vertical registration
};

// Stand-alone wrapper: the calibration page plus OK/Cancel.
class InkSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit InkSettingsDialog(QWidget *parent = nullptr);

private slots:
    void accept() override;

private:
    InkSettingsPage *m_page = nullptr;
};
