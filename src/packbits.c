/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "packbits.h"
#include <string.h>

/*
 * PackBits compress: scan src for runs of identical bytes and literal
 * sequences.  Output format:
 *   n >= 0    : copy next (n+1) bytes literally
 *   -128 < n < 0 : repeat next byte (1-n) times
 *   n == -128 : no-op (skip)
 */
int packbits_compress(const uint8_t *src, int src_len,
                      uint8_t *dst, int dst_cap)
{
    int si = 0, di = 0;

    while (si < src_len) {
        /* Look for a run of identical bytes (min 3 to be worth it) */
        int run = 1;
        while (si + run < src_len && src[si + run] == src[si] && run < 128)
            run++;

        if (run >= 3) {
            /* Emit run-length */
            if (di + 2 > dst_cap) return -1;
            dst[di++] = (uint8_t)(1 - run);    /* e.g. run=3 → -2 → 0xFE */
            dst[di++] = src[si];
            si += run;
        } else {
            /* Emit literal sequence: collect non-run bytes */
            int lit_start = si;
            int lit_len = 0;

            while (si < src_len && lit_len < 128) {
                /* Check if a run starts here (3+ identical) */
                int ahead = 1;
                while (si + ahead < src_len &&
                       src[si + ahead] == src[si] && ahead < 3)
                    ahead++;

                if (ahead >= 3 && lit_len > 0)
                    break;  /* run starts, flush literal first */

                si++;
                lit_len++;
            }

            if (di + 1 + lit_len > dst_cap) return -1;
            dst[di++] = (uint8_t)(lit_len - 1);
            memcpy(&dst[di], &src[lit_start], lit_len);
            di += lit_len;
        }
    }

    return di;
}

/*
 * PackBits decompress.
 */
int packbits_decompress(const uint8_t *src, int src_len,
                        uint8_t *dst, int expected_len)
{
    int si = 0, di = 0;

    while (si < src_len && di < expected_len) {
        int8_t n = (int8_t)src[si++];

        if (n >= 0) {
            /* Literal: copy (n+1) bytes */
            int count = n + 1;
            if (si + count > src_len || di + count > expected_len)
                return -1;
            memcpy(&dst[di], &src[si], count);
            si += count;
            di += count;
        } else if (n > -128) {
            /* Run: repeat next byte (1-n) times */
            int count = 1 - n;
            if (si >= src_len || di + count > expected_len)
                return -1;
            memset(&dst[di], src[si++], count);
            di += count;
        }
        /* n == -128: no-op */
    }

    return di;
}
