/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QGraphicsScene>
#include "Units.h"

// Scene coordinate system: 1 unit == 1 millimetre.
// Draws the print-bed rectangle and a unit-based grid in the background.
class PrintBedScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit PrintBedScene(double bedWmm = 300.0, double bedHmm = 450.0,
                           Unit unit = Unit::Cm, QObject *parent = nullptr);

    double bedWidthMm() const { return m_bedW; }
    double bedHeightMm() const { return m_bedH; }
    void setBedSize(double wmm, double hmm);
    void setUnit(Unit u);
    void setBedVisible(bool v);   // hide bed/grid (empty canvas) when no image
    void setWhitePreview(bool on);  // black bed for white-ink layer preview

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    double m_bedW;   // bed width  in mm
    double m_bedH;   // bed height in mm
    Unit   m_unit;
    bool   m_bedVisible = true;
    bool   m_whitePreview = false;
    // Grid spacing derived from the unit: minor = 1 unit, major = 5 units.
    double minorMm() const { return unitMm(m_unit); }
    double majorMm() const { return unitMm(m_unit) * 5.0; }
};
