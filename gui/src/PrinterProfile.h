/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QList>

// ── Printer profiles ─────────────────────────────────────────────────────────
//
// Everything that belongs to ONE physical machine lives in its own QSettings
// group, so the app can drive several printers without re-calibrating each time:
//
//   printers/schema  = storage version (guards the one-shot legacy migration)
//   printers/list    = QStringList of ids, in display order
//   printers/active  = id of the profile currently in use
//   printers/p1/name = "L1800 #1"
//   printers/p1/…    = device, ink layout, ICC, density calibration, UV, mechanics
//
// Ids are opaque ("p1", "p2", …) rather than slugs of the name, so renaming a
// profile can never break the active-profile pointer.
//
// What is deliberately NOT here: bed size and display unit (app/), ink prices
// (inkcost/), and the per-job export choices (export/). See the plan file.

struct PrinterMeta {
    QString id;
    QString name;
};

// All profiles in display order. Never empty after printersMigrateLegacy().
QList<PrinterMeta> printersLoad();

QString printerName(const QString &id);
void    printerRename(const QString &id, const QString &name);

// QSettings group prefix for a profile, e.g. "printers/p1" — the string every
// settings reader/writer should use instead of the old flat "dwrip" group.
QString printerGroup(const QString &id);

QString activePrinterId();          // falls back to the first profile
void    setActivePrinter(const QString &id);
// Convenience: printerGroup(activePrinterId()).
QString activePrinterGroup();

// Create an empty profile (engine defaults) or a full copy of an existing one.
// Both return the new id and append it to printers/list.
QString printerCreate(const QString &name);
QString printerDuplicate(const QString &srcId, const QString &newName);

// Removes the profile and its settings. Refuses to delete the last one (a
// profile must always exist), returning false.
bool printerRemove(const QString &id);

// One-shot upgrade from the pre-profile layout: copies the flat "dwrip/" group
// into a first profile and resolves the active channel-profile NAME into that
// profile's channelOrder. Idempotent (guarded by printers/schema) and
// non-destructive — the old "dwrip/" group is left in place as a safety net,
// and still acts as a read fallback for any key a profile lacks.
void printersMigrateLegacy();

// ── Portable single-profile files ────────────────────────────────────────────
//
// Export writes a small .ini next to a COPY of the ICC profile:
//     MyPrinter.dwrip-profile   (settings + the icc file's basename)
//     MyPrinter.icm             (only when the profile has an ICC)
// The absolute ICC path is deliberately not carried across machines; import
// copies the sibling .icm into a managed folder and repoints the profile at it.
//
// Import NEVER overwrites an existing profile — it always appends a new one
// (de-duplicating the name), so pulling in a colleague's file can't destroy a
// calibration. Returns the new profile's id, or an empty string on failure.
bool    printerExport(const QString &id, const QString &path, QString *error);
QString printerImport(const QString &path, QString *error, QString *warning);

// The keys a profile owns. Exposed so the migration and the duplicate helper
// stay in sync with the settings readers automatically.
QStringList printerSettingKeys();

// ── Per-profile settings access ──────────────────────────────────────────────
// Every reader and writer of a printer setting should go through these three
// so the group prefix and the legacy fallback rule live in exactly one place.
//
// Reads resolve   printers/<active>/<key>  →  dwrip/<key>  →  def
// (the legacy leg keeps a pre-migration install working even if a key was
//  added to printerSettingKeys() after the migration ran).
// Writes ALWAYS target the active profile; nothing writes back to "dwrip/".
QVariant printerValue(const QString &key, const QVariant &def = QVariant());
void     printerSetValue(const QString &key, const QVariant &value);
bool     printerContains(const QString &key);
