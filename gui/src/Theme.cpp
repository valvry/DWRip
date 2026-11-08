/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "Theme.h"
#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QWidget>

namespace Theme {

QFont pickUiFont()
{
    const QStringList prefer = {"Inter", "Segoe UI", "Roboto", "Noto Sans",
                               "Cantarell", "DejaVu Sans"};
    const QStringList have = QFontDatabase::families();
    QFont f;
    for (const QString &fam : prefer) {
        if (have.contains(fam, Qt::CaseInsensitive)) {
            f.setFamily(fam);
            break;
        }
    }
    f.setPixelSize(13);
    return f;
}

void applyShadow(QWidget *w, int blur, int dy, int alpha)
{
    auto *e = new QGraphicsDropShadowEffect(w);
    e->setBlurRadius(blur);
    e->setOffset(0, dy);
    e->setColor(QColor(15, 23, 42, alpha));   // slate-900 @ alpha
    w->setGraphicsEffect(e);
}

QString modernStyleSheet()
{
    return QStringLiteral(R"(
        QMainWindow, QDialog { background: #f3f5f9; color: #1e293b; }

        /* Menu bar */
        QMenuBar { background: transparent; padding: 2px 6px; color: #475569; }
        QMenuBar::item { background: transparent; border-radius: 6px; padding: 5px 10px; }
        QMenuBar::item:selected { background: #e8eef7; color: #1e293b; }
        QMenu { background: #ffffff; border: 1px solid #e7ebf1; border-radius: 10px; padding: 6px; }
        QMenu::item { border-radius: 6px; padding: 7px 22px; }
        QMenu::item:selected { background: #eef4ff; color: #1d4ed8; }
        QMenu::separator { height: 1px; background: #eef1f6; margin: 5px 8px; }

        /* Header toolbar */
        QToolBar#mainToolBar { background: #ffffff; border: none; spacing: 6px; padding: 8px 12px; }
        QToolBar#mainToolBar::separator { background: #e7ebf1; width: 1px; margin: 8px 8px; }
        QToolBar#mainToolBar QToolButton {
            background: transparent; border: 1px solid transparent; border-radius: 8px;
            padding: 6px 10px; color: #334155; font-weight: 500;
        }
        QToolBar#mainToolBar QToolButton:hover { background: #eef4ff; color: #1d4ed8; }
        QToolBar#mainToolBar QToolButton:pressed { background: #dbe8ff; }
        /* Active toggle (e.g. White / Colour-limit preview) — clearly distinct */
        QToolBar#mainToolBar QToolButton:checked {
            background: #dbe8ff; border: 1px solid #2563eb; color: #1d4ed8;
        }
        QToolBar#mainToolBar QToolButton:checked:hover { background: #cdddff; }
        /* Primary action (Export) */
        QToolBar#mainToolBar QToolButton[primary="true"] {
            background: #2563eb; color: #ffffff; border: none; padding: 7px 14px; font-weight: 600;
        }
        QToolBar#mainToolBar QToolButton[primary="true"]:hover { background: #1d4ed8; }
        QToolBar#mainToolBar QToolButton[primary="true"]:pressed { background: #1e40af; }

        /* Dock panels — float as cards on the app background */
        QDockWidget { color: #64748b; font-size: 11px; font-weight: 700; }
        QDockWidget::title {
            background: transparent; padding: 8px 10px 4px 12px; text-align: left;
            text-transform: uppercase; letter-spacing: 1px;
        }
        QFrame#card { background: #ffffff; border: 1px solid #e7ebf1; border-radius: 12px; }

        /* Tabs — underline accent */
        QTabWidget::pane { border: none; background: transparent; top: -1px; }
        QTabBar::tab {
            background: transparent; border: none; border-bottom: 2px solid transparent;
            padding: 8px 14px; margin-right: 2px; color: #64748b; font-weight: 600;
        }
        QTabBar::tab:selected { color: #2563eb; border-bottom: 2px solid #2563eb; }
        QTabBar::tab:hover:!selected { color: #334155; }

        /* Group boxes — visible section panels */
        QGroupBox {
            background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 10px;
            margin-top: 18px; padding: 12px 10px 10px 10px; font-weight: 600; color: #334155;
        }
        QGroupBox::title {
            subcontrol-origin: margin; subcontrol-position: top left; left: 10px;
            padding: 2px 8px; color: #475569;
        }

        /* Inputs */
        QDoubleSpinBox, QSpinBox, QComboBox, QLineEdit {
            background: #ffffff; border: 1px solid #dfe4ec; border-radius: 8px;
            padding: 5px 8px; min-height: 22px; color: #1e293b;
            selection-background-color: #2563eb; selection-color: #ffffff;
        }
        QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus, QLineEdit:focus {
            border: 2px solid #2563eb; padding: 4px 7px;
        }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
        QAbstractSpinBox {
            padding-right: 18px;   /* room for the stepper column */
        }
        QAbstractSpinBox:focus { padding-right: 17px; }
        QAbstractSpinBox::up-button {
            subcontrol-origin: border; subcontrol-position: top right;
            width: 18px; border: none; border-left: 1px solid #eef1f6;
            border-top-right-radius: 8px;
        }
        QAbstractSpinBox::down-button {
            subcontrol-origin: border; subcontrol-position: bottom right;
            width: 18px; border: none; border-left: 1px solid #eef1f6;
            border-bottom-right-radius: 8px;
        }
        QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: #eef4ff; }
        QAbstractSpinBox::up-arrow   { image: url(:/icons/chevron-up.svg);   width: 11px; height: 11px; }
        QAbstractSpinBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 11px; height: 11px; }
        QComboBox QAbstractItemView {
            background: #ffffff; border: 1px solid #e7ebf1;
            selection-background-color: #eef4ff; selection-color: #1d4ed8; outline: 0;
        }
        /* Give popup items an explicit height. Without this, styling the combo
         * view under Fusion leaves items with a tiny sizeHint, so the popup
         * mis-sizes and scrolls even for a few items. */
        QComboBox QAbstractItemView::item {
            min-height: 26px; padding: 2px 8px;
        }

        /* Buttons */
        QPushButton, QToolButton {
            background: #ffffff; border: 1px solid #dfe4ec; border-radius: 8px;
            padding: 7px 12px; color: #334155; font-weight: 500;
        }
        QPushButton:hover, QToolButton:hover { background: #f6f8fc; border-color: #c7d2e5; }
        QPushButton:pressed, QToolButton:pressed { background: #eef2f8; }
        QToolButton:checked { background: #eef4ff; border-color: #2563eb; color: #1d4ed8; }
        QPushButton[primary="true"] {
            background: #2563eb; border: none; color: #ffffff; font-weight: 600; padding: 8px 16px;
        }
        QPushButton[primary="true"]:hover { background: #1d4ed8; }
        QPushButton[primary="true"]:pressed { background: #1e40af; }
        QPushButton:disabled, QToolButton:disabled { color: #b6c0cf; background: #f4f6fa; border-color: #eaeef4; }

        /* Checkboxes */
        QCheckBox { color: #334155; spacing: 7px; }

        /* Scrollbars — slim & modern */
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #cdd5e2; border-radius: 5px; min-height: 28px; }
        QScrollBar::handle:vertical:hover { background: #aab6c9; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal { background: #cdd5e2; border-radius: 5px; min-width: 28px; }
        QScrollBar::handle:horizontal:hover { background: #aab6c9; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        /* Status bar */
        QStatusBar { background: #ffffff; border-top: 1px solid #e7ebf1; color: #64748b; }
        QStatusBar::item { border: none; }
        QStatusBar QLabel { color: #64748b; padding: 0 6px; }

        QToolTip { background: #1e293b; color: #ffffff; border: none; border-radius: 6px; padding: 5px 8px; }
    )");
}

} // namespace Theme
