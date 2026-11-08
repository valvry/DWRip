/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QString>
#include <optional>

// JobConfig mirrors the dwrip CLI contract 1:1 (see src/main.c getopt_long table).
// This is the decoupling seam: the GUI never links dwrip's C code, it only fills
// this struct and DwripRunner translates it into argv for `./dwrip`.
//
// To support a new dwrip flag: add a field here, map it in DwripRunner::buildArgs(),
// and add a widget that sets it. Nothing else in the GUI needs to know about dwrip.
struct JobConfig {
    // Core I/O
    QString inputPath;                 // -i  (the composited bed PNG, set at export)
    QString whitePath;                 // -w  separate white channel PNG (optional)
    QString outputPath;                // -o  output .prn

    // Channels / quality
    QString channels = "YKWWMC";       // -c  YKWWMC | WWYKMC | CMYKWW
    QString resolution = "1440x1440";  // -R  720x360|720x720|1440x720|1440x1440|2880x2880
    QString dropMode = "ML";           // -D  SML|SM|ML|S|M|L  ladder dot sizes
    std::optional<int> inkColorPct;    // --ink-color  global CMYK density scale, 0..200
    std::optional<int> colorLimitPct;  // -l  total CMYK TAC cap, 0..100
    std::optional<int> whiteLimitPct;  // -L  white density scale, 0..100
    QString whiteMode;                 // --white-mode  alpha | luminance
    std::optional<int> whiteDarkPct;   // --white-dark-pct  white under darkest colour
    std::optional<int> whiteCurveX;    // --white-curve X,D  alpha→white ramp
    std::optional<int> whiteCurveDelta;
    std::optional<double> whiteChokeMm;  // --white-choke  shrink white inward
    QString trim;                      // --trim  per-channel density e.g. "C:-12,Y:-5"
    QString curveC, curveM, curveY, curveK;  // --curve-*  "d25,d50,d75" (empty = off)
    QString curvePointC, curvePointM, curvePointY, curvePointK; // --curve-point-* "X,D"
    QString structDitherMethod;        // --struct-dither  dot dispersion (empty=default)
    bool chart = false;                // --chart  per-channel density wedge (no input)
    bool chartGray = false;            // --chart-gray  K vs C+M+Y grey-balance chart
    bool regTest = false;              // --reg-test  white↔colour registration target (no input)
    bool nozzleCheck = false;          // --nozzle-check  per-nozzle staircase clog test (no input)
    QString cleanLevel;                // --clean LEVEL   head cleaning (no input, no raster)
    std::optional<int> regV;           // --reg-v  colour-vs-white vertical reg offset (rows)

    // Print mechanics
    bool whiteFirst = false;           // -W
    bool bidi = true;                  // -b / -u (false => --unidi)
    QString mode = "both";             // -m  both | color | white
    QString singleWeave;               // --single-weave fast|dense (single layer;
                                       // empty = fast, the engine default)
    int zones = 2;                     // -Z  2 | 3
    QString z3Mode;                    // --z3-mode  same|mirror|image (empty=same)
    QString z3ImagePath;               // --z3-image  PNG for z3Mode==image
    bool estimate = false;             // --estimate  tally ink, print report, no PRN
    int embossLayers = 0;              // --emboss  0=off, 8/16/32 white-relief preset
    QString spotPath;                  // --spot  raised-area mask PNG (emboss)
    bool underbase = false;            // --underbase  thin white under colour (emboss)
    bool embossGrayscale = false;      // --emboss-height grayscale  variable height
    bool embossSplit = false;          // --emboss-mode split  (Cara A: separate runs)
    int embossWhiteRuns = 1;           // --emboss-white-runs  K (>90 layers)
    int embossColorReg = 0;            // --emboss-color-reg  colour-run feed offset

    // Size & margins (size_mm drives -S WxH; computed from bed layout at export)
    std::optional<double> sizeWmm;     // -S  width  in mm
    std::optional<double> sizeHmm;     // -S  height in mm
    std::optional<double> marginMm;    // -M  left margin (mm)
    std::optional<double> uvTailMm;    // -U  UV tail curing (mm)
    std::optional<int> uvTailSpeed;    // --uv-tail-speed (>=1)
    std::optional<int> leadInFeedStep; // --lead-in-feed-step (0=off, default 45)
    QString leadInSweep;               // --lead-in-sweep off|short|full (empty=off)

    // ICC
    QString iccPath;                   // -I
    QString intent;                    // --intent perceptual|relative|relative-bpc|saturation|absolute

    // Direct send (optional)
    QString sendDevice;                // -s  e.g. /dev/usb/lp0 (CLI only)

    // CUPS spool (GUI print path): the PRN is generated first, then handed to
    // `lp -d <cupsQueue> -o raw`. Empty = generate file only (no spool).
    QString cupsQueue;                 // CUPS printer/queue name, e.g. "L1800"
};
