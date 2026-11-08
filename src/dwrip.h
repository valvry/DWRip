/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef DWRIP_H
#define DWRIP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* ── Printer constants (Epson L1800) ─────────────────────── */
#define NOZZLES_PER_HEAD 90
#define NOZZLE_SPACING_UNITS                                                   \
  16 /* 1/90 inch = 16 × 1/1440 inch between nozzles (measured: 25mm / 89     \
        gaps) */
#define NONWEAVE_ADVANCE                                                       \
  (NOZZLES_PER_HEAD * NOZZLE_SPACING_UNITS) /* 720 units */
#define RASTER_DPI_H 360  /* PRN raster stored at 360 dpi horizontal */
#define RASTER_DPI_V 1440 /* Raster stored at 1440 dpi vertical (microweave)   \
                           */
#define DPI_H 1440        /* Effective print resolution (via microweave) */
#define DPI_V 1440
#define WEAVE_PASS_ADVANCE 11 /* ESC(v) per normal pass */
#define WEAVE_INITIAL_V 22    /* first ESC(v) before first pass */

/* AcroRIP Phase Engine Staggering.
 * A phase window is 63 normal passes (advance 11) + 1 separator pass
 * (advance 27): 63*11 + 27 = 720 = 45 nozzles × 16 units.  The separator
 * fires on the pass whose advance lands the carriage on a multiple of
 * PHASE_ROW_DISTANCE, counting the initial WEAVE_INITIAL_V advance. */
#define PHASE_SEP_ADVANCE 27 /* Special advance separating phase windows */
#define PHASE_ROW_DISTANCE                                                     \
  720 /* Total rows (1/1440") advanced per phase (63*11 + 27 = 720) */
#define MAX_CHANNELS 6
#define BPP 2        /* 2 bits per pixel = 4 dot levels */
#define DOT_LEVELS 4 /* 0=none, 1=small, 2=medium, 3=large */

/* ── ESC/P color codes (physical nozzle rows) ────────────── */
#define CC_BLACK 0x00
#define CC_CYAN 0x01
#define CC_MAGENTA 0x02
#define CC_YELLOW 0x04
#define CC_LIGHT_CYAN 0x11
#define CC_LIGHT_MAG 0x12

/* ── Image color channels (logical) ──────────────────────── */
typedef enum {
  IMG_CYAN = 0,
  IMG_MAGENTA,
  IMG_YELLOW,
  IMG_BLACK,
  IMG_WHITE,   /* single white channel, duplicated to W1+W2 */
  IMG_CHANNELS /* = 5 */
} img_channel_t;

/* ── How the white base is generated ─────────────────────────
 * ALPHA is what the engine has always done: white = the image's alpha, so an
 * opaque artwork gets one flat level and a soft edge fades out with it.
 * LUMINANCE keeps that and scales it by the brightness of the colour on top —
 * see apply_white_luminance(). ALPHA is 0 so the default stays the old path. */
typedef enum {
  WHITE_MODE_ALPHA = 0,
  WHITE_MODE_LUMINANCE
} white_mode_t;

/* ── Channel map entry ───────────────────────────────────── */
typedef struct {
  uint8_t color_code;        /* ESC/P color code (physical nozzle) */
  const char *name;          /* human-readable name */
  bool is_white;             /* true for white channels */
  img_channel_t src_channel; /* which image channel feeds this slot */
} channel_map_t;

/*
 * -- Drop mode: which dot sizes the structure ladder may stack --
 *
 * The mode names the TIERS available, smallest first; the halftone fills all
 * `hits` deposits at the lowest tier, then upgrades them one by one to the
 * next tier (exactly the build-up measured in AcroRIP's own PRNs, see
 * §dropsize in CLAUDE.md).  Tone stays linear in physical volume, so the
 * *ceiling* is what a mode changes: modes topping out at large reach the same
 * max density, medium-topped modes cap at ~42%, small-only at ~13%.
 *
 * Numeric values 0..2 are frozen (SML/ML/M shipped first); new modes append.
 */
typedef enum {
  DROP_SML = 0, /* small → medium → large  (finest highlights)     */
  DROP_ML,      /* medium → large          (default, = AcroRIP ML) */
  DROP_M,       /* medium only             (caps at ~42% density)  */
  DROP_S,       /* small only              (caps at ~13%)          */
  DROP_L,       /* large only              (coarsest, best UV cure)*/
  DROP_SM       /* small → medium          (caps at ~42%)          */
} drop_mode_t;

/* -- --structure dither dispersion method -- */
typedef enum {
  SD_JITTER = 0,  /* FS + hash threshold jitter (default; breaks period-2) */
  SD_FS,          /* plain Floyd-Steinberg (regular texture on flats) */
  SD_BLUENOISE,   /* void-and-cluster blue-noise ordered rounding */
  SD_BLUEJITTER,  /* FS + blue-noise threshold jitter (smooth, no ordered grid) */
  SD_SERPENTINE,  /* serpentine (boustrophedon) FS — breaks worms, mask-free */
  SD_SERPRANDOM,  /* serpentine + randomized diffusion weights (blue-noise ED,
                     Ostromoukhov/Ulichney family; mask-free → no grid) */
  SD_SERPJARVIS,       /* serpentine + Jarvis-Judice-Ninke 12-tap kernel:
                          wider error spread → lower low-freq noise, mask-free */
  SD_SERPJARVISRANDOM, /* SD_SERPJARVIS + randomized per-tap weights */
  SD_BLUENOISEHQ  /* ordered blue-noise, properly tuned: bigger anisotropic
                     mask (physical-distance VAC energy) + per-channel offset */
} struct_dither_t;

/* -- Single Layer Print Modes -- */
typedef enum {
  PRINT_BOTH = 0, /* Normal White + Color split zone */
  PRINT_COLOR,    /* Single layer Color using 90 nozzles */
  PRINT_WHITE     /* Single layer White using 90 nozzles */
} print_mode_t;

/*
 * -- Single-layer weave: how many carriage sweeps pass over each row --
 *
 * The UV lamp rides on the carriage, so the cure dose a row receives is
 * proportional to how many times the carriage sweeps over it.  Single-layer
 * printing has always DOUBLED the advance (22/54 instead of 11/27) to run 2x
 * faster; the hidden cost is that each row then sees only half the sweeps, so
 * it gets roughly half the UV dose.  Thin/soft tints suffer most — a very thin
 * film has a large surface-to-volume ratio, so oxygen inhibition leaves a
 * bigger fraction of it tacky.  See docs/known_issues.md §2.
 *
 * Every level above FAST borrows a zone-split geometry and fires only the one
 * active layer's zone, so the ink per row never changes — just the sweeps.
 * Ignored in both-mode, which already runs the dense pattern.
 *
 * The levels below trade print time for cure dose one-for-one: sweeps per row
 * = 2880 / (virtual nozzles), so halving the nozzles doubles both the dose and
 * the pass count.  Ink per row is unchanged at every level (always 4 deposits;
 * FAST's 8 deposits land on half the rows, which is the same ink per area).
 *
 *   level  virtual nozzles  advance   sweeps/720u   dose    time
 *   FAST        90           22/54        32        1x      1x   (720 dpi V)
 *   DENSE       45           11/27        64        2x      2x
 *   ULTRA       30            7/8         96        3x      3x
 *   MAX         18            4/5        160        5x      5x
 *
 * DENSE/ULTRA reuse the weave geometry already proven by 2-zone and 3-zone
 * both-mode printing; MAX's 4/5 pattern was verified uniform by simulation and
 * is re-checked at runtime by build_phase_weave_table's hit-uniformity guard.
 * ULTRA/MAX are tuned for 1440 vertical (nozzle spacing 16), like -Z 3.
 */
typedef enum {
  SINGLE_WEAVE_FAST = 0, /* 90 nozzles, advance 22/54 — fastest, ~50% UV dose  */
  SINGLE_WEAVE_DENSE,    /* 45 nozzles, advance 11/27 — 2x dose, 2x slower     */
  SINGLE_WEAVE_ULTRA,    /* 30 nozzles, advance 7/8   — 3x dose, 3x slower     */
  SINGLE_WEAVE_MAX       /* 18 nozzles, advance 4/5   — 5x dose, 5x slower     */
} single_weave_t;

typedef enum {
  RES_WEAVE_PHASE = 0, /* AcroRIP phase weave: normal advance + separator */
  RES_WEAVE_FIXED      /* Fixed feed advance per pass */
} resolution_weave_mode_t;

typedef enum {
  LEAD_IN_SWEEP_OFF = 0,
  LEAD_IN_SWEEP_SHORT,
  LEAD_IN_SWEEP_FULL
} lead_in_sweep_mode_t;

typedef enum {
  Z3_SAME = 0, /* zone-3 = duplicate of zone-1 colour (default, byte-identical) */
  Z3_MIRROR,   /* zone-3 = horizontal mirror of zone-1 colour */
  Z3_IMAGE     /* zone-3 = a separate image */
} z3_mode_t;

/* ── Print job settings ──────────────────────────────────── */
typedef struct {
  int width_px;          /* image width in pixels */
  int height_px;         /* image height in pixels */
  int dpi;               /* target DPI (1440) */
  bool white_first;      /* true=white before color each pass */
  bool bidirectional;    /* true=bidi, false=unidi */
  float scale;           /* image scaling factor */
  float color_limit;     /* ink limit 0.0-1.0 for CMYK */
  float white_limit;     /* ink limit 0.0-1.0 for white */
  float left_margin_mm;  /* left margin in millimeters for UV padding */
  /* -D: parsed and stored, but RESERVED — no consumer yet. The structure
     ladder is medium+large by construction. Kept for a planned option to cap
     or extend the ladder's drop sizes (see CLAUDE.md §structmode). */
  drop_mode_t drop_mode;
  /* dithered[] always holds deposit PLANS from halftone_fs_structure()
     (medium-stack ladder); the structured dither is the only halftone path. */
  struct_dither_t struct_dither; /* --struct-dither: dispersion method */
  print_mode_t print_mode; /* Type of split/single layer print */
  single_weave_t single_weave; /* -m color/white only: sweep density (UV dose) */
  int zone_count;        /* head split zones: 2 (default) or 3 (C/W/C) */
  z3_mode_t z3_mode;     /* -Z 3 only: zone-3 colour source (same/mirror/image).
                            Non-SAME → zones 0 & 2 print independent images at
                            4 hits each (two-sided transparent print) */
  int emboss_layers;     /* --emboss: 0 = off, else 8/16/32 = white relief
                            preset (Cara B asymmetric-zone weave, 1440 only) */
  bool underbase;        /* --underbase: thin 4-hit white under colour in
                            non-spot areas (emboss mode) */
  bool emboss_grayscale; /* --emboss-height grayscale: map spot mask level →
                            per-pixel height (0..N layers) instead of on/off */
  bool emboss_split;     /* --emboss-mode split (Cara A): white relief in its own
                            run(s) using all 90 nozzles, colour in a separate
                            full-quality run — printed sequentially */
  int emboss_white_runs; /* --emboss-white-runs K: stack K white runs (>90 lyr) */
  int emboss_color_reg;  /* --emboss-color-reg: feed offset (1/1440") of the
                            colour run to register onto the relief */
  float uv_tail_mm;      /* extra curing travel after image end (carriage-
                            mounted UV lamp keeps sweeping); 0 = off */
  int uv_tail_speed;     /* advance multiplier for tail passes (1 = same
                            UV dose as the image interior) */
  int reg_v;             /* --reg-v: vertical registration offset (rows,
                            1/1440") of the COLOUR layer vs white in 2/3-zone
                            both-mode; compensates feed error, 0 = off */
  int lead_in_feed_step;  /* --lead-in-feed-step: max ESC(v) unit during
                            transparent lead-in; 0 = single normal advance */
  lead_in_sweep_mode_t lead_in_sweep_mode; /* optional dummy sweep during
                                              transparent lead-in */
  /* ── Resolution (runtime weave/raster; default = 1440x1440) ── */
  int raster_dpi_h;      /* horizontal data grid (h_base): image scale + bpl */
  int raster_dpi_v;      /* vertical data grid (v_base): rows + weave unit */
  int nozzle_spacing;    /* v_base / 90 (16 @1440, 8 @720, 32 @2880) */
  resolution_weave_mode_t weave_mode; /* resolution-specific feed model */
  int pass_advance;      /* normal ESC(v) advance (11) */
  int sep_advance;       /* phase separator advance (spacing + 11) */
  int fixed_advance;     /* RES_WEAVE_FIXED: ESC(v) advance */
  int phase_distance;    /* 45 × nozzle_spacing (dual-zone offset) */
  int initial_v;         /* first ESC(v) before pass 0 */
  int page_top_units;    /* ESC(c) top margin, signed */
  int paper_width_units; /* ESC(S) paper width */
  uint8_t escU_pV, escU_pH; /* ESC(U) unit divisors (header) */
  uint8_t escE;          /* ESC(e) dot-size byte (header) */
  uint8_t escD[4];       /* ESC(D) raster-resolution payload (header) */
  int num_channels;      /* number of active channel maps */
  channel_map_t channels[MAX_CHANNELS];
  /* ── GUI progress: pass-loop PROGRESS is emitted as prog_base + pass*span.
   * Lets main.c reserve an early slice of the bar for the pre-pass dither.
   * span == 0 is treated as 100 → callers that leave these zero get 0..100. */
  int prog_base;
  int prog_span;
  /* ── UV tail sweep width: content-fitted dot columns (source pixels within
   * the printed width). When uv_tail_col_hi > uv_tail_col_lo the tail dots
   * track the ink extent of the trailing rows instead of the full canvas
   * edges; {0,0} (default) = legacy full-width (x=0 .. right edge). */
  int uv_tail_col_lo;
  int uv_tail_col_hi;
} print_settings_t;

/* ── Raster band (one color, one pass) ───────────────────── */
typedef struct {
  uint8_t color_code;
  int bytes_per_line;  /* uncompressed */
  int nlines;          /* typically 90 */
  uint8_t *compressed; /* PackBits compressed data */
  size_t compressed_len;
} raster_band_t;

/* ── Image buffer ────────────────────────────────────────── */
typedef struct {
  int width;
  int height;
  float dpi_x;           /* from PNG pHYs chunk, 0 if unknown */
  float dpi_y;
  /* 8-bit per channel per pixel, [channel][y * width + x] */
  uint8_t *channel[IMG_CHANNELS];
} image_t;

#endif /* DWRIP_H */
