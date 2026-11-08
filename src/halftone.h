/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef HALFTONE_H
#define HALFTONE_H

#include <stdint.h>
#include "dwrip.h"

/*
 * Apply per-channel ink scale: src[i] *= limit (0.0..1.0).
 * Used for the white channel (single channel, no hue to shift).
 * Modifies src in-place.
 */
void apply_ink_limit(uint8_t *src, int count, float limit);

/*
 * Per-channel density scale: src[i] = clamp(src[i] * factor, 0, 255).
 * Like apply_ink_limit but supports factor > 1.0 (boost) with clamping.
 * Used for per-channel ink trim (--trim) — calibration applied BEFORE the
 * total-ink cap, so apply_total_ink_limit() keeps its balance-preserving
 * meaning on the trimmed values. Modifies src in-place.
 */
void apply_channel_scale(uint8_t *src, int count, float factor);

/*
 * Per-channel correction curve (linearization), applied BEFORE --trim.
 * deltas[3] are signed percents at the 25/50/75% anchors: the curve passes
 * through (0,0), (64, 64·(1+d25/100)), (128, 128·(1+d50/100)),
 * (191, 191·(1+d75/100)), (255,255), clamped monotone, piecewise-linear LUT.
 * Unlike the linear --trim this bends the midtones only — the tool for dot
 * gain / grey-balance calibration. {0,0,0} is a strict no-op.
 * Modifies src in-place.
 */
void apply_channel_curve(uint8_t *src, int count, const int deltas[3]);

/*
 * Single-point smooth correction curve, applied BEFORE --trim.
 * point_pct is the input tone position (1..99). delta_pct is a signed
 * percent at that point: y = x * (1 + delta/100), clamped to 0..255.
 * The LUT is monotone cubic through (0,0), the control point, and (255,255),
 * so neighbouring tones follow the dragged point smoothly. delta 0 is a
 * strict no-op. Modifies src in-place.
 */
void apply_channel_point_curve(uint8_t *src, int count,
                               int point_pct, int delta_pct);

/*
 * Perceived brightness of a separated CMYK image, one byte per pixel.
 *
 * Measured from the SEPARATION, before --ink-color / --trim / curves / TAC:
 * the question is how dark the artwork is, not how much ink the density
 * settings happen to lay down. Reading it after those made lowering the ink
 * density quietly add white back — and put the on-screen preview permanently
 * out of step with the print, since the GUI would have had to reimplement the
 * whole colour pipeline to agree.
 *
 * Uses the naive inverse of rgb_to_cmyk(). Caller owns `lum` (count bytes).
 */
void separation_luminance(const uint8_t *c, const uint8_t *m, const uint8_t *y,
                          const uint8_t *k, int count, uint8_t *lum);

/*
 * Modulate the white base by the brightness of the colour that will sit on top
 * of it: thin under dark areas, full under light ones. Dark ink hides the media
 * on its own, so a full white backing there mostly costs white ink and piles up
 * total ink; light colours need all of it to read correctly on dark film.
 *
 * `lum` comes from separation_luminance(). dark_pct is the white density under
 * the darkest colour (100 = no modulation, a strict no-op). The factor SCALES
 * the existing white value rather than replacing it, so it layers on top of the
 * alpha the white was built from — a soft fade at the edges keeps fading in
 * this mode too. Modifies white in-place.
 */
void apply_white_luminance(uint8_t *white, const uint8_t *lum, int count,
                           int dark_pct);

/*
 * Choke (shrink) the white base inward by `choke_mm`, so the colour on top
 * overhangs it slightly.
 *
 * White/colour registration is never perfect — the two layers are laid down
 * 720 units of paper travel apart (see --reg-v) — and when it slips, white
 * peeks out along the edge as a bright halo on dark media. Pulling the white in
 * hides the error under the colour.
 *
 * ⚠️ The raster is ANISOTROPIC: 360 dpi across, 1440 dpi down by default, so a
 * pixel is four times wider than it is tall. One shrink radius in pixels would
 * choke four times deeper horizontally than vertically — hence the separate
 * mm-per-pixel for each axis. Callers pass those from the resolution preset.
 *
 * Grayscale erosion with a rectangular window: exact along the axes, and up to
 * ~41% deeper at a 45° edge than a true disc would be. At the choke depths this
 * is for (tenths of a millimetre) that overshoot is far below what the dither
 * and the registration error itself contribute.
 *
 * Edges of the canvas replicate rather than count as empty: artwork that runs
 * to the edge has no overhang to hide under there, so eating into it would only
 * expose media. choke_mm <= 0 is a strict no-op. Returns 0, or -1 out of memory.
 */
int apply_white_choke(uint8_t *white, int width, int height,
                      float mm_per_px_x, float mm_per_px_y, float choke_mm);

/* ── Drop-size tiers (the rungs of the structure ladder) ──────────────────
 *
 * A drop mode is an ordered list of dot sizes, smallest first.  Volumes are
 * in MEDIUM units, from Gutenprint's drop table for ESC(e) 0x33
 * (S:M:L = 0.125:0.42:1.0 → 0.298 : 1.0 : 2.381).
 *
 * A deposit plan `p` (what halftone_fs_structure emits, 0..ntiers*H) reads as:
 * stage j = (p-1)/H selects the tier being upgraded INTO, r = p - j*H is how
 * many of the H deposits have reached it; the remaining H-r sit one tier
 * below.  So p = 0..H fills up at tier 0, p = H+1..2H upgrades to tier 1, and
 * so on — the same build-up AcroRIP's PRNs show (fill the hits, then grow).
 */
#define DROP_MAX_TIERS 3
#define DROP_MAX_HITS  8
#define DROP_MAX_PLANS (DROP_MAX_TIERS * DROP_MAX_HITS + 1)

typedef struct {
    int     ntiers;
    uint8_t code[DROP_MAX_TIERS];  /* 2bpp dot code (1/2/3), ascending volume */
    float   vol [DROP_MAX_TIERS];  /* drop volume in medium units             */
} drop_tiers_t;

drop_tiers_t drop_tiers(drop_mode_t mode);

/* Ladder volume of plan p (medium units), 0 <= p <= ntiers*hits. */
float drop_plan_volume(const drop_tiers_t *t, int plan, int hits);

/* Flattened plan → dot code table for one `hits` value, so the band builder's
 * inner loop is two array lookups instead of arithmetic.  code[p][idx] is the
 * 2bpp code the idx-th deposit of plan p must emit (0 = this deposit stays
 * empty).  Deposits are ordered LARGEST FIRST, matching the original
 * medium/large builder byte-for-byte. */
typedef struct {
    uint8_t code[DROP_MAX_PLANS][DROP_MAX_HITS];
    int     nplans;                /* ntiers*hits + 1 */
} drop_lut_t;

void drop_build_lut(drop_lut_t *lut, const drop_tiers_t *t, int hits);

/*
 * The structured halftone — the ONLY halftone path.  Emits a per-pixel
 * deposit-PLAN plane (1 byte/px), see the ladder description above.  Tone is
 * linear in physical volume, so a mode's top tier sets the density ceiling.
 * With use_mask (H = 8 modes) only the 50% dash mask may fire, doubling
 * per-pixel capacity so max ink per area equals the unmasked H = 4 case.
 */
/* channel_idx: which of the up to MAX_CHANNELS planes this call is dithering.
 * Ignored by every method except SD_BLUENOISEHQ, where it offsets the mask
 * lookup so different channels don't sample identical mask phase (see
 * bn2_threshold in halftone.c). Pass 0 for single-channel/test callers. */
void halftone_fs_structure(const uint8_t *src, int width, int height,
                           uint8_t *dst, int hits, int use_mask,
                           struct_dither_t method, int channel_idx,
                           drop_mode_t drop_mode);

/* 50%-duty horizontal-dash mask used by --structure in H=8 modes. */
int halftone_structure_mask(int x, int y);

/*
 * Total ink limit (TAC cap) across the four CMYK channels, balance-preserving.
 *
 * limit_frac is 0.0..1.0; the per-pixel total C+M+Y+K is capped at
 * limit_frac × (4 × 255), i.e. limit_frac × 400% coverage.  Only pixels whose
 * total exceeds the cap are scaled (all four channels by the same factor, so
 * their ratio — the hue — is preserved); pixels under the cap are untouched,
 * so neutrals and midtones do not shift.  limit_frac >= 1.0 is a no-op.
 * Modifies the four channels in-place.
 */
void apply_total_ink_limit(uint8_t *c, uint8_t *m, uint8_t *y, uint8_t *k,
                           int count, float limit_frac);

#endif /* HALFTONE_H */
