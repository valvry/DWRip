/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef CHANNEL_H
#define CHANNEL_H

#include "dwrip.h"

/*
 * Predefined channel orders for DTF printing.
 * Each function fills settings->channels[] and sets settings->num_channels.
 */

/* YKWWMC: Y=slot K, K=slot C, W=slot M&Y(LC,LM), M=slot LC→M?, C=slot LM→C?
 * Actually based on the sample analysis:
 *   Slot K (0x00)  → Yellow image
 *   Slot C (0x01)  → Cyan image (was Black in original, remapped)
 *   Slot M (0x02)  → Magenta image
 *   Slot Y (0x04)  → Yellow image ...
 *
 * The mapping from CLAUDE.md for YKWWMC:
 *   Physical slot K  (0x00) = ink Yellow
 *   Physical slot C  (0x01) = ink Black (K)
 *   Physical slot M  (0x02) = ink White 1
 *   Physical slot Y  (0x04) = ink White 2
 *   Physical slot LC (0x11) = ink Magenta
 *   Physical slot LM (0x12) = ink Cyan
 *
 * Wait - let me reconsider. From docs: YKWWMC means the cartridge slots
 * from left to right are: Y, K, W, W, M, C
 * L1800 slots: Black(0x00), Cyan(0x01), Magenta(0x02), Yellow(0x04), LC(0x11), LM(0x12)
 *
 * Mapping: slot_name(code) → ink_installed
 *   Black(0x00)  → Y ink  → receives image Yellow data
 *   Cyan(0x01)   → K ink  → receives image Black data
 *   Magenta(0x02)→ W ink  → receives image White data
 *   Yellow(0x04) → W ink  → receives image White data
 *   LC(0x11)     → M ink  → receives image Magenta data
 *   LM(0x12)     → C ink  → receives image Cyan data
 */
void channel_setup_YKWWMC(print_settings_t *settings);

/* WWYKMC:
 *   Black(0x00)  → W ink → White data
 *   Cyan(0x01)   → W ink → White data
 *   Magenta(0x02)→ Y ink → Yellow data
 *   Yellow(0x04) → K ink → Black data
 *   LC(0x11)     → M ink → Magenta data
 *   LM(0x12)     → C ink → Cyan data
 */
void channel_setup_WWYKMC(print_settings_t *settings);

/* CMYKWW (standard, no remap):
 *   Black(0x00)  → K ink → Black data (not used in DTF usually)
 *   Cyan(0x01)   → C ink → Cyan data
 *   Magenta(0x02)→ M ink → Magenta data
 *   Yellow(0x04) → Y ink → Yellow data
 *   LC(0x11)     → W ink → White data
 *   LM(0x12)     → W ink → White data
 */
void channel_setup_CMYKWW(print_settings_t *settings);

/* Setup from a string like "YKWWMC", "WWYKMC", "CMYKWW" */
int channel_setup_from_string(print_settings_t *settings, const char *order);

#endif /* CHANNEL_H */
