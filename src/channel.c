/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "channel.h"
#include <string.h>
#include <stdio.h>

/*
 * Physical nozzle-row assignment verified via per-code test print
 * (slotmap_XX.prn).  Mapping index-in-order-string → ESC/i code:
 *
 *   Index 0: 0x04  (first  head position)
 *   Index 1: 0x00  (second head position)
 *   Index 2: 0x12  (third  head position)
 *   Index 3: 0x11  (fourth head position)
 *   Index 4: 0x01  (fifth  head position)
 *   Index 5: 0x02  (sixth  head position)
 *
 * NOTE: index 0 and index 1 were swapped (0x00 ↔ 0x04) after a cross-printer
 * test print. The old table routed slot 1→0x00, slot 2→0x04, which came out
 * reversed physically. It stayed invisible while slots 1 & 2 both held White
 * (WWYKMC config: W↔W is a no-op); a printer with different inks in those two
 * slots exposed the swap.
 *
 * For order "WWYKMC" this routes (visually identical to before — both white):
 *   W→0x04, W→0x00, Y→0x12, K→0x11, M→0x01, C→0x02
 *
 * If the printer tank layout changes, re-run `./dwrip -X slotmap` and
 * update this table accordingly.
 */
static const uint8_t slot_codes[6] = {
    CC_YELLOW,     /* 0x04 */
    CC_BLACK,      /* 0x00 */
    CC_LIGHT_MAG,  /* 0x12 */
    CC_LIGHT_CYAN, /* 0x11 */
    CC_CYAN,       /* 0x01 */
    CC_MAGENTA     /* 0x02 */
};

/* Map a character to the image channel it represents */
static img_channel_t char_to_channel(char c)
{
    switch (c) {
    case 'C': return IMG_CYAN;
    case 'M': return IMG_MAGENTA;
    case 'Y': return IMG_YELLOW;
    case 'K': return IMG_BLACK;
    case 'W': return IMG_WHITE;
    default:  return IMG_CHANNELS; /* invalid */
    }
}

static bool char_is_white(char c)
{
    return c == 'W';
}

void channel_setup_YKWWMC(print_settings_t *settings)
{
    channel_setup_from_string(settings, "YKWWMC");
}

void channel_setup_WWYKMC(print_settings_t *settings)
{
    channel_setup_from_string(settings, "WWYKMC");
}

void channel_setup_CMYKWW(print_settings_t *settings)
{
    channel_setup_from_string(settings, "CMYKWW");
}

/*
 * Parse a 6-character channel order string.
 * Each character (C/M/Y/K/W) names the ink installed in a physical head slot;
 * char[i] is routed to the ESC/i color code slot_codes[i] (see table above):
 *   char[0] → 0x04   char[1] → 0x00   char[2] → 0x12
 *   char[3] → 0x11   char[4] → 0x01   char[5] → 0x02
 *
 * So "YKWWMC" means: slot 1 holds Y → its Yellow image data is sent to code
 * 0x04; slot 2 holds K → Black data to code 0x00; and so on down the slots.
 */
int channel_setup_from_string(print_settings_t *settings, const char *order)
{
    if (strlen(order) != 6) {
        fprintf(stderr, "Channel order must be exactly 6 characters (e.g. YKWWMC)\n");
        return -1;
    }

    settings->num_channels = 6;
    int white_num = 1;  /* max 6 slots, so max single digit */

    for (int i = 0; i < 6; i++) {
        char c = order[i];
        img_channel_t ch = char_to_channel(c);
        if (ch == IMG_CHANNELS) {
            fprintf(stderr, "Invalid channel character '%c' at position %d\n", c, i);
            return -1;
        }

        settings->channels[i].color_code = slot_codes[i];
        settings->channels[i].is_white = char_is_white(c);
        settings->channels[i].src_channel = ch;

        /* Generate human-readable name */
        static char names[6][4];
        if (char_is_white(c)) {
            names[i][0] = 'W';
            names[i][1] = '0' + (white_num++ % 10);
            names[i][2] = '\0';
        } else {
            names[i][0] = c;
            names[i][1] = '\0';
        }
        settings->channels[i].name = names[i];
    }

    return 0;
}
