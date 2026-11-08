/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef PRN_GEN_H
#define PRN_GEN_H

#include "dwrip.h"
#include <stdio.h>

/* Default dual-zone vertical nozzle split (data-level, like AcroRIP).
 * Zone B (leading edge) = nozzles 45-89: prints first on paper.
 * Zone A (trailing edge) = nozzles  0-44: prints same area later.
 * The shared weave table holds virtual nozzles only; channels place data
 * in their assigned zone's nozzle rows (see channel_zone_mask).
 * With zone_count == 3 the head splits into 3×30 nozzles with a 480-unit
 * step instead — these constants describe the 2-zone layout. */
#define NOZZLE_ZONE_B_START 45
#define ZONE_OFFSET (NOZZLE_ZONE_B_START * NOZZLE_SPACING_UNITS) /* 720 */

/*
 * PRN file generator for Epson L1800.
 *
 * Usage:
 *   FILE *fp = fopen("output.prn", "wb");
 *   prn_write_header(fp, &settings);
 *   // for each pass:
 *   prn_write_vadvance(fp, advance_units);
 *   prn_write_band(fp, &band);
 *   // ...
 *   prn_write_trailer(fp);
 *   fclose(fp);
 */

/* Write PRN header: EJL init, REMOTE1 config, ESC/P setup commands */
void prn_write_header(FILE *fp, const print_settings_t *settings);

/* Write vertical advance command: ESC(v) */
void prn_write_vadvance(FILE *fp, uint32_t units);

/* Write horizontal reset: ESC($) to position 0 */
void prn_write_hreset(FILE *fp);

/* Write a carriage return */
void prn_write_cr(FILE *fp);

/* Write one raster band: ESC i header + compressed data */
void prn_write_band(FILE *fp, uint8_t color_code, uint8_t compression,
                    uint8_t bpp, uint16_t bytes_per_line, uint16_t nlines,
                    const uint8_t *data, size_t data_len);

/* Write PRN trailer: CR, FF, REMOTE1 end job */
void prn_write_trailer(FILE *fp);

/*
 * Physical deposits per image row for this channel: weave hits (4 dual/3-zone,
 * 8 single-layer) × zones the channel fires in (3-zone colour fires in 2).
 * Used by --hit-split to size the volume scale (0..3×hits) and split codes.
 */
int prn_channel_hits(const print_settings_t *s, const channel_map_t *ch);

/*
 * High-level: generate complete PRN from dithered image channels.
 *
 * Default: dithered[i] is a 2-bit packed raster for channel i,
 * sized (bytes_per_line * height_px) bytes; bytes_per_line = ceil(width_px/4).
 * With settings->hit_split: dithered[i] is a per-pixel ink VOLUME plane
 * (width_px * height_px bytes, values 0..3×prn_channel_hits(ch)); the band
 * builder splits each pixel's volume across its weave hits.  bytes_per_line
 * keeps its packed-band meaning in both modes.
 */
/*
 * dithered_z3 (optional, may be NULL): parallel per-channel planes for the
 * trailing colour zone (zone 2) in -Z 3 both-mode when settings->z3_mode is
 * MIRROR/IMAGE. When non-NULL, zone 2 reads dithered_z3[i] (an independent
 * image, 4 hits) instead of the shared zone-0 source. z3_mode SAME → pass NULL
 * (zones 0 & 2 share dithered[i], the legacy 8-hit sandwich).
 */
/*
 * ink_dots (optional, may be NULL): out — tally of emitted 2bpp dots per channel
 * [ch][code] where code 1/2/3 = small/medium/large (index 0 unused). Multi-hit
 * and zones are counted naturally (each hit is a separate band). Used for ink /
 * cost estimation.
 * count_only: run the weave + tally but skip the header/PackBits/file writes
 * (output_path ignored) — a fast estimate with no PRN written.
 */
int prn_generate(const char *output_path,
                 const print_settings_t *settings,
                 uint8_t *dithered[MAX_CHANNELS],
                 uint8_t *dithered_z3[MAX_CHANNELS],
                 int bytes_per_line, int height_px,
                 uint64_t ink_dots[MAX_CHANNELS][4], bool count_only);

/* Emboss / spot 3D (Cara B): asymmetric-zone weave — solid white relief
 * (top nozzles, cured per-pass) + colour on top (bottom nozzles).
 * settings->emboss_layers picks the preset (8/16/32). color_dithered = colour
 * structure planes (4 hits); white_cap = per-pixel white hit count (w×h,
 * 0/underbase/spot). ink_dots optional. 1440×1440 only.
 * Returns 0 on success. emboss_is_valid_layers(N) validates the preset. */
/*
 * `white_cap` is the RELIEF: a per-pixel count of SOLID white hits, because
 * height is built by stacking whole layers — a half-covered layer is not half
 * as tall. `white_plan` is the UNDERBASE, the thin white under colour outside
 * the raised area, and that one carries no height, so it is an ordinary
 * dithered plane like every other ink: -L, --white-mode and -D all reach it.
 * `underbase_hits` is how many deposits its plans were built for (0 = none).
 * The two are mutually exclusive per pixel.
 */
int prn_generate_emboss(const char *output_path,
                        const print_settings_t *settings,
                        uint8_t *color_dithered[MAX_CHANNELS],
                        const uint8_t *white_cap,
                        const uint8_t *white_plan, int underbase_hits,
                        int bytes_per_line, int height_px,
                        uint64_t ink_dots[MAX_CHANNELS][4], bool count_only);
int emboss_is_valid_layers(int layers);        /* Cara B: 8/16/32 */
int emboss_is_valid_split_layers(int layers);  /* Cara A: 15/30/45/90 */

/*
 * Nozzle-check test print (like a printer's built-in nozzle check).
 * Bypasses the weave: emits one raster band per physical nozzle row
 * (6 colour codes), each a diagonal staircase where nozzle i fires a short
 * dash at x = i·step. A missing dash = a clogged nozzle. `settings` supplies
 * the resolution-derived header bytes; only the header fields are used.
 */
int prn_generate_nozzle_check(const char *output_path,
                              const print_settings_t *settings);

/*
 * Head cleaning — the printer's own maintenance routine, not a print job.
 *
 * The bytes are the ESC| service command captured from WIC Reset Utility's own
 * log; see docs/analisis_cleaning_wic.md for the framing and how to capture
 * more levels.  Only levels that have actually been observed on the wire are
 * listed: cleaning costs ink and waste-counter headroom, so an unknown level is
 * refused rather than guessed at.
 */
typedef struct {
  const char *name;  /* CLI/GUI name, e.g. "gentle" */
  uint8_t level;     /* last byte of the service command */
  int seconds;       /* measured duration, for the user-facing warning */
} clean_level_t;

/* Look up by name; NULL when the level has not been captured. */
const clean_level_t *prn_clean_level(const char *name);
/* The whole table (for --clean list and the GUI menu). */
const clean_level_t *prn_clean_levels(int *count);

/* Write the cleaning command — a complete, spoolable file of a few bytes.
 * No page setup and no form feed: this must not pull media. */
int prn_generate_clean(const char *output_path, const clean_level_t *level);

#endif /* PRN_GEN_H */
