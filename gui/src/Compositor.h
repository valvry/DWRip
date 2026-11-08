/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QImage>
#include <QSizeF>

class ImageItem;

// Render the active image (with its non-destructive transform) onto a transparent
// RGBA canvas spanning from the bed origin (0,0) to the image's bottom-right.
// The X/Y offset becomes transparent top-left padding (dwrip treats alpha=0 as no
// ink), so the on-bed position is preserved. `sizeMmOut` is the canvas size in mm
// (use as dwrip's -S). `dpi` is the composite source density.
QImage renderActiveImage(ImageItem *item, double dpi, QSizeF &sizeMmOut);

// Build the "white-ink layer" image for on-screen preview. Mirrors the engine's
// white pipeline exactly, in the same order (src/main.c):
//   alpha  →  [luminance modulation]  →  [alpha→white curve]  →  ×whiteLimit
// where the white base starts as the source alpha (src/image.c: IMG_WHITE = a),
// luminance modulation is src/halftone.c apply_white_luminance() and the curve
// is apply_channel_point_curve(). RGB is forced to white and the per-pixel
// alpha carries the amount, so it composites over the black preview bed:
// transparent where there is no white ink, opaque where solid.
//
// Keep this in step with the engine — a preview that disagrees with what is
// printed is worse than no preview.
//
// darkPct 100 and curveDelta 0 are the defaults and are strict no-ops.
// `chokePx` shrinks the white inward, in SOURCE pixels — the caller converts
// from mm using the item's print size, because this image is square-pixel while
// the engine's raster is not.
QImage buildWhiteLayer(const QImage &src, int whiteLimitPct,
                       bool luminanceMode = false, int darkPct = 100,
                       int curveX = 50, int curveDelta = 0, int chokePx = 0);

// Soft-proof of the colour ink-limit (-l, TAC cap), mirroring the dwrip engine
// WITHOUT ICC: per pixel RGB→CMYK (UCR, src/image.c rgb_to_cmyk), then if the
// total C+M+Y+K exceeds the cap = pct/100×4×255 all four channels are scaled by
// cap/sum (src/halftone.c apply_total_ink_limit), then inverse-UCR back to RGB.
// Under-cap pixels are unchanged; over-inked pixels lighten. Source alpha is
// preserved. pct>=100 is a no-op cap (returns a plain copy).
QImage buildColorLimitProof(const QImage &src, int colorLimitPct);

// 2.5D shaded-relief preview of an emboss job: derives a per-pixel height from
// the spot mask (same rule as the engine — alpha convention if >10% transparent,
// else luminance; binary threshold or grayscale scale; underbase as a floor),
// computes surface normals and applies directional (Lambert) lighting, then
// tints by the colour artwork → "colour on relief". `layers` sets the vertical
// exaggeration. Returns an opaque preview image at `color`'s size.
QImage buildEmbossPreview(const QImage &color, const QImage &spot,
                          int layers, bool grayscale, bool underbase);
