/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QWidget>

// Photoshop-style single-point tone-curve editor for one ink channel.
// Endpoints are fixed. Drag the point horizontally to choose the tone region
// and vertically to set the signed density delta. Double-click resets to 50,0.
class CurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit CurveEditor(QWidget *parent = nullptr);

    void setPoint(int tonePct, int deltaPct); // tone position 1..99, delta +/-90
    void point(int *tonePct, int *deltaPct) const;
    void setCurveColor(const QColor &c);     // ink colour for the curve line

    QSize sizeHint() const override { return {280, 220}; }
    QSize minimumSizeHint() const override { return {220, 180}; }

signals:
    void changed();                          // any handle moved / reset

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    QRectF plotRect() const;                 // drawing area inside margins
    int pointXByte() const;                  // control input value 1..254
    int pointYByte() const;                  // control output value 0..255
    int curveValue(int x) const;             // smooth monotone LUT preview
    QPointF handlePos() const;               // widget coords of the handle
    bool hitHandle(const QPointF &p) const;

    int m_tonePct = 50;
    int m_deltaPct = 0;
    bool m_drag = false;
    QColor m_color = QColor(37, 99, 235);
};
