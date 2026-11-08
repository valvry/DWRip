/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "halftone.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

void apply_ink_limit(uint8_t *src, int count, float limit)
{
    if (limit >= 1.0f)
        return;

    for (int i = 0; i < count; i++) {
        src[i] = (uint8_t)(src[i] * limit + 0.5f);
    }
}

void apply_channel_scale(uint8_t *src, int count, float factor)
{
    if (factor == 1.0f)
        return;

    for (int i = 0; i < count; i++) {
        int v = (int)(src[i] * factor + 0.5f);
        src[i] = v > 255 ? 255 : (v < 0 ? 0 : v);
    }
}

void apply_channel_curve(uint8_t *src, int count, const int deltas[3])
{
    if (deltas[0] == 0 && deltas[1] == 0 && deltas[2] == 0)
        return;   /* identity — output byte-identical */

    /* Control points: endpoints fixed, anchors 25/50/75% scaled by their
     * signed delta. Monotonicity enforced so the LUT never inverts. */
    int px[5] = {0, 64, 128, 191, 255};
    int py[5];
    py[0] = 0;
    py[4] = 255;
    for (int a = 0; a < 3; a++) {
        int v = (int)(px[a + 1] * (1.0f + deltas[a] / 100.0f) + 0.5f);
        py[a + 1] = v > 255 ? 255 : (v < 0 ? 0 : v);
    }
    for (int a = 1; a < 5; a++)
        if (py[a] < py[a - 1])
            py[a] = py[a - 1];

    /* Piecewise-linear LUT through the 5 points. */
    uint8_t lut[256];
    for (int seg = 0; seg < 4; seg++) {
        int x0 = px[seg], x1 = px[seg + 1];
        int y0 = py[seg], y1 = py[seg + 1];
        for (int x = x0; x <= x1; x++)
            lut[x] = (uint8_t)(y0 + (int)((y1 - y0) * (float)(x - x0) /
                                          (float)(x1 - x0) + 0.5f));
    }

    for (int i = 0; i < count; i++)
        src[i] = lut[src[i]];
}

static float pchip_endpoint_slope(float h0, float h1, float d0, float d1)
{
    float m = ((2.0f * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
    if (m * d0 <= 0.0f)
        return 0.0f;
    return m;
}

void apply_channel_point_curve(uint8_t *src, int count,
                               int point_pct, int delta_pct)
{
    if (delta_pct == 0)
        return;   /* identity -- output byte-identical */

    if (point_pct < 1) point_pct = 1;
    if (point_pct > 99) point_pct = 99;
    if (delta_pct < -90) delta_pct = -90;
    if (delta_pct > 90) delta_pct = 90;

    int px[3] = {0, (point_pct * 255 + 50) / 100, 255};
    if (px[1] < 1) px[1] = 1;
    if (px[1] > 254) px[1] = 254;

    int py[3];
    py[0] = 0;
    py[2] = 255;
    {
        int v = (int)(px[1] * (1.0f + delta_pct / 100.0f) + 0.5f);
        py[1] = v > 255 ? 255 : (v < 0 ? 0 : v);
    }
    if (py[1] < py[0]) py[1] = py[0];
    if (py[2] < py[1]) py[2] = py[1];

    const float h0 = (float)(px[1] - px[0]);
    const float h1 = (float)(px[2] - px[1]);
    const float d0 = (float)(py[1] - py[0]) / h0;
    const float d1 = (float)(py[2] - py[1]) / h1;
    float m[3];

    m[0] = pchip_endpoint_slope(h0, h1, d0, d1);
    if (d0 <= 0.0f || d1 <= 0.0f) {
        m[1] = 0.0f;
    } else {
        const float w0 = 2.0f * h1 + h0;
        const float w1 = h1 + 2.0f * h0;
        m[1] = (w0 + w1) / (w0 / d0 + w1 / d1);
    }
    m[2] = pchip_endpoint_slope(h1, h0, d1, d0);

    /* Fritsch-Carlson guard: keep every Hermite segment monotone. */
    for (int seg = 0; seg < 2; seg++) {
        const float dk = seg == 0 ? d0 : d1;
        if (dk <= 0.0f) {
            m[seg] = 0.0f;
            m[seg + 1] = 0.0f;
            continue;
        }
        const float a = m[seg] / dk;
        const float b = m[seg + 1] / dk;
        const float s = a * a + b * b;
        if (s > 9.0f) {
            const float t = 3.0f / sqrtf(s);
            m[seg] = t * a * dk;
            m[seg + 1] = t * b * dk;
        }
    }

    uint8_t lut[256];
    for (int seg = 0; seg < 2; seg++) {
        const int x0 = px[seg], x1 = px[seg + 1];
        const int y0 = py[seg], y1 = py[seg + 1];
        const float h = (float)(x1 - x0);
        for (int x = x0; x <= x1; x++) {
            const float t = (float)(x - x0) / h;
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float y =
                (2.0f * t3 - 3.0f * t2 + 1.0f) * y0 +
                (t3 - 2.0f * t2 + t) * h * m[seg] +
                (-2.0f * t3 + 3.0f * t2) * y1 +
                (t3 - t2) * h * m[seg + 1];
            int iv = (int)(y + 0.5f);
            if (iv < 0) iv = 0;
            if (iv > 255) iv = 255;
            lut[x] = (uint8_t)iv;
        }
    }
    for (int x = 1; x < 256; x++)
        if (lut[x] < lut[x - 1])
            lut[x] = lut[x - 1];

    for (int i = 0; i < count; i++)
        src[i] = lut[src[i]];
}

void apply_total_ink_limit(uint8_t *c, uint8_t *m, uint8_t *y, uint8_t *k,
                           int count, float limit_frac)
{
    if (limit_frac >= 1.0f)
        return;

    /* Cap on the per-pixel total of the four channels (each 0..255). */
    float cap = limit_frac * 4.0f * 255.0f;

    for (int i = 0; i < count; i++) {
        int sum = c[i] + m[i] + y[i] + k[i];
        if (sum > cap) {
            float s = cap / (float)sum;
            c[i] = (uint8_t)(c[i] * s + 0.5f);
            m[i] = (uint8_t)(m[i] * s + 0.5f);
            y[i] = (uint8_t)(y[i] * s + 0.5f);
            k[i] = (uint8_t)(k[i] * s + 0.5f);
        }
    }
}


/*
 * --structure: AcroRIP-style structured halftone.
 *
 * Tone is expressed in physical MEDIUM-drop units (large = 2.38 medium,
 * from Gutenprint's drop table for ESC(e) 0x33: S:M:L = 0.125:0.42:1.0).
 * Each pixel gets a deposit PLAN index p (1 byte):
 *   p = 0..H        → p medium drops (stack-count modulation)
 *   p = H+1..2H     → (2H-p) mediums + (p-H) larges (upgrade to large)
 * so plan volume vol(p) = p                  for p <= H
 *                       = H + (p-H)*1.38     for p >  H  (medium→large
 *                         upgrade adds the volume DIFFERENCE, 2.38-1)
 * The band builder spreads a plan's deposits across the H weave hits with
 * the (k+x) rotation, giving disjoint per-pass dashes like AcroRIP.
 *
 * `use_mask` restricts firing to a 50% horizontal-dash pattern (capacity
 * doubles on masked pixels).  The production --structure path runs with
 * use_mask = 0: cross-mode density parity is already automatic because the
 * single-layer weave prints only even rows (8 hits x 1/2 rows = 4 hits x
 * all rows), which is exactly what AcroRIP's output measures as.  The mask
 * is kept for experiments.
 */
/* Drop volumes in MEDIUM units, from Gutenprint's table for ESC(e) 0x33
 * (S:M:L = 0.125:0.42:1.0).  Both constants come from that one source, so
 * they stay mutually consistent; the GUI ink-cost defaults (1.5/3.7/8.8 pL)
 * imply a slightly larger small (0.41) — recalibrate BOTH here if a physical
 * measurement settles it. */
#define STRUCT_SMALL_MU 0.298f  /* volume of a small drop in medium units */
#define STRUCT_LARGE_MU 2.38f   /* volume of a large drop in medium units */

drop_tiers_t drop_tiers(drop_mode_t mode)
{
    /* code 1 = small, 2 = medium, 3 = large (the 2bpp dot codes). */
    switch (mode) {
    case DROP_S:   return (drop_tiers_t){1, {1},       {STRUCT_SMALL_MU}};
    case DROP_M:   return (drop_tiers_t){1, {2},       {1.0f}};
    case DROP_L:   return (drop_tiers_t){1, {3},       {STRUCT_LARGE_MU}};
    case DROP_SM:  return (drop_tiers_t){2, {1, 2},    {STRUCT_SMALL_MU, 1.0f}};
    case DROP_SML: return (drop_tiers_t){3, {1, 2, 3}, {STRUCT_SMALL_MU, 1.0f,
                                                        STRUCT_LARGE_MU}};
    case DROP_ML:
    default:       return (drop_tiers_t){2, {2, 3},    {1.0f, STRUCT_LARGE_MU}};
    }
}

float drop_plan_volume(const drop_tiers_t *t, int plan, int hits)
{
    if (plan <= 0)
        return 0.0f;
    int j = (plan - 1) / hits;          /* tier being upgraded into */
    if (j >= t->ntiers)
        j = t->ntiers - 1;
    int r = plan - j * hits;            /* deposits that reached tier j */
    if (r > hits) r = hits;
    float v = r * t->vol[j];
    if (j > 0)
        v += (hits - r) * t->vol[j - 1];
    return v;
}

void drop_build_lut(drop_lut_t *lut, const drop_tiers_t *t, int hits)
{
    memset(lut, 0, sizeof(*lut));
    lut->nplans = t->ntiers * hits + 1;
    for (int p = 1; p < lut->nplans; p++) {
        int j = (p - 1) / hits;
        int r = p - j * hits;
        /* n[tier] deposits at each tier; only j and j-1 can be populated. */
        int n_hi = r, n_lo = (j > 0) ? hits - r : 0;
        int idx = 0;
        for (int i = 0; i < n_hi && idx < hits; i++)   /* largest first */
            lut->code[p][idx++] = t->code[j];
        for (int i = 0; i < n_lo && idx < hits; i++)
            lut->code[p][idx++] = t->code[j - 1];
    }
}

int halftone_structure_mask(int x, int y)
{
    /* 4-px horizontal dashes, phase-alternating per row: 50% duty,
     * x-parity balanced — matches the measured AcroRIP pattern class. */
    return (((x >> 2) + y) & 1) == 0;
}

/* Deterministic per-pixel threshold jitter in [-0.5, 0.5] (SD_JITTER).
 * A cheap integer hash → spatially white noise that breaks Floyd-Steinberg's
 * period-2 lock on flat tones without any global RNG state. */
static float struct_jitter(int x, int y)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((float)(h & 0xffffu) / 65535.0f) - 0.5f;
}

/* Deterministic hash in [0,1] with a per-tap salt k (SD_SERPRANDOM weights). */
static float struct_hash01(int x, int y, int k)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u
               + (unsigned)k * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h & 0xffffu) / 65535.0f;
}

/* ── Void-and-cluster blue-noise mask (SD_BLUENOISE), generated once ── */
#define BN_SIZE 64
#define BN_N    (BN_SIZE * BN_SIZE)
static float bn_mask[BN_N];   /* thresholds in [0,1), tileable */
static int   bn_ready = 0;

static void bn_add_energy(float *e, int idx, float sign)
{
    int cy = idx / BN_SIZE, cx = idx % BN_SIZE;
    const int R = 4;
    const float inv2s2 = 1.0f / (2.0f * 1.5f * 1.5f);
    for (int dy = -R; dy <= R; dy++) {
        int yy = ((cy + dy) % BN_SIZE + BN_SIZE) % BN_SIZE;
        for (int dx = -R; dx <= R; dx++) {
            int xx = ((cx + dx) % BN_SIZE + BN_SIZE) % BN_SIZE;
            e[yy * BN_SIZE + xx] += sign * expf(-(float)(dx*dx + dy*dy) * inv2s2);
        }
    }
}

static int bn_extreme(const float *e, const uint8_t *b, int want_one, int want_max)
{
    int best = -1;
    float bv = 0.0f;
    for (int i = 0; i < BN_N; i++) {
        if ((b[i] != 0) != (want_one != 0))
            continue;
        if (best < 0 || (want_max ? e[i] > bv : e[i] < bv)) {
            bv = e[i];
            best = i;
        }
    }
    return best;
}

static void bn_generate(void)
{
    uint8_t b[BN_N];
    float e[BN_N];
    unsigned rng = 2463534242u;

    /* ~10% random initial prototype */
    memset(e, 0, sizeof(e));
    int ones = 0;
    for (int i = 0; i < BN_N; i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        b[i] = ((rng & 0xff) < 26) ? 1 : 0;
        if (b[i]) { bn_add_energy(e, i, 1.0f); ones++; }
    }
    /* Spread the prototype: tightest cluster → largest void, until stable. */
    for (int it = 0; it < BN_N * 4; it++) {
        int c = bn_extreme(e, b, 1, 1);
        b[c] = 0; bn_add_energy(e, c, -1.0f);
        int v = bn_extreme(e, b, 0, 0);
        b[v] = 1; bn_add_energy(e, v, 1.0f);
        if (v == c) break;
    }

    uint8_t b0[BN_N];
    float   e0[BN_N];
    memcpy(b0, b, sizeof(b));
    memcpy(e0, e, sizeof(e));
    int rank[BN_N];

    /* Phase 1: remove tightest clusters, ranks (ones-1)..0. */
    for (int k = ones - 1; k >= 0; k--) {
        int c = bn_extreme(e, b, 1, 1);
        rank[c] = k;
        b[c] = 0; bn_add_energy(e, c, -1.0f);
    }
    /* Phase 2: from the stable pattern, fill largest voids, ranks ones..N-1. */
    memcpy(b, b0, sizeof(b));
    memcpy(e, e0, sizeof(e));
    for (int k = ones; k < BN_N; k++) {
        int v = bn_extreme(e, b, 0, 0);
        rank[v] = k;
        b[v] = 1; bn_add_energy(e, v, 1.0f);
    }
    for (int i = 0; i < BN_N; i++)
        bn_mask[i] = (float)rank[i] / (float)(BN_N - 1);
    bn_ready = 1;
}

static float bn_threshold(int x, int y)
{
    if (!bn_ready) bn_generate();
    return bn_mask[(y % BN_SIZE) * BN_SIZE + (x % BN_SIZE)];
}

/* ── SD_BLUENOISEHQ: a properly-tuned ordered blue-noise mask, kept fully
 * separate from bn_mask/bn_threshold above so SD_BLUENOISE stays untouched
 * (byte-identical) as a baseline reference. Three corrections vs the plain
 * mask:
 *   1. Rectangular 256x64 (not square 64x64) — the horizontal period that
 *      showed up as a visible grid on prints (64px @ 360dpi ~= 4.5mm) grows
 *      to ~18mm; vertical period (64px @ 1440dpi ~= 1.13mm) was already fine,
 *      so its size is left alone (keeps generation cost bounded).
 *   2. Physical-distance VAC energy — 1 raster column (360dpi) covers 4x the
 *      physical distance of 1 raster row (1440dpi), so bn2_add_energy scales
 *      dx by 4 before the Gaussian falloff. Makes void/cluster spacing round
 *      on PAPER, not round in raster-index space (which prints stretched
 *      4x horizontally).
 *   3. Per-channel phase offset (bn2_channel_offset) — every ink channel
 *      sampling the *same* mask value at the same (x,y) was stacking
 *      identical structure across C/M/Y/K/W1/W2, reinforcing any residual
 *      periodicity instead of cancelling it. */
#define BN2_W 256
#define BN2_H 64
#define BN2_N (BN2_W * BN2_H)
static float bn2_mask[BN2_N];
static int   bn2_ready = 0;

static void bn2_add_energy(float *e, int idx, float sign)
{
    int cy = idx / BN2_W, cx = idx % BN2_W;
    const int R = 4;
    const float inv2s2 = 1.0f / (2.0f * 1.5f * 1.5f);
    for (int dy = -R; dy <= R; dy++) {
        int yy = ((cy + dy) % BN2_H + BN2_H) % BN2_H;
        for (int dx = -R; dx <= R; dx++) {
            int xx = ((cx + dx) % BN2_W + BN2_W) % BN2_W;
            float pdx = (float)(dx * 4);   /* physical-distance weighting */
            e[yy * BN2_W + xx] += sign * expf(-(pdx*pdx + (float)(dy*dy)) * inv2s2);
        }
    }
}

static int bn2_extreme(const float *e, const uint8_t *b, int want_one, int want_max)
{
    int best = -1;
    float bv = 0.0f;
    for (int i = 0; i < BN2_N; i++) {
        if ((b[i] != 0) != (want_one != 0))
            continue;
        if (best < 0 || (want_max ? e[i] > bv : e[i] < bv)) {
            bv = e[i];
            best = i;
        }
    }
    return best;
}

static void bn2_generate(void)
{
    uint8_t *b = malloc(BN2_N);
    float   *e = malloc(BN2_N * sizeof(float));
    if (!b || !e) { free(b); free(e); return; }
    unsigned rng = 2463534242u;

    memset(e, 0, BN2_N * sizeof(float));
    int ones = 0;
    for (int i = 0; i < BN2_N; i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        b[i] = ((rng & 0xff) < 26) ? 1 : 0;
        if (b[i]) { bn2_add_energy(e, i, 1.0f); ones++; }
    }
    for (int it = 0; it < BN2_N * 4; it++) {
        int c = bn2_extreme(e, b, 1, 1);
        b[c] = 0; bn2_add_energy(e, c, -1.0f);
        int v = bn2_extreme(e, b, 0, 0);
        b[v] = 1; bn2_add_energy(e, v, 1.0f);
        if (v == c) break;
    }

    uint8_t *b0 = malloc(BN2_N);
    float   *e0 = malloc(BN2_N * sizeof(float));
    int     *rank = malloc(BN2_N * sizeof(int));
    if (!b0 || !e0 || !rank) { free(b); free(e); free(b0); free(e0); free(rank); return; }
    memcpy(b0, b, BN2_N);
    memcpy(e0, e, BN2_N * sizeof(float));

    for (int k = ones - 1; k >= 0; k--) {
        int c = bn2_extreme(e, b, 1, 1);
        rank[c] = k;
        b[c] = 0; bn2_add_energy(e, c, -1.0f);
    }
    memcpy(b, b0, BN2_N);
    memcpy(e, e0, BN2_N * sizeof(float));
    for (int k = ones; k < BN2_N; k++) {
        int v = bn2_extreme(e, b, 0, 0);
        rank[v] = k;
        b[v] = 1; bn2_add_energy(e, v, 1.0f);
    }
    for (int i = 0; i < BN2_N; i++)
        bn2_mask[i] = (float)rank[i] / (float)(BN2_N - 1);
    bn2_ready = 1;

    free(b); free(e); free(b0); free(e0); free(rank);
}

static void bn2_channel_offset(int ch, int *ox, int *oy)
{
    *ox = (int)(struct_hash01(ch, 0, 97) * BN2_W) % BN2_W;
    *oy = (int)(struct_hash01(ch, 0, 98) * BN2_H) % BN2_H;
}

static float bn2_threshold(int x, int y, int channel_idx)
{
    if (!bn2_ready) bn2_generate();
    int ox, oy;
    bn2_channel_offset(channel_idx, &ox, &oy);
    int xx = ((x + ox) % BN2_W + BN2_W) % BN2_W;
    int yy = ((y + oy) % BN2_H + BN2_H) % BN2_H;
    return bn2_mask[yy * BN2_W + xx];
}

/* Error-diffusion tap kernels, as (dx, dy, weight) triples. dx is mirrored by
 * the scan direction `dir` (serpentine); dy indexes the rotating error-row
 * buffers. FS = the classic 4-tap/16 Floyd-Steinberg kernel (used by every
 * mode except the two SD_*JARVIS* ones — kept byte-identical to the old
 * hand-unrolled code). JJN = Jarvis-Judice-Ninke, 12-tap/48 spread over 3
 * rows: wider error spread → lower low-frequency noise even before any
 * randomization. */
typedef struct { int dx, dy; float w; } dtap_t;

static const dtap_t FS_TAPS[4] = {
    {1, 0, 7.0f}, {-1, 1, 3.0f}, {0, 1, 5.0f}, {1, 1, 1.0f}
};
#define FS_DIV 16.0f

static const dtap_t JJN_TAPS[12] = {
    {1, 0, 7.0f}, {2, 0, 5.0f},
    {-2, 1, 3.0f}, {-1, 1, 5.0f}, {0, 1, 7.0f}, {1, 1, 5.0f}, {2, 1, 3.0f},
    {-2, 2, 1.0f}, {-1, 2, 3.0f}, {0, 2, 5.0f}, {1, 2, 3.0f}, {2, 2, 1.0f}
};
#define JJN_DIV 48.0f

void halftone_fs_structure(const uint8_t *src, int width, int height,
                           uint8_t *dst, int hits, int use_mask,
                           struct_dither_t method, int channel_idx,
                           drop_mode_t drop_mode)
{
    drop_tiers_t tiers = drop_tiers(drop_mode);
    int nplans = tiers.ntiers * hits + 1;   /* plan indices 0..T·H */
    float vol[DROP_MAX_PLANS];
    for (int p = 0; p < nplans; p++)
        vol[p] = drop_plan_volume(&tiers, p, hits);
    float vmax_px = vol[nplans - 1];        /* per (masked) pixel */
    float vmax_area = use_mask ? vmax_px / 2.0f : vmax_px;

    /* 3 rotating error-row buffers: [0]=current row (dy=0, already being
     * consumed as we scan), [1]=next row (dy=1), [2]=row after next (dy=2,
     * only written to by the JJN kernel). FS-kernel modes never touch [2] —
     * it stays zero and rotates harmlessly, so their output is unaffected. */
    float *err_buf[3];
    for (int r = 0; r < 3; r++) {
        err_buf[r] = calloc(width, sizeof(float));
        if (!err_buf[r]) {
            for (int j = 0; j < r; j++) free(err_buf[j]);
            return;
        }
    }
    memset(dst, 0, (size_t)width * height);

    /* Serpentine modes alternate scan direction each row (boustrophedon) to
     * break Floyd-Steinberg's directional worms; SD_SERPRANDOM/SERPJARVIS*
     * additionally randomize the diffusion weights per pixel (Ulichney/
     * Ostromoukhov-family variable-coefficient ED) to push the spectrum
     * toward blue noise. All are mask-free, so — unlike SD_BLUENOISE(HQ) —
     * nothing can tile into a grid. Raster modes keep dir=+1, FS kernel,
     * fixed 7/3/5/1 → byte-identical to the pre-refactor code. */
    int serpentine = (method == SD_SERPENTINE || method == SD_SERPRANDOM ||
                       method == SD_SERPJARVIS || method == SD_SERPJARVISRANDOM);
    int randomize  = (method == SD_SERPRANDOM || method == SD_SERPJARVISRANDOM);
    int use_jjn    = (method == SD_SERPJARVIS || method == SD_SERPJARVISRANDOM);
    const dtap_t *taps = use_jjn ? JJN_TAPS : FS_TAPS;
    int ntaps = use_jjn ? 12 : 4;
    float tdiv = use_jjn ? JJN_DIV : FS_DIV;

    for (int y = 0; y < height; y++) {
        int dir = (serpentine && (y & 1)) ? -1 : 1;
        int xstart = (dir > 0) ? 0 : width - 1;
        int xend   = (dir > 0) ? width : -1;
        for (int x = xstart; x != xend; x += dir) {
            float val = src[(size_t)y * width + x] * vmax_area / 255.0f
                        + err_buf[0][x];
            float error;

            if (use_mask && !halftone_structure_mask(x, y)) {
                error = val;                /* forward everything */
            } else {
                if (val < 0.0f) val = 0.0f;
                if (val > vmax_px) val = vmax_px;

                int best;
                if (method == SD_BLUENOISE || method == SD_BLUENOISEHQ) {
                    /* Ordered rounding in volume space: find the two ladder
                     * levels bracketing val, choose by the blue-noise mask. */
                    int lo = 0;
                    while (lo < nplans - 1 && vol[lo + 1] <= val)
                        lo++;
                    int hi = (lo < nplans - 1) ? lo + 1 : lo;
                    float span = vol[hi] - vol[lo];
                    float frac = span > 0.0f ? (val - vol[lo]) / span : 0.0f;
                    float thr = (method == SD_BLUENOISEHQ)
                                    ? bn2_threshold(x, y, channel_idx)
                                    : bn_threshold(x, y);
                    best = (frac > thr) ? hi : lo;
                } else {
                    /* Nearest ladder level with a threshold perturbation, so
                     * flat tones don't lock into Floyd-Steinberg worms. The
                     * full residual is still error-diffused below, so density
                     * self-corrects — a perturbation biases *texture*, never
                     * low-frequency density, hence no ordered-mask grid.
                     *   SD_JITTER     = white-noise hash  → aperiodic but grainy
                     *   SD_BLUEJITTER = blue-noise mask   → smooth, no grid */
                    float t = val;
                    if (method == SD_JITTER)
                        t += struct_jitter(x, y);        /* ±0.5, white noise */
                    else if (method == SD_BLUEJITTER)
                        t += bn_threshold(x, y) - 0.5f;  /* ±0.5, blue noise  */
                    best = 0;
                    float bd = (t < 0.0f) ? -t : t;
                    for (int p = 1; p < nplans; p++) {
                        float d = t - vol[p];
                        if (d < 0) d = -d;
                        if (d < bd) { bd = d; best = p; }
                    }
                }
                dst[(size_t)y * width + x] = (uint8_t)best;
                error = val - vol[best];
            }

            /* Diffuse error, direction-aware: dx of each tap is mirrored by
             * `dir` (serpentine), dy selects the row buffer. Weights are the
             * kernel's fixed fractions, unless randomize perturbs each tap
             * ±40% and renormalizes (Ulichney/Ostromoukhov-family variable-
             * coefficient ED). When randomize is false, sum is exactly `tdiv`
             * (the kernel's analytic total) — same literal division as the
             * old hand-unrolled code, so non-randomized modes stay byte-
             * identical. */
            float w[12], sum;
            if (randomize) {
                sum = 0.0f;
                for (int t = 0; t < ntaps; t++) {
                    float p = 0.8f * (struct_hash01(x, y, t) - 0.5f);  /* ±40% */
                    w[t] = taps[t].w * (1.0f + p);
                    if (w[t] < 0.05f) w[t] = 0.05f;
                    sum += w[t];
                }
            } else {
                for (int t = 0; t < ntaps; t++)
                    w[t] = taps[t].w;
                sum = tdiv;
            }

            for (int t = 0; t < ntaps; t++) {
                int dy = taps[t].dy;
                if (dy > 0 && y + dy >= height)
                    continue;
                int xx = x + dir * taps[t].dx;
                if (xx < 0 || xx >= width)
                    continue;
                err_buf[dy][xx] += error * (w[t] / sum);
            }
        }
        float *tmp = err_buf[0];
        err_buf[0] = err_buf[1];
        err_buf[1] = err_buf[2];
        err_buf[2] = tmp;
        memset(err_buf[2], 0, (size_t)width * sizeof(float));
    }

    free(err_buf[0]);
    free(err_buf[1]);
    free(err_buf[2]);
}

void separation_luminance(const uint8_t *c, const uint8_t *m, const uint8_t *y,
                          const uint8_t *k, int count, uint8_t *lum)
{
    for (int i = 0; i < count; i++) {
        /* Naive inverse of rgb_to_cmyk(): good enough to rank pixels by how
         * much they already hide the media, which is all this needs. */
        const int r = (255 - c[i]) * (255 - k[i]) / 255;
        const int g = (255 - m[i]) * (255 - k[i]) / 255;
        const int b = (255 - y[i]) * (255 - k[i]) / 255;
        int v = (int)(0.299f * r + 0.587f * g + 0.114f * b + 0.5f);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        lum[i] = (uint8_t)v;
    }
}

void apply_white_luminance(uint8_t *white, const uint8_t *lum, int count,
                           int dark_pct)
{
    if (dark_pct >= 100 || !lum)
        return;   /* no modulation -- output byte-identical */
    if (dark_pct < 0)
        dark_pct = 0;

    /* Precompute the factor for every brightness, so the per-pixel work is a
     * lookup rather than a divide. */
    float factor[256];
    for (int v = 0; v < 256; v++)
        factor[v] = (dark_pct + (100 - dark_pct) * (v / 255.0f)) / 100.0f;

    for (int i = 0; i < count; i++) {
        if (white[i] == 0)
            continue;
        const int v = (int)(white[i] * factor[lum[i]] + 0.5f);
        white[i] = (uint8_t)(v > 255 ? 255 : (v < 0 ? 0 : v));
    }
}

/* Sliding-window minimum over one line, window [i-r, i+r], borders replicated.
 * Monotonic deque, so the cost is O(n) rather than O(n*r) — the vertical radius
 * on a 1440 dpi raster can be a hundred pixels. `dq` is scratch of n ints. */
static void erode_line(const uint8_t *in, int stride_in, uint8_t *out,
                       int stride_out, int n, int r, int *dq)
{
    if (r <= 0) {
        for (int i = 0; i < n; i++)
            out[(size_t)i * stride_out] = in[(size_t)i * stride_in];
        return;
    }
    int head = 0, tail = 0;   /* dq holds indices, values increasing */
    /* Prime the window with everything up to r (left border replicates in[0],
     * which can never be smaller than in[0] itself, so it needs no entry). */
    for (int i = 0; i <= r && i < n; i++) {
        const uint8_t v = in[(size_t)i * stride_in];
        while (tail > head && in[(size_t)dq[tail - 1] * stride_in] >= v)
            tail--;
        dq[tail++] = i;
    }
    for (int i = 0; i < n; i++) {
        const int add = i + r;
        if (add < n && i > 0) {
            const uint8_t v = in[(size_t)add * stride_in];
            while (tail > head && in[(size_t)dq[tail - 1] * stride_in] >= v)
                tail--;
            dq[tail++] = add;
        }
        while (tail > head && dq[head] < i - r)
            head++;
        uint8_t m = in[(size_t)dq[head] * stride_in];
        /* Replicated borders: the window hanging off an end sees that end's
         * value, which is already in the deque, so nothing more to do. */
        out[(size_t)i * stride_out] = m;
    }
}

int apply_white_choke(uint8_t *white, int width, int height,
                      float mm_per_px_x, float mm_per_px_y, float choke_mm)
{
    if (choke_mm <= 0.0f || width <= 0 || height <= 0)
        return 0;   /* no-op -- output byte-identical */

    /* Same physical depth on both axes, which on this raster is a very
     * different number of pixels for each. */
    int rx = (mm_per_px_x > 0.0f) ? (int)(choke_mm / mm_per_px_x + 0.5f) : 0;
    int ry = (mm_per_px_y > 0.0f) ? (int)(choke_mm / mm_per_px_y + 0.5f) : 0;
    if (rx <= 0 && ry <= 0)
        return 0;

    const size_t n = (size_t)width * height;
    uint8_t *tmp = malloc(n);
    int *dq = malloc(sizeof(int) * (size_t)(width > height ? width : height));
    if (!tmp || !dq) {
        free(tmp); free(dq);
        return -1;
    }

    for (int y = 0; y < height; y++)
        erode_line(&white[(size_t)y * width], 1, &tmp[(size_t)y * width], 1,
                   width, rx, dq);
    for (int x = 0; x < width; x++)
        erode_line(&tmp[x], width, &white[x], width, height, ry, dq);

    free(tmp);
    free(dq);
    return 0;
}
