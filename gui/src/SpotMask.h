/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QImage>

// ── Spot / emboss mask generation ────────────────────────────────────────────
//
// The emboss engine wants a height map: white = raised, black = flat, and with
// --emboss-height grayscale every level in between is a real height. Building
// that by hand in an editor is the slow part of an emboss job, so this derives
// a first version straight from the artwork.
//
// The result is always FULLY OPAQUE. The engine picks its mask convention by
// looking at how much of the mask is transparent (>10% → "opaque shape is
// raised"), so an opaque mask always takes the luminance path — no guessing.
//
// Deliberately no depth-estimation model here. A depth model answers "how far
// away is this", which for a photographed brick wall is "it is a flat wall" —
// true and useless. The relief is in the shading, which is what Luminance
// reads.

enum class SpotMethod {
    Luminance,   // brightness IS height — textures: brick, wood, stone, cloth
    Shape,       // opaque area raised flat — logos, text, solid marks
    Bevel,       // Shape, but ramped in from the edge so it reads as carved
    Outline,     // only a band along the edge is raised — line art
};

struct SpotParams {
    SpotMethod method = SpotMethod::Luminance;
    bool invert = false;
    int blackPoint = 0;      // levels: input mapped so this becomes 0
    int whitePoint = 255;    // …and this becomes 255
    int smoothPx = 0;        // blur radius, source pixels (0 = off)
    int bevelPx = 12;        // Bevel: ramp width from the edge
    int outlinePx = 6;       // Outline: band width
    int levels = 0;          // quantise to N heights (0 = continuous)
};

// Build the mask. Transparent parts of `src` are always flat: relief outside
// the artwork would be white ink printed where nothing else is.
QImage buildSpotMask(const QImage &src, const SpotParams &p);

// Levels that suit THIS image, from the 2nd/98th percentile of its opaque
// pixels. Photographs rarely use the full range — a brick wall shot indoors
// sits around mid-grey — so fixed 0/255 defaults produce a mask where the
// raised parts never reach full height, and the first preview looks flat for
// no good reason. Ignores the extreme tails so a few specular pixels or a dark
// speck cannot set the whole range.
void suggestSpotLevels(const QImage &src, int *blackPoint, int *whitePoint);
