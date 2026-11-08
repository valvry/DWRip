/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QString>
#include <functional>

class QWidget;
struct JobConfig;

// Shared dwrip-invocation helpers (used by Export and the Ink/Trim chart test).

// Locate the dwrip binary built at the repo root (`make`). Empty if not found.
QString locateDwrip();

// Run dwrip(cfg) in a modal dialog that streams the log and shows ✓/✗ on finish.
// Warns (and returns) if the binary is missing. onFinished(exitCode) runs after
// the process ends (e.g. to clean up a temp file). Blocks until the dialog closes.
void runDwripWithLog(QWidget *parent, const JobConfig &cfg,
                     std::function<void(int)> onFinished = {});

// Ask the printer what it is doing, via `dwrip --status`.
//
// Needed because a spooled job counts as finished the moment CUPS hands the
// bytes over, which says nothing about the printer: a head cleaning runs for
// another minute after that. Returns the status code (0x04 = ready, 0x07 =
// cleaning, …), -2 when the device is held by another program (CUPS spooling),
// or -1 when the printer cannot be queried at all — no device, no permission, or
// a model that does not answer. `name` receives the human label.
int dwripPrinterStatus(QString *name = nullptr,
                       const QString &usbSerial = QString());

// Block in a modal dialog until a just-spooled maintenance job has actually
// finished on the printer: first wait for it to leave the ready state, then
// wait for it to come back. Returns quietly (and fast) when status cannot be
// read, so a printer that will not answer never traps the user.
void waitWhilePrinterBusy(QWidget *parent, const QString &title,
                          const QString &usbSerial = QString());

// One waste-ink counter as reported by `dwrip --waste`.
struct WasteCounter {
    QString name;
    int value = 0;
    double percent = 0;
};

// Read the printer's waste-ink counters over IEEE-1284.4. Manual only — this
// leaves the printer in D4 mode, where plain ESC/P goes unanswered until the
// printer is power-cycled (printing is unaffected, and the status reader falls
// back to D4, so nothing actually breaks). Empty list on failure.
QList<WasteCounter> dwripReadWaste(const QString &usbSerial = QString());

// Reset waste-ink counter 1 to zero. IRREVERSIBLE — it writes the printer's
// EEPROM. Writes one byte, reads it back, and stops if the value did not
// change, so a refused write costs nothing. Resetting does NOT empty the pad.
// Returns true only if the printer confirms the new value by read-back.
bool dwripResetWaste(const QString &usbSerial, QString *log = nullptr);
