/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QColor>
#include <QString>
#include <QFont>

class QWidget;

// Centralised modern light theme (blue accent, soft depth). One source of truth
// so the canvas, panels and stylesheet stay consistent.
namespace Theme {

// ── Colour tokens ──
inline QColor bgApp()      { return QColor("#f3f5f9"); }
inline QColor surface()    { return QColor("#ffffff"); }
inline QColor border()     { return QColor("#e7ebf1"); }
inline QColor divider()    { return QColor("#eef1f6"); }
inline QColor text()       { return QColor("#1e293b"); }
inline QColor textSecondary() { return QColor("#64748b"); }
inline QColor textMuted()  { return QColor("#94a3b8"); }
inline QColor accent()     { return QColor("#2563eb"); }
inline QColor accentHover(){ return QColor("#1d4ed8"); }
inline QColor accentTint() { return QColor("#eef4ff"); }

// Canvas
inline QColor desk()       { return QColor("#e9eef5"); }
inline QColor gridMinor()  { return QColor("#eef2f7"); }
inline QColor gridMajor()  { return QColor("#dbe2ec"); }
inline QColor bedOutline() { return QColor("#cbd5e6"); }

// ── Helpers ──
QFont   pickUiFont();                  // first available modern UI font @ 13px
QString modernStyleSheet();            // app-wide QSS
void    applyShadow(QWidget *w, int blur = 24, int dy = 6, int alpha = 26);

} // namespace Theme
