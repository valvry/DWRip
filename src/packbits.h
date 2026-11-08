/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef PACKBITS_H
#define PACKBITS_H

#include <stdint.h>
#include <stdlib.h>

/*
 * TIFF PackBits compression (as used by Epson ESC/P raster).
 *
 * Compress:  returns compressed size, or -1 on error.
 *   dst must be at least src_len * 2 bytes (worst case).
 *
 * Decompress: returns decompressed size, or -1 on error.
 *   dst must be at least expected_len bytes.
 */

int packbits_compress(const uint8_t *src, int src_len,
                      uint8_t *dst, int dst_cap);

int packbits_decompress(const uint8_t *src, int src_len,
                        uint8_t *dst, int expected_len);

#endif /* PACKBITS_H */
