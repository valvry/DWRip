/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "canvas/PrintBedView.h"
#include "canvas/PrintBedScene.h"
#include "canvas/ImageItem.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QApplication>
#include <algorithm>
#include <cmath>
#include <limits>

static constexpr int kHandle = 7;   // handle hit/half-draw size (px)
static constexpr int kKnobGap = 26; // rotate-knob distance above top edge (px)

PrintBedView::PrintBedView(QGraphicsScene *scene, Unit unit, QWidget *parent)
    : QGraphicsView(scene, parent),
      m_unit(unit),
      m_cursorScene(std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::quiet_NaN())
{
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::ScrollHandDrag);   // pan with left-drag on empty area
    setMouseTracking(true);
    setFrameStyle(QFrame::NoFrame);
    // NB: do NOT set a view background brush — that would make QGraphicsView skip
    // the scene's drawBackground(), hiding the bed. The scene paints the desk.
}

void PrintBedView::setUnit(Unit u)
{
    m_unit = u;
    viewport()->update();
}

void PrintBedView::setActiveItem(ImageItem *item)
{
    m_active = item;
    m_drag = H_None;
    viewport()->update();
}

void PrintBedView::setBedVisible(bool v)
{
    if (m_bedVisible == v) return;
    m_bedVisible = v;
    viewport()->update();
}

// ── Transform handles ──────────────────────────────────────────────

bool PrintBedView::handlesVisible() const
{
    return m_active && m_active->isSelected();
}

QPolygonF PrintBedView::cornersScene() const
{
    // mapToScene(QRectF) returns TL, TR, BR, BL of the (rotated) item rect.
    return m_active->mapToScene(m_active->boundingRect());
}

QPoint PrintBedView::rotKnobVp() const
{
    const QPolygonF c = cornersScene();
    const QPointF topMid = (c[0] + c[1]) / 2.0;
    const QPoint topMidVp = mapFromScene(topMid);
    const QPoint centerVp = mapFromScene(m_active->mapToScene(m_active->boundingRect().center()));
    QPointF dir = QPointF(topMidVp - centerVp);
    const double len = std::hypot(dir.x(), dir.y());
    if (len > 1e-6) dir /= len; else dir = QPointF(0, -1);
    return topMidVp + (dir * kKnobGap).toPoint();
}

PrintBedView::Handle PrintBedView::handleAt(const QPoint &vp) const
{
    if (!handlesVisible())
        return H_None;
    if ((vp - rotKnobVp()).manhattanLength() <= kHandle + 3)
        return H_Rot;
    const QPolygonF c = cornersScene();
    const Handle map[4] = {H_TL, H_TR, H_BR, H_BL};
    for (int i = 0; i < 4; ++i) {
        if ((vp - mapFromScene(c[i])).manhattanLength() <= kHandle + 3)
            return map[i];
    }
    return H_None;
}

void PrintBedView::beginHandleDrag(Handle h, const QPointF &scenePress)
{
    m_drag = h;
    const QRectF br = m_active->boundingRect();
    if (h == H_Rot) {
        m_centerScene = m_active->mapToScene(br.center());
        m_pressRot = m_active->rotationDeg();
        m_startAngle = std::atan2(scenePress.y() - m_centerScene.y(),
                                  scenePress.x() - m_centerScene.x());
        return;
    }
    const QPolygonF c = cornersScene();           // TL, TR, BR, BL
    const int idx = (h == H_TL ? 0 : h == H_TR ? 1 : h == H_BR ? 2 : 3);
    const int opp = (idx + 2) % 4;
    m_anchorScene = c[opp];
    const QPointF locals[4] = {br.topLeft(), br.topRight(), br.bottomRight(), br.bottomLeft()};
    m_oppLocal = locals[opp];
    const QPointF o = m_active->mapToScene(QPointF(0, 0));
    auto norm = [](QPointF v) { double l = std::hypot(v.x(), v.y()); return l > 1e-9 ? v / l : QPointF(1, 0); };
    m_axisX = norm(m_active->mapToScene(QPointF(1, 0)) - o);
    m_axisY = norm(m_active->mapToScene(QPointF(0, 1)) - o);
    m_aspect = m_active->aspect();
}

void PrintBedView::updateHandleDrag(const QPointF &scene)
{
    const bool shift = QApplication::keyboardModifiers() & Qt::ShiftModifier;
    if (m_drag == H_Rot) {
        double a = std::atan2(scene.y() - m_centerScene.y(), scene.x() - m_centerScene.x());
        double deg = m_pressRot + (a - m_startAngle) * 180.0 / M_PI;
        if (shift) deg = std::round(deg / 15.0) * 15.0;   // Shift = snap to 15°
        m_active->setRotationDeg(deg);
        const QPointF c2 = m_active->mapToScene(m_active->boundingRect().center());
        m_active->moveBy(m_centerScene.x() - c2.x(), m_centerScene.y() - c2.y());
    } else {
        const QPointF vec = scene - m_anchorScene;
        const double sW = std::abs(QPointF::dotProduct(vec, m_axisX));
        const double sH = std::abs(QPointF::dotProduct(vec, m_axisY));
        double newW, newH;
        if (shift) { newW = sW; newH = sH; }              // Shift = free aspect
        else { newW = std::max(sW, sH * m_aspect); newH = newW / m_aspect; }
        newW = std::max(newW, 1.0);
        newH = std::max(newH, 1.0);
        m_active->setSizeMm(newW, newH);
        const QPointF opp = m_active->mapToScene(m_oppLocal);
        m_active->moveBy(m_anchorScene.x() - opp.x(), m_anchorScene.y() - opp.y());
    }
    emit itemEdited();
}

void PrintBedView::drawHandles(QPainter *p)
{
    if (!handlesVisible())
        return;
    p->save();
    p->resetTransform();
    p->setRenderHint(QPainter::Antialiasing, true);

    const QPolygonF c = cornersScene();
    QPolygon vp;
    for (const QPointF &pt : c)
        vp << mapFromScene(pt);

    QPen pen(QColor(0x2f, 0x7d, 0xe1));
    pen.setWidth(2);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawPolygon(vp);

    const QPoint knob = rotKnobVp();
    const QPoint topMid = (vp[0] + vp[1]) / 2;
    p->drawLine(topMid, knob);
    p->setBrush(QColor(0xff, 0xff, 0xff));
    p->drawEllipse(knob, kHandle, kHandle);
    p->setBrush(QColor(0xff, 0xff, 0xff));
    for (const QPoint &h : vp)
        p->drawRoundedRect(QRect(h.x() - kHandle, h.y() - kHandle, kHandle * 2, kHandle * 2), 3, 3);

    p->restore();
}

double PrintBedView::niceStepMm() const
{
    // Aim for a major tick roughly every ~60 px, snapped to nice values in the
    // active display unit (so cm gives 1/2/5/10 cm steps).
    const double pxPerMm = std::max(zoomFactor(), 1e-6);
    const double um = unitMm(m_unit);
    const double rawUnit = (60.0 / pxPerMm) / um;
    const double mag = std::pow(10.0, std::floor(std::log10(rawUnit)));
    const double norm = rawUnit / mag;
    double nice;
    if (norm < 1.5)      nice = 1;
    else if (norm < 3.5) nice = 2;
    else if (norm < 7.5) nice = 5;
    else                 nice = 10;
    return nice * mag * um;   // back to mm
}

void PrintBedView::zoomBy(double factor)
{
    const double next = zoomFactor() * factor;
    if (next < 0.5 || next > 40.0)   // clamp to 0.5..40 px/mm
        return;
    scale(factor, factor);
    emit viewChanged();
}

void PrintBedView::fitBed(const QRectF &bedRect)
{
    // Fit to WIDTH only: the bed width (+ margin) fills the viewport width; a tall
    // bed simply overflows vertically and is reached by scrolling down.
    const double margin  = 0.18 * bedRect.width();
    const double targetW = bedRect.width() + 2.0 * margin;
    const double vpW = std::max(1, viewport()->width());
    const double s = std::clamp(vpW / targetW, 0.5, 40.0);

    resetTransform();
    scale(s, s);
    // Align the top of the bed near the top of the viewport, centred horizontally.
    const double halfVpH = (viewport()->height() / s) / 2.0;
    centerOn(bedRect.center().x(), bedRect.top() - margin + halfVpH);
    emit viewChanged();
}

void PrintBedView::wheelEvent(QWheelEvent *e)
{
    // Ctrl + wheel = zoom; plain wheel = scroll up/down (default behaviour).
    if (e->modifiers() & Qt::ControlModifier) {
        zoomBy(std::pow(1.0015, e->angleDelta().y()));
        e->accept();
    } else {
        QGraphicsView::wheelEvent(e);
    }
}

void PrintBedView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const Handle h = handleAt(e->pos());
        if (h != H_None) {
            beginHandleDrag(h, mapToScene(e->pos()));
            e->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(e);
}

void PrintBedView::mouseMoveEvent(QMouseEvent *e)
{
    m_cursorScene = mapToScene(e->pos());
    emit mouseMovedScene(m_cursorScene);

    if (m_drag != H_None) {
        updateHandleDrag(m_cursorScene);
        viewport()->update();
        e->accept();
        return;
    }

    if (handlesVisible()) {   // hover cursor feedback
        switch (handleAt(e->pos())) {
        case H_TL: case H_BR: viewport()->setCursor(Qt::SizeFDiagCursor); break;
        case H_TR: case H_BL: viewport()->setCursor(Qt::SizeBDiagCursor); break;
        case H_Rot:           viewport()->setCursor(Qt::CrossCursor);      break;
        default:              viewport()->unsetCursor();                   break;
        }
    }
    viewport()->update();   // refresh ruler cursor markers + handles
    QGraphicsView::mouseMoveEvent(e);
}

void PrintBedView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_drag != H_None) {
        m_drag = H_None;
        emit itemEdited();
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void PrintBedView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    emit viewChanged();
}

void PrintBedView::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    emit viewChanged();
}

void PrintBedView::drawForeground(QPainter *painter, const QRectF & /*rect*/)
{
    if (!m_bedVisible)
        return;   // empty canvas: no rulers/handles
    auto *bs = qobject_cast<PrintBedScene *>(scene());
    if (!bs)
        return;
    const double bedW = bs->bedWidthMm();
    const double bedH = bs->bedHeightMm();

    // Draw in device (pixel) coordinates so the rulers keep a constant size.
    painter->save();
    painter->resetTransform();

    painter->setRenderHint(QPainter::Antialiasing, false);   // crisp 1px ticks

    const QPoint tl = mapFromScene(QPointF(0, 0));
    const QPoint br = mapFromScene(QPointF(bedW, bedH));
    const QColor bandCol(0xf8, 0xfa, 0xfc);
    const QColor ink(0x48, 0x55, 0x66);      // major ticks + labels
    const QColor faint(0xc4, 0xce, 0xda);    // minor ticks
    const QColor sep(0xd8, 0xe0, 0xea);      // band/bed separator line
    const QColor accent(0x2f, 0x7d, 0xe1);
    const double step  = niceStepMm();
    const double minor = step / 5.0;         // 5 minor divisions per labelled tick
    const double um = unitMm(m_unit);

    QFont f = painter->font();
    f.setPixelSize(9);
    painter->setFont(f);

    // ── Horizontal ruler — glued just above the bed's top edge ──
    {
        const int top  = tl.y() - kRuler;
        const int base = tl.y();             // ticks grow down toward the bed edge
        painter->fillRect(QRect(tl.x() - kRuler, top, br.x() - tl.x() + kRuler, kRuler), bandCol);

        painter->setPen(faint);
        for (double mm = minor; mm < bedW; mm += minor) {
            const int x = mapFromScene(QPointF(mm, 0)).x();
            painter->drawLine(x, base - 4, x, base - 1);
        }
        painter->setPen(ink);
        for (double mm = 0; mm <= bedW + 1e-6; mm += step) {
            const int x = mapFromScene(QPointF(mm, 0)).x();
            painter->drawLine(x, base - 8, x, base - 1);
            const QString label = QString::number(mm / um, 'g', 6);
            const bool last = (mm + step > bedW + 1e-6);
            if (last)   // right-align the final label so it stays inside the bed width
                painter->drawText(QRect(x - 40, top, 39, 12), Qt::AlignRight | Qt::AlignVCenter, label);
            else
                painter->drawText(x + 3, top + 11, label);
        }
        painter->setPen(sep);
        painter->drawLine(tl.x() - kRuler, base - 1, br.x(), base - 1);

        if (!std::isnan(m_cursorScene.x())) {
            const int x = mapFromScene(m_cursorScene).x();
            painter->setPen(accent);
            painter->drawLine(x, top, x, base - 1);
        }
    }

    // ── Vertical ruler — glued just left of the bed's left edge ──
    {
        const int left = tl.x() - kRuler;
        const int base = tl.x();             // ticks grow right toward the bed edge
        painter->fillRect(QRect(left, tl.y(), kRuler, br.y() - tl.y()), bandCol);

        painter->setPen(faint);
        for (double mm = minor; mm < bedH; mm += minor) {
            const int y = mapFromScene(QPointF(0, mm)).y();
            painter->drawLine(base - 4, y, base - 1, y);
        }
        painter->setPen(ink);
        for (double mm = 0; mm <= bedH + 1e-6; mm += step) {
            const int y = mapFromScene(QPointF(0, mm)).y();
            painter->drawLine(base - 8, y, base - 1, y);
            const QString label = QString::number(mm / um, 'g', 6);
            const bool last = (mm + step > bedH + 1e-6);
            painter->save();
            painter->translate(left + 11, y);
            painter->rotate(-90);
            // Normal labels start just below the tick; the last one ends at the tick.
            if (last)
                painter->drawText(QRect(0, -kRuler / 2, 39, kRuler), Qt::AlignLeft | Qt::AlignVCenter, label);
            else
                painter->drawText(QRect(-39, -kRuler / 2, 38, kRuler), Qt::AlignRight | Qt::AlignVCenter, label);
            painter->restore();
        }
        painter->setPen(sep);
        painter->drawLine(base - 1, tl.y(), base - 1, br.y());

        if (!std::isnan(m_cursorScene.y())) {
            const int y = mapFromScene(m_cursorScene).y();
            painter->setPen(accent);
            painter->drawLine(left, y, base - 1, y);
        }
    }

    painter->restore();

    drawHandles(painter);   // transform handles on top of everything
}
