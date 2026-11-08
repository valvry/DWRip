/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "prn_gen.h"
#include "packbits.h"
#include "halftone.h"   /* drop_tiers_t / drop_lut_t — the -D drop ladder */
#include <stdio.h>
#include <string.h>

static int effective_zone_count(const print_settings_t *s);

/* ────────────────────────────────────────────────────────────
 * Fixed header bytes extracted from AcroRIP sample files.
 * All 5 samples (white_first, color_first, color_only,
 * white_only, different channel orders) have identical
 * header and trailer.
 * ──────────────────────────────────────────────────────────── */

/* Section 1: EJL init + printer reset (0x00 - 0x1e) */
static const uint8_t ejl_init[] = {
    0x00, 0x00, 0x00, 0x1b, 0x01, /* ESC SOH */
    '@',  'E',  'J',  'L',  ' ',  '1', '2', '8', '4', '.',
    '4',  0x0a,                                             /* @EJL 1284.4\n */
    '@',  'E',  'J',  'L',  ' ',  ' ', ' ', ' ', ' ', 0x0a, /* @EJL     \n */
    0x1b, 0x40,                                             /* ESC @ reset */
    0x1b, 0x40, /* ESC @ reset (twice) */
};

/* Section 2: REMOTE1 JS block + padding (0x1f - 0x4f) */
static const uint8_t remote1_js[] = {
    0x1b, 0x28, 0x52, 0x08, 0x00,                /* ESC (R len=8 */
    0x00, 'R',  'E',  'M',  'O',  'T', 'E', '1', /* REMOTE1 */
    'J',  'S',  0x01, 0x00, 0x00,                /* JS 01 00 00 */
    0x1b, 0x00, 0x00, 0x00,                      /* end REMOTE */
    0x00, 0x00, 0x00,                            /* padding */
};

/* Section 3: second EJL init (0x39 - 0x4f) */
static const uint8_t ejl_init2[] = {
    0x1b, 0x01, '@', 'E', 'J', 'L', ' ', '1', '2', '8', '4', '.',
    '4',  0x0a, '@', 'E', 'J', 'L', ' ', ' ', ' ', ' ', ' ', 0x0a,
};

/* Section 4: REMOTE1 config block */
static const uint8_t remote1_config[] = {
    0x1b, 0x40,                                    /* ESC @ reset */
    0x1b, 0x28, 0x52, 0x08, 0x00,                  /* ESC (R len=8 */
    0x00, 'R',  'E',  'M',  'O',  'T',  'E',  '1', /* REMOTE1 */
    'P',  'M',  0x02, 0x00, 0x00, 0x00, /* PM 02 00 00 00 - paper media */
    'D',  'P',  0x02, 0x00, 0x00, 0x00, /* DP 02 00 00 00 */
    'S',  'N',  0x01, 0x00, 0x00,       /* SN 01 00 00 */
    'M',  'I',  0x04, 0x00, 0x00, 0x01, 0x0b, 0x63, /* MI - model ID L1800 */
    'F',  'P',  0x03, 0x00, 0x00, 0xb0, 0xff,       /* FP */
    'D',  'R',  0x04, 0x00, 0x00, 0x00, 0x00, 0x00, /* DR */
    'I',  'K',  0x02, 0x00, 0x00, 0x31,             /* IK - ink type '1' */
    'U',  'S',  0x03, 0x00, 0x00, 0x00, 0x00,       /* US */
    'U',  'S',  0x03, 0x00, 0x00, 0x01, 0x00,       /* US */
    'U',  'S',  0x03, 0x00, 0x00, 0x02, 0x00,       /* US */
    'E',  'X',  0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, /* EX */
    'A',  'C',  0x02, 0x00, 0x00, 0x00,                         /* AC */
    'A',  'C',  0x02, 0x00, 0x00, 0x40,                         /* AC */
    'I',  'R',  0x02, 0x00, 0x00, 0x03, /* IR - resolution? */
    'E',  'X',  0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, /* EX */
    'P',  'P',  0x03, 0x00, 0x00, 0x01, 0xff, /* PP - paper path */
    0x1b, 0x00, 0x00, 0x00,                   /* end REMOTE */
};

/* Section 5: ESC/P raster setup commands */
static void write_escp_setup(FILE *fp, const print_settings_t *s) {
  /* ESC (G 01 00 01 — graphics mode ON */
  uint8_t cmd_g[] = {0x1b, 0x28, 0x47, 0x01, 0x00, 0x01};
  fwrite(cmd_g, 1, sizeof(cmd_g), fp);

  /* ESC (U 05 00 p1 pV pH 80 16 — unit/resolution (p1 == pV) */
  uint8_t cmd_u[] = {0x1b, 0x28, 0x55, 0x05, 0x00,
                     s->escU_pV, s->escU_pV, s->escU_pH, 0x80, 0x16};
  fwrite(cmd_u, 1, sizeof(cmd_u), fp);

  /* ESC (K 02 00 00 02 — dot size medium+large */
  uint8_t cmd_k[] = {0x1b, 0x28, 0x4b, 0x02, 0x00, 0x00, 0x02};
  fwrite(cmd_k, 1, sizeof(cmd_k), fp);

  /* ESC (i 03 00 00 00 00 — microweave/interleave mode */
  uint8_t cmd_i[] = {0x1b, 0x28, 0x69, 0x03, 0x00, 0x00, 0x00, 0x00};
  fwrite(cmd_i, 1, sizeof(cmd_i), fp);

  /* ESC U 00 — bidirectional (00=bidi, 01=unidi) */
  uint8_t cmd_bidi[] = {0x1b, 0x55, s->bidirectional ? 0x00 : 0x01};
  fwrite(cmd_bidi, 1, sizeof(cmd_bidi), fp);

  /* ESC (e 02 00 00 XX — dot size fine-tune (per resolution) */
  uint8_t cmd_e[] = {0x1b, 0x28, 0x65, 0x02, 0x00, 0x00, s->escE};
  fwrite(cmd_e, 1, sizeof(cmd_e), fp);

  /* ESC (m 01 00 20 */
  uint8_t cmd_m[] = {0x1b, 0x28, 0x6d, 0x01, 0x00, 0x20};
  fwrite(cmd_m, 1, sizeof(cmd_m), fp);

  /* ESC (D 04 00 .. — base raster resolution (per resolution) */
  uint8_t cmd_d[] = {0x1b, 0x28, 0x44, 0x04, 0x00,
                     s->escD[0], s->escD[1], s->escD[2], s->escD[3]};
  fwrite(cmd_d, 1, sizeof(cmd_d), fp);

  /* ESC (C 04 00 LL LL LL LL — page length in current vertical units (LE32).
   * Must cover the full head travel: image + trailing-zone overlap
   * (720 dual / 960 triple) + head span (90×16) + initial advance, plus
   * slack.  AcroRIP uses an even larger margin (6541 for 2268 rows). */
  /* All terms in raster_dpi_v units (= page-management unit for this preset). */
  /* Trailing-zone overlap = (eff-1) zones behind the leading one.  Derived from
   * the EFFECTIVE zone count so the denser single-layer cure weaves (which run
   * 3 or 5 zones' worth of geometry) reserve enough page.  Single-layer fast
   * (eff 1) keeps the old 45-nozzle allowance: harmless slack, and changing it
   * would move bytes in every existing output. */
  int zc = effective_zone_count(s);
  uint32_t zone_span = (zc > 1)
      ? (uint32_t)(zc - 1) * (NOZZLES_PER_HEAD / (uint32_t)zc)
            * (uint32_t)s->nozzle_spacing
      : 45u * (uint32_t)s->nozzle_spacing;
  uint32_t uv_tail = (s->uv_tail_mm > 0)
                         ? (uint32_t)(s->uv_tail_mm / 25.4f * s->raster_dpi_v + 0.5f)
                         : 0;
  uint32_t page_len = (uint32_t)s->height_px +
                      zone_span + NOZZLES_PER_HEAD * (uint32_t)s->nozzle_spacing +
                      (uint32_t)s->initial_v + uv_tail + (uint32_t)s->raster_dpi_v;
  uint8_t page_len_le[4] = {page_len & 0xff, (page_len >> 8) & 0xff,
                            (page_len >> 16) & 0xff, (page_len >> 24) & 0xff};
  uint8_t cmd_c_hdr[] = {0x1b, 0x28, 0x43, 0x04, 0x00};
  fwrite(cmd_c_hdr, 1, sizeof(cmd_c_hdr), fp);
  fwrite(page_len_le, 1, 4, fp);

  /* ESC (c 08 00 top bottom — margins. Top is resolution-specific:
   * -1440 for 1440-unit modes, -720 for 720-unit modes. */
  int32_t top = s->page_top_units ? s->page_top_units : -1440;
  uint32_t top_u = (uint32_t)top;
  uint8_t top_le[4] = {top_u & 0xff, (top_u >> 8) & 0xff,
                       (top_u >> 16) & 0xff, (top_u >> 24) & 0xff};
  uint8_t cmd_cc[] = {0x1b, 0x28, 0x63, 0x08, 0x00,
                      top_le[0], top_le[1], top_le[2], top_le[3]};
  fwrite(cmd_cc, 1, sizeof(cmd_cc), fp);
  fwrite(page_len_le, 1, 4, fp);

  /* ESC (S 08 00 WW WW WW WW HH HH HH HH — paper size */
  uint32_t paper_w = s->paper_width_units > 0
                         ? (uint32_t)s->paper_width_units
                         : 18720u;
  uint8_t cmd_s[13] = {0x1b, 0x28, 0x53, 0x08, 0x00};
  cmd_s[5] = paper_w & 0xff;
  cmd_s[6] = (paper_w >> 8) & 0xff;
  cmd_s[7] = (paper_w >> 16) & 0xff;
  cmd_s[8] = (paper_w >> 24) & 0xff;
  memcpy(&cmd_s[9], page_len_le, 4);
  fwrite(cmd_s, 1, 13, fp);
}

/* ── Trailer ─────────────────────────────────────────────── */
static const uint8_t trailer_bytes[] = {
    0x0d, 0x0c,                                   /* CR + Form Feed */
    0x1b, 0x40,                                   /* ESC @ reset */
    0x1b, 0x28, 0x52, 0x08, 0x00,                 /* ESC (R len=8 */
    0x00, 'R',  'E',  'M',  'O',  'T',  'E', '1', /* REMOTE1 */
    'I',  'R',  0x02, 0x00, 0x00, 0x02, /* IR 02 00 00 02 - end print */
    'L',  'D',  0x00, 0x00,             /* LD 00 00 */
    0x1b, 0x00, 0x00, 0x00,             /* end REMOTE */
    0x1b, 0x40,                         /* ESC @ reset */
    0x1b, 0x28, 0x52, 0x08, 0x00,       /* ESC (R len=8 */
    0x00, 'R',  'E',  'M',  'O',  'T',  'E', '1', /* REMOTE1 */
    'L',  'D',  0x00, 0x00,                       /* LD 00 00 */
    'J',  'E',  0x01, 0x00, 0x00,                 /* JE 01 00 00 - job end */
    0x1b, 0x00, 0x00, 0x00,                       /* end REMOTE */
};

/* ── Public API ──────────────────────────────────────────── */

void prn_write_header(FILE *fp, const print_settings_t *settings) {
  fwrite(ejl_init, 1, sizeof(ejl_init), fp);
  fwrite(remote1_js, 1, sizeof(remote1_js), fp);
  fwrite(ejl_init2, 1, sizeof(ejl_init2), fp);
  fwrite(remote1_config, 1, sizeof(remote1_config), fp);
  write_escp_setup(fp, settings);
}

void prn_write_vadvance(FILE *fp, uint32_t units) {
  uint8_t cmd[] = {0x1b,
                   0x28,
                   0x76,
                   0x04,
                   0x00,
                   units & 0xff,
                   (units >> 8) & 0xff,
                   (units >> 16) & 0xff,
                   (units >> 24) & 0xff};
  fwrite(cmd, 1, sizeof(cmd), fp);
}

static int lead_in_advance_step(const print_settings_t *s) {
  if (s->lead_in_feed_step > 0)
    return s->lead_in_feed_step;
  if (s->lead_in_feed_step == 0 && s->lead_in_sweep_mode == LEAD_IN_SWEEP_OFF)
    return 0;

  if (s->weave_mode == RES_WEAVE_FIXED) {
    int step = s->fixed_advance > 0 ? s->fixed_advance : s->pass_advance;
    if (step <= 0)
      step = 45;
    return step > 45 ? 45 : step;
  }

  if (s->pass_advance > 0)
    return s->pass_advance;
  return 11;
}

void prn_write_hreset(FILE *fp) {
  uint8_t cmd[] = {0x1b, 0x28, 0x24, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
  fwrite(cmd, 1, sizeof(cmd), fp);
}

void prn_write_cr(FILE *fp) {
  uint8_t cr = 0x0d;
  fwrite(&cr, 1, 1, fp);
}

void prn_write_band(FILE *fp, uint8_t color_code, uint8_t compression,
                    uint8_t bpp, uint16_t bytes_per_line, uint16_t nlines,
                    const uint8_t *data, size_t data_len) {
  /* ESC i CC COMP BPP BPL_lo BPL_hi NL_lo NL_hi [data] */
  uint8_t hdr[] = {
      0x1b,
      0x69,
      color_code,
      compression,
      bpp,
      bytes_per_line & 0xff,
      (bytes_per_line >> 8) & 0xff,
      nlines & 0xff,
      (nlines >> 8) & 0xff,
  };
  fwrite(hdr, 1, sizeof(hdr), fp);
  fwrite(data, 1, data_len, fp);
}

static int build_dummy_sweep_band(uint8_t *raw_band, size_t raw_band_size,
                                  int total_bytes_per_line,
                                  lead_in_sweep_mode_t sweep_mode,
                                  uint8_t *comp_buf, size_t comp_cap,
                                  int *out_comp_total) {
  memset(raw_band, 0, raw_band_size);
  for (int ln = 0; ln < NOZZLES_PER_HEAD; ln++) {
    uint8_t *row = &raw_band[(size_t)ln * total_bytes_per_line];
    row[0] |= 0x01;                        /* tiny dot near left edge */
    if (sweep_mode == LEAD_IN_SWEEP_FULL)
      row[total_bytes_per_line - 1] |= 0x01; /* tiny dot near right edge */
  }

  int comp_total = 0;
  for (int ln = 0; ln < NOZZLES_PER_HEAD; ln++) {
    int cl = packbits_compress(&raw_band[ln * total_bytes_per_line],
                               total_bytes_per_line,
                               &comp_buf[comp_total],
                               comp_cap - comp_total);
    if (cl < 0)
      return -1;
    comp_total += cl;
  }
  *out_comp_total = comp_total;
  return 0;
}

static void prn_write_lead_in_vadvance(FILE *fp, uint32_t units,
                                       const print_settings_t *s,
                                       int use_dummy_sweep,
                                       uint8_t dummy_code,
                                       uint16_t bytes_per_line,
                                       const uint8_t *dummy_data,
                                       size_t dummy_data_len) {
  uint32_t step = (uint32_t)lead_in_advance_step(s);
  if (step == 0)
    step = units;

  while (units > 0) {
    uint32_t adv = units > step ? step : units;
    prn_write_vadvance(fp, adv);
    units -= adv;

    if (use_dummy_sweep && units > 0) {
      prn_write_hreset(fp);
      prn_write_band(fp, dummy_code, 1, BPP, bytes_per_line,
                     NOZZLES_PER_HEAD, dummy_data, dummy_data_len);
      prn_write_cr(fp);
    }
  }
}

void prn_write_trailer(FILE *fp) {
  fwrite(trailer_bytes, 1, sizeof(trailer_bytes), fp);
}

/* ── High-level PRN generation ───────────────────────────── */

/*
 * Full phase-based microweave engine (matches AcroRIP).
 *
 * Raster stored at 1440 dpi vertical (each raster row = 1/1440 inch = 1 unit).
 * Nozzle physical spacing = 1/90 inch = 16 units.
 * Pass advance = 11 units (coprime with 16 → cycles all 16 sub-positions).
 * Phase structure: 63 passes × 11 units + 1 pass × 27 units = 720 = 45×16.
 *
 * Zone split is done at the DATA level, not structurally:
 *   - A 45-virtual-nozzle weave table is shared by both zones.
 *   - The layer printed FIRST goes to nozzles 45-89 (Zone B, leading edge).
 *   - The layer printed SECOND goes to nozzles 0-44 (Zone A, trailing edge),
 *     reading image rows delayed by ZONE_OFFSET (720 units).
 *   - Which layer (white/color) leads depends on the white_first setting.
 */

/* Inverted weave index: for each pass, the list of image rows it covers.
 * A pass fires at most n_virtual nozzles, so counts are bounded by 90. */
typedef struct {
  int count;
  int rows[NOZZLES_PER_HEAD];
  uint8_t vnozzles[NOZZLES_PER_HEAD];
} pass_assign_t;

/*
 * Advance value for a pass, by effective zone count:
 *
 *   zone_count_eff == 2 (dual zone, AcroRIP verified):
 *     27-unit separator every 64th ESC(v) so each phase covers exactly
 *     63×11 + 27 = 720 units = 45 nozzles × 16 units.  The separator is
 *     anchored to the absolute carriage position (including the initial
 *     22-unit advance): it fires on the pass whose advance lands the head
 *     on a multiple of the phase distance.
 *
 *   zone_count_eff == 1 (single layer):
 *     same rule with DOUBLE the advance space (22, 54) to print 2x
 *     faster, hitting only even rows at 720dpi effectively.
 *
 *   zone_count_eff == 3 (C/W/C sandwich):
 *     alternating 7/8 → 96 passes × avg 7.5 = 720 units per phase.
 *     With 30 virtual nozzles this is the pattern that yields a perfectly
 *     uniform 4 hits/row (the 11/27 pattern gives a 2/3-hit mix → banding).
 *
 * cum_units = total advance accumulated since the first pass (y - y_start).
 */
static int phase_advance(int cum_units, int zone_count_eff,
                         const print_settings_t *s) {
  if (s->weave_mode == RES_WEAVE_FIXED)
    return s->fixed_advance > 0 ? s->fixed_advance : s->pass_advance;

  if (zone_count_eff == 3)
    return (cum_units % 15 == 0) ? 7 : 8; /* 3-zone: tuned for 1440 spacing */
  if (zone_count_eff == 5)
    return (cum_units % 9 == 0) ? 4 : 5;  /* 5-zone cure weave: avg 4.5 = 18/4 */

  int mult = (zone_count_eff == 1) ? 2 : 1;
  int phase = s->phase_distance * mult;  /* 45×spacing (×2 single-layer) */
  int sep_adv = s->sep_advance * mult;   /* (spacing+11) (×2 single-layer) */

  if ((cum_units + s->initial_v) % phase == phase - sep_adv)
    return sep_adv;
  return s->pass_advance * mult;
}

/*
 * Which zones a channel fires in.  Bit z set = channel prints in zone z.
 * Zone 0 is the leading edge (highest nozzle numbers, lands on the film
 * first); higher zones trail behind reading earlier image rows.
 *
 *   2 zones: first ink layer → zone 0, second layer → zone 1
 *            (which layer leads depends on white_first).
 *   3 zones: Color/White/Color sandwich — CMYK fires in zones 0 AND 2
 *            (duplicate data), white in zone 1.  white_first is ignored.
 *            For a future varnish channel: give it mask 0x4 and reduce
 *            color back to 0x1.
 */
/* Per-pixel hit-rotation offset for the plane-mode band builders.
 *
 * A plain `(k + x) % hits` rotation is PERIODIC in x (period = hits): which
 * sweep lays a column's first drop cycles every 8 columns, and with UV inks
 * (cured per sweep) that deposition-order period prints as fine vertical
 * gloss banding (measured: first-hit autocorr +0.91 at lag 8 vs AcroRIP's
 * flat ~0).  Hashing (x, y) decorrelates the rotation per pixel — still
 * volume-exact (any per-pixel offset is), but order-flat across columns. */
static inline int hit_rotation(int x, int y) {
  unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (int)((h >> 16) & 0x7fff);
}

static unsigned channel_zone_mask(const channel_map_t *ch,
                                  const print_settings_t *s) {
  if (s->print_mode != PRINT_BOTH)
    return 0x1u; /* single layer: one full-head zone */
  if (s->zone_count == 3)
    return ch->is_white ? 0x2u : 0x5u;
  bool leading = (ch->is_white == s->white_first);
  return leading ? 0x1u : 0x2u;
}

/* Head zone count actually used by the weave.  Single-layer normally collapses
 * to 1 full-head zone (advance doubled, 2x faster); SINGLE_WEAVE_DENSE instead
 * borrows the dual-zone geometry so the carriage — and the UV lamp on it —
 * sweeps each row twice as often at identical ink (see single_weave_t).
 * Invalid splits fall back to 2. */
static int effective_zone_count(const print_settings_t *s) {
  int eff;
  if (s->print_mode == PRINT_BOTH) {
    eff = s->zone_count;
  } else {
    switch (s->single_weave) {
    case SINGLE_WEAVE_DENSE: eff = 2; break;
    case SINGLE_WEAVE_ULTRA: eff = 3; break;
    case SINGLE_WEAVE_MAX:   eff = 5; break;
    default:                 eff = 1; break;
    }
  }
  if (eff < 1 || NOZZLES_PER_HEAD % eff != 0)
    eff = 2;
  return eff;
}

int prn_channel_hits(const print_settings_t *s, const channel_map_t *ch) {
  int eff = effective_zone_count(s);
  int max_hits = (eff == 1) ? 8 : 4;
  unsigned mask = channel_zone_mask(ch, s) & ((1u << eff) - 1u);
  int zones = 0;
  while (mask) {
    zones += (int)(mask & 1u);
    mask >>= 1;
  }
  /* z3 split: in 3-zone both-mode with a non-SAME zone-3 source, colour zones
   * 0 & 2 print DIFFERENT images, so each is an independent single-zone deposit
   * (4 hits) instead of one image shared across both (8 hits). */
  if (s->z3_mode != Z3_SAME && eff == 3 && !ch->is_white)
    zones = 1;
  return max_hits * (zones ? zones : 1);
}

/*
 * Shared 45-nozzle weave table with phase-based advance.
 *
 * Virtual nozzles 0-44 act as a "master timing belt" shared by both zones:
 *   Zone B (leading, prints the first layer): actual nozzle = V + 45
 *   Zone A (trailing, prints the second layer): actual nozzle = V
 *
 * Each pass, ALL 45 virtual nozzles participate in BOTH zones simultaneously.
 * The zone offset (720 units) is the natural gap between nozzle 0 and 45.
 *
 * Phase advance: every 64th pass gets a 27-unit advance (not 11), producing
 * 63*11 + 27 = 720 = 45*16 units per phase — matching AcroRIP.
 *
 * The table is stored inverted (per pass → covered rows) so band assembly
 * only walks the ≤90 rows of one pass instead of the whole image height.
 */
static pass_assign_t *build_phase_weave_table(int height_total,
                                              int n_virtual,
                                              int max_hits,
                                              int zone_count_eff,
                                              const print_settings_t *s,
                                              int *out_total_passes,
                                              int **out_pass_advances) {

  int adv_cap = height_total + 2000;

  pass_assign_t *table = calloc(adv_cap, sizeof(*table));
  if (!table) return NULL;

  uint8_t *row_hits = calloc(height_total, 1);
  int *advances = malloc(adv_cap * sizeof(int));
  if (!advances || !row_hits) {
    free(table);
    free(row_hits);
    free(advances);
    return NULL;
  }

  int y_start = -(n_virtual * s->nozzle_spacing); /* -720 / -360 / -1440 */
  int y = y_start;
  int pass = 0;
  int max_passes = adv_cap;

  /* Run the weave until the head has completely cleared the total height area */
  while (y < height_total && pass < max_passes) {
    for (int v = 0; v < n_virtual; v++) {
      int row = y + v * s->nozzle_spacing;
      if (row >= 0 && row < height_total) {
        /* Allow up to max_hits per row */
        if (row_hits[row] < max_hits) {
          int idx = table[pass].count;
          table[pass].rows[idx] = row;
          table[pass].vnozzles[idx] = (uint8_t)v;
          table[pass].count++;
          row_hits[row]++;
        }
      }
    }

    int adv = phase_advance(y - y_start, zone_count_eff, s);
    advances[pass] = adv;
    y += adv;
    pass++;
  }

  /* Hit-uniform check: every fully-overlapped interior row must reach max_hits.
   * A wrong phase for a resolution shows up here as banding (min < max_hits). */
  {
    int lo = n_virtual * s->nozzle_spacing;
    int hi = height_total - n_virtual * s->nozzle_spacing;
    int mn = max_hits, mx = 0;
    for (int r = lo; r < hi; r++) {
      if (row_hits[r] < mn) mn = row_hits[r];
      if (row_hits[r] > mx) mx = row_hits[r];
    }
    if (hi > lo && mn != max_hits && s->weave_mode == RES_WEAVE_PHASE)
      fprintf(stderr,
              "[weave] WARNING: non-uniform coverage (interior hits %d..%d, "
              "want %d) — possible banding at this resolution\n",
              mn, mx, max_hits);
  }

  free(row_hits);
  *out_total_passes = pass;
  *out_pass_advances = advances;
  return table;
}

static int pass_touches_image_region(const pass_assign_t *pa, int src_height,
                                     unsigned zone_mask, int zone_count_eff,
                                     int zone_step, int reg_v) {
  for (int h = 0; h < pa->count; h++) {
    int row = pa->rows[h];
    for (int z = 0; z < zone_count_eff; z++) {
      if (!((zone_mask >> z) & 1u))
        continue;

      int image_y = row - z * zone_step + reg_v;
      if (image_y >= 0 && image_y < src_height)
        return 1;
    }
  }
  return 0;
}

/*
 * Fill a 90-row band buffer for one (channel, pass) from the inverted table.
 * This is the only band builder: the structured dither is the only halftone
 * path.
 *
 * Zone geometry — the table holds virtual nozzles 0..n_virtual-1, each mapped
 * to every zone the channel fires in (zone_mask):
 *   actual nozzle = V + (zone_count_eff - 1 - z) * n_virtual
 *                   (zone 0 = leading edge = highest nozzle numbers)
 *   image_y       = row - z * zone_step + reg_v
 * Trailing zones read earlier image rows: by the time the paper has advanced
 * one zone_step, the trailing zone lands exactly on the rows the zone ahead
 * of it already printed.
 *
 * `plans` is a deposit-plan plane from halftone_fs_structure; `lut` turns a
 * plan into per-deposit dot codes for the active drop mode (-D).  Hit k of a
 * pixel fires:
 *     idx  = (k + rot(x,y)) % hits
 *     code = lut->code[plan][idx]   (largest tier first, 0 = nothing)
 * The rotation spreads a stack across the weave passes (disjoint dashes per
 * pass, AcroRIP-style build-up).  hits_seen[y] counts deposits per image row
 * for THIS channel; passes are generated sequentially, so k spans 0..hits-1
 * exactly once per interior row (edge rows get fewer and lose the top codes).
 *
 * nozzle_has_data[90] is set per nozzle line that received non-zero data
 * (a plan plane may be all-zero on rows the weave wants active).  Returns 1
 * if any nozzle has data.
 */
static int build_pass_band_structure(const pass_assign_t *pa,
                                     const uint8_t *plans, int width_px,
                                     int margin_bytes, int total_bytes_per_line,
                                     int src_height,
                                     unsigned zone_mask, int zone_count_eff,
                                     int n_virtual, int zone_step, int reg_v,
                                     int hits, uint8_t *hits_seen,
                                     const uint8_t *plans_z3, uint8_t *hits_seen_z3,
                                     const drop_lut_t *lut,
                                     uint8_t *raw_band, size_t raw_band_size,
                                     uint8_t nozzle_has_data[NOZZLES_PER_HEAD]) {
  memset(raw_band, 0, raw_band_size);
  memset(nozzle_has_data, 0, NOZZLES_PER_HEAD);
  int has_data = 0;

  for (int h = 0; h < pa->count; h++) {
    int row = pa->rows[h];
    int virtual_nozzle = pa->vnozzles[h];

    for (int z = 0; z < zone_count_eff; z++) {
      if (!((zone_mask >> z) & 1u))
        continue;

      int actual_nozzle = virtual_nozzle + (zone_count_eff - 1 - z) * n_virtual;
      int image_y = row - z * zone_step + reg_v;

      if (image_y < 0 || image_y >= src_height)
        continue;

      /* Trailing colour zone (z==2) uses the independent zone-3 plans + its
       * own hit counter, so image B deposits separately from image A. */
      int z3 = (z == 2 && plans_z3);
      int k = (z3 ? hits_seen_z3 : hits_seen)[image_y]++;
      const uint8_t *prow = &(z3 ? plans_z3 : plans)[(size_t)image_y * width_px];
      uint8_t *out = &raw_band[actual_nozzle * total_bytes_per_line
                               + margin_bytes];
      for (int x = 0; x < width_px; x++) {
        int p = prow[x];
        if (!p)
          continue;
        int idx = (k + hit_rotation(x, image_y)) % hits;
        int code = lut->code[p][idx];   /* 0 = this deposit stays empty */
        if (!code)
          continue;
        out[x >> 2] |= (uint8_t)(code << (6 - (x & 3) * 2)); /* MSB first */
        nozzle_has_data[actual_nozzle] = 1;
        has_data = 1;
      }
    }
  }
  return has_data;
}

/* ═══════════════════ EMBOSS / SPOT 3D (Cara B) ═══════════════════
 * Asymmetric-zone weave over the full 90 nozzles: the top `white_noz` nozzles
 * lay SOLID white (relief, cured per-pass by the carriage UV lamp before the
 * next layer), the bottom nozzles lay colour on top. Presets validated by
 * tools/emboss_weave_sim.py (hits must be integer or the relief bands). */
typedef struct {
  int layers, white_noz, adv_a, adv_b, white_hits, color_hits;
} emboss_preset_t;
static const emboss_preset_t EMBOSS_PRESETS[] = {
    { 8, 60, 7, 8,  8, 4},   /* thin,  ~1.5x slower */
    {16, 72, 4, 5, 16, 4},   /* medium ~2.5x */
    {32, 80, 2, 3, 32, 4},   /* thick  ~4.5x, practical ceiling */
};
/* Cara A (split): white-only run — ALL 90 nozzles white, uniform advance 90/N,
 * colour prints in a separate full-quality run. Taller relief per run. */
static const emboss_preset_t EMBOSS_SPLIT_PRESETS[] = {
    {15, 90, 6, 6, 15, 0},
    {30, 90, 3, 3, 30, 0},
    {45, 90, 2, 2, 45, 0},
    {90, 90, 1, 1, 90, 0},
};
static const emboss_preset_t *emboss_lookup(int layers) {
  for (unsigned i = 0; i < sizeof(EMBOSS_PRESETS)/sizeof(EMBOSS_PRESETS[0]); i++)
    if (EMBOSS_PRESETS[i].layers == layers) return &EMBOSS_PRESETS[i];
  return NULL;
}
static const emboss_preset_t *emboss_lookup_split(int layers) {
  for (unsigned i = 0; i < sizeof(EMBOSS_SPLIT_PRESETS)/sizeof(EMBOSS_SPLIT_PRESETS[0]); i++)
    if (EMBOSS_SPLIT_PRESETS[i].layers == layers) return &EMBOSS_SPLIT_PRESETS[i];
  return NULL;
}
int emboss_is_valid_layers(int layers) { return emboss_lookup(layers) != NULL; }
int emboss_is_valid_split_layers(int layers) {
  return emboss_lookup_split(layers) != NULL;
}

/* Full-head weave: nozzle v ≥ color_noz = white (leading), v < color_noz =
 * colour (trailing). Advance alternates adv_a/adv_b per pass. White & colour
 * capped independently per row so the split is exact. */
static pass_assign_t *build_emboss_weave_table(
    int height_px, int spacing, int color_noz, int adv_a, int adv_b,
    int white_hits, int color_hits, int *out_passes, int **out_advances) {
  int y_start = -(NOZZLES_PER_HEAD * spacing);
  int cap = (height_px - y_start) + 2000;
  pass_assign_t *table = calloc(cap, sizeof(*table));
  int *advances = malloc((size_t)cap * sizeof(int));
  uint8_t *wh = calloc(height_px, 1), *ch = calloc(height_px, 1);
  if (!table || !advances || !wh || !ch) {
    free(table); free(advances); free(wh); free(ch); return NULL;
  }
  int y = y_start, pass = 0;
  while (y < height_px && pass < cap) {
    for (int v = 0; v < NOZZLES_PER_HEAD; v++) {
      int row = y + v * spacing;
      if (row < 0 || row >= height_px) continue;
      if (v >= color_noz) { if (wh[row] >= white_hits) continue; wh[row]++; }
      else                { if (ch[row] >= color_hits) continue; ch[row]++; }
      int idx = table[pass].count;
      table[pass].rows[idx] = row;
      table[pass].vnozzles[idx] = (uint8_t)v;
      table[pass].count++;
    }
    advances[pass] = (pass % 2 == 0) ? adv_a : adv_b;
    y += advances[pass];
    pass++;
  }
  int lo = NOZZLES_PER_HEAD * spacing, hi = height_px - NOZZLES_PER_HEAD * spacing;
  int bad = 0;
  for (int r = lo; r < hi; r++)
    if (wh[r] != white_hits || ch[r] != color_hits) bad++;
  if (hi > lo && bad)
    fprintf(stderr, "[emboss] WARNING: %d interior rows non-uniform "
                    "(want white=%d colour=%d) — relief may band\n",
            bad, white_hits, color_hits);
  free(wh); free(ch);
  *out_passes = pass; *out_advances = advances;
  return table;
}

/* White in emboss, two kinds in one band.
 *
 * Relief (white_cap): SOLID (code 3) while cap > the row's white-hit index —
 * height comes from stacking whole cured layers, so a dithered layer would
 * simply be a shorter one, not a lighter one.
 *
 * Underbase (white_plan): the thin white under colour outside the raised area.
 * It carries no height, so it is dithered like any other ink and answers to
 * -L, --white-mode and the -D ladder. It deposits during the first
 * `underbase_hits` hits of the row.
 *
 * A pixel is one or the other, never both. */
static int emboss_white_band(const pass_assign_t *pa, const uint8_t *white_cap,
                             const uint8_t *white_plan, int underbase_hits,
                             const drop_lut_t *ulut,
                             int width_px, int margin_bytes, int total_bpl,
                             int color_noz, uint8_t *white_seen,
                             uint8_t *raw_band, size_t raw_band_size) {
  memset(raw_band, 0, raw_band_size);
  int has = 0, last_v = -1;
  for (int h = 0; h < pa->count; h++) {
    int v = pa->vnozzles[h];
    if (v < color_noz) continue;
    int row = pa->rows[h];
    int k = white_seen[row]++;
    const uint8_t *cap = &white_cap[(size_t)row * width_px];
    const uint8_t *plan = white_plan ? &white_plan[(size_t)row * width_px] : NULL;
    uint8_t *out = &raw_band[(size_t)v * total_bpl + margin_bytes];
    for (int x = 0; x < width_px; x++) {
      int code = 0;
      if (cap[x] > k) {
        code = 3;                                   /* relief layer */
      } else if (plan && plan[x] && k < underbase_hits) {
        const int idx = (k + hit_rotation(x, row)) % underbase_hits;
        code = ulut->code[plan[x]][idx];             /* underbase */
      }
      if (!code) continue;
      out[x >> 2] |= (uint8_t)(code << (6 - (x & 3) * 2));
      has = 1;
      if (last_v < 0) last_v = v;
    }
  }
  /* Defeat logic-seeking: a microscopic dot at x=0 (in the left margin) forces
   * the carriage to sweep the full width from home, so the carriage-mounted UV
   * lamp passes over the ink and cures it (matches the normal-mode dummy). */
  if (has && margin_bytes > 0 && last_v >= 0)
    raw_band[(size_t)last_v * total_bpl] |= 0x01;
  return has;
}

/* Colour (structure plans) in the trailing zone, `hits` deposits per row. */
static int emboss_color_band(const pass_assign_t *pa, const uint8_t *plans,
                             int width_px, int margin_bytes, int total_bpl,
                             int color_noz, int hits, uint8_t *color_seen,
                             const drop_lut_t *lut,
                             uint8_t *raw_band, size_t raw_band_size) {
  memset(raw_band, 0, raw_band_size);
  int has = 0, last_v = -1;
  for (int h = 0; h < pa->count; h++) {
    int v = pa->vnozzles[h];
    if (v >= color_noz) continue;
    int row = pa->rows[h];
    int k = color_seen[row]++;
    const uint8_t *prow = &plans[(size_t)row * width_px];
    uint8_t *out = &raw_band[(size_t)v * total_bpl + margin_bytes];
    for (int x = 0; x < width_px; x++) {
      int p = prow[x];
      if (!p) continue;
      int idx = (k + hit_rotation(x, row)) % hits;
      int code = lut->code[p][idx];
      if (!code) continue;
      out[x >> 2] |= (uint8_t)(code << (6 - (x & 3) * 2));
      has = 1;
      if (last_v < 0) last_v = v;
    }
  }
  if (has && margin_bytes > 0 && last_v >= 0)   /* logic-seeking defeat (UV) */
    raw_band[(size_t)last_v * total_bpl] |= 0x01;
  return has;
}

/* Emit one built raw_band as a band (advance flush / CR / hreset / PackBits). */
static void emboss_emit(FILE *fp, uint8_t cc, const uint8_t *raw_band,
                        uint8_t *comp_buf, int total_bpl, size_t comp_cap,
                        int ch_idx, uint64_t ink_dots[MAX_CHANNELS][4],
                        int *pass_has_band, int *pending_advance,
                        size_t raw_band_size, int count_only) {
  if (ink_dots)
    for (size_t b = 0; b < raw_band_size; b++) {
      uint8_t by = raw_band[b];
      if (!by) continue;
      ink_dots[ch_idx][(by >> 6) & 3]++; ink_dots[ch_idx][(by >> 4) & 3]++;
      ink_dots[ch_idx][(by >> 2) & 3]++; ink_dots[ch_idx][(by) & 3]++;
    }
  if (count_only) { *pass_has_band = 1; return; }   /* tally only, no PRN */
  if (!*pass_has_band) {
    if (*pending_advance > 0) {
      prn_write_vadvance(fp, (uint32_t)*pending_advance);
      *pending_advance = 0;
    }
  } else {
    prn_write_cr(fp);
  }
  prn_write_hreset(fp);
  int comp_total = 0;
  for (int ln = 0; ln < NOZZLES_PER_HEAD; ln++) {
    int cl = packbits_compress(&raw_band[(size_t)ln * total_bpl], total_bpl,
                               &comp_buf[comp_total], (int)(comp_cap - comp_total));
    if (cl < 0) return;
    comp_total += cl;
  }
  prn_write_band(fp, cc, 1, BPP, total_bpl, NOZZLES_PER_HEAD, comp_buf,
                 (size_t)comp_total);
  *pass_has_band = 1;
}

int prn_generate_emboss(const char *output_path, const print_settings_t *settings,
                        uint8_t *color_dithered[MAX_CHANNELS],
                        const uint8_t *white_cap,
                        const uint8_t *white_plan, int underbase_hits,
                        int bytes_per_line,
                        int height_px, uint64_t ink_dots[MAX_CHANNELS][4],
                        bool count_only) {
  const emboss_preset_t *pr = settings->emboss_split
      ? emboss_lookup_split(settings->emboss_layers)
      : emboss_lookup(settings->emboss_layers);
  if (!pr) {
    fprintf(stderr, "Invalid emboss preset: %d\n", settings->emboss_layers);
    return -1;
  }
  const int spacing = settings->nozzle_spacing;
  const int color_noz = NOZZLES_PER_HEAD - pr->white_noz;
  const int width_px = settings->width_px;
  if (ink_dots) memset(ink_dots, 0, sizeof(uint64_t) * MAX_CHANNELS * 4);

  FILE *fp = NULL;
  if (!count_only) {
    fp = fopen(output_path, "wb");
    if (!fp) { perror("fopen"); return -1; }
    prn_write_header(fp, settings);
  }

  int margin_bytes = (int)(settings->left_margin_mm / 25.4f *
                           (settings->raster_dpi_h / (8.0f / BPP)) + 0.5f);
  int total_bpl = bytes_per_line + margin_bytes;
  size_t raw_band_size = (size_t)total_bpl * NOZZLES_PER_HEAD;
  size_t comp_cap = raw_band_size * 2 + 16;
  uint8_t *raw_band = malloc(raw_band_size);
  uint8_t *comp_buf = malloc(comp_cap);
  uint8_t *white_seen = calloc(height_px, 1);
  uint8_t *color_seen[MAX_CHANNELS] = {0};
  for (int i = 0; i < settings->num_channels; i++)
    if (!settings->channels[i].is_white)
      color_seen[i] = calloc(height_px, 1);

  /* Colour in emboss always runs at pr->color_hits deposits per row. */
  drop_tiers_t emboss_tiers = drop_tiers(settings->drop_mode);
  drop_lut_t emboss_lut;
  drop_build_lut(&emboss_lut, &emboss_tiers, pr->color_hits);
  /* The underbase runs at its own (smaller) deposit count, so it needs its own
   * ladder — the colour one is built for pr->color_hits. */
  drop_lut_t underbase_lut;
  drop_build_lut(&underbase_lut, &emboss_tiers,
                 underbase_hits > 0 ? underbase_hits : 1);

  int total_passes = 0, *advances = NULL;
  pass_assign_t *weave = build_emboss_weave_table(
      height_px, spacing, color_noz, pr->adv_a, pr->adv_b,
      pr->white_hits, pr->color_hits, &total_passes, &advances);
  if (!raw_band || !comp_buf || !white_seen || !weave) {
    fprintf(stderr, "Out of memory (emboss)\n");
    free(raw_band); free(comp_buf); free(white_seen); free(weave); free(advances);
    for (int i = 0; i < MAX_CHANNELS; i++) free(color_seen[i]);
    if (fp) fclose(fp);
    return -1;
  }

  printf("Emboss %d-layer: white %d-noz/%d-hit, colour %d-noz/%d-hit, "
         "advance %d/%d → %d passes\n", pr->layers, pr->white_noz,
         pr->white_hits, color_noz, pr->color_hits, pr->adv_a, pr->adv_b,
         total_passes);

  if (!count_only)
    prn_write_vadvance(fp, settings->initial_v);
  int pending_advance = 0;

  int prog_step = total_passes > 50 ? total_passes / 50 : 1;
  int prog_span = settings->prog_span ? settings->prog_span : 100;
  for (int pass = 0; pass < total_passes; pass++) {
    if (!count_only && pass % prog_step == 0) {
      printf("PROGRESS %d\n", settings->prog_base + pass * prog_span / total_passes);
      fflush(stdout);
    }
    int pass_has_band = 0;

    /* White relief (built once, emitted for every white slot). */
    if (emboss_white_band(&weave[pass], white_cap, white_plan, underbase_hits,
                          &underbase_lut, width_px, margin_bytes,
                          total_bpl, color_noz, white_seen, raw_band,
                          raw_band_size)) {
      for (int i = 0; i < settings->num_channels; i++)
        if (settings->channels[i].is_white)
          emboss_emit(fp, settings->channels[i].color_code, raw_band, comp_buf,
                      total_bpl, comp_cap, i, ink_dots, &pass_has_band,
                      &pending_advance, raw_band_size, count_only);
    }
    /* Colour on top. */
    for (int i = 0; i < settings->num_channels; i++) {
      if (settings->channels[i].is_white || !color_dithered[i]) continue;
      if (emboss_color_band(&weave[pass], color_dithered[i], width_px,
                            margin_bytes, total_bpl, color_noz, pr->color_hits,
                            color_seen[i], &emboss_lut, raw_band, raw_band_size))
        emboss_emit(fp, settings->channels[i].color_code, raw_band, comp_buf,
                    total_bpl, comp_cap, i, ink_dots, &pass_has_band,
                    &pending_advance, raw_band_size, count_only);
    }

    if (!count_only && pass_has_band) prn_write_cr(fp);
    if (pass < total_passes - 1) pending_advance += advances[pass];
  }

  if (!count_only) {
    prn_write_trailer(fp);
    fclose(fp);
    printf("Emboss PRN written: %s\n", output_path);
  }
  free(raw_band); free(comp_buf); free(white_seen); free(weave); free(advances);
  for (int i = 0; i < MAX_CHANNELS; i++) free(color_seen[i]);
  return 0;
}

int prn_generate(const char *output_path, const print_settings_t *settings,
                 uint8_t *dithered[MAX_CHANNELS],
                 uint8_t *dithered_z3[MAX_CHANNELS], int bytes_per_line,
                 int height_px,
                 uint64_t ink_dots[MAX_CHANNELS][4], bool count_only) {
  /* count_only: run the full weave and tally emitted dots (ink_dots) but skip
   * the header/PackBits/file writes — a fast ink estimate without a PRN. */
  if (ink_dots)
    memset(ink_dots, 0, sizeof(uint64_t) * MAX_CHANNELS * 4);

  FILE *fp = NULL;
  if (!count_only) {
    fp = fopen(output_path, "wb");
    if (!fp) {
      perror("fopen");
      return -1;
    }
    prn_write_header(fp, settings);
  }

  /* Calculate margin padding */
  int margin_bytes = (int)(settings->left_margin_mm / 25.4f * (settings->raster_dpi_h / (8.0f / BPP)) + 0.5f);
  int total_bytes_per_line = bytes_per_line + margin_bytes;

  /* Temp buffers for PackBits compression */
  size_t raw_band_size = (size_t)total_bytes_per_line * NOZZLES_PER_HEAD;
  size_t comp_cap = raw_band_size * 2 + 16;
  uint8_t *comp_buf = malloc(comp_cap);
  uint8_t *raw_band = malloc(raw_band_size);
  uint8_t *lead_dummy_comp = NULL;
  size_t lead_dummy_comp_len = 0;
  uint8_t lead_dummy_code = 0;

  if (!comp_buf || !raw_band) {
    fprintf(stderr, "Out of memory\n");
    if (fp) fclose(fp);
    free(comp_buf);
    free(raw_band);
    return -1;
  }

  /* Build ordered channel list based on white_first setting */
  int ch_order[MAX_CHANNELS];
  int ch_count = 0;

  if (settings->white_first) {
    for (int i = 0; i < settings->num_channels; i++)
      if (settings->channels[i].is_white)
        ch_order[ch_count++] = i;
    for (int i = 0; i < settings->num_channels; i++)
      if (!settings->channels[i].is_white)
        ch_order[ch_count++] = i;
  } else {
    for (int i = 0; i < settings->num_channels; i++)
      if (!settings->channels[i].is_white)
        ch_order[ch_count++] = i;
    for (int i = 0; i < settings->num_channels; i++)
      if (settings->channels[i].is_white)
        ch_order[ch_count++] = i;
  }

  if (!count_only && settings->lead_in_sweep_mode != LEAD_IN_SWEEP_OFF && ch_count > 0) {
    uint8_t *lead_dummy_raw = malloc(raw_band_size);
    lead_dummy_comp = malloc(comp_cap);
    if (!lead_dummy_raw || !lead_dummy_comp) {
      fprintf(stderr, "Out of memory (lead-in dummy sweep)\n");
      if (fp) fclose(fp);
      free(lead_dummy_raw);
      free(lead_dummy_comp);
      free(comp_buf);
      free(raw_band);
      return -1;
    }

    lead_dummy_code = settings->channels[ch_order[0]].color_code;
    for (int i = 0; i < ch_count; i++) {
      if (settings->channels[ch_order[i]].is_white) {
        lead_dummy_code = settings->channels[ch_order[i]].color_code;
        break;
      }
    }

    int dummy_len = 0;
    if (build_dummy_sweep_band(lead_dummy_raw, raw_band_size,
                               total_bytes_per_line,
                               settings->lead_in_sweep_mode,
                               lead_dummy_comp, comp_cap, &dummy_len) < 0) {
      fprintf(stderr, "Failed to build lead-in dummy sweep\n");
      if (fp) fclose(fp);
      free(lead_dummy_raw);
      free(lead_dummy_comp);
      free(comp_buf);
      free(raw_band);
      return -1;
    }
    lead_dummy_comp_len = (size_t)dummy_len;
    free(lead_dummy_raw);
  }

  /* ── Full 90-nozzle phase-based weave (AcroRIP architecture) ───
   * The head splits into zone_count_eff equal zones of n_virtual nozzles;
   * single-layer mode is the degenerate 1-zone case (full head, no split).
   * height_total = image height + trailing-zone overlap span.
   */
  int zone_count_eff = effective_zone_count(settings);
  int n_virtual = NOZZLES_PER_HEAD / zone_count_eff; /* 45, 30 or 90 */
  int zone_step = n_virtual * settings->nozzle_spacing; /* 45/30/90 × spacing */
  int height_total = height_px + (zone_count_eff - 1) * zone_step;
  int max_hits = (zone_count_eff == 1) ? 8 : 4;

  int total_passes = 0;
  int *pass_advances = NULL;

  pass_assign_t *weave_table =
      build_phase_weave_table(height_total, n_virtual, max_hits, zone_count_eff, settings, &total_passes, &pass_advances);

  if (!weave_table) {
    fprintf(stderr, "Out of memory (weave table)\n");
    if (fp) fclose(fp);
    free(lead_dummy_comp);
    free(comp_buf);
    free(raw_band);
    return -1;
  }

  /* Per-channel deposit counters. hits_seen_z3 is a parallel counter for the
   * trailing colour zone (zone 2) when a separate zone-3 image is active, so
   * image B deposits independently of image A. */
  int ch_hits[MAX_CHANNELS] = {0};
  uint8_t *hits_seen[MAX_CHANNELS] = {0};
  uint8_t *hits_seen_z3[MAX_CHANNELS] = {0};
  /* Plan → dot-code tables for the active drop mode, one per distinct hit
   * count (channels differ: 4 in dual-zone, 8 single-layer / shared zones). */
  drop_tiers_t tiers = drop_tiers(settings->drop_mode);
  drop_lut_t drop_luts[DROP_MAX_HITS + 1];
  for (int h = 1; h <= DROP_MAX_HITS; h++)
    drop_build_lut(&drop_luts[h], &tiers, h);
  for (int i = 0; i < settings->num_channels; i++) {
    ch_hits[i] = prn_channel_hits(settings, &settings->channels[i]);
    hits_seen[i] = calloc(height_px, 1);
    if (dithered_z3 && dithered_z3[i])
      hits_seen_z3[i] = calloc(height_px, 1);
    if (!hits_seen[i] || (dithered_z3 && dithered_z3[i] && !hits_seen_z3[i])) {
      fprintf(stderr, "Out of memory (hit counters)\n");
      for (int j = 0; j <= i; j++) { free(hits_seen[j]); free(hits_seen_z3[j]); }
      free(weave_table);
      free(pass_advances);
      free(lead_dummy_comp);
      if (fp) fclose(fp);
      free(comp_buf);
      free(raw_band);
      return -1;
    }
  }

  printf("%s (%d-nozzle): %d rows (%d span) → %d passes [structure]\n",
         settings->weave_mode == RES_WEAVE_FIXED ? "Fixed Feed" : "Phase Weave",
         n_virtual, height_px, height_total, total_passes);
  if (settings->reg_v != 0 && settings->print_mode == PRINT_BOTH)
    printf("Vertical registration: colour layer shifted %+d rows (1/1440\")\n",
           settings->reg_v);

  /* Initial head position (matches AcroRIP samples) */
  if (!count_only)
    prn_write_vadvance(fp, settings->initial_v);

  int result = 0;
  int bands_emitted = 0;
  int pending_advance = 0;
  int image_region_started = 0;

  int prog_step = total_passes > 50 ? total_passes / 50 : 1;
  int prog_span = settings->prog_span ? settings->prog_span : 100;
  for (int pass = 0; pass < total_passes; pass++) {
    if (!count_only && pass % prog_step == 0) {   /* ~2% steps for a progress bar */
      printf("PROGRESS %d\n", settings->prog_base + pass * prog_span / total_passes);
      fflush(stdout);
    }
    int pass_has_any_band = 0;
    int pass_touches_image = 0;

    for (int ci = 0; ci < ch_count; ci++) {
      int ch_idx = ch_order[ci];
      const channel_map_t *ch = &settings->channels[ch_idx];

      unsigned zone_mask = channel_zone_mask(ch, settings);

      /* Vertical registration offset only nudges the COLOUR layer in both-mode
       * (white is the reference); single-layer / white channels: 0. */
      int reg_v = (settings->print_mode == PRINT_BOTH && !ch->is_white)
                      ? settings->reg_v : 0;

      if (!pass_touches_image &&
          pass_touches_image_region(&weave_table[pass], height_px, zone_mask,
                                    zone_count_eff, zone_step, reg_v)) {
        pass_touches_image = 1;
      }

      uint8_t *z3_src = dithered_z3 ? dithered_z3[ch_idx] : NULL;
      uint8_t nozzle_has_data[NOZZLES_PER_HEAD];
      int has_data = build_pass_band_structure(&weave_table[pass],
                      dithered[ch_idx],
                      settings->width_px, margin_bytes, total_bytes_per_line,
                      height_px, zone_mask, zone_count_eff,
                      n_virtual, zone_step, reg_v,
                      ch_hits[ch_idx], hits_seen[ch_idx],
                      z3_src, hits_seen_z3[ch_idx],
                      &drop_luts[ch_hits[ch_idx]],
                      raw_band, raw_band_size, nozzle_has_data);

      /* Skip empty bands */
      if (!has_data)
        continue;

      /* Defeat Logic Seeking on the left by placing a microscopic dummy pixel
       * at x=0. Without this, the Epson firmware will skip the zero-byte
       * margin padding and the carriage won't sweep from home/cap area.
       * The dot MUST land on a nozzle within this channel's active zone!
       */
      if (margin_bytes > 0) {
          int dummy_placed = 0;
          for (int v = 0; v < NOZZLES_PER_HEAD; v++) {
              if (nozzle_has_data[v]) {
                  raw_band[v * total_bytes_per_line] |= 0x01;
                  dummy_placed = 1;
                  break;
              }
          }

          if (!dummy_placed) {
              /* Top nozzle of the channel's leading-most active zone */
              int z_lead = 0;
              while (z_lead < zone_count_eff && !((zone_mask >> z_lead) & 1u))
                  z_lead++;
              int dummy_nozzle = (zone_count_eff - z_lead) * n_virtual - 1;
              raw_band[dummy_nozzle * total_bytes_per_line] |= 0x01;
          }
      }

      /* Tally emitted dots per channel (both modes): 4 × 2bpp codes/byte,
       * code 1/2/3 = small/medium/large. Multi-hit & zones counted naturally
       * (each hit is a separate band). Zero bytes skipped for speed. */
      if (ink_dots) {
        for (size_t b = 0; b < raw_band_size; b++) {
          uint8_t by = raw_band[b];
          if (!by) continue;
          ink_dots[ch_idx][(by >> 6) & 3]++;
          ink_dots[ch_idx][(by >> 4) & 3]++;
          ink_dots[ch_idx][(by >> 2) & 3]++;
          ink_dots[ch_idx][(by) & 3]++;
        }
      }

      if (!count_only) {
        /* Flush accumulated advance before the first band of this pass */
        if (!pass_has_any_band && pending_advance > 0) {
          if (bands_emitted == 0)
            prn_write_lead_in_vadvance(
                fp, (uint32_t)pending_advance, settings,
                settings->lead_in_sweep_mode != LEAD_IN_SWEEP_OFF &&
                    lead_dummy_comp != NULL,
                lead_dummy_code, (uint16_t)total_bytes_per_line,
                lead_dummy_comp, lead_dummy_comp_len);
          else
            prn_write_vadvance(fp, (uint32_t)pending_advance);
          pending_advance = 0;
        }

        if (pass_has_any_band) {
          prn_write_cr(fp);
        }
        prn_write_hreset(fp);

        /* Compress per-line (each nozzle line independently, like AcroRIP) */
        int comp_total = 0;
        for (int ln = 0; ln < NOZZLES_PER_HEAD; ln++) {
          int cl = packbits_compress(&raw_band[ln * total_bytes_per_line],
                                     total_bytes_per_line,
                                     &comp_buf[comp_total],
                                     comp_cap - comp_total);
          if (cl < 0) { result = -1; break; }
          comp_total += cl;
        }
        if (result < 0) break;

        prn_write_band(fp, ch->color_code, 1, BPP, total_bytes_per_line,
                       NOZZLES_PER_HEAD, comp_buf, (size_t)comp_total);
      }
      pass_has_any_band = 1;
      bands_emitted++;
    }
    if (result < 0)
      break;

    if (!count_only && pass_has_any_band) {
      prn_write_cr(fp);
    }

    if (pass_touches_image)
      image_region_started = 1;

    /* Phase-based advance (11 or 27 depending on pass index).  Passes before
     * the head reaches the canvas are only lead-in travel; once a pass touches
     * the image region, preserve later empty advances so transparent PNG/canvas
     * rows keep their physical height. */
    if (pass < total_passes - 1) {
      if (image_region_started)
        pending_advance += pass_advances[pass];
    }
  }

  /* ── UV tail curing passes ─────────────────────────────────
   * The UV lamp rides the carriage: once the last data band is sent the
   * carriage stops sweeping, so the tail of the image would never be
   * cured.  Keep the carriage sweeping while the paper advances an extra
   * uv_tail_mm: each tail pass carries a band with two microscopic dots
   * (x = 0 and the right edge) to defeat logic seeking and force a
   * full-width sweep.  The dots land on blank film past the design.
   */
  int tail_units = (settings->uv_tail_mm > 0)
                       ? (int)(settings->uv_tail_mm / 25.4f
                               * settings->raster_dpi_v + 0.5f)
                       : 0;
  int tail_speed = (settings->uv_tail_speed > 0) ? settings->uv_tail_speed : 1;

  if (!count_only && result == 0 && tail_units > 0) {
    memset(raw_band, 0, raw_band_size);
    /* Tail dots force a carriage sweep so the UV lamp cures the trailing ink.
     * By default they sit at the full raster edges (x=0 .. right edge); when
     * main.c has measured the trailing-row ink extent, fit them to that so the
     * sweep follows the content width (e.g. a shape that narrows at the end
     * cures with a narrow sweep instead of the whole canvas). */
    int lo_byte = 0, hi_byte = total_bytes_per_line - 1;
    if (settings->uv_tail_col_hi > settings->uv_tail_col_lo) {
      lo_byte = margin_bytes + settings->uv_tail_col_lo / 4;
      hi_byte = margin_bytes + settings->uv_tail_col_hi / 4;
      if (lo_byte < 0) lo_byte = 0;
      if (hi_byte > total_bytes_per_line - 1) hi_byte = total_bytes_per_line - 1;
      if (hi_byte < lo_byte) hi_byte = lo_byte;
    }
    /* A UV margin exists precisely so the carriage sweeps out past the ink and
     * drags the (offset) lamp across it — the same reason the in-image bands
     * plant a dummy at x=0.  Fitting the LEFT tail dot to the content would
     * park the sweep at the artwork edge and never enter the margin, so the
     * carriage stops short of the capping side and the leading edge of the
     * trailing ink never passes under the lamp.  Anchor it at x=0 whenever a
     * margin was asked for; the RIGHT dot still follows the content so a shape
     * that narrows at the end keeps its shorter, more focused sweep. */
    if (margin_bytes > 0)
      lo_byte = 0;
    raw_band[lo_byte] |= 0x01;   /* left dot: content-left (or x=0) */
    raw_band[hi_byte] |= 0x01;   /* right dot: content-right (or right edge) */

    int comp_total = 0;
    for (int ln = 0; ln < NOZZLES_PER_HEAD; ln++) {
      int cl = packbits_compress(&raw_band[ln * total_bytes_per_line],
                                 total_bytes_per_line,
                                 &comp_buf[comp_total], comp_cap - comp_total);
      if (cl < 0) { result = -1; break; }
      comp_total += cl;
    }

    /* Least-visible ink for the two tail dots: prefer a white channel */
    uint8_t tail_code = settings->channels[ch_order[0]].color_code;
    for (int i = 0; i < ch_count; i++) {
      if (settings->channels[ch_order[i]].is_white) {
        tail_code = settings->channels[ch_order[i]].color_code;
        break;
      }
    }

    if (result == 0) {
      int cum = 0; /* continue the advance pattern from the end of weave */
      for (int p = 0; p < total_passes; p++) cum += pass_advances[p];

      int traveled = 0, tail_passes = 0;
      while (traveled < tail_units) {
        int adv = phase_advance(cum, zone_count_eff, settings) * tail_speed;
        prn_write_vadvance(fp, (uint32_t)(adv + pending_advance));
        pending_advance = 0;
        prn_write_hreset(fp);
        prn_write_band(fp, tail_code, 1, BPP, total_bytes_per_line,
                       NOZZLES_PER_HEAD, comp_buf, (size_t)comp_total);
        prn_write_cr(fp);
        cum += adv; traveled += adv; tail_passes++;
      }
      printf("UV tail curing: %.1f mm → %d extra passes (speed %dx)\n",
             settings->uv_tail_mm, tail_passes, tail_speed);
    }
  }

  if (!count_only)
    prn_write_trailer(fp);

  if (fp) fclose(fp);
  free(lead_dummy_comp);
  free(comp_buf);
  free(raw_band);
  free(weave_table);
  free(pass_advances);
  for (int i = 0; i < MAX_CHANNELS; i++) {
    free(hits_seen[i]);
    free(hits_seen_z3[i]);
  }

  if (result == 0) {
    printf("Emitted %d bands across %d passes\n", bands_emitted, total_passes);
  }

  return result;
}

/* Emit one 90-line band for the nozzle check: per-line PackBits then ESC i.
 * Bands in the same pass are separated by CR + ESC($); `first` skips the CR. */
static int nc_emit_band(FILE *fp, uint8_t code, const uint8_t *raw, int bpl,
                        uint8_t *comp, int comp_cap_total, int first) {
  if (!first)
    prn_write_cr(fp);            /* same pass: CR + ESC($), no vertical advance */
  prn_write_hreset(fp);
  int ct = 0;
  for (int ln = 0; ln < NOZZLES_PER_HEAD; ln++) {
    int cl = packbits_compress(&raw[(size_t)ln * bpl], bpl,
                               &comp[ct], comp_cap_total - ct);
    if (cl < 0)
      return -1;
    ct += cl;
  }
  prn_write_band(fp, code, 1, BPP, (uint16_t)bpl, NOZZLES_PER_HEAD,
                 comp, (size_t)ct);
  return 0;
}

int prn_generate_nozzle_check(const char *output_path,
                              const print_settings_t *settings) {
  FILE *fp = fopen(output_path, "wb");
  if (!fp) { perror("fopen"); return -1; }

  /* Six physical nozzle rows (head slots), identified on the print by ink
   * colour: K · C · M · Y · LC(white1) · LM(white2), left→right. */
  static const uint8_t codes[6] = {
      CC_BLACK, CC_CYAN, CC_MAGENTA, CC_YELLOW, CC_LIGHT_CYAN, CC_LIGHT_MAG};

  /* Two stacked models, 6 colours side by side each, 90 nozzles = 1" tall.
   *  Model 1 — solid comb: every nozzle a FULL solid horizontal line. The block
   *            reads as a solid bar; a clog = a clear white streak across it.
   *  Model 2 — line staircase: every nozzle a SHORT line (SEG dots) starting
   *            where the previous nozzle's line ended (contiguous), wrapped
   *            every PER_GROUP into a new descending row — like a diagonal made
   *            of line segments. A clog = a gap in the staircase. */
  print_settings_t s = *settings;

  /* Left margin (‑M): shift the whole pattern right, same as a normal print. */
  int margin_bytes = (int)(s.left_margin_mm / 25.4f *
                           (s.raster_dpi_h / (8.0f / BPP)) + 0.5f);
  if (margin_bytes < 0) margin_bytes = 0;

  const int GAP_BYTES = 16;
  const int M1_COL    = 40;                       /* model-1 solid line length */
  const int bpl1      = margin_bytes + 6 * (M1_COL + GAP_BYTES) + 4;
  const int SEG       = 5;                        /* model-2 line = 5 dots */
  const int PER_GROUP = 15;                       /* nozzles per staircase row */
  const int M2_COL    = PER_GROUP * SEG;          /* model-2 column data width */
  const int bpl2      = margin_bytes + 6 * (M2_COL + GAP_BYTES) + 4;
  const int bplmax    = bpl2 > bpl1 ? bpl2 : bpl1;

  const int spacing   = s.nozzle_spacing;
  const int block_adv = NOZZLES_PER_HEAD * spacing + 30 * spacing; /* 1 band + gap */
  s.height_px  = 2 * block_adv + s.raster_dpi_v;  /* two models + 1" pad */
  s.uv_tail_mm = 0.0f;
  prn_write_header(fp, &s);

  /* Initial head position — the first ESC(v) loads paper into the print zone
   * (matches AcroRIP / prn_generate). Without it nothing prints. */
  prn_write_vadvance(fp, s.initial_v);

  const int comp_cap = (bplmax * 2 + 16) * NOZZLES_PER_HEAD;
  const size_t raw_size = (size_t)bplmax * NOZZLES_PER_HEAD;
  uint8_t *raw = calloc(raw_size, 1);
  uint8_t *comp = malloc(comp_cap);
  if (!raw || !comp) { free(raw); free(comp); if (fp) fclose(fp); return -1; }

  int result = 0;

  /* ── Model 1: solid comb (all 6 colours at one vertical position) ── */
  for (int k = 0; k < 6 && result == 0; k++) {
    memset(raw, 0, raw_size);
    int col = margin_bytes + k * (M1_COL + GAP_BYTES);
    for (int i = 0; i < NOZZLES_PER_HEAD; i++)
      for (int b = 0; b < M1_COL; b++)
        raw[(size_t)i * bpl1 + col + b] = 0xFF;   /* full solid line */
    result = nc_emit_band(fp, codes[k], raw, bpl1, comp, comp_cap, k == 0);
  }

  /* Advance to the second model. */
  if (result == 0) {
    prn_write_cr(fp);
    prn_write_vadvance(fp, (uint32_t)block_adv);
  }

  /* ── Model 2: line staircase (contiguous short segments, wrapped in rows) ── */
  for (int k = 0; k < 6 && result == 0; k++) {
    memset(raw, 0, raw_size);
    int col = margin_bytes + k * (M2_COL + GAP_BYTES);
    for (int i = 0; i < NOZZLES_PER_HEAD; i++) {
      int within = i % PER_GROUP;                 /* position along this row */
      int x = col + within * SEG;                 /* start = end of previous */
      for (int d = 0; d < SEG && x + d < bpl2; d++)
        raw[(size_t)i * bpl2 + x + d] = 0xFF;
    }
    result = nc_emit_band(fp, codes[k], raw, bpl2, comp, comp_cap, k == 0);
  }

  if (result == 0)
    prn_write_cr(fp);
  prn_write_trailer(fp);

  free(raw);
  free(comp);
  if (fp) fclose(fp);
  if (result == 0)
    printf("Nozzle check: 2 models (solid comb + line staircase), "
           "6 colours side-by-side, %d nozzles each\n", NOZZLES_PER_HEAD);
  return result;
}

/* ── Head cleaning ──────────────────────────────────────────────────────────
 *
 * Not a print job: no header, no raster, no form feed (a form feed here would
 * pull media through for nothing).  The whole file is the one service command
 * WIC Reset Utility sends, captured verbatim from its own log — the framing is
 * documented in docs/analisis_cleaning_wic.md:
 *
 *   1B 40   1B 7C 00 06 00   51 05 84 7B 42   <level>
 *   ESC @   ESC | cmd len=6  cleaning key     level byte
 *
 * Only captured levels are listed.  Other level bytes have been seen in WIC's
 * log (02, 0A, 0E, 11) but their meaning is unverified, and a wrong guess is
 * fired at real hardware at the cost of ink and waste-counter headroom — so
 * they stay out until someone captures them.
 */
static const clean_level_t clean_levels[] = {
    {"gentle", 0x00, 76},
    {"normal", 0x01, 91},
};

const clean_level_t *prn_clean_levels(int *count) {
  if (count)
    *count = (int)(sizeof(clean_levels) / sizeof(clean_levels[0]));
  return clean_levels;
}

const clean_level_t *prn_clean_level(const char *name) {
  int n = 0;
  const clean_level_t *t = prn_clean_levels(&n);
  for (int i = 0; i < n; i++)
    if (strcmp(t[i].name, name) == 0)
      return &t[i];
  return NULL;
}

int prn_generate_clean(const char *output_path, const clean_level_t *level) {
  if (!level)
    return -1;
  uint8_t cmd[] = {0x1b, 0x40,                          /* ESC @ reset      */
                   0x1b, 0x7c, 0x00, 0x06, 0x00,        /* ESC | cmd len=6  */
                   0x51, 0x05, 0x84, 0x7b, 0x42,        /* cleaning key     */
                   level->level};
  FILE *fp = fopen(output_path, "wb");
  if (!fp) { perror("fopen"); return -1; }
  size_t w = fwrite(cmd, 1, sizeof(cmd), fp);
  if (fclose(fp) != 0 || w != sizeof(cmd)) {
    fprintf(stderr, "Failed to write cleaning command\n");
    return -1;
  }
  return 0;
}
