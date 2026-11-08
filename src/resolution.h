/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef RESOLUTION_H
#define RESOLUTION_H

#include <stdint.h>
#include "dwrip.h"

/* A print-resolution preset. Header bytes, page units, and feed motion are
 * emitted per preset (values reverse-engineered from AcroRIP samples in
 * samples/resolusion/).
 *
 *   h_base = horizontal data grid dpi  → image scale + bytes/line
 *   v_base = vertical data grid dpi    → nozzle_spacing = v_base/90, raster rows
 *   weave_mode + advance fields define the per-pass feed model
 */
typedef struct {
  const char *name; /* e.g. "1440x1440" */
  int h_base;       /* horizontal raster dpi (scaling + bytes/line) */
  int v_base;       /* vertical raster dpi (weave unit + row count) */
  uint8_t escU_pV;  /* ESC(U) vertical-unit divisor (5760/v_label) */
  uint8_t escU_pH;  /* ESC(U) horizontal-unit divisor (5760/h_label) */
  uint8_t escE;     /* ESC(e) dot-size second byte */
  uint8_t escD[4];  /* ESC(D) raster-resolution payload */
  int initial_v;    /* first ESC(v), from AcroRIP samples */
  int page_top_units;    /* ESC(c) top margin */
  int paper_width_units; /* ESC(S) paper width */
  resolution_weave_mode_t weave_mode;
  int pass_advance;  /* RES_WEAVE_PHASE normal advance */
  int sep_advance;   /* RES_WEAVE_PHASE separator advance */
  int fixed_advance; /* RES_WEAVE_FIXED advance */
} resolution_t;

/* Look up a preset by exact name; NULL if unknown. */
const resolution_t *resolution_lookup(const char *name);

/* Comma-separated list of valid names (for --help / error messages). */
const char *resolution_names(void);

#endif /* RESOLUTION_H */
