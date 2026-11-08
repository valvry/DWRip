/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "canvas/PrintBedScene.h"
#include "Theme.h"
#include <QPainter>
#include <cmath>

PrintBedScene::PrintBedScene(double bedWmm, double bedHmm, Unit unit, QObject *parent)
    : QGraphicsScene(parent), m_bedW(bedWmm), m_bedH(bedHmm), m_unit(unit)
{
    // Give the scene some breathing room around the bed (20 mm margin).
    setSceneRect(-60, -60, m_bedW + 120, m_bedH + 120);
    // "Desk" colour — distinctly darker than the white media so the bed stands out.
    setBackgroundBrush(Theme::desk());
}

void PrintBedScene::setBedSize(double wmm, double hmm)
{
    m_bedW = wmm;
    m_bedH = hmm;
    setSceneRect(-60, -60, m_bedW + 120, m_bedH + 120);
    update();
}

void PrintBedScene::setUnit(Unit u)
{
    m_unit = u;
    update();
}

void PrintBedScene::setBedVisible(bool v)
{
    if (m_bedVisible == v) return;
    m_bedVisible = v;
    update();
}

void PrintBedScene::setWhitePreview(bool on)
{
    if (m_whitePreview == on) return;
    m_whitePreview = on;
    update();
}

void PrintBedScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    // White-ink preview: keep the normal desk, but make the bed pure black so
    // white ink stands out. Grid/shadow are suppressed for a clean black bed.
    if (m_whitePreview) {
        painter->fillRect(rect, Theme::desk());
        if (m_bedVisible) {
            const QRectF bed(0, 0, m_bedW, m_bedH);
            painter->fillRect(bed, QColor(0, 0, 0));
            QPen outline(QColor(255, 255, 255, 40));
            outline.setCosmetic(true);
            outline.setWidth(1);
            painter->setPen(outline);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(bed);
        }
        return;
    }

    // Fill the whole exposed area with the desk colour (covers regions beyond
    // sceneRect too, so the bed always reads as a sheet on a desk).
    painter->fillRect(rect, Theme::desk());
    if (!m_bedVisible)
        return;   // empty canvas: just the desk, no sheet/grid/outline

    const QRectF bed(0, 0, m_bedW, m_bedH);

    // Soft layered drop shadow so the media reads as a sheet sitting on the desk.
    painter->fillRect(bed.translated(0, 7), QColor(15, 23, 42, 10));
    painter->fillRect(bed.translated(0, 3), QColor(15, 23, 42, 16));

    // Bed surface (white media).
    painter->fillRect(bed, QColor(0xff, 0xff, 0xff));

    // Clip grid to the visible bed area.
    const QRectF area = rect.intersected(bed);
    if (area.isEmpty())
        return;

    auto drawLines = [&](double stepMm, const QColor &col) {
        if (stepMm <= 0) return;
        QPen pen(col);
        pen.setCosmetic(true);   // constant width regardless of zoom
        pen.setWidth(1);
        painter->setPen(pen);

        const double x0 = std::floor(area.left() / stepMm) * stepMm;
        for (double x = x0; x <= area.right(); x += stepMm) {
            if (x < 0 || x > m_bedW) continue;
            painter->drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
        const double y0 = std::floor(area.top() / stepMm) * stepMm;
        for (double y = y0; y <= area.bottom(); y += stepMm) {
            if (y < 0 || y > m_bedH) continue;
            painter->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }
    };

    drawLines(minorMm(), Theme::gridMinor());   // minor grid
    drawLines(majorMm(), Theme::gridMajor());   // major grid

    // Bed outline.
    QPen outline(Theme::bedOutline());
    outline.setCosmetic(true);
    outline.setWidth(1);
    painter->setPen(outline);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(bed);
}
