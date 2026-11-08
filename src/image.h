/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef IMAGE_H
#define IMAGE_H

#include "dwrip.h"
#include <lcms2.h>

/* Rendering intent + flags resolved from the --intent option. */
typedef struct {
  cmsUInt32Number intent;  /* INTENT_PERCEPTUAL, etc. */
  cmsUInt32Number flags;   /* e.g. cmsFLAGS_BLACKPOINTCOMPENSATION */
} icc_intent_t;

/*
 * Load a PNG image and separate into CMYK + White channels.
 *
 * Supports two input modes:
 *   1. RGBA PNG: RGB channels are converted to CMY using simple subtraction,
 *      K is derived, and alpha channel becomes the White channel.
 *   2. Separate files: load color PNG + white PNG separately.
 *
 * If icc_path is non-NULL, RGB→CMYK uses the printer profile via lcms with
 * the given rendering intent; otherwise the built-in UCR formula is used.
 *
 * Returns 0 on success, -1 on error.
 * Caller must free each channel with free().
 */
int image_load_rgba(const char *path, const char *icc_path,
                    icc_intent_t intent, image_t *img);

/*
 * Load a separate white channel PNG (grayscale or uses first channel).
 * Must be same dimensions as the color image.
 */
int image_load_white(const char *path, image_t *img);

/* Free all channel buffers in an image */
void image_free(image_t *img);

/*
 * Allocate `dst` as a horizontal (left↔right) mirror of `src`: same dimensions,
 * every channel row reversed. Used for the 3-zone mirrored zone-3 layer.
 * Returns 0 on success, -1 on allocation failure (dst left freed/empty).
 */
int image_flip_h_copy(const image_t *src, image_t *dst);

/*
 * Create a test pattern image (for development/testing).
 * Generates a gradient pattern in all channels.
 */
void image_create_test(image_t *img, int width, int height);

/*
 * Create a solid-fill test pattern (all channels = value).
 * Used for per-channel mapping verification — halftone will produce
 * a uniform dot density, making the emitted ink color easy to identify.
 */
void image_create_solid(image_t *img, int width, int height, uint8_t value);

/*
 * Create a per-channel step wedge for visual density calibration.
 * 4 columns (C, M, Y, K) each ramped in 11 steps 0→100%, white channel 0.
 * Used to match DWRip's physical ink density to the conditions a borrowed
 * ICC profile was built under (compare by eye, no instrument needed).
 */
void image_create_wedge(image_t *img, int width, int height);

/*
 * Create a white↔colour vertical registration calibration target.
 * A vertical stack of marks: each has a thick WHITE reference bar and a thin
 * BLACK (colour) line pre-shifted up by the mark's label L rows, labelled with
 * L (candidate --reg-v). When printed in both-mode 2-zone, the mark whose black
 * line sits centred on the white bar is perfectly registered → its label is the
 * --reg-v value to use. Both channels are written directly (ICC/UCR bypassed).
 */
void image_create_reg_target(image_t *img, int width, int height);

/*
 * Create a grey-balance calibration chart: 5 rows of grey levels
 * (20/35/50/65/80%), each with a K-only patch next to a C=M=Y composite
 * patch. Printed colour-only; the user tunes the C/M/Y correction curves
 * until the CMY grey shows no colour cast next to the K grey.
 * Channels written directly (ICC/UCR bypassed).
 */
void image_create_gray_chart(image_t *img, int width, int height);

/*
 * Resize all channels to new dimensions using bilinear interpolation.
 * Updates img->width and img->height on success.  Returns 0 on success.
 *
 * Used to scale source image to the asymmetric raster dimensions required
 * by the L1800 (horizontal 360 dpi × vertical 1440 dpi).
 */
int image_resize(image_t *img, int new_width, int new_height);

/*
 * Write one channel plane as an 8-bit grayscale PNG (255 = full ink
 * coverage, matching the in-memory convention). Used by --dump-cmyk to
 * export the separation for inspection/comparison.  Returns 0 on success.
 */
int image_write_gray(const char *path, const uint8_t *plane,
                     int width, int height);

#endif /* IMAGE_H */
