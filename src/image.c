/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "image.h"
#include <png.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <lcms2.h>

static int alloc_channels(image_t *img)
{
    size_t npixels = (size_t)img->width * img->height;
    for (int i = 0; i < IMG_CHANNELS; i++) {
        img->channel[i] = calloc(npixels, 1);
        if (!img->channel[i]) {
            fprintf(stderr, "Out of memory allocating channel %d\n", i);
            image_free(img);
            return -1;
        }
    }
    return 0;
}

/*
 * Simple RGB → CMYK conversion using UCR (Under Color Removal).
 *
 * C = 1 - R - K
 * M = 1 - G - K
 * Y = 1 - B - K
 * K = min(1-R, 1-G, 1-B)
 *
 * Values are 0-255 where 255 = full ink.
 */
static void rgb_to_cmyk(uint8_t r, uint8_t g, uint8_t b,
                        uint8_t *c, uint8_t *m, uint8_t *y, uint8_t *k)
{
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    float kf = 1.0f - fmaxf(rf, fmaxf(gf, bf));
    if (kf >= 1.0f) {
        *c = *m = *y = 0;
        *k = 255;
        return;
    }

    float cf = (1.0f - rf - kf) / (1.0f - kf);
    float mf = (1.0f - gf - kf) / (1.0f - kf);
    float yf = (1.0f - bf - kf) / (1.0f - kf);

    *c = (uint8_t)(cf * 255.0f + 0.5f);
    *m = (uint8_t)(mf * 255.0f + 0.5f);
    *y = (uint8_t)(yf * 255.0f + 0.5f);
    *k = (uint8_t)(kf * 255.0f + 0.5f);
}

int image_load_rgba(const char *path, const char *icc_path,
                    icc_intent_t intent, image_t *img)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror(path);
        return -1;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                             NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return -1;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    img->width = png_get_image_width(png, info);
    img->height = png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);

    /* Physical dimensions from pHYs chunk (pixels per meter → DPI) */
    png_uint_32 res_x = 0, res_y = 0;
    int unit_type = PNG_RESOLUTION_UNKNOWN;
    if (png_get_pHYs(png, info, &res_x, &res_y, &unit_type)
        && unit_type == PNG_RESOLUTION_METER && res_x > 0 && res_y > 0) {
        img->dpi_x = (float)res_x * 0.0254f;
        img->dpi_y = (float)res_y * 0.0254f;
    } else {
        img->dpi_x = 0;
        img->dpi_y = 0;
    }

    /* Convert to 8-bit RGBA regardless of input format */
    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    /* Allocate channels */
    memset(img->channel, 0, sizeof(img->channel));
    if (alloc_channels(img) < 0) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    cmsHPROFILE hInProfile = NULL;
    cmsHPROFILE hOutProfile = NULL;
    cmsHTRANSFORM hTransform = NULL;

    if (icc_path) {
        hInProfile = cmsCreate_sRGBProfile();
        hOutProfile = cmsOpenProfileFromFile(icc_path, "r");
        if (!hOutProfile) {
            fprintf(stderr, "Failed to load ICC profile: %s\n", icc_path);
            image_free(img);
            png_destroy_read_struct(&png, &info, NULL);
            fclose(fp);
            if (hInProfile) cmsCloseProfile(hInProfile);
            return -1;
        }
        hTransform = cmsCreateTransform(hInProfile, TYPE_RGB_8,
                                        hOutProfile, TYPE_CMYK_8,
                                        intent.intent, intent.flags);
    }

    /* Read row by row */
    png_bytep row = malloc(png_get_rowbytes(png, info));
    /* Row-sized staging buffers so lcms transforms a whole scanline per
     * call instead of paying the per-call overhead for every pixel. */
    uint8_t *row_rgb = NULL, *row_cmyk = NULL;
    if (hTransform) {
        row_rgb = malloc((size_t)img->width * 3);
        row_cmyk = malloc((size_t)img->width * 4);
    }
    if (!row || (hTransform && (!row_rgb || !row_cmyk))) {
        free(row);
        free(row_rgb);
        free(row_cmyk);
        if (hTransform) cmsDeleteTransform(hTransform);
        if (hOutProfile) cmsCloseProfile(hOutProfile);
        if (hInProfile) cmsCloseProfile(hInProfile);
        image_free(img);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    for (int y = 0; y < img->height; y++) {
        png_read_row(png, row, NULL);

        if (hTransform) {
            for (int x = 0; x < img->width; x++) {
                row_rgb[x * 3 + 0] = row[x * 4 + 0];
                row_rgb[x * 3 + 1] = row[x * 4 + 1];
                row_rgb[x * 3 + 2] = row[x * 4 + 2];
            }
            cmsDoTransform(hTransform, row_rgb, row_cmyk, img->width);
        }

        for (int x = 0; x < img->width; x++) {
            uint8_t r = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t b = row[x * 4 + 2];
            uint8_t a = row[x * 4 + 3];

            uint8_t c, m, yy, k;
            if (hTransform) {
                c = row_cmyk[x * 4 + 0];
                m = row_cmyk[x * 4 + 1];
                yy = row_cmyk[x * 4 + 2];
                k = row_cmyk[x * 4 + 3];
            } else {
                rgb_to_cmyk(r, g, b, &c, &m, &yy, &k);
            }

            if (a == 0) {
                c = m = yy = k = 0;
            } else if (a < 255) {
                c = (c * a) / 255;
                m = (m * a) / 255;
                yy = (yy * a) / 255;
                k = (k * a) / 255;
            }

            int idx = y * img->width + x;
            img->channel[IMG_CYAN][idx] = c;
            img->channel[IMG_MAGENTA][idx] = m;
            img->channel[IMG_YELLOW][idx] = yy;
            img->channel[IMG_BLACK][idx] = k;
            img->channel[IMG_WHITE][idx] = a;   /* white proportional to alpha */
        }
    }

    if (hTransform) cmsDeleteTransform(hTransform);
    if (hOutProfile) cmsCloseProfile(hOutProfile);
    if (hInProfile) cmsCloseProfile(hInProfile);

    free(row_rgb);
    free(row_cmyk);
    free(row);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    printf("Loaded %s: %dx%d RGBA → CMYK+W\n", path, img->width, img->height);
    return 0;
}

int image_load_white(const char *path, image_t *img)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror(path);
        return -1;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                             NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    int w = png_get_image_width(png, info);
    int h = png_get_image_height(png, info);

    if (w != img->width || h != img->height) {
        fprintf(stderr, "White channel size %dx%d doesn't match image %dx%d\n",
                w, h, img->width, img->height);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    /* Convert to 8-bit grayscale */
    int bit_depth = png_get_bit_depth(png, info);
    int color_type = png_get_color_type(png, info);
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        png_set_rgb_to_gray(png, 1, 0.299, 0.587);
    if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA || color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        png_set_strip_alpha(png);
    png_read_update_info(png, info);

    png_bytep row = malloc(png_get_rowbytes(png, info));
    for (int y = 0; y < h; y++) {
        png_read_row(png, row, NULL);
        memcpy(&img->channel[IMG_WHITE][y * w], row, w);
    }

    free(row);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    printf("Loaded white channel from %s: %dx%d\n", path, w, h);
    return 0;
}

void image_free(image_t *img)
{
    for (int i = 0; i < IMG_CHANNELS; i++) {
        free(img->channel[i]);
        img->channel[i] = NULL;
    }
}

int image_flip_h_copy(const image_t *src, image_t *dst)
{
    dst->width = src->width;
    dst->height = src->height;
    dst->dpi_x = src->dpi_x;
    dst->dpi_y = src->dpi_y;
    for (int i = 0; i < IMG_CHANNELS; i++)
        dst->channel[i] = NULL;

    const int w = src->width, h = src->height;
    for (int p = 0; p < IMG_CHANNELS; p++) {
        if (!src->channel[p])
            continue;
        dst->channel[p] = malloc((size_t)w * h);
        if (!dst->channel[p]) {
            image_free(dst);
            return -1;
        }
        for (int y = 0; y < h; y++) {
            const uint8_t *srow = &src->channel[p][(size_t)y * w];
            uint8_t *drow = &dst->channel[p][(size_t)y * w];
            for (int x = 0; x < w; x++)
                drow[x] = srow[w - 1 - x];
        }
    }
    return 0;
}

void image_create_test(image_t *img, int width, int height)
{
    img->width = width;
    img->height = height;
    alloc_channels(img);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            /* Horizontal gradient for each channel */
            uint8_t val = (uint8_t)((x * 255) / (width - 1));
            img->channel[IMG_CYAN][idx] = val;
            img->channel[IMG_MAGENTA][idx] = val;
            img->channel[IMG_YELLOW][idx] = val;
            img->channel[IMG_BLACK][idx] = (uint8_t)((y * 255) / (height - 1));
            img->channel[IMG_WHITE][idx] = 200;  /* uniform white */
        }
    }
}

void image_create_solid(image_t *img, int width, int height, uint8_t value)
{
    img->width = width;
    img->height = height;
    alloc_channels(img);

    size_t n = (size_t)width * height;
    for (int i = 0; i < IMG_CHANNELS; i++) {
        memset(img->channel[i], value, n);
    }
}

void image_create_wedge(image_t *img, int width, int height)
{
    img->width = width;
    img->height = height;
    alloc_channels(img);

    /* 4 vertical column bands: C, M, Y, K. Within each column the value
     * ramps top→bottom in 11 steps (0,10,...,100%). White stays 0. */
    const img_channel_t cols[4] = { IMG_CYAN, IMG_MAGENTA, IMG_YELLOW, IMG_BLACK };
    for (int y = 0; y < height; y++) {
        int step = (y * 11) / height;          /* 0..10 */
        if (step > 10) step = 10;
        uint8_t val = (uint8_t)((step * 255) / 10);
        for (int x = 0; x < width; x++) {
            int col = (x * 4) / width;          /* 0..3 */
            if (col > 3) col = 3;
            int idx = y * width + x;
            img->channel[cols[col]][idx] = val;
        }
    }
}

/* Minimal 5×7 bitmap font: glyphs '0'..'9' then '-'. Each row = low 5 bits,
 * MSB = leftmost column. Used to label registration marks. */
static const uint8_t REG_FONT[11][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
};

/* Draw one glyph (index 0..10) at (x0,y0) into a channel, scaled by `s`. */
static void reg_draw_glyph(image_t *img, img_channel_t ch, int gi,
                           int x0, int y0, int s)
{
    if (gi < 0 || gi > 10) return;
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (REG_FONT[gi][r] & (1 << (4 - c)))
                for (int dy = 0; dy < s; dy++)
                    for (int dx = 0; dx < s; dx++) {
                        int x = x0 + c * s + dx, y = y0 + r * s + dy;
                        if (x >= 0 && x < img->width && y >= 0 && y < img->height)
                            img->channel[ch][(size_t)y * img->width + x] = 255;
                    }
}

/* Draw a signed integer label right-anchored so it ends at x_right. */
static void reg_draw_number(image_t *img, img_channel_t ch, int value,
                            int x_right, int y0, int s)
{
    int gw = 6 * s;                 /* glyph advance (5 px + 1 spacing) × s */
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d", value);
    for (int i = n - 1, col = 0; i >= 0; i--, col++) {
        int gi = (buf[i] == '-') ? 10 : (buf[i] - '0');
        reg_draw_glyph(img, ch, gi, x_right - (col + 1) * gw + s, y0, s);
    }
}

/* Fill a solid horizontal bar [x0,x1) × [y0,y1) in one channel. */
static void reg_fill_bar(image_t *img, img_channel_t ch,
                         int x0, int x1, int y0, int y1)
{
    if (y0 < 0) y0 = 0;
    if (y1 > img->height) y1 = img->height;
    if (x0 < 0) x0 = 0;
    if (x1 > img->width) x1 = img->width;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            img->channel[ch][(size_t)y * img->width + x] = 255;
}

void image_create_reg_target(image_t *img, int width, int height)
{
    img->width = width;
    img->height = height;
    alloc_channels(img);   /* calloc → all channels 0 (no ink) */

    /* Candidate --reg-v values, symmetric around 0. Label L draws the colour
     * line shifted UP by L rows; the physically-centred mark's label = reg-v. */
    const int REG_MIN = -48, REG_MAX = 48, REG_STEP = 4;
    const int count = (REG_MAX - REG_MIN) / REG_STEP + 1;   /* 25 */

    const int bar_x0 = width * 35 / 100;      /* white/colour bars start here */
    const int bar_x1 = width * 95 / 100;
    const int white_h = 26;                   /* thick white reference bar */
    const int color_h = 4;                    /* thin colour line */
    const int fs = 3;                         /* label font scale (15×21 px) */

    const int pitch = height / (count + 1);   /* vertical spacing per mark */

    for (int i = 0; i < count; i++) {
        int L  = REG_MIN + i * REG_STEP;
        int Yc = pitch * (i + 1);              /* white bar centre row */

        /* White reference bar (thick), centred at Yc. */
        reg_fill_bar(img, IMG_WHITE, bar_x0, bar_x1,
                     Yc - white_h / 2, Yc + white_h / 2);

        /* Colour line (thin), pre-shifted UP by L rows. */
        int Ycol = Yc - L;
        reg_fill_bar(img, IMG_BLACK, bar_x0, bar_x1,
                     Ycol - color_h / 2, Ycol + color_h / 2);

        /* Label L (colour), right-anchored just left of the bars, at Yc. */
        reg_draw_number(img, IMG_BLACK, L, bar_x0 - 12, Yc - (7 * fs) / 2, fs);

        /* Emphasise the L=0 reference: white tick marks flanking the bar. */
        if (L == 0) {
            reg_fill_bar(img, IMG_WHITE, bar_x0 - 30, bar_x0 - 8,
                         Yc - white_h / 2, Yc + white_h / 2);
            reg_fill_bar(img, IMG_WHITE, bar_x1 + 8, bar_x1 + 30,
                         Yc - white_h / 2, Yc + white_h / 2);
        }
    }
}

/* Fill a rectangle with one value in one channel (greyscale patch). */
static void gray_fill(image_t *img, img_channel_t ch, uint8_t v,
                      int x0, int x1, int y0, int y1)
{
    if (x0 < 0) x0 = 0;
    if (x1 > img->width) x1 = img->width;
    if (y0 < 0) y0 = 0;
    if (y1 > img->height) y1 = img->height;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            img->channel[ch][(size_t)y * img->width + x] = v;
}

void image_create_gray_chart(image_t *img, int width, int height)
{
    img->width = width;
    img->height = height;
    alloc_channels(img);   /* calloc → all channels 0 */

    /* 5 rows of grey levels; each row: [label] [K-only patch][C=M=Y patch].
     * Calibrate by eye: tweak the C/M/Y curves until the CMY grey shows no
     * colour cast next to the K grey (brightness match is NOT the goal). */
    static const int levels[5] = {20, 35, 50, 65, 80};
    const int label_w = 120;                       /* label gutter */
    const int gap = 16;                            /* patch separator */
    const int patch_w = (width - label_w - 3 * gap) / 2;
    const int kx0 = label_w + gap;
    const int cx0 = kx0 + patch_w + gap;
    const int row_h = height / 5;
    const int fs = 3;                              /* 15×21 px labels */

    for (int r = 0; r < 5; r++) {
        int L = levels[r];
        uint8_t v = (uint8_t)((L * 255 + 50) / 100);
        int y0 = r * row_h + 20;
        int y1 = (r + 1) * row_h - 20;             /* white separator strip */

        gray_fill(img, IMG_BLACK, v, kx0, kx0 + patch_w, y0, y1);
        gray_fill(img, IMG_CYAN,    v, cx0, cx0 + patch_w, y0, y1);
        gray_fill(img, IMG_MAGENTA, v, cx0, cx0 + patch_w, y0, y1);
        gray_fill(img, IMG_YELLOW,  v, cx0, cx0 + patch_w, y0, y1);

        reg_draw_number(img, IMG_BLACK, L, label_w - 16,
                        (y0 + y1) / 2 - (7 * fs) / 2, fs);
    }
}

static void resize_channel_bilinear(const uint8_t *src, int sw, int sh,
                                    uint8_t *dst, int dw, int dh)
{
    float fx = (float)sw / dw;
    float fy = (float)sh / dh;
    for (int y = 0; y < dh; y++) {
        float sy = (y + 0.5f) * fy - 0.5f;
        int y0 = (int)floorf(sy);
        int y1 = y0 + 1;
        float wy = sy - y0;
        if (y0 < 0) { y0 = 0; wy = 0; }
        if (y1 >= sh) y1 = sh - 1;

        for (int x = 0; x < dw; x++) {
            float sx = (x + 0.5f) * fx - 0.5f;
            int x0 = (int)floorf(sx);
            int x1 = x0 + 1;
            float wx = sx - x0;
            if (x0 < 0) { x0 = 0; wx = 0; }
            if (x1 >= sw) x1 = sw - 1;

            float a = src[y0 * sw + x0] * (1 - wx) + src[y0 * sw + x1] * wx;
            float b = src[y1 * sw + x0] * (1 - wx) + src[y1 * sw + x1] * wx;
            float v = a * (1 - wy) + b * wy;
            if (v < 0) v = 0; else if (v > 255) v = 255;
            dst[y * dw + x] = (uint8_t)(v + 0.5f);
        }
    }
}

int image_resize(image_t *img, int new_width, int new_height)
{
    if (new_width <= 0 || new_height <= 0) return -1;
    if (new_width == img->width && new_height == img->height) return 0;

    size_t new_npx = (size_t)new_width * new_height;
    uint8_t *new_ch[IMG_CHANNELS] = {0};
    for (int i = 0; i < IMG_CHANNELS; i++) {
        new_ch[i] = malloc(new_npx);
        if (!new_ch[i]) {
            fprintf(stderr, "Out of memory resizing channel %d\n", i);
            for (int j = 0; j < i; j++) free(new_ch[j]);
            return -1;
        }
        resize_channel_bilinear(img->channel[i], img->width, img->height,
                                new_ch[i], new_width, new_height);
    }

    for (int i = 0; i < IMG_CHANNELS; i++) {
        free(img->channel[i]);
        img->channel[i] = new_ch[i];
    }
    img->width = new_width;
    img->height = new_height;

    printf("Resized to %dx%d (bilinear)\n", new_width, new_height);
    return 0;
}

int image_write_gray(const char *path, const uint8_t *plane,
                     int width, int height)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        perror(path);
        return -1;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return -1;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_GRAY,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    for (int y = 0; y < height; y++)
        png_write_row(png, (png_const_bytep)(plane + (size_t)y * width));

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 0;
}
