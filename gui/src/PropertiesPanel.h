/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QWidget>
#include "Units.h"

class ImageItem;
class QDoubleSpinBox;
class QToolButton;
class QPushButton;

// Right-panel "Layout" tab: numeric position/size/rotation plus
// mirror/rotate/reset controls for the active image. Edits are
// non-destructive — they only drive the ImageItem's transform.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget *parent = nullptr);

    void setUnit(Unit u);
    void bind(ImageItem *item);     // nullptr → disabled
    void refresh();                 // pull all values from the item

    // True when a rotation of `deg` leaves the item's own width running
    // vertically on the bed. Static so the self-test can check the rule.
    static bool axesSwappedFor(double deg);
    void refreshPosition();         // pull only X/Y (used during drag)

private:
    void applyPos();
    void applyWidth();
    void applyHeight();

    // True when the item's own width runs vertically on the bed, i.e. it has
    // been turned roughly a quarter. The Width/Height fields describe what the
    // user sees on the bed, so when the axes swap the fields must drive the
    // opposite dimension of the item — otherwise typing a width after a 90°
    // rotation changes the height.
    bool axesSwapped() const;
    void applyPercent();
    void applyRotation();
    void doReset();
    void updateSuffixes();

    Unit m_unit = Unit::Cm;
    ImageItem *m_item = nullptr;
    bool m_updating = false;

    QDoubleSpinBox *m_x = nullptr;
    QDoubleSpinBox *m_y = nullptr;
    QDoubleSpinBox *m_w = nullptr;
    QDoubleSpinBox *m_h = nullptr;
    QDoubleSpinBox *m_pct = nullptr;
    QDoubleSpinBox *m_rot = nullptr;
    QToolButton *m_lock = nullptr;    // aspect lock (checkable)
    QToolButton *m_flipH = nullptr;
    QToolButton *m_flipV = nullptr;
    QWidget *m_body = nullptr;         // everything that should disable w/o image
};
