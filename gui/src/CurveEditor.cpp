/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "CurveEditor.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

CurveEditor::CurveEditor(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(false);
    setCursor(Qt::CrossCursor);
}

void CurveEditor::setPoint(int tonePct, int deltaPct)
{
    m_tonePct = qBound(1, tonePct, 99);
    m_deltaPct = qBound(-90, deltaPct, 90);
    update();
}

void CurveEditor::point(int *tonePct, int *deltaPct) const
{
    if (tonePct)
        *tonePct = m_tonePct;
    if (deltaPct)
        *deltaPct = m_deltaPct;
}

void CurveEditor::setCurveColor(const QColor &c)
{
    m_color = c;
    update();
}

QRectF CurveEditor::plotRect() const
{
    return QRectF(10, 8, width() - 20, height() - 26);
}

int CurveEditor::pointXByte() const
{
    return qBound(1, (m_tonePct * 255 + 50) / 100, 254);
}

int CurveEditor::pointYByte() const
{
    const int x = pointXByte();
    const int y = int(x * (1.0 + m_deltaPct / 100.0) + 0.5);
    return qBound(0, y, 255);
}

static double endpointSlope(double h0, double h1, double d0, double d1)
{
    const double m = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
    return m * d0 <= 0.0 ? 0.0 : m;
}

int CurveEditor::curveValue(int x) const
{
    if (m_deltaPct == 0)
        return qBound(0, x, 255);

    const int px[3] = {0, pointXByte(), 255};
    const int py[3] = {0, pointYByte(), 255};
    const double h0 = px[1] - px[0];
    const double h1 = px[2] - px[1];
    const double d0 = (py[1] - py[0]) / h0;
    const double d1 = (py[2] - py[1]) / h1;
    double m[3];

    m[0] = endpointSlope(h0, h1, d0, d1);
    if (d0 <= 0.0 || d1 <= 0.0) {
        m[1] = 0.0;
    } else {
        const double w0 = 2.0 * h1 + h0;
        const double w1 = h1 + 2.0 * h0;
        m[1] = (w0 + w1) / (w0 / d0 + w1 / d1);
    }
    m[2] = endpointSlope(h1, h0, d1, d0);

    for (int seg = 0; seg < 2; seg++) {
        const double dk = seg == 0 ? d0 : d1;
        if (dk <= 0.0) {
            m[seg] = 0.0;
            m[seg + 1] = 0.0;
            continue;
        }
        const double a = m[seg] / dk;
        const double b = m[seg + 1] / dk;
        const double s = a * a + b * b;
        if (s > 9.0) {
            const double t = 3.0 / std::sqrt(s);
            m[seg] = t * a * dk;
            m[seg + 1] = t * b * dk;
        }
    }

    const int seg = x <= px[1] ? 0 : 1;
    const int x0 = px[seg], x1 = px[seg + 1];
    const int y0 = py[seg], y1 = py[seg + 1];
    const double h = x1 - x0;
    const double t = (x - x0) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double y =
        (2.0 * t3 - 3.0 * t2 + 1.0) * y0 +
        (t3 - 2.0 * t2 + t) * h * m[seg] +
        (-2.0 * t3 + 3.0 * t2) * y1 +
        (t3 - t2) * h * m[seg + 1];
    return qBound(0, int(y + 0.5), 255);
}

QPointF CurveEditor::handlePos() const
{
    const QRectF r = plotRect();
    const double x = r.left() + r.width() * pointXByte() / 255.0;
    const double y = r.bottom() - r.height() * pointYByte() / 255.0;
    return {x, y};
}

bool CurveEditor::hitHandle(const QPointF &p) const
{
    return QLineF(p, handlePos()).length() <= 10.0;
}

void CurveEditor::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plotRect();

    g.fillRect(rect(), palette().base());
    g.setPen(QColor(216, 222, 231));
    g.drawRect(r);

    g.setPen(QColor(233, 238, 244));
    for (int i = 1; i < 4; i++) {
        const double x = r.left() + r.width() * i / 4.0;
        const double y = r.top() + r.height() * i / 4.0;
        g.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        g.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }

    g.setPen(QPen(QColor(203, 213, 225), 1, Qt::DashLine));
    g.drawLine(r.bottomLeft(), r.topRight());

    QPainterPath path;
    for (int x = 0; x <= 255; x++) {
        const int yv = curveValue(x);
        const QPointF p(r.left() + r.width() * x / 255.0,
                        r.bottom() - r.height() * yv / 255.0);
        if (x == 0)
            path.moveTo(p);
        else
            path.lineTo(p);
    }
    g.setPen(QPen(m_color, 2));
    g.drawPath(path);

    const QPointF h = handlePos();
    g.setBrush(m_drag ? m_color : palette().base().color());
    g.setPen(QPen(m_color, 2));
    g.drawEllipse(h, 6, 6);

    QFont f = g.font();
    f.setPixelSize(10);
    g.setFont(f);
    g.setPen(QColor(100, 116, 139));
    const QString lbl = QString("%1% tone  %2%3")
                            .arg(m_tonePct)
                            .arg(m_deltaPct > 0 ? "+" : "")
                            .arg(m_deltaPct);
    g.drawText(QRectF(r.left(), r.bottom() + 2, r.width(), 14),
               Qt::AlignHCenter, lbl);
}

void CurveEditor::mousePressEvent(QMouseEvent *e)
{
    m_drag = hitHandle(e->position());
    if (m_drag)
        update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_drag)
        return;

    const QRectF r = plotRect();
    const double xNorm = (e->position().x() - r.left()) / r.width();
    m_tonePct = qBound(1, int(xNorm * 100.0 + 0.5), 99);

    double out = (r.bottom() - e->position().y()) / r.height() * 255.0;
    out = qBound(0.0, out, 255.0);
    const int px = pointXByte();
    const double rawDelta = out / px * 100.0 - 100.0;
    m_deltaPct = qBound(-90, int(rawDelta + (rawDelta >= 0.0 ? 0.5 : -0.5)), 90);

    update();
    emit changed();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag) {
        m_drag = false;
        update();
    }
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (hitHandle(e->position())) {
        m_tonePct = 50;
        m_deltaPct = 0;
        update();
        emit changed();
    }
}
