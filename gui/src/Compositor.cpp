/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "Compositor.h"
#include "canvas/ImageItem.h"
#include <QPainter>
#include <algorithm>
#include <vector>
#include <cmath>
#include <vector>

QImage renderActiveImage(ImageItem *item, double dpi, QSizeF &sizeMmOut)
{
    if (!item)
        return {};

    const QRectF bb = item->sceneBoundingRect();   // mm, axis-aligned (handles rotation)
    const double wMm = std::max(bb.right(), 0.0);   // from bed origin (0,0)
    const double hMm = std::max(bb.bottom(), 0.0);
    sizeMmOut = QSizeF(wMm, hMm);

    const double s = dpi / 25.4;                    // mm -> px
    const int wPx = std::max(1, int(std::ceil(wMm * s)));
    const int hPx = std::max(1, int(std::ceil(hMm * s)));

    QImage out(wPx, hPx, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    const int dpm = int(std::lround(dpi / 0.0254));
    out.setDotsPerMeterX(dpm);
    out.setDotsPerMeterY(dpm);

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.scale(s, s);                                  // painter coords = scene mm
    p.setTransform(item->sceneTransform(), true);   // combine → item-local px
    p.drawPixmap(0, 0, item->pixmap());
    p.end();

    return out;
}

// Sliding-window minimum, borders replicated — the preview's counterpart to
// apply_white_choke() in the engine. Square pixels here, so one radius.
static void erodeAlpha(std::vector<uint8_t> &v, int w, int h, int r)
{
    if (r < 1)
        return;
    std::vector<uint8_t> tmp(v.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int m = 255;
            for (int k = -r; k <= r; ++k)
                m = std::min<int>(m, v[size_t(y) * w + std::clamp(x + k, 0, w - 1)]);
            tmp[size_t(y) * w + x] = uint8_t(m);
        }
    for (int x = 0; x < w; ++x)
        for (int y = 0; y < h; ++y) {
            int m = 255;
            for (int k = -r; k <= r; ++k)
                m = std::min<int>(m, tmp[size_t(std::clamp(y + k, 0, h - 1)) * w + x]);
            v[size_t(y) * w + x] = uint8_t(m);
        }
}

QImage buildWhiteLayer(const QImage &src, int whiteLimitPct,
                       bool luminanceMode, int darkPct, int curveX,
                       int curveDelta, int chokePx)
{
    if (src.isNull())
        return {};
    const int pct = std::clamp(whiteLimitPct, 0, 100);
    const int dark = std::clamp(darkPct, 0, 100);

    // The engine's alpha→white curve, as a LUT (src/halftone.c
    // apply_channel_point_curve). Built once per call; identity when delta 0.
    uint8_t curve[256];
    for (int i = 0; i < 256; ++i)
        curve[i] = uint8_t(i);
    if (curveDelta != 0) {
        const int px = std::clamp((std::clamp(curveX, 1, 99) * 255 + 50) / 100, 1, 254);
        const int py = std::clamp(int(px * (1.0 + std::clamp(curveDelta, -90, 90) / 100.0) + 0.5),
                                  0, 255);
        // Piecewise-linear is close enough for a screen preview; the engine's
        // monotone-cubic differs by a shade mid-segment, not in character.
        for (int x = 0; x <= px; ++x)
            curve[x] = uint8_t(py * x / (px ? px : 1));
        for (int x = px; x < 256; ++x)
            curve[x] = uint8_t(py + (255 - py) * (x - px) / (255 - px ? 255 - px : 1));
    }

    const QImage in = src.convertToFormat(QImage::Format_ARGB32);
    const int W = in.width(), H = in.height();
    std::vector<uint8_t> amount(size_t(W) * H);
    for (int y = 0; y < H; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        for (int x = 0; x < W; ++x) {
            int a = qAlpha(s[x]);
            if (a > 0 && luminanceMode && dark < 100) {
                // Thin the backing where the colour on top is dark. The preview
                // works from the source RGB; the engine measures the printed
                // CMYK, which round-trips to the same brightness closely enough
                // to judge the effect by eye.
                const int lum = qGray(s[x]);
                const double f = (dark + (100 - dark) * (lum / 255.0)) / 100.0;
                a = int(a * f + 0.5);
            }
            a = curve[std::clamp(a, 0, 255)];
            // alpha × limit, rounded — mirrors apply_ink_limit()'s (v*limit+0.5f).
            amount[size_t(y) * W + x] = uint8_t((a * pct + 50) / 100);
        }
    }

    // Choke last, as the engine does: it decides where the white goes, the
    // steps above decide how much.
    erodeAlpha(amount, W, H, chokePx);

    QImage out(in.size(), QImage::Format_ARGB32);
    for (int y = 0; y < H; ++y) {
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < W; ++x)
            d[x] = qRgba(255, 255, 255, amount[size_t(y) * W + x]);
    }
    out.setDotsPerMeterX(src.dotsPerMeterX());
    out.setDotsPerMeterY(src.dotsPerMeterY());
    return out;
}

QImage buildEmbossPreview(const QImage &colorSrc, const QImage &spotSrc,
                          int layers, bool grayscale, bool underbase)
{
    if (colorSrc.isNull() || spotSrc.isNull())
        return {};
    const QImage color = colorSrc.convertToFormat(QImage::Format_ARGB32);
    const int W = color.width(), H = color.height();
    const QImage spot = spotSrc.convertToFormat(QImage::Format_ARGB32)
                            .scaled(W, H, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);

    // Mask convention: alpha (opaque = raised) only if >10% is transparent.
    long transp = 0;
    for (int y = 0; y < H; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(spot.constScanLine(y));
        for (int x = 0; x < W; ++x)
            if (qAlpha(s[x]) < 128) ++transp;
    }
    const bool useAlpha = transp * 10 > (long)W * H;
    const float floorH = underbase ? 4.0f / std::max(1, layers) : 0.0f;

    // Per-pixel height in [0,1], clipped to where the artwork has ink (opaque),
    // matching the engine (no white outside the print footprint).
    std::vector<float> h((size_t)W * H, 0.0f);
    for (int y = 0; y < H; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(spot.constScanLine(y));
        const QRgb *c = reinterpret_cast<const QRgb *>(color.constScanLine(y));
        for (int x = 0; x < W; ++x) {
            int sig = useAlpha ? qAlpha(s[x])
                               : (qRed(s[x]) + qGreen(s[x]) + qBlue(s[x])) / 3;
            float hv = grayscale ? sig / 255.0f : (sig > 127 ? 1.0f : 0.0f);
            bool colorHere = qAlpha(c[x]) > 127;
            if (colorHere && hv < floorH) hv = floorH;
            if (!colorHere) hv = 0.0f;
            h[(size_t)y * W + x] = hv;
        }
    }

    // Directional light (from top-left, above) and height exaggeration.
    const float zscale = std::max(1, layers) / 4.0f;   // 8→2, 16→4, 32→8
    float lx = -0.45f, ly = -0.55f, lz = 0.70f;
    float ll = std::sqrt(lx*lx + ly*ly + lz*lz); lx/=ll; ly/=ll; lz/=ll;

    QImage out(W, H, QImage::Format_ARGB32);
    auto at = [&](int x, int y) {
        return h[(size_t)std::clamp(y,0,H-1) * W + std::clamp(x,0,W-1)];
    };
    for (int y = 0; y < H; ++y) {
        const QRgb *c = reinterpret_cast<const QRgb *>(color.constScanLine(y));
        QRgb *o = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < W; ++x) {
            float dx = (at(x+1,y) - at(x-1,y)) * zscale;
            float dy = (at(x,y+1) - at(x,y-1)) * zscale;
            float nl = std::sqrt(dx*dx + dy*dy + 1.0f);
            float nx = -dx/nl, ny = -dy/nl, nz = 1.0f/nl;
            float diff = std::max(0.0f, nx*lx + ny*ly + nz*lz);
            float shade = 0.42f + 0.55f*diff + 0.12f*h[(size_t)y*W+x]; // amb+diff+lift
            // Base = colour artwork over a light bed where transparent.
            int a = qAlpha(c[x]);
            int br = a ? qRed(c[x])   : 205;
            int bg = a ? qGreen(c[x]) : 205;
            int bb = a ? qBlue(c[x])  : 205;
            auto ap = [&](int v){ int r=int(v*shade+0.5f); return r<0?0:(r>255?255:r); };
            o[x] = qRgb(ap(br), ap(bg), ap(bb));
        }
    }
    return out;
}

QImage buildColorLimitProof(const QImage &src, int colorLimitPct)
{
    if (src.isNull())
        return {};
    const int pct = std::clamp(colorLimitPct, 0, 100);
    QImage in = src.convertToFormat(QImage::Format_ARGB32);
    if (pct >= 100)
        return in;   // cap 400% = no-op

    const float cap = pct / 100.0f * 4.0f * 255.0f;
    for (int y = 0; y < in.height(); ++y) {
        QRgb *px = reinterpret_cast<QRgb *>(in.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            const QRgb p = px[x];
            const float rf = qRed(p)   / 255.0f;
            const float gf = qGreen(p) / 255.0f;
            const float bf = qBlue(p)  / 255.0f;

            // Forward UCR — matches src/image.c rgb_to_cmyk().
            int c, m, yy, k;
            const float kf = 1.0f - std::max(rf, std::max(gf, bf));
            if (kf >= 1.0f) {
                c = m = yy = 0;
                k = 255;
            } else {
                c  = int((1.0f - rf - kf) / (1.0f - kf) * 255.0f + 0.5f);
                m  = int((1.0f - gf - kf) / (1.0f - kf) * 255.0f + 0.5f);
                yy = int((1.0f - bf - kf) / (1.0f - kf) * 255.0f + 0.5f);
                k  = int(kf * 255.0f + 0.5f);
            }

            // TAC cap — matches src/halftone.c apply_total_ink_limit().
            const int sum = c + m + yy + k;
            if (sum > cap) {
                const float s = cap / float(sum);
                c  = int(c  * s + 0.5f);
                m  = int(m  * s + 0.5f);
                yy = int(yy * s + 0.5f);
                k  = int(k  * s + 0.5f);
            }

            // Inverse UCR → RGB (exact inverse of the forward transform).
            const int r = (255 - c)  * (255 - k) / 255;
            const int g = (255 - m)  * (255 - k) / 255;
            const int b = (255 - yy) * (255 - k) / 255;
            px[x] = qRgba(r, g, b, qAlpha(p));   // keep source alpha
        }
    }
    return in;
}
