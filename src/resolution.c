/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "resolution.h"
#include <string.h>

/* Header bytes, page units, and feed motion measured from AcroRIP exports in
 * samples/resolusion/. h_base differs from the label because the 2bpp dots
 * provide extra horizontal levels (e.g. "1440" prints on a 360-dpi data grid).
 * 720x360 uses the same 720-unit header/page grid as 720x720, with a coarser
 * fixed feed advance. */
static const resolution_t PRESETS[] = {
    /* name        h_base v_base  pV    pH    escE  escD[4]                   init top    width  mode             pass sep fixed */
    {"720x720",     720,   720,  0x08, 0x08, 0x21, {0x40, 0x38, 0xa0, 0x14},   45, -720,   9360, RES_WEAVE_FIXED,    0,  0,  45},
    {"720x360",     720,   720,  0x08, 0x08, 0x21, {0x40, 0x38, 0xa0, 0x14},  270, -720,   9360, RES_WEAVE_FIXED,    0,  0,  90},
    {"1440x720",    360,   720,  0x08, 0x04, 0x33, {0x40, 0x38, 0xa0, 0x28},   11, -720,   9360, RES_WEAVE_PHASE,   11, 19,   0},
    {"1440x1440",   360,  1440,  0x04, 0x04, 0x33, {0x40, 0x38, 0xa0, 0x28},   22, -1440, 18720, RES_WEAVE_PHASE,   11, 27,   0},
    {"2880x2880",   720,  2880,  0x02, 0x02, 0x25, {0x40, 0x38, 0xa0, 0x14},   33, -2880, 37440, RES_WEAVE_PHASE,   11, 43,   0},
};
#define N_PRESETS ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

const resolution_t *resolution_lookup(const char *name) {
  for (int i = 0; i < N_PRESETS; i++)
    if (strcmp(PRESETS[i].name, name) == 0)
      return &PRESETS[i];
  return NULL;
}

const char *resolution_names(void) {
  return "720x360, 720x720, 1440x720, 1440x1440, 2880x2880";
}
