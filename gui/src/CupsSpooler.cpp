/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "CupsSpooler.h"
#include <QProcess>
#include <QSet>

// Run a short-lived command, capture merged output. Returns exit code, or -1 if
// it never started / timed out. CUPS CLI calls are effectively instant.
static int runCmd(const QString &program, const QStringList &args,
                  QString *out, int timeoutMs = 5000)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(timeoutMs)) {
        if (out) *out = program + ": not found";
        return -1;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        if (out) *out = program + ": timed out";
        return -1;
    }
    if (out) *out = QString::fromLocal8Bit(p.readAll());
    return p.exitCode();
}

QStringList cupsListPrinters()
{
    QStringList names;
    QString out;

    // `lpstat -e` prints one destination name per line.
    if (runCmd("lpstat", {"-e"}, &out) == 0) {
        for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
            const QString n = line.trimmed();
            if (!n.isEmpty())
                names << n;
        }
    }

    // Fallback: `lpstat -a` → "<name> accepting requests since ...".
    if (names.isEmpty() && runCmd("lpstat", {"-a"}, &out) == 0) {
        for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
            const QString n = line.section(' ', 0, 0).trimmed();
            if (!n.isEmpty())
                names << n;
        }
    }

    names.removeDuplicates();
    return names;
}

bool cupsSpool(const QString &queue, const QString &prnPath,
               QString *jobId, QString *err)
{
    if (queue.isEmpty()) {
        if (err) *err = "No CUPS queue selected";
        return false;
    }
    QString out;
    const int code = runCmd("lp", {"-d", queue, "-o", "raw", prnPath}, &out, 15000);
    if (code != 0) {
        if (err) *err = out.trimmed().isEmpty()
                            ? QString("lp failed (exit %1)").arg(code)
                            : out.trimmed();
        return false;
    }
    // Output: "request id is <queue>-<n> (1 file(s))".
    if (jobId) {
        const int idx = out.indexOf("request id is");
        if (idx >= 0)
            *jobId = out.mid(idx + 13).trimmed().section(' ', 0, 0);
    }
    return true;
}

// Scan `lpstat -p` to learn which job is on the head and which queues are
// stopped. CUPS phrasing: "printer X now printing X-7." / "printer X disabled".
static void cupsPrinterStates(QSet<QString> *nowPrinting, QSet<QString> *stopped)
{
    QString out;
    if (runCmd("lpstat", {"-p"}, &out) != 0)
        return;
    for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
        const int p = line.indexOf("now printing ");
        if (p >= 0) {
            QString id = line.mid(p + 13).trimmed().section(' ', 0, 0);
            if (id.endsWith('.')) id.chop(1);   // trailing period
            if (!id.isEmpty()) nowPrinting->insert(id);
        } else if (line.contains("disabled") || line.contains("is stopped")) {
            const QString name = line.section(' ', 1, 1);   // "printer <name> ..."
            if (!name.isEmpty()) stopped->insert(name);
        }
    }
}

// Strip the trailing "-<number>" from a job id to get its queue name.
// The queue name itself may contain '-' (e.g. "L1800-Series-7" → "L1800-Series").
static QString queueOfJob(const QString &jobId)
{
    const int dash = jobId.lastIndexOf('-');
    return dash > 0 ? jobId.left(dash) : jobId;
}

QList<CupsJob> cupsListJobs()
{
    QList<CupsJob> jobs;
    QString out;
    if (runCmd("lpstat", {"-o"}, &out) != 0)
        return jobs;

    QSet<QString> nowPrinting, stopped;
    cupsPrinterStates(&nowPrinting, &stopped);

    // Lines: "<id>  <user>  <size>  <date/time...>".
    for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
        const QStringList f = line.simplified().split(' ', Qt::SkipEmptyParts);
        if (f.size() < 3)
            continue;
        CupsJob j;
        j.id   = f.at(0);
        j.user = f.at(1);
        j.size = f.at(2);
        if (f.size() > 3)
            j.when = QStringList(f.mid(3)).join(' ');

        if (nowPrinting.contains(j.id))
            j.state = "Processing";
        else if (stopped.contains(queueOfJob(j.id)))
            j.state = "Held";
        else
            j.state = "Pending";

        jobs << j;
    }
    return jobs;
}

bool cupsCancel(const QString &jobId, QString *err)
{
    if (jobId.isEmpty())
        return false;
    QString out;
    const int code = runCmd("cancel", {jobId}, &out);
    if (code != 0) {
        if (err) *err = out.trimmed().isEmpty()
                            ? QString("cancel failed (exit %1)").arg(code)
                            : out.trimmed();
        return false;
    }
    return true;
}

QString cupsDeviceUri(const QString &queue)
{
    if (queue.isEmpty())
        return {};
    QString out;
    if (runCmd("lpstat", {"-v", queue}, &out) != 0)
        return {};
    // "device for L1800-Series: usb://EPSON/L1800%20Series?serial=EXAMPLE_SERIAL"
    const int colon = out.indexOf(": ");
    if (colon < 0)
        return {};
    return out.mid(colon + 2).trimmed();
}

QString cupsUsbSerial(const QString &queue)
{
    const QString uri = cupsDeviceUri(queue);
    if (!uri.startsWith("usb:"))
        return {};                       // not local hardware
    const int i = uri.indexOf("serial=");
    if (i < 0)
        return {};
    QString sn = uri.mid(i + 7);
    const int amp = sn.indexOf('&');
    if (amp >= 0)
        sn = sn.left(amp);
    return sn.trimmed();
}
