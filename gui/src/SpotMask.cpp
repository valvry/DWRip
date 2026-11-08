/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "SpotMask.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Chamfer distance transform: for every pixel inside the shape, how far is the
// nearest pixel outside it. Two sweeps with the 3-4 kernel — a few percent off
// true Euclidean, which is far below what a printed bevel can show.
std::vector<int> distanceInside(const std::vector<uint8_t> &shape, int w, int h)
{
    const int kBig = 1 << 20;
    std::vector<int> d(size_t(w) * h);
    for (size_t i = 0; i < d.size(); ++i)
        d[i] = shape[i] ? kBig : 0;

    auto at = [&](int x, int y) -> int & { return d[size_t(y) * w + x]; };
    auto relax = [&](int &v, int other, int cost) {
        if (other + cost < v)
            v = other + cost;
    };
    // Outside the canvas counts as outside the shape. Without this a fully
    // opaque artwork — a photographed texture fills its whole frame — has no
    // boundary at all, so Bevel and Outline would find nothing to work from.
    auto edge = [&](int x, int y) { relax(at(x, y), 0, 3); };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int &v = at(x, y);
            if (!v) continue;
            if (y == 0 || x == 0 || y == h - 1 || x == w - 1)
                edge(x, y);
            if (y > 0) {
                relax(v, at(x, y - 1), 3);
                if (x > 0)      relax(v, at(x - 1, y - 1), 4);
                if (x + 1 < w)  relax(v, at(x + 1, y - 1), 4);
            }
            if (x > 0)          relax(v, at(x - 1, y), 3);
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            int &v = at(x, y);
            if (!v) continue;
            if (y + 1 < h) {
                relax(v, at(x, y + 1), 3);
                if (x + 1 < w)  relax(v, at(x + 1, y + 1), 4);
                if (x > 0)      relax(v, at(x - 1, y + 1), 4);
            }
            if (x + 1 < w)      relax(v, at(x + 1, y), 3);
        }
    for (int &v : d)
        v = (v >= kBig) ? kBig : v / 3;   // back to pixel units
    return d;
}

// Separable box blur, run three times — close enough to a Gaussian, and cheap.
void blur(std::vector<uint8_t> &v, int w, int h, int radius)
{
    if (radius < 1)
        return;
    std::vector<uint8_t> tmp(v.size());
    const int win = radius * 2 + 1;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {          // horizontal
            int sum = 0;
            for (int x = -radius; x <= radius; ++x)
                sum += v[size_t(y) * w + std::clamp(x, 0, w - 1)];
            for (int x = 0; x < w; ++x) {
                tmp[size_t(y) * w + x] = uint8_t(sum / win);
                const int add = v[size_t(y) * w + std::clamp(x + radius + 1, 0, w - 1)];
                const int sub = v[size_t(y) * w + std::clamp(x - radius, 0, w - 1)];
                sum += add - sub;
            }
        }
        for (int x = 0; x < w; ++x) {          // vertical
            int sum = 0;
            for (int y = -radius; y <= radius; ++y)
                sum += tmp[size_t(std::clamp(y, 0, h - 1)) * w + x];
            for (int y = 0; y < h; ++y) {
                v[size_t(y) * w + x] = uint8_t(sum / win);
                const int add = tmp[size_t(std::clamp(y + radius + 1, 0, h - 1)) * w + x];
                const int sub = tmp[size_t(std::clamp(y - radius, 0, h - 1)) * w + x];
                sum += add - sub;
            }
        }
    }
}

}  // namespace

QImage buildSpotMask(const QImage &src, const SpotParams &p)
{
    if (src.isNull())
        return {};

    const QImage in = src.convertToFormat(QImage::Format_ARGB32);
    const int w = in.width(), h = in.height();
    const size_t n = size_t(w) * h;

    std::vector<uint8_t> height(n, 0);
    std::vector<uint8_t> inside(n, 0);       // artwork footprint
    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            inside[i] = qAlpha(s[x]) > 127;
            height[i] = inside[i] ? uint8_t(qGray(s[x])) : 0;
        }
    }

    switch (p.method) {
    case SpotMethod::Luminance:
        break;                               // brightness is already the height
    case SpotMethod::Shape:
        for (size_t i = 0; i < n; ++i)
            height[i] = inside[i] ? 255 : 0;
        break;
    case SpotMethod::Bevel: {
        // Ramp from the edge inward, then flat on top: the shape keeps its full
        // height in the middle and only the rim slopes, which is what makes an
        // embossed logo read as carved rather than stuck on.
        const std::vector<int> d = distanceInside(inside, w, h);
        const int ramp = std::max(1, p.bevelPx);
        for (size_t i = 0; i < n; ++i)
            height[i] = inside[i]
                ? uint8_t(std::min(255, d[i] * 255 / ramp))
                : 0;
        break;
    }
    case SpotMethod::Outline: {
        const std::vector<int> d = distanceInside(inside, w, h);
        const int band = std::max(1, p.outlinePx);
        for (size_t i = 0; i < n; ++i)
            height[i] = (inside[i] && d[i] <= band) ? 255 : 0;
        break;
    }
    }

    if (p.invert)
        for (size_t i = 0; i < n; ++i)
            if (inside[i])
                height[i] = uint8_t(255 - height[i]);

    // Levels. This is the control that matters most on textures: it decides how
    // deep the mortar lines sit and how much of the surface counts as "top".
    const int lo = std::clamp(p.blackPoint, 0, 254);
    const int hi = std::clamp(p.whitePoint, lo + 1, 255);
    if (lo != 0 || hi != 255)
        for (size_t i = 0; i < n; ++i) {
            const int v = (height[i] - lo) * 255 / (hi - lo);
            height[i] = uint8_t(std::clamp(v, 0, 255));
        }

    blur(height, w, h, p.smoothPx);

    // Nothing raised outside the artwork — that would be white ink printed
    // where no colour follows. Re-applied after the blur, which spreads.
    for (size_t i = 0; i < n; ++i)
        if (!inside[i])
            height[i] = 0;

    // Quantise to the number of layers the emboss preset actually prints, so
    // the preview shows the steps that will really be there instead of a smooth
    // ramp the printer cannot produce.
    if (p.levels > 1)
        for (size_t i = 0; i < n; ++i) {
            const int step = (height[i] * p.levels + 127) / 255;
            height[i] = uint8_t(std::min(255, step * 255 / p.levels));
        }

    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        QRgb *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int v = height[size_t(y) * w + x];
            d[x] = qRgba(v, v, v, 255);      // opaque → engine reads luminance
        }
    }
    out.setDotsPerMeterX(src.dotsPerMeterX());
    out.setDotsPerMeterY(src.dotsPerMeterY());
    return out;
}

void suggestSpotLevels(const QImage &src, int *blackPoint, int *whitePoint)
{
    if (blackPoint) *blackPoint = 0;
    if (whitePoint) *whitePoint = 255;
    if (src.isNull())
        return;

    const QImage in = src.convertToFormat(QImage::Format_ARGB32);
    long hist[256] = {0};
    long total = 0;
    for (int y = 0; y < in.height(); ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        for (int x = 0; x < in.width(); ++x)
            if (qAlpha(s[x]) > 127) {          // opaque artwork only
                hist[qGray(s[x])]++;
                total++;
            }
    }
    if (total < 16)
        return;

    const long loCut = total * 2 / 100, hiCut = total * 98 / 100;
    int lo = 0, hi = 255;
    long acc = 0;
    for (int v = 0; v < 256; ++v) {
        acc += hist[v];
        if (acc >= loCut) { lo = v; break; }
    }
    acc = 0;
    for (int v = 0; v < 256; ++v) {
        acc += hist[v];
        if (acc >= hiCut) { hi = v; break; }
    }
    if (hi <= lo)
        return;                                 // flat image — leave it alone
    if (blackPoint) *blackPoint = std::clamp(lo, 0, 254);
    if (whitePoint) *whitePoint = std::clamp(hi, lo + 1, 255);
}
