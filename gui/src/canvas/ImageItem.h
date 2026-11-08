/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QGraphicsPixmapItem>
#include <QImage>
#include <QPixmap>
#include <functional>

// A draggable, selectable image placed on the print bed.
// Non-destructive: the source pixels are never modified — size, rotation and
// mirror are stored as numbers and applied via a QTransform at render time.
// 1 scene unit == 1 mm.
class ImageItem : public QGraphicsPixmapItem {
public:
    explicit ImageItem(const QImage &img, const QString &path);

    // ── Size (print size on the bed, mm) ──
    double widthMm() const  { return m_wmm; }
    double heightMm() const { return m_hmm; }
    void setSizeMm(double wMm, double hMm);
    double nativeWmm() const { return m_baseWmm; }   // size implied by DPI
    double nativeHmm() const { return m_baseHmm; }
    double aspect() const;                            // native W/H ratio

    // ── Rotation (degrees, clockwise) ──
    double rotationDeg() const { return m_rotDeg; }
    void setRotationDeg(double deg);
    void rotateBy(double deg) { setRotationDeg(m_rotDeg + deg); }

    // ── Mirror ──
    bool flipH() const { return m_flipH; }
    bool flipV() const { return m_flipV; }
    void setFlipH(bool on);
    void setFlipV(bool on);

    // ── Position (top-left of the on-bed bounding box, scene mm) ──
    QPointF topLeftMm() const;
    void setTopLeftMm(const QPointF &p);

    QString sourcePath() const { return m_path; }
    double dpi() const { return m_dpi; }

    // ── Preview overlay (white-ink layer or colour ink-limit soft-proof) ──
    // Supply the alternate render (same pixel size as the source) and toggle
    // whether it is shown instead of the original. Geometry/transform unaffected.
    void setPreviewPixmap(const QPixmap &pm);
    void setPreviewActive(bool on);

    // Invoked when the item is dragged (position changed) so the UI can refresh.
    void setOnChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    void applyTransform();   // rebuild the QTransform from size/rotation/mirror

    QString m_path;
    double m_dpi = 96.0;
    double m_baseWmm = 0.0;   // native size (mm) at 100%
    double m_baseHmm = 0.0;
    double m_wmm = 0.0;       // current print size (mm)
    double m_hmm = 0.0;
    double m_rotDeg = 0.0;
    bool   m_flipH = false;
    bool   m_flipV = false;
    QPixmap m_colorPixmap;     // original colour render
    QPixmap m_previewPixmap;   // active preview render (white layer / soft-proof)
    bool   m_previewActive = false;
    std::function<void()> m_onChanged;
};
