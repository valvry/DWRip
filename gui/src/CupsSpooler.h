/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// Thin wrappers around the CUPS command-line tools (lp / lpstat / cancel).
// All calls are synchronous but instantaneous — they only talk to the local
// cupsd, which owns the actual print queue, so the GUI never blocks on the
// physical printer (that is the whole point of spooling).

// Available CUPS destinations (queues). From `lpstat -e` (fallback `lpstat -a`).
QStringList cupsListPrinters();

// Backend URI of a queue, from `lpstat -v` — e.g.
//     usb://EPSON/L1800%20Series?serial=EXAMPLE_SERIAL
//     smb://PRINTSERVER/PRINTER_QUEUE
// Empty when the queue is unknown. The scheme is what tells local hardware
// (usb:) apart from a printer that lives on another machine (smb:, ipp:, …).
QString cupsDeviceUri(const QString &queue);

// The USB serial from a queue's device URI, or empty when the queue is not a
// local USB printer. This is the bridge between CUPS's world (queue names) and
// the kernel's (/dev/usb/lpN) — the same string appears in sysfs.
QString cupsUsbSerial(const QString &queue);

// Hand a printer-ready PRN to CUPS: `lp -d <queue> -o raw <prnPath>`.
// `-o raw` is mandatory — the PRN is already ESC/P Raster, CUPS must not filter
// it. On success returns true and (if non-null) fills *jobId with e.g. "L1800-7".
// On failure returns false and (if non-null) fills *err with a message.
bool cupsSpool(const QString &queue, const QString &prnPath,
               QString *jobId = nullptr, QString *err = nullptr);

struct CupsJob {
    QString id;      // e.g. "L1800-7"
    QString user;
    QString size;    // human string as reported by lpstat
    QString when;    // submission time text
    QString state;   // "Processing" | "Pending" | "Held" (derived from lpstat -p)
};

// Active jobs across all queues, from `lpstat -o`.
QList<CupsJob> cupsListJobs();

// Cancel a single job by id: `cancel <jobId>`.
bool cupsCancel(const QString &jobId, QString *err = nullptr);
