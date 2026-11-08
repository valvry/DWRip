/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QString>

// The scene's canonical unit is always millimetre (1 scene unit == 1 mm),
// matching dwrip's CLI contract. "Unit" is only a display layer for rulers and
// the status bar.
enum class Unit { Mm, Cm };

inline double unitMm(Unit u)          { return u == Unit::Cm ? 10.0 : 1.0; }
inline const char *unitSuffix(Unit u) { return u == Unit::Cm ? "cm" : "mm"; }

// Format a millimetre value into the active display unit, e.g. 305.0 mm -> "30.5 cm".
inline QString fmtUnit(double valueMm, Unit u, int prec = 1)
{
    return QString::number(valueMm / unitMm(u), 'f', prec) + " " + unitSuffix(u);
}
