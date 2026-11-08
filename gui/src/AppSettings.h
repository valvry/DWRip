/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include "Units.h"
#include <QSettings>

// In-memory application settings (canvas tier), persisted via QSettings group "app/".
struct AppSettings {
    double bedWmm = 300.0;     // 30 cm
    double bedHmm = 450.0;     // 45 cm
    Unit   unit   = Unit::Cm;  // default display unit

    void load() {
        QSettings s;
        s.beginGroup("app");
        bedWmm = s.value("bedWmm", bedWmm).toDouble();
        bedHmm = s.value("bedHmm", bedHmm).toDouble();
        unit   = s.value("unit", int(unit)).toInt() == int(Unit::Mm) ? Unit::Mm : Unit::Cm;
        s.endGroup();
    }

    void save() const {
        QSettings s;
        s.beginGroup("app");
        s.setValue("bedWmm", bedWmm);
        s.setValue("bedHmm", bedHmm);
        s.setValue("unit", int(unit));
        s.endGroup();
    }
};
