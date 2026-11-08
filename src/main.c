/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "dwrip.h"
#include "image.h"
#include "halftone.h"
#include "channel.h"
#include "prn_gen.h"
#include "status.h"
#include "d4.h"
#include <unistd.h>   /* sleep() — --status --watch */
#include "resolution.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/stat.h>
#include <errno.h>

/* GUI progress split: the pre-pass dither drives 0..DITHER_PROGRESS_SPAN, the
 * PRN pass loop covers the rest (prog_base = DITHER_PROGRESS_SPAN). */
#define DITHER_PROGRESS_SPAN 25

// --- Interactive Mode Helpers ---
static void trim_newline(char *str) {
    size_t len = strlen(str);
    if (len > 0 && str[len-1] == '\n') str[len-1] = '\0';
    if (len > 1 && str[len-2] == '\r') str[len-2] = '\0';
}

static void prompt_string(const char *prompt, const char *example, const char *def, char *out, size_t out_size) {
    if (def && strlen(def) > 0)
        printf("?> %s (e.g., %s) [default: %s]: ", prompt, example, def);
    else
        printf("?> %s (e.g., %s): ", prompt, example);

    char buf[256];
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_newline(buf);
        if (strlen(buf) > 0) {
            strncpy(out, buf, out_size - 1);
            out[out_size - 1] = '\0';
            return;
        }
    }
    // Fallback to default
    if (def) {
        strncpy(out, def, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        out[0] = '\0';
    }
}

static bool prompt_bool(const char *prompt, bool def) {
    printf("?> %s [%s/%s] (default: %s): ", prompt,
           def ? "Y" : "y", def ? "n" : "N", def ? "Y" : "N");
    char buf[16];
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_newline(buf);
        if (strlen(buf) > 0) {
            char c = tolower((unsigned char)buf[0]);
            if (c == 'y') return true;
            if (c == 'n') return false;
        }
    }
    return def;
}

static float prompt_float(const char *prompt, const char *example, float def) {
    printf("?> %s (e.g., %s) [default: %.1f]: ", prompt, example, def);
    char buf[64];
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_newline(buf);
        if (strlen(buf) > 0) {
            return strtof(buf, NULL);
        }
    }
    return def;
}
// --------------------------------

static void usage(const char *prog)
{
    fprintf(stderr,
        "DWRip - DTF RIP for Epson L1800 (Linux)\n"
        "\n"
        "Usage: %s [options] -i <input.png> -o <output.prn>\n"
        "\n"
        "Options:\n"
        "  -i, --input FILE       Input PNG image (RGBA)\n"
        "  -w, --white FILE       Separate white channel PNG (optional)\n"
        "  -o, --output FILE      Output PRN file\n"
        "  -c, --channels ORDER   Channel order: YKWWMC, WWYKMC, CMYKWW (default: YKWWMC)\n"
        "  -I, --icc FILE         ICC profile for CMYK conversion (requires lcms2)\n"
        "  -D, --drop-mode MODE   Dot sizes the structure ladder may stack, smallest\n"
        "                         first: SML, SM, ML (default), S, M, L. Tone stays\n"
        "                         linear in physical volume, so the TOP tier sets the\n"
        "                         density ceiling and the BOTTOM tier sets how fine\n"
        "                         the lightest highlight can be:\n"
        "                           SML  finest highlights, full density (smallest\n"
        "                                step 3.1%% of max vs 10.5%% for ML)\n"
        "                           ML   default; matches AcroRIP's ML samples\n"
        "                           L    same density, dots 2.38x bigger and fewer —\n"
        "                                coarser, but cures better on thin/soft tints\n"
        "                           SM   caps density at ~42%% (no large drop)\n"
        "                           M    caps at ~42%%   S caps at ~13%%\n"
        "  -W, --white-first      Print white before color (default: color first)\n"
        "  -Z, --zones N          Head split zones: 2 (default, Color/White) or\n"
        "                         3 (Color/White/Color sandwich, 30 nozzles/zone,\n"
        "                         ~1.5x slower; zone 3 duplicates CMYK for testing)\n"
        "      --z3-mode MODE     -Z 3 only: trailing colour zone source —\n"
        "                         same (default, 2x sandwich), mirror (horizontal\n"
        "                         flip of front), image (separate PNG). mirror/image\n"
        "                         = two-sided transparent print (front A / white /\n"
        "                         back B), each side 4 hits (not 2x stacked)\n"
        "      --z3-image PATH    PNG for --z3-mode image (resized to print size)\n"
        "  -b, --bidi             Bidirectional printing (default: bidi)\n"
        "  -u, --unidi            Unidirectional printing\n"
        "      --ink-color PCT    Colour ink density %%: scales ALL of C+M+Y+K by\n"
        "                         PCT/100 on every pixel (100 = off, 70 = ×0.70).\n"
        "                         Like --trim for all four channels — uniformly\n"
        "                         lightens/darkens. Applied before the TAC cap.\n"
        "  -l, --color-limit PCT  Total CMYK ink cap (TAC) %%: limits per-pixel\n"
        "                         C+M+Y+K to PCT%% of 400%% (e.g. 70 = cap 280%%). Only\n"
        "                         over-inked pixels are pulled back, ratio preserved\n"
        "                         (no hue shift). 100 = no limit. A ceiling, NOT a\n"
        "                         density control — use --ink-color for density.\n"
        "  -L, --white-limit PCT  White ink density %% (scale, default: 100)\n"
        "      --white-mode MODE  How the white base is built: alpha (default,\n"
        "                         white = the image alpha) or luminance (that,\n"
        "                         scaled by the brightness of the colour on top).\n"
        "      --white-dark-pct N White density under the DARKEST colour, for\n"
        "                         --white-mode luminance (default 100 = off).\n"
        "                         Dark ink hides the media by itself; 50-60 is a\n"
        "                         sensible first try.\n"
        "      --white-choke MM   Shrink the white base inward by MM so the colour\n"
        "                         overhangs it, hiding a white halo when the two\n"
        "                         layers register slightly off. 0.1-0.3 is typical;\n"
        "                         default 0 = off.\n"
        "      --white-curve X,D  Bend the alpha->white ramp: tone position X\n"
        "                         (1..99) and signed percent D there. Sets how\n"
        "                         long a soft edge takes to fade out, which -L\n"
        "                         cannot do (it scales everything). Default off.\n"
        "  -M, --margin MM        Left margin padding in mm (default: 0)\n"
        "  -U, --uv-tail MM       Extra UV curing travel after image end so the\n"
        "                         carriage-mounted UV lamp cures the tail\n"
        "                         (default: 5, 0 = off)\n"
        "      --uv-tail-speed N  Advance multiplier for tail passes (default: 1\n"
        "                         = same UV dose as image; 4 = 4x faster, ~25%% dose)\n"
        "      --lead-in-feed-step N\n"
        "                         Max ESC(v) unit for transparent lead-in feed\n"
        "                         before the first ink band (default: 45, 0 = off)\n"
        "      --lead-in-sweep MODE\n"
        "                         Experimental transparent lead-in dummy sweep:\n"
        "                         off (default), short (left/capping side), full\n"
        "      --lead-in-dummy-sweep\n"
        "                         Legacy alias for --lead-in-sweep full\n"
        "  -m, --mode MODE        Layer mode: both (default), color, white\n"
        "      --single-weave M   -m color/white only: carriage sweeps per row, which\n"
        "                         sets the UV cure dose (the lamp rides the carriage).\n"
        "                         Ink per row is IDENTICAL at every level; only the\n"
        "                         number of sweeps (and so the time) changes:\n"
        "                           fast  (default) 90 nozzles, adv 22/54 — 1x dose,\n"
        "                                 and only even rows print (720 dpi vertical)\n"
        "                           dense 45 nozzles, adv 11/27 — 2x dose, 2x slower,\n"
        "                                 every row prints (full 1440 dpi vertical)\n"
        "                           ultra 30 nozzles, adv 7/8   — 3x dose, 3x slower\n"
        "                           max   18 nozzles, adv 4/5   — 5x dose, 5x slower\n"
        "                         Step up until soft/thin tints cure. ultra and max\n"
        "                         need -R 1440x1440. Ignored in -m both (already 2x).\n"
        "  -t, --test WxH         Generate test pattern (e.g. 568x227)\n"
        "  -s, --send DEVICE      Send to printer (e.g. /dev/usb/lp0)\n"
        "  -S, --size WxH         Target print size in mm (auto-resize, e.g. 40x40 or 40)\n"
        "  -R, --resolution NAME  720x360 | 720x720 | 1440x720 | 1440x1440 (default) | 2880x2880\n"
        "                         (2880 is very slow / huge; 3-zone tuned for 1440)\n"
        "  -T, --only-code HEX    Emit PRN with single color code (e.g. 0x11)\n"
        "  -X, --mapping-test PFX Generate 6 per-code test PRNs (PFX_00.prn..PFX_12.prn)\n"
        "      --intent MODE      ICC rendering intent: perceptual (default),\n"
        "                         relative, relative-bpc, saturation, absolute\n"
        "      --icc-test         With -I: print RGB->CMYK swatch table for all\n"
        "                         intents and exit (no PRN)\n"
        "      --estimate         Tally emitted ink dots per channel and print an\n"
        "                         INK_REPORT (no PRN written) for cost estimation\n"
        "      --emboss N         Spot 3D relief: N=8/16/32 white layers (solid\n"
        "                         white stacked + colour on top). Needs -R\n"
        "                         1440x1440 + --spot. (lihat §emboss)\n"
        "      --spot FILE        Raised-area mask PNG (white=raised) for --emboss\n"
        "      --underbase on|off Thin 4-hit white under colour in non-spot areas\n"
        "                         (emboss; needed on dark/clear media)\n"
        "      --emboss-height MODE  binary (default, spot on/off) or grayscale\n"
        "                         (mask level → per-pixel height 0..N layers)\n"
        "      --emboss-mode MODE  single (default, Cara B: 1 run, N=8/16/32) or\n"
        "                         split (Cara A: white relief run(s) 90-nozzle +\n"
        "                         separate full-quality colour run, N=15/30/45/90)\n"
        "      --emboss-white-runs K  split: stack K white runs (>90 layers)\n"
        "      --emboss-color-reg N   split: colour-run feed offset (1/1440\") to\n"
        "                         register the colour onto the relief\n"
        "      --chart            Emit a per-channel density step wedge PRN\n"
        "                         (ICC bypassed) for visual density calibration\n"
        "      --trim SPEC        Per-channel density trim, applied BEFORE -l.\n"
        "                         SPEC = C:-12,Y:-5,K:+3 (signed %%, factor 1+d/100).\n"
        "                         Fixes hue/over-ink per channel; -l unchanged.\n"
        "      --curve-c D,D,D    Correction curve per channel (also --curve-m/-y/-k):\n"
        "                         signed %% at the 25/50/75%% anchors, endpoints fixed.\n"
        "                         Bends midtones only (dot gain / grey balance);\n"
        "                         applied BEFORE --trim. E.g. --curve-c -5,-12,-6\n"
        "      --curve-point-c X,D Smooth one-point curve per channel (also\n"
        "                         --curve-point-m/-y/-k): X = tone position 1..99%%,\n"
        "                         D = signed %% at that point. Endpoints fixed,\n"
        "                         monotone cubic LUT. E.g. --curve-point-c 50,-12\n"
        "      --chart-gray       Emit a grey-balance chart PRN: K-only vs C+M+Y grey\n"
        "                         patches at 5 levels. Adjust the C/M/Y curves until\n"
        "                         the CMY grey looks as neutral as the K grey.\n"
        "      --reg-v N          Vertical registration: shift COLOUR layer N rows\n"
        "                         (1/1440\") vs white in 2/3-zone both-mode, to\n"
        "                         correct feed misalignment. 0 = off.\n"
        "      --reg-test         Emit a white↔colour registration calibration PRN\n"
        "                         (both-mode 2-zone): pick the mark that overlaps →\n"
        "                         its label is the --reg-v value to use.\n"
        "      --nozzle-check     Emit a nozzle-check PRN: 2 stacked models (solid\n"
        "                         comb + line staircase), 6 inks side by side each\n"
        "                         (K/C/M/Y/W/W). A missing line/gap = a clogged nozzle.\n"
        "      --status           Query the printer and print its state, e.g.\n"
        "                         `STATUS 04 ready`. Use it to tell when a\n"
        "                         cleaning has finished. Reads /dev/usb/lp*.\n"
        "      --device PATH      Printer device for --status (default: the first\n"
        "                         Epson /dev/usb/lp* found).\n"
        "      --waste             Read the waste-ink counters over IEEE-1284.4.\n"
        "      --waste-reset       Reset waste-ink counter 1 to zero. IRREVERSIBLE.\n"
        "                         Writes each byte then reads it back, and stops if\n"
        "                         the first write does not take. Resetting does NOT\n"
        "                         empty the pad — service the pad first.\n"
        "                         Manual only. Leaves the printer in D4 mode, where\n"
        "                         plain ESC/P status stops answering until it exits\n"
        "                         (printing is unaffected).\n"
        "      --eeprom AA[,AA..]  Diagnostic: dump raw EEPROM bytes at these hex\n"
        "                         addresses (read-only; never writes).\n"
        "      --device-serial SN  For --status: pick the printer with this USB\n"
        "                         serial (the value CUPS puts in its device URI),\n"
        "                         instead of the first Epson found.\n"
        "      --watch             With --status: keep polling once a second and\n"
        "                         print every change with a timestamp, until\n"
        "                         Ctrl-C. Use it to trace a cleaning cycle.\n"
        "      --clean LEVEL      Run a printer head cleaning (no image needed).\n"
        "                         LEVEL = gentle | normal, or `list` to show them.\n"
        "                         Writes a few-byte command file; spool it like a\n"
        "                         PRN. Costs ink and raises the waste-ink counter.\n"
        "      --dump-cmyk DIR    Diagnostic: write per-channel grayscale PNGs\n"
        "                         (255 = 100%% ink) into DIR at two stages:\n"
        "                         sep_* right after ICC/UCR separation (+resize),\n"
        "                         final_* right before dither (after --trim/-l/-L).\n"
        "                         Without -o, exits after dumping (no PRN).\n"
        "      --structure        Accepted for compatibility (no-op). The structured\n"
        "                         dither is now the ONLY halftone path: tone linear\n"
        "                         in physical drop volume (medium units, large=2.38);\n"
        "                         highlights = sparse single mediums, midtones =\n"
        "                         growing medium stacks, solids = medium->large\n"
        "                         upgrades, spread across disjoint passes. Density\n"
        "                         parity across print modes.\n"
        "      --struct-dither M  Dot dispersion. Mask-free error\n"
        "                         diffusion (cannot grid, ranked by print test):\n"
        "                         serpjarvis (DEFAULT, best — serpentine +\n"
        "                         Jarvis-Judice-Ninke 12-tap kernel), \n"
        "                         serpjarvisrandom (+ randomized weights, 2nd),\n"
        "                         serprandom (serpentine FS + randomized\n"
        "                         weights, 3rd), serpentine (boustrophedon FS),\n"
        "                         jitter (old default, FS+hash, grainy), fs\n"
        "                         (plain FS, worms — not for photos).\n"
        "                         Mask-based/ordered — ALL show a grid on film,\n"
        "                         kept for reference only: bluenoise,\n"
        "                         bluenoisehq (bigger anisotropic mask +\n"
        "                         per-channel offset), bluejitter (worst).\n"
        "  -h, --help             Show this help\n"
        "\n"
        "Examples:\n"
        "  %s -i design.png -o output.prn -c YKWWMC -W\n"
        "  %s -i design.png -o output.prn -S 40x40        (print at 40x40 mm)\n"
        "  %s -i design.png -o output.prn -S 100          (100mm wide, aspect preserved)\n"
        "  %s -X slotmap                                  (6 files, 20x20mm solid)\n",
        prog, prog, prog, prog, prog);
}

/* Dump RGB→CMYK separation for representative swatches across all rendering
 * intents, so the user can eyeball which intent best matches a soft-proof
 * without printing.  Returns process exit code. */
static int run_icc_test(const char *icc_path)
{
    cmsHPROFILE in = cmsCreate_sRGBProfile();
    cmsHPROFILE out = cmsOpenProfileFromFile(icc_path, "r");
    if (!out) {
        fprintf(stderr, "Failed to open ICC profile: %s\n", icc_path);
        if (in) cmsCloseProfile(in);
        return 1;
    }

    struct { const char *name; unsigned char rgb[3]; } sw[] = {
        {"Putih   ", {255,255,255}}, {"Abu 50% ", {128,128,128}},
        {"Hitam   ", {0,0,0}},       {"Merah   ", {255,0,0}},
        {"Hijau   ", {0,255,0}},     {"Biru    ", {0,0,255}},
        {"Kuning  ", {255,255,0}},   {"Cyan    ", {0,255,255}},
        {"Magenta ", {255,0,255}},   {"Kulit   ", {224,172,140}},
    };
    struct { const char *name; cmsUInt32Number intent, flags; } modes[] = {
        {"Perceptual",   INTENT_PERCEPTUAL, 0},
        {"Relative",     INTENT_RELATIVE_COLORIMETRIC, 0},
        {"Relative+BPC", INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_BLACKPOINTCOMPENSATION},
        {"Saturation",   INTENT_SATURATION, 0},
    };

    printf("ICC profile: %s\n", icc_path);
    printf("Swatch RGB -> CMYK (%% tinta) per rendering intent:\n");
    for (int m = 0; m < 4; m++) {
        cmsHTRANSFORM t = cmsCreateTransform(in, TYPE_RGB_8, out, TYPE_CMYK_8,
                                             modes[m].intent, modes[m].flags);
        if (!t) continue;
        printf("\n== %s ==\n", modes[m].name);
        printf("%-9s %6s %6s %6s %6s   TAC%%\n", "warna", "C", "M", "Y", "K");
        for (int i = 0; i < 10; i++) {
            unsigned char cmyk[4];
            cmsDoTransform(t, sw[i].rgb, cmyk, 1);
            printf("%-9s %6.1f %6.1f %6.1f %6.1f  %6.1f\n", sw[i].name,
                   cmyk[0]/2.55, cmyk[1]/2.55, cmyk[2]/2.55, cmyk[3]/2.55,
                   (cmyk[0]+cmyk[1]+cmyk[2]+cmyk[3])/2.55);
        }
        cmsDeleteTransform(t);
    }
    cmsCloseProfile(out);
    cmsCloseProfile(in);
    return 0;
}

/* Parse a per-channel trim spec like "C:-12,Y:-5,K:+3" into density factors
 * (1 + delta/100), clamped to [0, 2]. Index order: 0=C 1=M 2=Y 3=K.
 * Returns 0 on success, -1 on a parse error. */
static int parse_trim(const char *spec, float trim[4])
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", spec);

    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        char ch = 0;
        float delta = 0.0f;
        if (sscanf(tok, " %c : %f", &ch, &delta) != 2) {
            fprintf(stderr, "Invalid --trim term: '%s' (use e.g. C:-12)\n", tok);
            return -1;
        }
        int idx;
        switch (ch) {
        case 'C': case 'c': idx = 0; break;
        case 'M': case 'm': idx = 1; break;
        case 'Y': case 'y': idx = 2; break;
        case 'K': case 'k': idx = 3; break;
        default:
            fprintf(stderr, "Invalid --trim channel '%c' (use C/M/Y/K)\n", ch);
            return -1;
        }
        float f = 1.0f + delta / 100.0f;
        if (f < 0.0f) f = 0.0f;
        if (f > 2.0f) f = 2.0f;
        trim[idx] = f;
    }
    return 0;
}

/* Parse a --curve-x spec "d25,d50,d75" (signed percents, clamped ±90). */
static int parse_curve(const char *spec, int out[3])
{
    int d[3];
    if (sscanf(spec, " %d , %d , %d", &d[0], &d[1], &d[2]) != 3) {
        fprintf(stderr, "Invalid curve spec: '%s' (use D25,D50,D75 e.g. -10,-15,-8)\n",
                spec);
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        if (d[i] < -90) d[i] = -90;
        if (d[i] >  90) d[i] =  90;
        out[i] = d[i];
    }
    return 0;
}

/* Parse a --curve-point-x spec "X,D" (tone position %, signed delta %). */
static int parse_curve_point(const char *spec, int out[2])
{
    int x, d;
    char extra;
    if (sscanf(spec, " %d , %d %c", &x, &d, &extra) != 2) {
        fprintf(stderr, "Invalid curve point spec: '%s' "
                        "(use X,D e.g. 50,-12)\n", spec);
        return -1;
    }
    if (x < 1 || x > 99) {
        fprintf(stderr, "Invalid curve point X: %d (use 1..99)\n", x);
        return -1;
    }
    if (d < -90) d = -90;
    if (d >  90) d =  90;
    out[0] = x;
    out[1] = d;
    return 0;
}

/* --dump-cmyk: write the 5 separation planes as grayscale PNGs (255 = 100%
 * ink) into dir, named {prefix}_01_C.png .. {prefix}_05_W.png. All planes are
 * dumped regardless of -m: mode filtering only affects channel routing later,
 * the separation itself always exists. Returns 0 on success. */
static int dump_cmyk_planes(const char *dir, const char *prefix,
                            const image_t *img)
{
    static const struct { int ch; const char *name; } planes[] = {
        { IMG_CYAN,    "01_C" },
        { IMG_MAGENTA, "02_M" },
        { IMG_YELLOW,  "03_Y" },
        { IMG_BLACK,   "04_K" },
        { IMG_WHITE,   "05_W" },
    };

    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
        perror(dir);
        return -1;
    }

    for (size_t i = 0; i < sizeof(planes) / sizeof(planes[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s_%s.png", dir, prefix,
                 planes[i].name);
        if (image_write_gray(path, img->channel[planes[i].ch],
                             img->width, img->height) < 0) {
            fprintf(stderr, "Failed to write %s\n", path);
            return -1;
        }
        printf("  Dumped %s (%dx%d)\n", path, img->width, img->height);
    }
    return 0;
}

/* Print the per-channel ink-usage report from the emitted-dot tally. The GUI
 * parses the INK_REPORT lines; the human lines use approximate L1800 drop
 * volumes (calibratable in the GUI, which recomputes µL/cost itself). */
static void print_ink_report(const print_settings_t *s,
                             uint64_t ink_dots[MAX_CHANNELS][4])
{
    const double pl[4] = {0.0, 1.5, 3.7, 8.8}; /* none/small/medium/large (pL) */
    printf("Ink usage estimate (per channel, approx):\n");
    for (int i = 0; i < s->num_channels; i++) {
        unsigned long long ns = ink_dots[i][1], nm = ink_dots[i][2],
                           nl = ink_dots[i][3];
        double ul = (ns * pl[1] + nm * pl[2] + nl * pl[3]) / 1e6; /* pL→µL */
        printf("INK_REPORT %s %llu %llu %llu\n", s->channels[i].name, ns, nm, nl);
        printf("  %-3s: small=%llu medium=%llu large=%llu -> ~%.3f uL\n",
               s->channels[i].name, ns, nm, nl, ul);
    }
}

/* Fit the UV-tail sweep to the ink in the trailing rows: find the horizontal
 * ink extent over the last `tail_rows` rows (anchored at the last inked row),
 * so the tail carriage sweep follows the content — a shape that narrows toward
 * the end cures with a narrow sweep instead of the whole canvas. Only the last
 * rows matter because the lamp trails the nozzles: everything above was already
 * cured while printing. Leaves col_lo/col_hi = 0/0 (engine full-width fallback)
 * when the image is blank. Ink = any CMYK or white/alpha coverage (a safe
 * superset of the dithered dots, so it never under-covers). */
static void fit_uv_tail_to_content(const image_t *img, int tail_rows,
                                   int *col_lo, int *col_hi)
{
    *col_lo = 0; *col_hi = 0;
    if (!img || img->width <= 0 || img->height <= 0) return;
    if (tail_rows < 1) tail_rows = 1;

    int last_row = -1;
    for (int y = img->height - 1; y >= 0 && last_row < 0; y--) {
        for (int ch = 0; ch < IMG_CHANNELS; ch++) {
            const uint8_t *p = img->channel[ch];
            if (!p) continue;
            const uint8_t *row = &p[(size_t)y * img->width];
            for (int x = 0; x < img->width; x++)
                if (row[x]) { last_row = y; break; }
            if (last_row >= 0) break;
        }
    }
    if (last_row < 0) return;   /* blank → keep full-width fallback */

    int top = last_row - tail_rows + 1;
    if (top < 0) top = 0;

    int lo = img->width, hi = -1;
    for (int y = top; y <= last_row; y++) {
        for (int ch = 0; ch < IMG_CHANNELS; ch++) {
            const uint8_t *p = img->channel[ch];
            if (!p) continue;
            const uint8_t *row = &p[(size_t)y * img->width];
            for (int x = 0; x < img->width; x++)
                if (row[x]) { if (x < lo) lo = x; if (x > hi) hi = x; }
        }
    }
    if (hi < lo) return;
    *col_lo = lo;
    *col_hi = hi;
}

/* Cara A split output name: "base.prn" + idx + tag → "base.<idx>-<tag>.prn". */
static void emboss_split_name(const char *base, int idx, const char *tag,
                              char *out, size_t cap)
{
    const char *dot = strrchr(base, '.');
    int stem = (dot && strcmp(dot, ".prn") == 0) ? (int)(dot - base)
                                                 : (int)strlen(base);
    snprintf(out, cap, "%.*s.%d-%s.prn", stem, base, idx, tag);
}

/* --struct-dither CLI key, for log/status output. */
static const char *struct_dither_name(struct_dither_t m)
{
    switch (m) {
    case SD_FS:               return "fs";
    case SD_BLUENOISE:        return "bluenoise";
    case SD_BLUEJITTER:       return "bluejitter";
    case SD_SERPENTINE:       return "serpentine";
    case SD_SERPRANDOM:       return "serprandom";
    case SD_SERPJARVIS:       return "serpjarvis";
    case SD_SERPJARVISRANDOM: return "serpjarvisrandom";
    case SD_BLUENOISEHQ:      return "bluenoisehq";
    case SD_JITTER: default:  return "jitter";
    }
}

static const char *drop_mode_name(drop_mode_t m)
{
    switch (m) {
    case DROP_SML: return "SML";
    case DROP_M:   return "M";
    case DROP_S:   return "S";
    case DROP_L:   return "L";
    case DROP_SM:  return "SM";
    case DROP_ML: default: return "ML";
    }
}

/*
 * One status reading: plain ESC/P first, D4 second.
 *
 * ESC/P is cheap and leaves the printer's mode alone, so it stays the everyday
 * path. It goes silent when the printer has been left in IEEE-1284.4 mode by a
 * service tool, and only then is D4 worth its cost — at that point the printer
 * is already in D4, so asking that way makes nothing worse.
 */
static int query_status(const char *device)
{
    int code = status_query(device, 1);
    if (code >= 0 || code == PRN_STATUS_DEVICE_BUSY)
        return code;

    char devbuf[256];
    const char *dev = device;
    if (!dev && status_find_device(devbuf, sizeof(devbuf)) == 0)
        dev = devbuf;
    if (!dev)
        return -1;

    d4_session_t d4;
    if (d4_open(dev, &d4) != 0)
        return -1;
    uint8_t rep[256];
    int rd = d4_status(&d4, rep, sizeof(rep));
    if (rd > 0)
        code = status_parse_buffer(rep, (size_t)rd);
    d4_close(&d4);
    return code;
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *white_path = NULL;
    const char *output_path = NULL;
    const char *icc_path = NULL;
    const char *channel_order = "YKWWMC";
    const char *send_device = NULL;
    drop_mode_t drop_mode = DROP_ML; /* Default like AcroRIP */
    bool white_first = false;
    bool bidi = true;
    float color_limit = 1.0f;
    float white_limit = 1.0f;
    float ink_color = 1.0f;  /* --ink-color: global CMYK density scale (all 4) */
    bool estimate = false;   /* --estimate: tally ink & print report, no PRN */
    float trim[4] = {1.0f, 1.0f, 1.0f, 1.0f}; /* --trim: per-channel C,M,Y,K scale */
    int curve[4][3] = {{0}};  /* --curve-c/m/y/k: signed % at 25/50/75 anchors */
    bool curve_set[4] = {false, false, false, false};
    int curve_point[4][2] = {{50, 0}, {50, 0}, {50, 0}, {50, 0}};
    bool curve_point_set[4] = {false, false, false, false};
    bool chart_gray = false;  /* --chart-gray: K vs C+M+Y grey-balance chart */
    float left_margin_mm = 0.0f;
    int test_w = 0, test_h = 0;
    int only_code = -1;                 /* -T: single color code for channel mapping test */
    const char *mapping_prefix = NULL;  /* -X: generate 6 mapping-test PRNs */
    float size_w_mm = 0, size_h_mm = 0; /* -S: target print size in mm (0 = auto from aspect) */
    print_mode_t print_mode = PRINT_BOTH;
    single_weave_t single_weave = SINGLE_WEAVE_FAST; /* --single-weave */
    int zone_count = 2; /* -Z: 2 = Color/White, 3 = Color/White/Color */
    z3_mode_t z3_mode = Z3_SAME; /* --z3-mode: zone-3 colour source (3-zone) */
    const char *z3_image_path = NULL; /* --z3-image: PNG for zone-3 (image mode) */
    int emboss_layers = 0;       /* --emboss: 8/16/32 white-relief preset (0=off) */
    const char *spot_path = NULL; /* --spot: raised-area mask PNG */
    bool underbase = false;      /* --underbase: thin white under colour (emboss) */
    bool emboss_grayscale = false; /* --emboss-height grayscale: variable height */
    bool emboss_split = false;   /* --emboss-mode split (Cara A): separate runs */
    int emboss_white_runs = 1;   /* --emboss-white-runs K (>90 layers) */
    int emboss_color_reg = 0;    /* --emboss-color-reg: colour run feed offset */
    float uv_tail_mm = 5.0f;  /* -U: extra UV curing travel after image end */
    int uv_tail_speed = 1;    /* --uv-tail-speed: tail advance multiplier */
    int lead_in_feed_step = 45; /* --lead-in-feed-step: 0 = one normal advance */
    lead_in_sweep_mode_t lead_in_sweep_mode = LEAD_IN_SWEEP_OFF;
    icc_intent_t intent = { INTENT_PERCEPTUAL, 0 }; /* --intent */
    bool icc_test = false;    /* --icc-test: dump swatch table and exit */
    bool chart = false;       /* --chart: emit per-channel density wedge */
    bool reg_test = false;    /* --reg-test: emit white↔colour registration target */
    bool nozzle_check = false;/* --nozzle-check: emit per-nozzle staircase (clog test) */
    const char *clean_name = NULL; /* --clean: head cleaning level */
    bool want_status = false;      /* --status: query printer state and exit */
    const char *status_device = NULL; /* --device: override for --status */
    bool status_watch = false;     /* --watch: trace status changes */
    const char *status_serial = NULL; /* --device-serial: pick one printer */
    bool want_waste = false;       /* --waste: read waste-ink counters via D4 */
    const char *eeprom_list = NULL; /* --eeprom: raw address dump */
    bool want_waste_reset = false;  /* --waste-reset: zero the counter */
    int reg_v = 0;            /* --reg-v: colour-vs-white vertical reg offset */
    white_mode_t white_mode = WHITE_MODE_ALPHA; /* --white-mode */
    int white_dark_pct = 100; /* --white-dark-pct: 100 = no modulation */
    int white_curve_x = 50;   /* --white-curve X,D — delta 0 = off */
    int white_curve_d = 0;
    float white_choke_mm = 0.0f; /* --white-choke: shrink white inward (mm) */
    const char *resolution_name = "1440x1440"; /* -R: resolution preset */
    const char *dump_dir = NULL; /* --dump-cmyk: separation dump directory */
    /* Default = the print-test winner (see CLAUDE.md §structmode). Jarvis's
     * wide 12-tap spread is self-decorrelating, so it needs no weight
     * randomization and beats every mask-based mode on film. */
    struct_dither_t struct_dither = SD_SERPJARVIS; /* --struct-dither */

    static char input_buf[256] = {0};
    static char output_buf[256] = {0};
    static char icc_buf[256] = {0};
    static char channel_buf[32] = {0};
    static char mode_buf[16] = {0};
    static char size_buf[32] = {0};

    if (argc <= 1) {
        printf("\n==========================================\n");
        printf(" DWRip Interactive CLI Wizard\n");
        printf("==========================================\n");

        prompt_string("Input Image File", "design.png", "", input_buf, sizeof(input_buf));
        if (strlen(input_buf) > 0) input_path = input_buf;

        prompt_string("Output PRN File", "output.prn", "output.prn", output_buf, sizeof(output_buf));
        if (strlen(output_buf) > 0) output_path = output_buf;

        prompt_string("Channel Order", "YKWWMC, CMYKWW", "YKWWMC", channel_buf, sizeof(channel_buf));
        if (strlen(channel_buf) > 0) channel_order = channel_buf;

        prompt_string("Target Width Size in mm", "200, leave blank for original", "", size_buf, sizeof(size_buf));
        if (strlen(size_buf) > 0) {
            if (sscanf(size_buf, "%fx%f", &size_w_mm, &size_h_mm) < 1) {
                sscanf(size_buf, "%f", &size_w_mm);
            }
        }

        prompt_string("Layer Mode", "both/color/white", "both", mode_buf, sizeof(mode_buf));
        if (strcmp(mode_buf, "color") == 0) print_mode = PRINT_COLOR;
        else if (strcmp(mode_buf, "white") == 0) print_mode = PRINT_WHITE;

        zone_count = (int)prompt_float("Head Zones (2=Color/White, 3=Color/White/Color)", "2", 2.0f);
        if (zone_count != 2 && zone_count != 3) zone_count = 2;

        white_first = prompt_bool("Print White First?", false);
        left_margin_mm = prompt_float("Left Margin Padding in mm", "5.0", 0.0f);
        uv_tail_mm = prompt_float("UV Tail Curing in mm (0=off)", "5", 5.0f);
        ink_color = prompt_float("Colour Ink Density % (100=off, scales all CMYK)", "100", 100.0f) / 100.0f;
        color_limit = prompt_float("Total Ink Cap % (100=off, 70=cap 280% TAC)", "100", 100.0f) / 100.0f;
        white_limit = prompt_float("White Ink Density % (0-100)", "100", 100.0f) / 100.0f;

        prompt_string("ICC Profile Path", "sRGB.icc, leave blank to skip", "", icc_buf, sizeof(icc_buf));
        if (strlen(icc_buf) > 0) icc_path = icc_buf;

        if (icc_path) {
            static char intent_buf[24] = {0};
            prompt_string("Rendering Intent",
                          "perceptual/relative-bpc/saturation", "perceptual",
                          intent_buf, sizeof(intent_buf));
            if (strcmp(intent_buf, "relative") == 0) {
                intent.intent = INTENT_RELATIVE_COLORIMETRIC; intent.flags = 0;
            } else if (strcmp(intent_buf, "relative-bpc") == 0) {
                intent.intent = INTENT_RELATIVE_COLORIMETRIC;
                intent.flags = cmsFLAGS_BLACKPOINTCOMPENSATION;
            } else if (strcmp(intent_buf, "saturation") == 0) {
                intent.intent = INTENT_SATURATION; intent.flags = 0;
            } else if (strcmp(intent_buf, "absolute") == 0) {
                intent.intent = INTENT_ABSOLUTE_COLORIMETRIC; intent.flags = 0;
            }
        }

        printf("==========================================\n\n");
    } else {
        static struct option long_opts[] = {
            {"input",         required_argument, 0, 'i'},
            {"white",         required_argument, 0, 'w'},
            {"output",        required_argument, 0, 'o'},
            {"icc",           required_argument, 0, 'I'},
            {"drop-mode",     required_argument, 0, 'D'},
            {"channels",      required_argument, 0, 'c'},
            {"white-first",   no_argument,       0, 'W'},
            {"zones",         required_argument, 0, 'Z'},
            {"bidi",          no_argument,       0, 'b'},
            {"unidi",         no_argument,       0, 'u'},
            {"color-limit",   required_argument, 0, 'l'},
            {"white-limit",   required_argument, 0, 'L'},
            {"ink-color",     required_argument, 0, 1032}, /* long-only */
            {"margin",        required_argument, 0, 'M'},
            {"uv-tail",       required_argument, 0, 'U'},
            {"uv-tail-speed", required_argument, 0, 1000}, /* long-only */
            {"lead-in-feed-step", required_argument, 0, 1022}, /* long-only */
            {"lead-in-sweep", required_argument, 0, 1024}, /* long-only */
            {"single-weave",  required_argument, 0, 1042}, /* long-only */
            {"lead-in-dummy-sweep", no_argument, 0, 1023}, /* long-only */
            {"z3-mode",       required_argument, 0, 1030}, /* long-only */
            {"z3-image",      required_argument, 0, 1031}, /* long-only */
            {"estimate",      no_argument,       0, 1034}, /* long-only */
            {"emboss",        required_argument, 0, 1035}, /* long-only */
            {"spot",          required_argument, 0, 1036}, /* long-only */
            {"underbase",     required_argument, 0, 1037}, /* long-only */
            {"emboss-height", required_argument, 0, 1038}, /* long-only */
            {"emboss-mode",   required_argument, 0, 1039}, /* long-only */
            {"emboss-white-runs", required_argument, 0, 1040}, /* long-only */
            {"emboss-color-reg",  required_argument, 0, 1041}, /* long-only */
            {"mode",          required_argument, 0, 'm'},
            {"test",          required_argument, 0, 't'},
            {"send",          required_argument, 0, 's'},
            {"size",          required_argument, 0, 'S'},
            {"only-code",     required_argument, 0, 'T'},
            {"mapping-test",  required_argument, 0, 'X'},
            {"intent",        required_argument, 0, 1001}, /* long-only */
            {"icc-test",      no_argument,       0, 1002}, /* long-only */
            {"chart",         no_argument,       0, 1003}, /* long-only */
            {"trim",          required_argument, 0, 1004}, /* long-only */
            {"reg-v",         required_argument, 0, 1005}, /* long-only */
            {"reg-test",      no_argument,       0, 1006}, /* long-only */
            {"nozzle-check",  no_argument,       0, 1007}, /* long-only */
            {"curve-c",       required_argument, 0, 1008}, /* long-only */
            {"curve-m",       required_argument, 0, 1009}, /* long-only */
            {"curve-y",       required_argument, 0, 1010}, /* long-only */
            {"curve-k",       required_argument, 0, 1011}, /* long-only */
            {"chart-gray",    no_argument,       0, 1012}, /* long-only */
            {"dump-cmyk",     required_argument, 0, 1013}, /* long-only */
            {"structure",     no_argument,       0, 1016}, /* long-only, no-op */
            {"struct-dither", required_argument, 0, 1017}, /* long-only */
            {"curve-point-c", required_argument, 0, 1018}, /* long-only */
            {"curve-point-m", required_argument, 0, 1019}, /* long-only */
            {"curve-point-y", required_argument, 0, 1020}, /* long-only */
            {"curve-point-k", required_argument, 0, 1021}, /* long-only */
            {"clean",         required_argument, 0, 1043}, /* long-only */
            {"status",        no_argument,       0, 1044}, /* long-only */
            {"device",        required_argument, 0, 1045}, /* long-only */
            {"watch",         no_argument,       0, 1046}, /* long-only */
            {"device-serial", required_argument, 0, 1047}, /* long-only */
            {"waste",         no_argument,       0, 1048}, /* long-only */
            {"eeprom",        required_argument, 0, 1049}, /* long-only */
            {"waste-reset",   no_argument,       0, 1051}, /* long-only */
            {"white-mode",    required_argument, 0, 1052}, /* long-only */
            {"white-dark-pct",required_argument, 0, 1053}, /* long-only */
            {"white-curve",   required_argument, 0, 1054}, /* long-only */
            {"white-choke",   required_argument, 0, 1055}, /* long-only */
            {"resolution",    required_argument, 0, 'R'},
            {"help",          no_argument,       0, 'h'},
            {0, 0, 0, 0}
        };

        int opt;
        while ((opt = getopt_long(argc, argv, "i:w:o:I:D:c:WZ:bul:L:M:U:m:t:s:S:T:X:R:h", long_opts, NULL)) != -1) {
            switch (opt) {
            case 'i': input_path = optarg; break;
            case 'w': white_path = optarg; break;
            case 'o': output_path = optarg; break;
            case 'I': icc_path = optarg; break;
            case 'D':
                if (strcmp(optarg, "SML") == 0) drop_mode = DROP_SML;
                else if (strcmp(optarg, "ML") == 0) drop_mode = DROP_ML;
                else if (strcmp(optarg, "M") == 0) drop_mode = DROP_M;
                else if (strcmp(optarg, "S") == 0) drop_mode = DROP_S;
                else if (strcmp(optarg, "L") == 0) drop_mode = DROP_L;
                else if (strcmp(optarg, "SM") == 0) drop_mode = DROP_SM;
                else {
                    fprintf(stderr, "Unknown drop mode: %s "
                            "(use SML, SM, ML, S, M or L)\n", optarg);
                    return 1;
                }
                break;
            case 'c': channel_order = optarg; break;
            case 'W': white_first = true; break;
            case 'Z':
                zone_count = atoi(optarg);
                if (zone_count != 2 && zone_count != 3) {
                    fprintf(stderr, "Invalid zone count: %s (use 2 or 3)\n", optarg);
                    return 1;
                }
                break;
            case 'b': bidi = true; break;
            case 'u': bidi = false; break;
            case 'l': color_limit = atof(optarg) / 100.0f; break;
            case 'L': white_limit = atof(optarg) / 100.0f; break;
            case 1032: { /* --ink-color: global CMYK scale, clamp [0,2] */
                float v = atof(optarg) / 100.0f;
                ink_color = v < 0.0f ? 0.0f : (v > 2.0f ? 2.0f : v);
                break;
            }
            case 'M': left_margin_mm = atof(optarg); break;
            case 'U': uv_tail_mm = atof(optarg); break;
            case 1000:
                uv_tail_speed = atoi(optarg);
                if (uv_tail_speed < 1) {
                    fprintf(stderr, "Invalid uv-tail-speed: %s (use >= 1)\n", optarg);
                    return 1;
                }
                break;
            case 1022:
                lead_in_feed_step = atoi(optarg);
                if (lead_in_feed_step < 0) {
                    fprintf(stderr, "Invalid lead-in-feed-step: %s (use >= 0)\n", optarg);
                    return 1;
                }
                break;
            case 1023:
                lead_in_sweep_mode = LEAD_IN_SWEEP_FULL;
                break;
            case 1024:
                if (strcmp(optarg, "off") == 0) lead_in_sweep_mode = LEAD_IN_SWEEP_OFF;
                else if (strcmp(optarg, "short") == 0) lead_in_sweep_mode = LEAD_IN_SWEEP_SHORT;
                else if (strcmp(optarg, "full") == 0) lead_in_sweep_mode = LEAD_IN_SWEEP_FULL;
                else {
                    fprintf(stderr, "Invalid lead-in-sweep: %s (use off, short, full)\n", optarg);
                    return 1;
                }
                break;
            case 1042:
                if (strcmp(optarg, "fast") == 0) single_weave = SINGLE_WEAVE_FAST;
                else if (strcmp(optarg, "dense") == 0) single_weave = SINGLE_WEAVE_DENSE;
                else if (strcmp(optarg, "ultra") == 0) single_weave = SINGLE_WEAVE_ULTRA;
                else if (strcmp(optarg, "max") == 0) single_weave = SINGLE_WEAVE_MAX;
                else {
                    fprintf(stderr, "Invalid single-weave: %s "
                            "(use fast, dense, ultra, max)\n", optarg);
                    return 1;
                }
                break;
            case 1030:
                if (strcmp(optarg, "same") == 0) z3_mode = Z3_SAME;
                else if (strcmp(optarg, "mirror") == 0) z3_mode = Z3_MIRROR;
                else if (strcmp(optarg, "image") == 0) z3_mode = Z3_IMAGE;
                else {
                    fprintf(stderr, "Invalid z3-mode: %s (use same, mirror, image)\n", optarg);
                    return 1;
                }
                break;
            case 1031:
                z3_image_path = optarg;
                break;
            case 1034:
                estimate = true;
                break;
            case 1035:
                emboss_layers = atoi(optarg);   /* validated per mode below */
                break;
            case 1036:
                spot_path = optarg;
                break;
            case 1037:
                if (strcmp(optarg, "on") == 0) underbase = true;
                else if (strcmp(optarg, "off") == 0) underbase = false;
                else { fprintf(stderr, "Invalid --underbase %s (on/off)\n", optarg); return 1; }
                break;
            case 1038:
                if (strcmp(optarg, "binary") == 0) emboss_grayscale = false;
                else if (strcmp(optarg, "grayscale") == 0) emboss_grayscale = true;
                else { fprintf(stderr, "Invalid --emboss-height %s (binary/grayscale)\n", optarg); return 1; }
                break;
            case 1039:
                if (strcmp(optarg, "single") == 0) emboss_split = false;
                else if (strcmp(optarg, "split") == 0) emboss_split = true;
                else { fprintf(stderr, "Invalid --emboss-mode %s (single/split)\n", optarg); return 1; }
                break;
            case 1040:
                emboss_white_runs = atoi(optarg);
                if (emboss_white_runs < 1) { fprintf(stderr, "Invalid --emboss-white-runs (>=1)\n"); return 1; }
                break;
            case 1041:
                emboss_color_reg = atoi(optarg);
                break;
            case 'm':
                if (strcmp(optarg, "color") == 0) print_mode = PRINT_COLOR;
                else if (strcmp(optarg, "white") == 0) print_mode = PRINT_WHITE;
                else if (strcmp(optarg, "both") == 0) print_mode = PRINT_BOTH;
                else {
                    fprintf(stderr, "Unknown mode: %s\n", optarg);
                    return 1;
                }
                break;
            case 't':
                if (sscanf(optarg, "%dx%d", &test_w, &test_h) != 2) {
                    fprintf(stderr, "Invalid test size: %s (use WxH)\n", optarg);
                    return 1;
                }
                break;
            case 's': send_device = optarg; break;
            case 'S':
                /* Accept "WxH" or just "W" (height computed from aspect) */
                if (sscanf(optarg, "%fx%f", &size_w_mm, &size_h_mm) < 1) {
                    fprintf(stderr, "Invalid size: %s (use WxH or W in mm)\n", optarg);
                    return 1;
                }
                break;
            case 'T':
                only_code = (int)strtol(optarg, NULL, 0);
                break;
            case 'X': mapping_prefix = optarg; break;
            case 1001: /* --intent */
                if (strcmp(optarg, "perceptual") == 0) {
                    intent.intent = INTENT_PERCEPTUAL; intent.flags = 0;
                } else if (strcmp(optarg, "relative") == 0) {
                    intent.intent = INTENT_RELATIVE_COLORIMETRIC; intent.flags = 0;
                } else if (strcmp(optarg, "relative-bpc") == 0) {
                    intent.intent = INTENT_RELATIVE_COLORIMETRIC;
                    intent.flags = cmsFLAGS_BLACKPOINTCOMPENSATION;
                } else if (strcmp(optarg, "saturation") == 0) {
                    intent.intent = INTENT_SATURATION; intent.flags = 0;
                } else if (strcmp(optarg, "absolute") == 0) {
                    intent.intent = INTENT_ABSOLUTE_COLORIMETRIC; intent.flags = 0;
                } else {
                    fprintf(stderr, "Unknown intent: %s\n", optarg);
                    return 1;
                }
                break;
            case 1002: icc_test = true; break;
            case 1003: chart = true; break;
            case 1004: /* --trim */
                if (parse_trim(optarg, trim) != 0)
                    return 1;
                break;
            case 1005: /* --reg-v */
                reg_v = atoi(optarg);
                if (reg_v < -64) reg_v = -64;
                if (reg_v > 64)  reg_v = 64;
                break;
            case 1006: reg_test = true; break;
            case 1007: nozzle_check = true; break;
            case 1043: clean_name = optarg; break;
            case 1044: want_status = true; break;
            case 1045: status_device = optarg; break;
            case 1046: status_watch = true; break;
            case 1047: status_serial = optarg; break;
            case 1048: want_waste = true; break;
            case 1049: eeprom_list = optarg; break;
            case 1051: want_waste_reset = true; break;
            case 1052:
                if (strcmp(optarg, "alpha") == 0) {
                    white_mode = WHITE_MODE_ALPHA;
                } else if (strcmp(optarg, "luminance") == 0) {
                    white_mode = WHITE_MODE_LUMINANCE;
                } else {
                    fprintf(stderr, "Unknown --white-mode '%s' "
                                    "(alpha | luminance)\n", optarg);
                    return 1;
                }
                break;
            case 1053:
                white_dark_pct = atoi(optarg);
                if (white_dark_pct < 0) white_dark_pct = 0;
                if (white_dark_pct > 100) white_dark_pct = 100;
                break;
            case 1055:
                white_choke_mm = (float)atof(optarg);
                if (white_choke_mm < 0.0f) white_choke_mm = 0.0f;
                if (white_choke_mm > 5.0f) white_choke_mm = 5.0f;
                break;
            case 1054:
                if (sscanf(optarg, "%d,%d", &white_curve_x, &white_curve_d) != 2) {
                    fprintf(stderr, "Bad --white-curve '%s' (expected X,D)\n",
                            optarg);
                    return 1;
                }
                break;
            case 1008: case 1009: case 1010: case 1011: {
                const int ch = opt - 1008;
                if (curve_point_set[ch]) {
                    fprintf(stderr, "Cannot combine --curve-%c and "
                                    "--curve-point-%c for the same channel\n",
                            "cmyk"[ch], "cmyk"[ch]);
                    return 1;
                }
                if (parse_curve(optarg, curve[ch]) != 0)
                    return 1;
                curve_set[ch] = true;
                break;
            }
            case 1012: chart_gray = true; break;
            case 1013: dump_dir = optarg; break;
            case 1016: break;   /* --structure: no-op, structure is the only path */
            case 1017:
                if (strcmp(optarg, "jitter") == 0) struct_dither = SD_JITTER;
                else if (strcmp(optarg, "fs") == 0) struct_dither = SD_FS;
                else if (strcmp(optarg, "bluenoise") == 0) struct_dither = SD_BLUENOISE;
                else if (strcmp(optarg, "bluejitter") == 0) struct_dither = SD_BLUEJITTER;
                else if (strcmp(optarg, "serpentine") == 0) struct_dither = SD_SERPENTINE;
                else if (strcmp(optarg, "serprandom") == 0) struct_dither = SD_SERPRANDOM;
                else if (strcmp(optarg, "serpjarvis") == 0) struct_dither = SD_SERPJARVIS;
                else if (strcmp(optarg, "serpjarvisrandom") == 0) struct_dither = SD_SERPJARVISRANDOM;
                else if (strcmp(optarg, "bluenoisehq") == 0) struct_dither = SD_BLUENOISEHQ;
                else {
                    fprintf(stderr, "Unknown struct-dither: %s (use jitter, fs, "
                                    "bluenoise, bluejitter, serpentine, serprandom, "
                                    "serpjarvis, serpjarvisrandom, bluenoisehq)\n",
                                    optarg);
                    return 1;
                }
                break;
            case 1018: case 1019: case 1020: case 1021: {
                const int ch = opt - 1018;
                if (curve_set[ch]) {
                    fprintf(stderr, "Cannot combine --curve-%c and "
                                    "--curve-point-%c for the same channel\n",
                            "cmyk"[ch], "cmyk"[ch]);
                    return 1;
                }
                if (parse_curve_point(optarg, curve_point[ch]) != 0)
                    return 1;
                curve_point_set[ch] = true;
                break;
            }
            case 'R': resolution_name = optarg; break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
            }
        }
    }

    /* ── ICC swatch diagnostic: dump RGB→CMYK table for all intents ── */
    if (icc_test) {
        if (!icc_path) {
            fprintf(stderr, "Error: --icc-test requires an ICC profile (-I)\n");
            return 1;
        }
        return run_icc_test(icc_path);
    }

    /* ── Printer status: the only path that talks to the device directly.
     * A spooled job is "done" once the bytes are handed over, which says
     * nothing about whether the printer has finished acting on them. */
    /* ── Waste-ink counters / raw EEPROM: the only D4 path ── */
    if (want_waste || eeprom_list || want_waste_reset) {
        char devbuf[256];
        const char *dev = status_device;
        if (!dev) {
            if (status_serial) {
                if (status_find_device_by_serial(status_serial, devbuf,
                                                 sizeof(devbuf)) < 0) {
                    fprintf(stderr, "No attached printer with serial %s\n",
                            status_serial);
                    return 1;
                }
            } else if (status_find_device(devbuf, sizeof(devbuf)) < 0) {
                fprintf(stderr, "No Epson printer device found under "
                                "/dev/usb/lp*.\n");
                return 1;
            }
            dev = devbuf;
        }

        d4_session_t d4;
        if (d4_open(dev, &d4) < 0)
            return 1;
        int rc = 0;

        if (eeprom_list) {
            /* Comma-separated hex addresses, for mapping work. Read-only. */
            char list[512];
            snprintf(list, sizeof(list), "%s", eeprom_list);
            for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) {
                unsigned a = 0;
                if (sscanf(tok, "%x", &a) != 1 || a > 0xff) {
                    fprintf(stderr, "Bad EEPROM address '%s'\n", tok);
                    rc = 1;
                    continue;
                }
                uint8_t v = 0;
                if (d4_read_eeprom(&d4, (uint8_t)a, &v) == 0)
                    printf("EEPROM %02X = %02X (%u)\n", a, v, v);
                else {
                    fprintf(stderr, "EEPROM %02X: no reply\n", a);
                    rc = 1;
                }
            }
        }

        if (want_waste_reset) {
            /* Staged on purpose: write one byte, read it back, and only carry
             * on if it actually changed. A write that is silently ignored
             * stops here having done nothing, instead of being repeated
             * hopefully across every address. */
            static const uint8_t addrs[2] = {0x1C, 0x1D};
            int done = 0;
            for (size_t i = 0; i < sizeof(addrs); i++) {
                uint8_t before = 0, after = 0;
                if (d4_read_eeprom(&d4, addrs[i], &before) < 0) {
                    fprintf(stderr, "Could not read %02X before writing — "
                                    "stopping.\n", addrs[i]);
                    rc = 1;
                    break;
                }
                if (before == 0) {
                    printf("RESET %02X already 0\n", addrs[i]);
                    done++;
                    continue;
                }
                if (d4_write_eeprom(&d4, addrs[i], 0) < 0 ||
                    d4_read_eeprom(&d4, addrs[i], &after) < 0) {
                    fprintf(stderr, "RESET %02X failed\n", addrs[i]);
                    rc = 1;
                    break;
                }
                if (after != 0) {
                    fprintf(stderr, "RESET %02X ignored (still %02X) — the "
                                    "printer did not accept the write; "
                                    "stopping.\n", addrs[i], after);
                    rc = 1;
                    break;
                }
                printf("RESET %02X %02X -> 00\n", addrs[i], before);
                done++;
            }
            if (done == (int)sizeof(addrs))
                printf("Waste counter 1 reset. The pad itself is NOT emptied "
                       "by this.\n");
        }

        if (want_waste) {
            /* Addresses and limit derived from WIC's own reading; see
             * docs/analisis_cleaning_wic.md §waste. */
            static const struct { const char *name; uint8_t lo, hi; int limit; }
            counters[] = {
                {"Waste counter 1", 0x1C, 0x1D, 12475},
                {"Waste counter 2", 0x4C, 0x4D, 12475},
            };
            for (size_t i = 0; i < sizeof(counters)/sizeof(counters[0]); i++) {
                uint8_t lo = 0, hi = 0;
                if (d4_read_eeprom(&d4, counters[i].lo, &lo) < 0 ||
                    d4_read_eeprom(&d4, counters[i].hi, &hi) < 0) {
                    fprintf(stderr, "%s: no reply\n", counters[i].name);
                    rc = 1;
                    continue;
                }
                int v = lo | (hi << 8);
                printf("WASTE %d %d %.2f %s\n", (int)i + 1, v, counters[i].limit
                           ? v * 100.0 / counters[i].limit : 0.0,
                       counters[i].name);
            }
        }

        d4_close(&d4);
        return rc;
    }

    if (want_status) {
        /* A serial names one specific machine; resolve it to a node up front so
         * the rest of the path is the ordinary one. */
        char serial_dev[256];
        if (status_serial) {
            if (status_find_device_by_serial(status_serial, serial_dev,
                                             sizeof(serial_dev)) < 0) {
                fprintf(stderr, "No attached printer with serial %s\n",
                        status_serial);
                return 1;
            }
            status_device = serial_dev;
        }
        if (status_watch) {
            /* Trace mode: one line per change, so a maintenance cycle can be
             * followed from the outside without guessing at its phases. */
            int prev = -2;
            for (long t = 0;; t++) {
                int c = query_status(status_device);
                if (c != prev) {
                    if (c == PRN_STATUS_DEVICE_BUSY)
                        printf("[%4ld s] STATUS -- busy (device in use)\n", t);
                    else
                        printf("[%4ld s] STATUS %02X %s\n", t, c, status_name(c));
                    fflush(stdout);
                    prev = c;
                }
                sleep(1);
            }
        }
        int code = query_status(status_device);
        if (code == PRN_STATUS_DEVICE_BUSY) {
            /* A real observation, not a failure: the device is in use, so a
             * job is on its way out right now. */
            printf("STATUS -- busy\n");
            return 0;
        }
        if (code < 0) {
            fprintf(stderr,
                    "No answer from the printer, over plain ESC/P or D4.\n");
            return 1;
        }
        printf("STATUS %02X %s\n", code, status_name(code));
        return 0;
    }

    /* ── Head cleaning: a maintenance command, not a print job ──
     * No image, no settings, no raster — just the captured service command.
     * Exits here so none of the print-path validation below applies. */
    if (clean_name) {
        int nlev = 0;
        const clean_level_t *levels = prn_clean_levels(&nlev);
        if (strcmp(clean_name, "list") == 0) {
            printf("Cleaning levels (captured from WIC — see "
                   "docs/analisis_cleaning_wic.md):\n");
            for (int i = 0; i < nlev; i++)
                printf("  %-8s ~%d s\n", levels[i].name, levels[i].seconds);
            return 0;
        }
        const clean_level_t *lvl = prn_clean_level(clean_name);
        if (!lvl) {
            fprintf(stderr, "Error: unknown cleaning level '%s'. Known:", clean_name);
            for (int i = 0; i < nlev; i++)
                fprintf(stderr, " %s", levels[i].name);
            fprintf(stderr, "\nOther levels exist in WIC but their command bytes "
                            "have not been captured, and guessing one would fire "
                            "an unknown command at the printer.\n");
            return 1;
        }
        if (!output_path) {
            fprintf(stderr, "Error: --clean requires an output file (-o)\n");
            return 1;
        }
        if (prn_generate_clean(output_path, lvl) < 0)
            return 1;
        printf("Cleaning command written: %s (level %s, ~%d s)\n",
               output_path, lvl->name, lvl->seconds);
        printf("Send it to the printer to run the cleaning — it costs ink and "
               "raises the waste-ink counter.\n");
        return 0;
    }

    if (!output_path && !mapping_prefix && !dump_dir && !estimate) {
        fprintf(stderr, "Error: output file required (-o), mapping prefix (-X) "
                        "or dump dir (--dump-cmyk)\n");
        usage(argv[0]);
        return 1;
    }

    /* For mapping-test mode, default to a 20×20mm solid block if no source given.
     * 20mm @ 360dpi H = 283 px, @ 1440dpi V = 1134 rows. */
    if (mapping_prefix && !input_path && test_w == 0) {
        test_w = 283;
        test_h = 1134;
    }

    /* Density wedge ~60×40mm: 60mm@360dpi=850px, 40mm@1440dpi=2268 rows. */
    if (chart && !input_path && test_w == 0) {
        test_w = 850;
        test_h = 2268;
    }

    /* Registration target: fixed raster size (rows == reg-v units, res-agnostic).
     * 3400 rows fits 25 marks with pitch ~130 > 2×max offset (48). */
    if (reg_test && !input_path && test_w == 0) {
        test_w = 850;
        test_h = 3400;
    }

    /* Grey-balance chart ~60×50mm: 850px @360dpi, 2835 rows @1440dpi. */
    if (chart_gray && !input_path && test_w == 0) {
        test_w = 850;
        test_h = 2835;
    }

    /* Nozzle check builds its own bands; a tiny dummy image just satisfies the
     * "input required" guard and settings assembly (its data is unused). */
    if (nozzle_check && !input_path && test_w == 0) {
        test_w = 8;
        test_h = 8;
    }

    if (!input_path && test_w == 0) {
        fprintf(stderr, "Error: input image (-i) or test pattern (-t) required\n");
        usage(argv[0]);
        return 1;
    }

    /* ── Load or generate image ────────────────────────── */
    image_t img = {0};
    /* Declared here (before any `goto cleanup`) so cleanup can always free them
     * safely: the zone-3 mirror/image source + its dithered colour planes. */
    image_t img_z3 = {0};
    uint8_t *dithered[MAX_CHANNELS] = {0};
    uint8_t *dithered_z3[MAX_CHANNELS] = {0};
    /* Emboss only: where the artwork has ink, decided from the alpha BEFORE any
     * density setting touches the white plane. */
    uint8_t *art_footprint = NULL;
    /* Brightness of the SEPARATION, captured before any density setting can
     * change it — see separation_luminance(). */
    uint8_t *sep_lum = NULL;
    uint8_t *sep_lum_z3 = NULL;

    if (test_w > 0) {
        if (chart) {
            printf("Generating density wedge %dx%d (C/M/Y/K, ICC bypassed)\n", test_w, test_h);
            image_create_wedge(&img, test_w, test_h);
        } else if (chart_gray) {
            printf("Generating grey-balance chart %dx%d (K vs C+M+Y, ICC bypassed)\n",
                   test_w, test_h);
            image_create_gray_chart(&img, test_w, test_h);
        } else if (reg_test) {
            printf("Generating registration target %dx%d (white↔colour, both-mode 2-zone)\n",
                   test_w, test_h);
            image_create_reg_target(&img, test_w, test_h);
        } else if (mapping_prefix || only_code >= 0) {
            printf("Generating solid test pattern %dx%d\n", test_w, test_h);
            image_create_solid(&img, test_w, test_h, 200);
        } else {
            printf("Generating test pattern %dx%d\n", test_w, test_h);
            image_create_test(&img, test_w, test_h);
        }
    } else {
        printf("Loading image: %s\n", input_path);
        if (image_load_rgba(input_path, icc_path, intent, &img) < 0)
            return 1;
        if (white_path) {
            if (image_load_white(white_path, &img) < 0) {
                image_free(&img);
                return 1;
            }
            /* -w replaces the alpha-derived white outright, so the mode that
             * decides how to derive it no longer applies. Say so rather than
             * letting the flag look like it did something. */
            if (white_mode != WHITE_MODE_ALPHA)
                fprintf(stderr, "Note: --white-mode ignored — the white plane "
                                "comes from %s, not from the image alpha.\n",
                        white_path);
        }
    }

    printf("Image: %dx%d pixels (source)\n", img.width, img.height);

    /* ── Auto-determine print size if -S not given (skip for test patterns) ── */
    if (size_w_mm <= 0 && test_w == 0) {
        if (img.dpi_x > 0 && img.dpi_y > 0) {
            size_w_mm = img.width  / img.dpi_x * 25.4f;
            size_h_mm = img.height / img.dpi_y * 25.4f;
            printf("Auto-size from PNG metadata: %.1fx%.1f mm (pHYs %.1f×%.1f DPI)\n",
                   size_w_mm, size_h_mm, img.dpi_x, img.dpi_y);
        } else {
            size_w_mm = img.width  / 72.0f * 25.4f;
            size_h_mm = img.height / 72.0f * 25.4f;
            fprintf(stderr,
                "Warning: PNG has no pHYs metadata. Assuming 72 DPI → %.1fx%.1f mm.\n"
                "         Use -S WxH to override with explicit print size.\n",
                size_w_mm, size_h_mm);
        }
    }

    /* ── Resolution preset (weave/raster grid + header bytes) ── */
    const resolution_t *res = resolution_lookup(resolution_name);
    if (!res) {
        fprintf(stderr, "Unknown resolution: %s (use: %s)\n",
                resolution_name, resolution_names());
        image_free(&img);
        return 1;
    }

    /* ── Resize to raster dimensions based on target mm ── */
    if (size_w_mm > 0) {
        if (size_h_mm <= 0) {
            /* Preserve source aspect ratio */
            size_h_mm = size_w_mm * (float)img.height / (float)img.width;
        }
        int raster_w = (int)(size_w_mm / 25.4f * res->h_base + 0.5f);
        int raster_h = (int)(size_h_mm / 25.4f * res->v_base + 0.5f);
        printf("Target print: %.1fx%.1f mm → raster %dx%d (%s: %dH × %dV grid)\n",
               size_w_mm, size_h_mm, raster_w, raster_h, res->name,
               res->h_base, res->v_base);
        if (image_resize(&img, raster_w, raster_h) < 0) {
            image_free(&img);
            return 1;
        }
    }

    /* --dump-cmyk stage 1: pure ICC/UCR separation, post-resize so the
     * dimensions match the PRN raster grid (comparable to a decoded PRN). */
    if (dump_dir) {
        printf("Dumping separation (sep_*) to %s/\n", dump_dir);
        if (dump_cmyk_planes(dump_dir, "sep", &img) < 0) {
            image_free(&img);
            return 1;
        }
    }

    /* Registration target must print both layers with 2-zone weave and NO
     * reg-v (the offsets are baked into the pattern, labels are absolute). */
    if (reg_test) {
        print_mode = PRINT_BOTH;
        zone_count = 2;
        reg_v = 0;
    }

    /* Grey-balance chart: colour-only (judged on white film/media). */
    if (chart_gray)
        print_mode = PRINT_COLOR;

    /* ── Setup print settings ──────────────────────────── */
    print_settings_t settings = {0};
    settings.width_px = img.width;
    settings.height_px = img.height;
    settings.dpi = DPI_H;
    settings.white_first = white_first;
    settings.bidirectional = bidi;
    settings.color_limit = color_limit;
    settings.white_limit = white_limit;
    settings.left_margin_mm = left_margin_mm;
    settings.drop_mode = drop_mode;   /* drives the structure ladder tiers */
    settings.struct_dither = struct_dither;
    settings.print_mode = print_mode;
    settings.single_weave = single_weave;
    settings.zone_count = zone_count;
    /* Zone-3 source options only apply to 3-zone both-mode; ignore otherwise. */
    if (z3_mode != Z3_SAME && (zone_count != 3 || print_mode != PRINT_BOTH)) {
        fprintf(stderr, "[z3] --z3-mode ignored (needs -Z 3 and both-mode)\n");
        z3_mode = Z3_SAME;
    }
    if (z3_mode == Z3_IMAGE && !z3_image_path) {
        fprintf(stderr, "--z3-mode image requires --z3-image PATH\n");
        return 1;
    }
    settings.z3_mode = z3_mode;
    settings.emboss_layers = emboss_layers;
    settings.underbase = underbase;
    settings.emboss_grayscale = emboss_grayscale;
    settings.emboss_split = emboss_split;
    settings.emboss_white_runs = emboss_white_runs;
    settings.emboss_color_reg = emboss_color_reg;
    if (emboss_layers) {
        int ok = emboss_split ? emboss_is_valid_split_layers(emboss_layers)
                              : emboss_is_valid_layers(emboss_layers);
        if (!ok) {
            fprintf(stderr, "Invalid --emboss %d (%s mode: %s)\n", emboss_layers,
                    emboss_split ? "split" : "single",
                    emboss_split ? "15/30/45/90" : "8/16/32");
            return 1;
        }
        if (strcmp(res->name, "1440x1440") != 0) {
            fprintf(stderr, "--emboss requires -R 1440x1440 (presets validated there)\n");
            return 1;
        }
        if (!spot_path) {
            fprintf(stderr, "--emboss requires --spot FILE (raised-area mask)\n");
            return 1;
        }
        print_mode = PRINT_BOTH;         /* emboss always lays white + colour */
        settings.print_mode = PRINT_BOTH;
    }
    settings.uv_tail_mm = uv_tail_mm;
    settings.uv_tail_speed = uv_tail_speed;
    settings.reg_v = reg_v;
    settings.lead_in_feed_step = lead_in_feed_step;
    settings.lead_in_sweep_mode = lead_in_sweep_mode;

    /* Resolution-derived raster, page, and feed-motion params. */
    settings.raster_dpi_h = res->h_base;
    settings.raster_dpi_v = res->v_base;
    settings.nozzle_spacing = res->v_base / NOZZLES_PER_HEAD;   /* 8/16/32 */
    settings.weave_mode = res->weave_mode;
    settings.pass_advance = res->pass_advance;
    settings.sep_advance = res->sep_advance;
    settings.fixed_advance = res->fixed_advance;
    settings.phase_distance = (NOZZLES_PER_HEAD / 2) * settings.nozzle_spacing; /* 45×spacing */
    settings.initial_v = res->initial_v;
    settings.page_top_units = res->page_top_units;
    settings.paper_width_units = res->paper_width_units;
    settings.escU_pV = res->escU_pV;
    settings.escU_pH = res->escU_pH;
    settings.escE = res->escE;
    memcpy(settings.escD, res->escD, 4);
    if (zone_count == 3 && print_mode == PRINT_BOTH && res->v_base != 1440)
        fprintf(stderr, "Warning: 3-zone weave is tuned for 1440 vertical; "
                        "%s may band. Use 2-zone for this resolution.\n", res->name);

    if (zone_count == 3 && print_mode != PRINT_BOTH) {
        fprintf(stderr, "Warning: -Z 3 ignored in single-layer mode (full head, no zone split)\n");
    }
    if (zone_count == 3 && white_first) {
        fprintf(stderr, "Warning: -W ignored with -Z 3 (layer order fixed: Color/White/Color)\n");
    }
    if (single_weave != SINGLE_WEAVE_FAST && print_mode == PRINT_BOTH) {
        fprintf(stderr, "Warning: --single-weave ignored in -m both "
                        "(the dual-zone weave already runs the dense pattern)\n");
        single_weave = SINGLE_WEAVE_FAST;
        settings.single_weave = SINGLE_WEAVE_FAST;
    }
    /* ultra/max reuse hand-tuned advance patterns (7/8, 4/5) that assume the
     * 1440 nozzle spacing of 16, exactly like -Z 3. */
    if ((single_weave == SINGLE_WEAVE_ULTRA || single_weave == SINGLE_WEAVE_MAX) &&
        res->v_base != 1440) {
        fprintf(stderr, "Warning: --single-weave ultra/max is tuned for "
                        "1440 vertical; %s may band. Falling back to dense.\n",
                res->name);
        single_weave = SINGLE_WEAVE_DENSE;
        settings.single_weave = SINGLE_WEAVE_DENSE;
    }
    if (print_mode != PRINT_BOTH) {
        static const char *sw_name[] = {"fast", "dense", "ultra", "max"};
        static const int   sw_dose[] = {1, 2, 3, 5};
        printf("Single-layer weave: %s (%dx UV dose, %dx print time)\n",
               sw_name[single_weave], sw_dose[single_weave], sw_dose[single_weave]);
    }

    if (channel_setup_from_string(&settings, channel_order) < 0) {
        image_free(&img);
        return 1;
    }

    if (print_mode != PRINT_BOTH) {
        int filtered_num = 0;
        channel_map_t filtered[MAX_CHANNELS];
        for (int i = 0; i < settings.num_channels; i++) {
            if ((print_mode == PRINT_COLOR && !settings.channels[i].is_white) ||
                (print_mode == PRINT_WHITE && settings.channels[i].is_white)) {
                filtered[filtered_num++] = settings.channels[i];
            }
        }
        settings.num_channels = filtered_num;
        memcpy(settings.channels, filtered, sizeof(channel_map_t) * filtered_num);
    }

    printf("Channel order: %s (%s)\n", channel_order,
           white_first ? "white first" : "color first");
    printf("Ink limits: color total=%.0f%% (cap %.0f%% TAC), white=%.0f%%\n",
           color_limit * 100, color_limit * 400, white_limit * 100);
    printf("Print mode: %s\n", bidi ? "bidirectional" : "unidirectional");
    if (left_margin_mm > 0.0f) {
        printf("Margin padding: %.1f mm\n", left_margin_mm);
    }
    if (uv_tail_mm > 0.0f) {
        printf("UV tail curing: %.1f mm (speed %dx)\n", uv_tail_mm, uv_tail_speed);
    }
    if (lead_in_feed_step > 0) {
        printf("Transparent lead-in feed step: %d units\n", lead_in_feed_step);
    } else if (lead_in_sweep_mode != LEAD_IN_SWEEP_OFF) {
        printf("Transparent lead-in feed step: auto for dummy sweep\n");
    } else {
        printf("Transparent lead-in feed step: off\n");
    }
    if (lead_in_sweep_mode != LEAD_IN_SWEEP_OFF) {
        printf("Transparent lead-in dummy sweep: %s (experimental)\n",
               lead_in_sweep_mode == LEAD_IN_SWEEP_SHORT ? "short" : "full");
    }

    /* ── Apply ink limits ──────────────────────────────── */
    int npixels = img.width * img.height;

    /* Brightness for --white-mode luminance is taken HERE: after the resize, so
     * it is per raster pixel, but before the curves, --trim, --ink-color and
     * TAC below. "White under dark" then means dark in the artwork, not merely
     * dark once the density settings are done — reading it afterwards made
     * lowering --ink-color quietly put white back, and left the GUI preview
     * permanently disagreeing with the print. */
    if (white_mode == WHITE_MODE_LUMINANCE) {
        sep_lum = malloc(npixels);
        if (!sep_lum) {
            fprintf(stderr, "Out of memory (white luminance)\n");
            goto cleanup;
        }
        separation_luminance(img.channel[IMG_CYAN], img.channel[IMG_MAGENTA],
                             img.channel[IMG_YELLOW], img.channel[IMG_BLACK],
                             npixels, sep_lum);
    }
    /* Correction curves (--curve-*): linearization applied FIRST — bends the
     * midtones only (dot gain / grey balance); endpoints stay fixed. Then trim
     * (global slope), then the TAC cap. */
    {
        const img_channel_t curve_ch[4] = {IMG_CYAN, IMG_MAGENTA, IMG_YELLOW,
                                           IMG_BLACK};
        const char *curve_nm = "CMYK";
        for (int ci = 0; ci < 4; ci++) {
            if (curve_set[ci] &&
                (curve[ci][0] || curve[ci][1] || curve[ci][2])) {
                printf("Curve %c: %+d/%+d/%+d%% at 25/50/75\n", curve_nm[ci],
                       curve[ci][0], curve[ci][1], curve[ci][2]);
                apply_channel_curve(img.channel[curve_ch[ci]], npixels,
                                    curve[ci]);
            }
            if (curve_point_set[ci] && curve_point[ci][1] != 0) {
                printf("Curve point %c: %+d%% at %d%% tone\n", curve_nm[ci],
                       curve_point[ci][1], curve_point[ci][0]);
                apply_channel_point_curve(img.channel[curve_ch[ci]], npixels,
                                          curve_point[ci][0],
                                          curve_point[ci][1]);
            }
        }
    }
    /* Per-channel trim (--trim): density calibration applied BEFORE the TAC
     * cap, so apply_total_ink_limit() keeps its balance-preserving meaning and
     * never reverses the trim (it scales all four channels by one factor). */
    if (trim[0] != 1.0f || trim[1] != 1.0f || trim[2] != 1.0f || trim[3] != 1.0f) {
        printf("Per-channel trim: C×%.2f M×%.2f Y×%.2f K×%.2f\n",
               trim[0], trim[1], trim[2], trim[3]);
        apply_channel_scale(img.channel[IMG_CYAN],    npixels, trim[0]);
        apply_channel_scale(img.channel[IMG_MAGENTA], npixels, trim[1]);
        apply_channel_scale(img.channel[IMG_YELLOW],  npixels, trim[2]);
        apply_channel_scale(img.channel[IMG_BLACK],   npixels, trim[3]);
    }
    /* Global colour ink density (--ink-color): scales all four CMYK channels by
     * one factor — like --trim applied equally. Unlike the TAC cap it touches
     * EVERY pixel (not just over-ink), so it uniformly lightens/darkens. Applied
     * before the TAC cap, which stays the final over-ink ceiling. */
    if (ink_color != 1.0f) {
        printf("Colour ink density: ×%.2f (all CMYK)\n", ink_color);
        apply_channel_scale(img.channel[IMG_CYAN],    npixels, ink_color);
        apply_channel_scale(img.channel[IMG_MAGENTA], npixels, ink_color);
        apply_channel_scale(img.channel[IMG_YELLOW],  npixels, ink_color);
        apply_channel_scale(img.channel[IMG_BLACK],   npixels, ink_color);
    }
    /* Color: total-ink (TAC) cap — balance-preserving, no hue shift. */
    apply_total_ink_limit(img.channel[IMG_CYAN], img.channel[IMG_MAGENTA],
                          img.channel[IMG_YELLOW], img.channel[IMG_BLACK],
                          npixels, color_limit);
    /* Emboss clips its relief to the artwork footprint. That is a question of
     * geometry — where is there artwork — not of density, so it has to be
     * answered from the alpha before the white plane is scaled. Reading it
     * afterwards made white density erase the relief: -L 45 leaves 114, below
     * the >127 test, and the whole spot area silently vanished. */
    if (emboss_layers) {
        art_footprint = malloc(npixels);
        if (!art_footprint) {
            fprintf(stderr, "Out of memory (emboss footprint)\n");
            goto cleanup;
        }
        for (int i = 0; i < npixels; i++)
            art_footprint[i] = img.channel[IMG_WHITE][i] > 127;
    }

    /* White base shaping, before the density scale.
     *
     * Order matters: luminance modulation reads the CMYK that will actually be
     * printed, so it has to come after trim/--ink-color/TAC. Both steps SCALE
     * the white the alpha produced rather than replacing it, so a soft edge
     * still fades out in every mode. */
    /* Choke first: it decides WHERE white goes, the rest decides how much. */
    if (white_choke_mm > 0.0f &&
        apply_white_choke(img.channel[IMG_WHITE], img.width, img.height,
                          25.4f / settings.raster_dpi_h,
                          25.4f / settings.raster_dpi_v, white_choke_mm) < 0) {
        fprintf(stderr, "Out of memory (white choke)\n");
        goto cleanup;
    }
    if (white_mode == WHITE_MODE_LUMINANCE)
        apply_white_luminance(img.channel[IMG_WHITE], sep_lum, npixels,
                              white_dark_pct);
    apply_channel_point_curve(img.channel[IMG_WHITE], npixels,
                              white_curve_x, white_curve_d);

    /* White: simple per-channel scale (single channel, no hue to shift). */
    apply_ink_limit(img.channel[IMG_WHITE], npixels, white_limit);

    /* ── Emboss / spot 3D: solid white relief + colour on top ── */
    if (emboss_layers) {
        if (!output_path && !estimate) {
            fprintf(stderr, "--emboss requires -o OUTPUT\n");
            goto cleanup;
        }
        int bpl = (img.width + 3) / 4;
        int npix = img.width * img.height;

        /* White hit-cap plane from the spot mask (white/opaque = raised). */
        image_t spot = {0};
        if (image_load_rgba(spot_path, NULL, intent, &spot) < 0) {
            fprintf(stderr, "Failed to load spot mask: %s\n", spot_path);
            goto cleanup;
        }
        if (image_resize(&spot, img.width, img.height) < 0) {
            fprintf(stderr, "Failed to resize spot mask\n");
            image_free(&spot);
            goto cleanup;
        }
        uint8_t *white_cap = malloc(npix);
        uint8_t *color_planes[MAX_CHANNELS] = {0};
        if (!white_cap) { image_free(&spot); goto cleanup; }
        const int white_hits = emboss_layers;   /* preset layers == white hits */
        /* Two mask conventions: if the PNG has transparency, the OPAQUE shape is
         * the raised area (alpha); otherwise WHITE is raised (luminance). This
         * fits "draw the shape on a transparent bg" (any colour) AND a plain
         * white-on-black mask. */
        /* Use the alpha convention only when a SIGNIFICANT part is transparent
         * (mask marks the background with transparency). A mostly-opaque image
         * with a few anti-aliased edges keeps the luminance convention so its
         * texture (e.g. a brick pattern) isn't read as one solid slab. */
        long transp = 0;
        for (int i = 0; i < npix; i++)
            if (spot.channel[IMG_WHITE][i] < 128) transp++;
        int has_alpha = (transp * 10 > (long)npix);   /* >10% transparent */
        int raised_px = 0, underbase_px = 0;
        const int floor_hits = underbase ? 4 : 0;
        /* Relief and underbase are different things and are kept apart:
         * white_cap is stacked SOLID layers (height), white_ub is an ordinary
         * dithered plane (backing). A pixel belongs to one or the other. */
        uint8_t *white_ub = floor_hits > 0 ? calloc(npix, 1) : NULL;
        if (floor_hits > 0 && !white_ub) {
            free(white_cap); image_free(&spot); goto cleanup;
        }
        for (int i = 0; i < npix; i++) {
            int sig = has_alpha ? spot.channel[IMG_WHITE][i]        /* opaque=raised */
                                : (255 - spot.channel[IMG_BLACK][i]); /* white=raised */
            int h;
            if (emboss_grayscale)
                h = (sig * white_hits + 127) / 255;   /* level → 0..N layers */
            else
                h = (sig > 127) ? white_hits : 0;     /* binary on/off */
            /* Underbase = thin white UNDER the colour, so only where the main
             * artwork has ink (opaque). No white where the artwork is
             * transparent — this also clips the relief to the print footprint,
             * so no stray white ink outside the design. */
            int color_here = art_footprint[i];   /* artwork alpha, pre-density */
            if (!color_here) h = 0;
            if (h > floor_hits) {
                white_cap[i] = (uint8_t)h;       /* raised: solid layers */
                raised_px++;
            } else {
                white_cap[i] = 0;
                if (color_here && floor_hits > 0) {
                    /* Marked here, dithered below from the shaped white plane
                     * so -L / --white-mode / -D all apply to it. */
                    white_ub[i] = 1;
                    underbase_px++;
                }
            }
        }
        if (white_ub) {
            /* Dither the underbase off the real white plane, then clear it
             * where the relief already covers or the artwork is absent. */
            uint8_t *ub_plan = calloc(npix, 1);
            if (!ub_plan) { free(white_ub); free(white_cap); image_free(&spot); goto cleanup; }
            halftone_fs_structure(img.channel[IMG_WHITE], img.width, img.height,
                                  ub_plan, floor_hits, 0, struct_dither,
                                  IMG_WHITE, drop_mode);
            for (int i = 0; i < npix; i++)
                if (!white_ub[i]) ub_plan[i] = 0;
            free(white_ub);
            white_ub = ub_plan;
        }
        printf("Spot mask: %s, %s (%.0f%% raised, %.0f%% underbase, "
               "clipped to artwork)\n",
               has_alpha ? "opaque=raised (alpha)" : "white=raised (luminance)",
               emboss_grayscale ? "grayscale height" : "binary",
               100.0 * raised_px / npix, 100.0 * underbase_px / npix);
        if (raised_px == 0)
            fprintf(stderr, "[emboss] WARNING: spot mask has NO raised area — "
                    "check it (opaque shape on transparent bg, or white on black)\n");
        image_free(&spot);

        printf("Emboss: %d layers, spot=%s, underbase=%s, mode=%s\n", emboss_layers,
               spot_path, underbase ? "on" : "off", emboss_split ? "split (Cara A)"
               : "single (Cara B)");
        if (settings.left_margin_mm <= 0.0f)
            fprintf(stderr, "[emboss] WARNING: no UV margin (-M) — the carriage "
                    "may not sweep under the UV lamp, so layers won't cure. "
                    "Set a margin (e.g. -M 10) so the lamp reaches the ink.\n");
        if (estimate)
            printf("Estimating ink usage (no PRN written)...\n");
        uint64_t total_dots[MAX_CHANNELS][4] = {{0}};

        if (emboss_split) {
            /* Cara A: K white-only runs (all 90 nozzles) + one full-quality
             * colour run, written as separate PRNs printed in sequence. */
            uint8_t *no_color[MAX_CHANNELS] = {0};
            for (int k = 1; k <= emboss_white_runs; k++) {
                char wpath[600];
                emboss_split_name(output_path ? output_path : "emboss", k,
                                  "white", wpath, sizeof wpath);
                uint64_t wd[MAX_CHANNELS][4] = {{0}};
                /* The underbase goes down once, with the first run — repeating
                 * it in every stacking run would multiply the backing. */
                int rc = prn_generate_emboss(estimate ? NULL : wpath, &settings,
                                             no_color, white_cap,
                                             k == 1 ? white_ub : NULL,
                                             k == 1 ? floor_hits : 0,
                                             bpl, img.height, wd, estimate);
                if (rc < 0) { free(white_ub); free(white_cap); goto cleanup; }
                if (!estimate) printf("SPLIT_PRN %s\n", wpath);
                for (int a = 0; a < MAX_CHANNELS; a++)
                    for (int b = 0; b < 4; b++) total_dots[a][b] += wd[a][b];
            }
            free(white_cap);

            /* Colour run: full-quality colour-only (normal weave, 45 nozzles). */
            print_settings_t cs = settings;
            cs.emboss_layers = 0; cs.emboss_split = false;
            cs.print_mode = PRINT_COLOR;
            cs.initial_v += emboss_color_reg;          /* register onto the relief */
            int cn = 0;
            channel_map_t cf[MAX_CHANNELS];
            int cf_orig[MAX_CHANNELS];   /* filtered idx → original settings idx */
            for (int i = 0; i < settings.num_channels; i++)
                if (!settings.channels[i].is_white) {
                    cf_orig[cn] = i; cf[cn++] = settings.channels[i];
                }
            cs.num_channels = cn;
            memcpy(cs.channels, cf, sizeof(channel_map_t) * cn);

            uint8_t *cdith[MAX_CHANNELS] = {0};
            for (int i = 0; i < cn; i++) {
                int hits = prn_channel_hits(&cs, &cs.channels[i]);
                cdith[i] = calloc(npix, 1);
                if (!cdith[i]) goto cleanup;
                halftone_fs_structure(img.channel[cs.channels[i].src_channel],
                                      img.width, img.height, cdith[i], hits, 0,
                                      cs.struct_dither, i, cs.drop_mode);
            }
            char cpath[600];
            emboss_split_name(output_path ? output_path : "emboss",
                              emboss_white_runs + 1, "color", cpath, sizeof cpath);
            uint64_t cd[MAX_CHANNELS][4] = {{0}};
            int rc = prn_generate(estimate ? NULL : cpath, &cs, cdith, NULL,
                                  bpl, img.height, cd, estimate);
            for (int i = 0; i < MAX_CHANNELS; i++) free(cdith[i]);
            if (rc < 0) goto cleanup;
            if (!estimate) {
                printf("SPLIT_PRN %s\n", cpath);
                printf("Emboss split: %d white run(s) + 1 colour run. Print white "
                       "first (relief), then colour; do NOT move the media.\n",
                       emboss_white_runs);
            }
            /* Map colour counts back to their original channel slots (the colour
             * run used a filtered channel list). */
            for (int j = 0; j < cn; j++)
                for (int b = 0; b < 4; b++) total_dots[cf_orig[j]][b] += cd[j][b];
            print_ink_report(&settings, total_dots);
            printf("Done!\n");
            goto cleanup;
        }

        /* Cara B: single run — colour planes at 4 hits + emboss weave. */
        for (int i = 0; i < settings.num_channels; i++) {
            if (settings.channels[i].is_white) continue;
            color_planes[i] = calloc(npix, 1);
            if (!color_planes[i]) { free(white_ub); free(white_cap); goto cleanup; }
            halftone_fs_structure(img.channel[settings.channels[i].src_channel],
                                  img.width, img.height, color_planes[i], 4, 0,
                                  settings.struct_dither, i, settings.drop_mode);
        }
        int rc = prn_generate_emboss(estimate ? NULL : output_path, &settings,
                                     color_planes, white_cap,
                                     white_ub, floor_hits, bpl, img.height,
                                     total_dots, estimate);
        free(white_ub);
        free(white_cap);
        for (int i = 0; i < MAX_CHANNELS; i++) free(color_planes[i]);
        if (rc < 0) goto cleanup;
        print_ink_report(&settings, total_dots);
        printf("Done!\n");
        goto cleanup;
    }

    /* ── Halftone dithering: 8-bit → 2-bit ────────────── */
    int bytes_per_line = (img.width + 3) / 4;  /* 2bpp, 4 pixels per byte */
    /* dithered/dithered_z3/img_z3 declared near the top (before any goto) */

    /* --dump-cmyk stage 2: post --trim/-l/-L, right before dither — the exact
     * data the halftoner will consume. Without -o the dump IS the job. */
    if (dump_dir) {
        printf("Dumping pre-dither planes (final_*) to %s/\n", dump_dir);
        if (dump_cmyk_planes(dump_dir, "final", &img) < 0)
            goto cleanup;
        if (!output_path && !mapping_prefix) {
            printf("Dump-only mode (no -o): skipping dither/PRN.\n");
            goto cleanup;
        }
    }

    /* Nozzle check: bespoke per-nozzle emission (bypasses weave/dither). Uses
     * only the resolution-derived header fields in settings. */
    if (nozzle_check) {
        printf("Generating nozzle-check PRN: %s\n", output_path);
        if (prn_generate_nozzle_check(output_path, &settings) < 0)
            fprintf(stderr, "Nozzle-check generation failed\n");
        goto cleanup;
    }

    /* ── Zone-3 source (3-zone mirror/image): a second processed image whose
     * COLOUR planes feed the trailing zone (zone 2). Same colour pipeline as
     * the front so both sides match; white is unused by zone 2. ── */
    if (settings.z3_mode != Z3_SAME) {
        if (settings.z3_mode == Z3_MIRROR) {
            if (image_flip_h_copy(&img, &img_z3) < 0) {
                fprintf(stderr, "Out of memory (zone-3 mirror)\n");
                goto cleanup;
            }
            printf("Zone-3: horizontal mirror of front image\n");
        } else { /* Z3_IMAGE — load + resize + run the same colour pipeline */
            if (image_load_rgba(z3_image_path, icc_path, intent, &img_z3) < 0) {
                fprintf(stderr, "Failed to load zone-3 image: %s\n", z3_image_path);
                goto cleanup;
            }
            if (image_resize(&img_z3, img.width, img.height) < 0) {
                fprintf(stderr, "Failed to resize zone-3 image\n");
                goto cleanup;
            }
            int np3 = img_z3.width * img_z3.height;
            /* Same rule as the front image: brightness comes from the
             * separation, before the density settings below touch it. */
            if (white_mode == WHITE_MODE_LUMINANCE) {
                sep_lum_z3 = malloc(np3);
                if (!sep_lum_z3) {
                    fprintf(stderr, "Out of memory (zone-3 white luminance)\n");
                    goto cleanup;
                }
                separation_luminance(img_z3.channel[IMG_CYAN],
                                     img_z3.channel[IMG_MAGENTA],
                                     img_z3.channel[IMG_YELLOW],
                                     img_z3.channel[IMG_BLACK], np3, sep_lum_z3);
            }
            const img_channel_t cc[4] = {IMG_CYAN, IMG_MAGENTA, IMG_YELLOW,
                                         IMG_BLACK};
            for (int ci = 0; ci < 4; ci++) {
                if (curve_set[ci] &&
                    (curve[ci][0] || curve[ci][1] || curve[ci][2]))
                    apply_channel_curve(img_z3.channel[cc[ci]], np3, curve[ci]);
                if (curve_point_set[ci] && curve_point[ci][1] != 0)
                    apply_channel_point_curve(img_z3.channel[cc[ci]], np3,
                                              curve_point[ci][0],
                                              curve_point[ci][1]);
            }
            if (trim[0] != 1.0f || trim[1] != 1.0f ||
                trim[2] != 1.0f || trim[3] != 1.0f) {
                apply_channel_scale(img_z3.channel[IMG_CYAN],    np3, trim[0]);
                apply_channel_scale(img_z3.channel[IMG_MAGENTA], np3, trim[1]);
                apply_channel_scale(img_z3.channel[IMG_YELLOW],  np3, trim[2]);
                apply_channel_scale(img_z3.channel[IMG_BLACK],   np3, trim[3]);
            }
            if (ink_color != 1.0f) {
                apply_channel_scale(img_z3.channel[IMG_CYAN],    np3, ink_color);
                apply_channel_scale(img_z3.channel[IMG_MAGENTA], np3, ink_color);
                apply_channel_scale(img_z3.channel[IMG_YELLOW],  np3, ink_color);
                apply_channel_scale(img_z3.channel[IMG_BLACK],   np3, ink_color);
            }
            apply_total_ink_limit(img_z3.channel[IMG_CYAN],
                                  img_z3.channel[IMG_MAGENTA],
                                  img_z3.channel[IMG_YELLOW],
                                  img_z3.channel[IMG_BLACK], np3, color_limit);
            if (white_choke_mm > 0.0f &&
                apply_white_choke(img_z3.channel[IMG_WHITE], img_z3.width,
                                  img_z3.height, 25.4f / settings.raster_dpi_h,
                                  25.4f / settings.raster_dpi_v,
                                  white_choke_mm) < 0) {
                fprintf(stderr, "Out of memory (zone-3 white choke)\n");
                goto cleanup;
            }
            if (white_mode == WHITE_MODE_LUMINANCE)
                apply_white_luminance(img_z3.channel[IMG_WHITE], sep_lum_z3,
                                      np3, white_dark_pct);
            apply_channel_point_curve(img_z3.channel[IMG_WHITE], np3,
                                      white_curve_x, white_curve_d);
            apply_ink_limit(img_z3.channel[IMG_WHITE], np3, white_limit);
            printf("Zone-3: separate image %s\n", z3_image_path);
        }
    }

    printf("Dithering %d channels... (structure: deposit-plan planes)\n",
           settings.num_channels);
    for (int i = 0; i < settings.num_channels; i++) {
        /* Dithering the whole raster is the heavy pre-pass step; drive the GUI
         * bar across its first quarter so it starts moving instead of sitting
         * indeterminate. The pass loop then covers 25..100 (see prog_base). */
        if (!estimate) {
            printf("PROGRESS %d\n", i * DITHER_PROGRESS_SPAN / settings.num_channels);
            fflush(stdout);
        }
        const channel_map_t *ch = &settings.channels[i];
        int hits = prn_channel_hits(&settings, ch);
        size_t dith_size = (size_t)img.width * img.height;
        dithered[i] = calloc(dith_size, 1);
        if (!dithered[i]) {
            fprintf(stderr, "Out of memory\n");
            goto cleanup;
        }

        /* Channels fed by the same image channel (W1/W2 both read
         * IMG_WHITE) produce identical dithers — copy instead of redoing
         * the diffusion.  The plane scale depends on the channel's hit
         * count, so sharing also requires equal hits. */
        int dup = -1;
        for (int j = 0; j < i; j++) {
            if (settings.channels[j].src_channel == ch->src_channel &&
                prn_channel_hits(&settings, &settings.channels[j]) == hits) {
                dup = j;
                break;
            }
        }

        if (dup >= 0) {
            memcpy(dithered[i], dithered[dup], dith_size);
            printf("  Channel %d (%s, code 0x%02x): dithered (shared with channel %d)\n",
                   i, ch->name, ch->color_code, dup);
        } else {
            /* No spatial mask: cross-mode density parity is automatic
             * (single-layer = 8 hits on the even rows the weave prints
             * = both-mode's 4 hits on all rows), matching AcroRIP's own
             * measured behaviour (its 50%-empty pixels are simply the
             * single-layer odd rows; no checkerboard/dash mask exists). */
            halftone_fs_structure(img.channel[ch->src_channel],
                                  img.width, img.height, dithered[i], hits, 0,
                                  settings.struct_dither, i, settings.drop_mode);
            printf("  Channel %d (%s, code 0x%02x): structure-dithered "
                   "(%d hits, %s-ladder, %s)\n",
                   i, ch->name, ch->color_code, hits,
                   drop_mode_name(settings.drop_mode),
                   struct_dither_name(settings.struct_dither));
        }

        /* Zone-3 (mirror/image): dither the trailing zone's own source for
         * colour channels. Same hit count as dithered[i] (single-zone 4 here,
         * because prn_channel_hits splits colour zones when z3_mode != SAME). */
        if (settings.z3_mode != Z3_SAME && !ch->is_white) {
            dithered_z3[i] = calloc(dith_size, 1);
            if (!dithered_z3[i]) {
                fprintf(stderr, "Out of memory (zone-3 dither)\n");
                goto cleanup;
            }
            const uint8_t *s3 = img_z3.channel[ch->src_channel];
            halftone_fs_structure(s3, img.width, img.height,
                                  dithered_z3[i], hits, 0,
                                  settings.struct_dither, i, settings.drop_mode);
        }
    }

    /* ── Vertical Split Information ───────────────────── */
    if (settings.zone_count == 3 && print_mode == PRINT_BOTH) {
        printf("Vertical Zone Split (3 zones): Nozzle 60-89 (Color, leading), "
               "Nozzle 30-59 (White), Nozzle 0-29 (Color duplicate, trailing)\n");
        printf("Zone step: %d units (%.1f mm), weave 7/8 alternating (96 passes/phase)\n",
               (NOZZLES_PER_HEAD / 3) * settings.nozzle_spacing,
               (float)((NOZZLES_PER_HEAD / 3) * settings.nozzle_spacing)
                   * 25.4f / settings.raster_dpi_v);
    } else {
        if (settings.white_first) {
            printf("Vertical Zone Split: Nozzle 45-89 (White, leading), Nozzle 0-44 (Color, trailing)\n");
        } else {
            printf("Vertical Zone Split: Nozzle 45-89 (Color, leading), Nozzle 0-44 (White, trailing)\n");
        }
        printf("Zone offset: %d units (%.1f mm)\n",
               settings.phase_distance,
               (float)settings.phase_distance * 25.4f / settings.raster_dpi_v);
    }

    int effective_height = img.height;

    /* ── Mapping test mode: emit 6 PRNs, one per color code ── */
    if (mapping_prefix) {
        static const uint8_t test_codes[6] = {
            CC_BLACK, CC_CYAN, CC_MAGENTA, CC_YELLOW, CC_LIGHT_CYAN, CC_LIGHT_MAG
        };
        /* Use channel 0's dithered data as source (solid fill, any channel works). */
        uint8_t *src_dither = dithered[0];

        for (int k = 0; k < 6; k++) {
            print_settings_t ts = settings;
            ts.num_channels = 1;
            ts.channels[0].color_code = test_codes[k];
            ts.channels[0].is_white = false;
            ts.channels[0].src_channel = IMG_BLACK;
            ts.channels[0].name = "X";

            uint8_t *td[MAX_CHANNELS] = {0};
            td[0] = src_dither;

            char path[512];
            snprintf(path, sizeof(path), "%s_%02x.prn",
                     mapping_prefix, test_codes[k]);
            printf("Generating mapping-test PRN: %s (code 0x%02x)\n",
                   path, test_codes[k]);
            if (prn_generate(path, &ts, td, NULL, bytes_per_line, effective_height, NULL, false) < 0) {
                fprintf(stderr, "PRN generation failed for 0x%02x\n",
                        test_codes[k]);
                goto cleanup;
            }
        }
        printf("Mapping test: 6 PRNs written with prefix '%s'\n",
               mapping_prefix);
        printf("Print each one, note which ink comes out, then update slot_codes.\n");
        goto cleanup;
    }

    /* ── Single-code override (-T) ───────────────────────── */
    if (only_code >= 0) {
        printf("Only-code mode: emitting PRN with single color code 0x%02x\n",
               only_code);
        settings.num_channels = 1;
        settings.channels[0].color_code = (uint8_t)only_code;
        settings.channels[0].is_white = false;
        settings.channels[0].src_channel = IMG_BLACK;
        settings.channels[0].name = "X";
        /* Point dithered[0] at any existing dither (solid, so any channel OK) */
    }

    /* Fit the UV-tail sweep to the trailing content (narrowing shape → narrow
     * sweep); stays full-width if the image is blank. */
    if (settings.uv_tail_mm > 0.0f) {
        int tail_rows = (int)(settings.uv_tail_mm / 25.4f
                              * settings.raster_dpi_v + 0.5f);
        fit_uv_tail_to_content(&img, tail_rows,
                               &settings.uv_tail_col_lo, &settings.uv_tail_col_hi);
        if (settings.uv_tail_col_hi > settings.uv_tail_col_lo)
            printf("UV tail sweep fitted to trailing content: cols %d..%d of %d\n",
                   settings.uv_tail_col_lo, settings.uv_tail_col_hi, img.width);
    }

    /* ── Generate PRN (or ink estimate) ─────────────────── */
    uint64_t ink_dots[MAX_CHANNELS][4] = {{0}};
    uint8_t **z3src = settings.z3_mode != Z3_SAME ? dithered_z3 : NULL;
    if (estimate) {
        printf("Estimating ink usage (no PRN written)...\n");
        if (prn_generate(NULL, &settings, dithered, z3src,
                         bytes_per_line, effective_height, ink_dots, true) < 0) {
            fprintf(stderr, "Ink estimate failed\n");
            goto cleanup;
        }
        print_ink_report(&settings, ink_dots);
        printf("Done!\n");
        goto cleanup;
    }
    printf("Generating PRN: %s\n", output_path);
    settings.prog_base = DITHER_PROGRESS_SPAN;   /* pass loop drives 25..100 */
    settings.prog_span = 100 - DITHER_PROGRESS_SPAN;
    if (prn_generate(output_path, &settings, dithered, z3src,
                     bytes_per_line, effective_height, ink_dots, false) < 0) {
        fprintf(stderr, "PRN generation failed\n");
        goto cleanup;
    }

    printf("PRN written: %s\n", output_path);
    print_ink_report(&settings, ink_dots);

    /* ── Send to printer ───────────────────────────────── */
    if (send_device) {
        printf("Sending to printer: %s\n", send_device);
        FILE *prn = fopen(output_path, "rb");
        FILE *dev = fopen(send_device, "wb");
        if (!prn || !dev) {
            perror("Failed to open file for sending");
            if (prn) fclose(prn);
            if (dev) fclose(dev);
            goto cleanup;
        }
        uint8_t buf[4096];
        size_t n;
        size_t total = 0;
        while ((n = fread(buf, 1, sizeof(buf), prn)) > 0) {
            fwrite(buf, 1, n, dev);
            total += n;
        }
        fclose(prn);
        fclose(dev);
        printf("Sent %zu bytes to %s\n", total, send_device);
    }

    printf("Done!\n");

cleanup:
    for (int i = 0; i < MAX_CHANNELS; i++) {
        free(dithered[i]);
        free(dithered_z3[i]);
    }
    free(art_footprint);
    free(sep_lum);
    free(sep_lum_z3);
    image_free(&img);
    image_free(&img_z3);
    return 0;
}
