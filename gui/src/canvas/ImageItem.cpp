/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "canvas/ImageItem.h"
#include "canvas/PrintBedScene.h"
#include <QPixmap>
#include <QCursor>
#include <QTransform>
#include <algorithm>
#include <cmath>

// 1 inch = 25.4 mm. dotsPerMeterX is px per metre.
static double dpiFromImage(const QImage &img)
{
    const int dpmX = img.dotsPerMeterX();
    if (dpmX > 0)
        return dpmX * 0.0254;   // px/m -> px/inch
    return 96.0;                // sane fallback when PNG has no pHYs metadata
}

ImageItem::ImageItem(const QImage &img, const QString &path)
    : QGraphicsPixmapItem(QPixmap::fromImage(img)), m_path(path)
{
    m_dpi = dpiFromImage(img);
    m_baseWmm = img.width()  / m_dpi * 25.4;
    m_baseHmm = img.height() / m_dpi * 25.4;
    m_wmm = m_baseWmm;
    m_hmm = m_baseHmm;
    m_colorPixmap = pixmap();   // keep the colour render for preview toggling

    setFlags(QGraphicsItem::ItemIsMovable |
             QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemSendsGeometryChanges);   // enables itemChange() snapping
    setTransformationMode(Qt::SmoothTransformation);
    setCursor(Qt::OpenHandCursor);

    applyTransform();
}

double ImageItem::aspect() const
{
    return (m_baseHmm > 0.0) ? m_baseWmm / m_baseHmm : 1.0;
}

void ImageItem::applyTransform()
{
    const double pw = pixmap().width();
    const double ph = pixmap().height();
    if (pw <= 0 || ph <= 0)
        return;

    // Scale pixmap px -> mm, with mirror as a sign flip, around the pixmap centre.
    const double sx = (m_wmm / pw) * (m_flipH ? -1.0 : 1.0);
    const double sy = (m_hmm / ph) * (m_flipV ? -1.0 : 1.0);
    const double cx = pw / 2.0;
    const double cy = ph / 2.0;

    QTransform t;
    t.translate(cx, cy);
    t.rotate(m_rotDeg);
    t.scale(sx, sy);
    t.translate(-cx, -cy);
    setTransform(t);
}

void ImageItem::setPreviewPixmap(const QPixmap &pm)
{
    m_previewPixmap = pm;
    if (m_previewActive && !pm.isNull())
        setPixmap(pm);   // same pixel size as colour → transform stays valid
}

void ImageItem::setPreviewActive(bool on)
{
    m_previewActive = on;
    setPixmap(on && !m_previewPixmap.isNull() ? m_previewPixmap : m_colorPixmap);
}

void ImageItem::setSizeMm(double wMm, double hMm)
{
    if (wMm <= 0 || hMm <= 0)
        return;
    const QPointF tl = topLeftMm();   // keep top-left anchored across resize
    m_wmm = wMm;
    m_hmm = hMm;
    applyTransform();
    setTopLeftMm(tl);
}

void ImageItem::setRotationDeg(double deg)
{
    // Normalise to [0, 360).
    deg = std::fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;
    const QPointF tl = topLeftMm();
    m_rotDeg = deg;
    applyTransform();
    setTopLeftMm(tl);
}

void ImageItem::setFlipH(bool on)
{
    if (m_flipH == on) return;
    const QPointF tl = topLeftMm();
    m_flipH = on;
    applyTransform();
    setTopLeftMm(tl);
}

void ImageItem::setFlipV(bool on)
{
    if (m_flipV == on) return;
    const QPointF tl = topLeftMm();
    m_flipV = on;
    applyTransform();
    setTopLeftMm(tl);
}

QPointF ImageItem::topLeftMm() const
{
    return sceneBoundingRect().topLeft();
}

void ImageItem::setTopLeftMm(const QPointF &p)
{
    setPos(pos() + (p - sceneBoundingRect().topLeft()));
}

QVariant ImageItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    // Snap the item back inside the bed if a move would push it past an edge.
    // Uses the transformed (axis-aligned) bounding box so it works under rotation.
    if (change == ItemPositionChange) {
        if (auto *bed = qobject_cast<PrintBedScene *>(scene())) {
            QPointF pos = value.toPointF();
            const QRectF bb = transform().mapRect(boundingRect());  // relative to pos
            const double bw = bb.width(), bh = bb.height();
            // Allowed range for pos so that pos+bb stays within the bed.
            const double minX = -bb.left();
            const double maxX = std::max(minX, bed->bedWidthMm()  - bw - bb.left());
            const double minY = -bb.top();
            const double maxY = std::max(minY, bed->bedHeightMm() - bh - bb.top());
            pos.setX(std::clamp(pos.x(), minX, maxX));
            pos.setY(std::clamp(pos.y(), minY, maxY));
            return pos;
        }
    } else if (change == ItemPositionHasChanged) {
        if (m_onChanged)
            m_onChanged();
    }
    return QGraphicsPixmapItem::itemChange(change, value);
}
