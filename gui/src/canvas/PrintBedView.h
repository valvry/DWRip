/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QGraphicsView>
#include <QPointF>
#include <QPolygonF>
#include "Units.h"

class ImageItem;

// QGraphicsView whose rulers are drawn glued to the bed's top/left edges (they
// move with the bed when scrolling/zooming, instead of being fixed strips).
// Plain mouse-wheel scrolls; Ctrl+wheel zooms.
class PrintBedView : public QGraphicsView {
    Q_OBJECT
public:
    explicit PrintBedView(QGraphicsScene *scene, Unit unit = Unit::Cm,
                          QWidget *parent = nullptr);

    double zoomFactor() const { return transform().m11(); }
    void setUnit(Unit u);
    void setActiveItem(ImageItem *item);   // item shown with transform handles
    void setBedVisible(bool v);            // hide rulers when canvas is empty

public slots:
    void zoomBy(double factor);            // multiply current zoom (clamped)
    void zoomIn()  { zoomBy(1.25); }
    void zoomOut() { zoomBy(1.0 / 1.25); }
    void fitBed(const QRectF &bedRect);    // fit a rect (mm) to viewport WIDTH

signals:
    void viewChanged();
    void mouseMovedScene(const QPointF &scenePos);
    void itemEdited();                     // active item resized/rotated via handles

protected:
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent *e) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    double niceStepMm() const;             // tick spacing in mm for current zoom

    // ── Interactive transform handles ──
    enum Handle { H_None, H_TL, H_TR, H_BR, H_BL, H_Rot };
    bool   handlesVisible() const;
    QPolygonF cornersScene() const;        // TL, TR, BR, BL of the item rect (scene)
    QPoint rotKnobVp() const;              // rotate knob in viewport px
    Handle handleAt(const QPoint &vp) const;
    void   beginHandleDrag(Handle h, const QPointF &scenePress);
    void   updateHandleDrag(const QPointF &scene);
    void   drawHandles(QPainter *p);

    Unit m_unit;
    QPointF m_cursorScene;                 // last cursor pos (scene mm); NaN = none
    static constexpr int kRuler = 22;      // ruler band thickness (px)

    ImageItem *m_active = nullptr;
    bool m_bedVisible = true;
    Handle m_drag = H_None;
    QPointF m_anchorScene;                 // resize: fixed opposite corner
    QPointF m_oppLocal;                    // resize: opposite corner in item px
    QPointF m_axisX, m_axisY;              // resize: item axes in scene (unit vecs)
    double  m_aspect = 1.0;
    QPointF m_centerScene;                 // rotate: fixed centre
    double  m_pressRot = 0.0;              // rotate: rotation at press
    double  m_startAngle = 0.0;            // rotate: pointer angle at press
};
