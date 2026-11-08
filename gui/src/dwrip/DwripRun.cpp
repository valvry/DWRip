/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "dwrip/DwripRun.h"
#include "dwrip/DwripRunner.h"
#include "dwrip/JobConfig.h"
#include "CupsSpooler.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QSharedPointer>
#include <QStringList>
#include <QProcess>
#include <QRegularExpression>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QApplication>

QString locateDwrip()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/../../../dwrip",   // gui/build/ -> repo root
        appDir + "/../../dwrip",
        appDir + "/dwrip",
        "dwrip",                      // PATH / CWD
        "./dwrip",
    };
    for (const QString &c : candidates) {
        QFileInfo fi(c);
        if (fi.exists() && fi.isExecutable())
            return fi.absoluteFilePath();
    }
    return {};
}

void runDwripWithLog(QWidget *parent, const JobConfig &cfg,
                     std::function<void(int)> onFinished)
{
    const QString bin = locateDwrip();
    if (bin.isEmpty()) {
        QMessageBox::warning(parent, "dwrip",
            "dwrip binary not found.\nBuild it at the repo root first:  make");
        return;
    }

    auto *dlg = new QDialog(parent);
    dlg->setWindowTitle("Running dwrip…");
    dlg->resize(640, 360);
    auto *lay = new QVBoxLayout(dlg);
    auto *logView = new QPlainTextEdit(dlg);
    logView->setReadOnly(true);
    logView->setStyleSheet("font-family:monospace;");
    lay->addWidget(logView);
    // Progress bar — starts busy/indeterminate until dwrip emits the first
    // "PROGRESS <pct>" line (large images otherwise look frozen while it RIPs).
    auto *progress = new QProgressBar(dlg);
    progress->setRange(0, 0);          // indeterminate until first PROGRESS
    progress->setTextVisible(true);
    lay->addWidget(progress);
    // Bottom row: "close automatically when finished" (persisted) + Close button.
    auto *autoClose = new QCheckBox("Close automatically when finished", dlg);
    autoClose->setChecked(QSettings().value("ui/autoCloseRunLog", false).toBool());
    autoClose->setToolTip("On success the log dialog closes itself; it stays open "
                          "on error or a spool failure so you can read it.");
    QObject::connect(autoClose, &QCheckBox::toggled, dlg, [](bool on) {
        QSettings().setValue("ui/autoCloseRunLog", on);
    });
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    closeBtns->setEnabled(false);
    auto *btnRow = new QHBoxLayout();
    btnRow->addWidget(autoClose);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtns);
    lay->addLayout(btnRow);
    QObject::connect(closeBtns, &QDialogButtonBox::rejected, dlg, &QDialog::accept);

    auto *runner = new DwripRunner(dlg);
    runner->setBinaryPath(bin);
    const QString outPath = cfg.outputPath;
    const QString queue = cfg.cupsQueue;
    // Emboss Cara A prints multiple PRNs (white run(s) + colour) and announces
    // them as "SPLIT_PRN <path>" lines — collect them to spool in that order.
    auto splitPrns = QSharedPointer<QStringList>::create();
    QObject::connect(runner, &DwripRunner::log, dlg, [=](const QString &chunk) {
        // Split out PROGRESS <pct> lines: drive the bar, keep them out of the log.
        QStringList keep;
        for (const QString &line : chunk.split('\n')) {
            if (line.startsWith("PROGRESS ")) {
                bool ok = false;
                int pct = line.mid(9).trimmed().toInt(&ok);
                if (ok) {
                    if (progress->maximum() == 0) progress->setRange(0, 100);
                    progress->setValue(pct);
                }
                continue;
            }
            if (line.startsWith("SPLIT_PRN "))
                *splitPrns << line.mid(10).trimmed();
            keep << line;
        }
        if (!keep.isEmpty())
            logView->appendPlainText(keep.join('\n'));
    });
    QObject::connect(runner, &DwripRunner::finished, dlg, [=](int code) {
        progress->setRange(0, 100);
        progress->setValue(code == 0 ? 100 : progress->value());
        logView->appendPlainText(code == 0
            ? (outPath.isEmpty() ? QString("\n✓ Done") : "\n✓ Done — " + outPath)
            : QString("\n✗ Failed (exit %1)").arg(code));

        // Hand the freshly-written PRN(s) to CUPS in order. `lp` returns as soon
        // as each job is queued (cupsd owns it), so this doesn't block the UI.
        bool spoolFailed = false;
        if (code == 0 && !queue.isEmpty()) {
            QStringList toSpool = splitPrns->isEmpty()
                ? (outPath.isEmpty() ? QStringList() : QStringList{outPath})
                : *splitPrns;
            for (const QString &p : toSpool) {
                QString jobId, err;
                if (cupsSpool(queue, p, &jobId, &err))
                    logView->appendPlainText(QString("✓ Queued %1 → %2 (job %3)")
                                                 .arg(QFileInfo(p).fileName(), queue, jobId));
                else {
                    spoolFailed = true;
                    logView->appendPlainText(QString("✗ Spool %1 failed: %2")
                                                 .arg(QFileInfo(p).fileName(), err));
                }
            }
        }

        closeBtns->setEnabled(true);
        if (onFinished)
            onFinished(code);

        // Auto-close only on a clean success — keep the dialog up on any error
        // or spool failure so the log stays readable. Small delay so the user
        // sees the 100%/✓ state before it disappears.
        if (code == 0 && !spoolFailed && autoClose->isChecked())
            QTimer::singleShot(600, dlg, &QDialog::accept);
    });
    runner->run(cfg);
    dlg->exec();
}

int dwripPrinterStatus(QString *name, const QString &usbSerial)
{
    const QString bin = locateDwrip();
    if (bin.isEmpty())
        return -1;
    QStringList args{"--status"};
    // Naming the serial pins the query to ONE machine. Without it dwrip takes
    // the first Epson it finds, which need not be the one the user selected.
    if (!usbSerial.isEmpty())
        args << "--device-serial" << usbSerial;
    QProcess p;
    p.start(bin, args);
    if (!p.waitForFinished(5000)) {
        p.kill();
        return -1;
    }
    // dwrip prints one line: "STATUS 04 ready", or "STATUS -- busy" when the
    // device node is held by something else (a job going out).
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    if (out.contains("STATUS -- busy")) {
        if (name)
            *name = QStringLiteral("busy");
        return -2;
    }
    const QRegularExpression re("STATUS\\s+([0-9A-Fa-f]{2})\\s+(\\w+)");
    const auto m = re.match(out);
    if (!m.hasMatch())
        return -1;
    if (name)
        *name = m.captured(2);
    return m.captured(1).toInt(nullptr, 16);
}

void waitWhilePrinterBusy(QWidget *parent, const QString &title,
                          const QString &usbSerial)
{
    const int kReady = 0x04;
    // "Ready" has to hold for a while before we believe it. The printer reports
    // ready between the phases of a maintenance cycle — after the pump stops it
    // still parks and settles with the LED blinking — so the first 04 is not
    // the end. Likewise one unanswered query is not a reason to give up: a busy
    // printer sometimes just does not reply in time.
    const int kReadyStreak = 4;      // ≈8 s of uninterrupted ready
    const int kFailStreak  = 6;      // ≈12 s of silence before giving up

    // -2 (device held by CUPS) is fine — that is the job going out. Only a
    // printer that cannot be queried at all is a reason not to show a dialog
    // that could never finish.
    if (dwripPrinterStatus(nullptr, usbSerial) == -1)
        return;

    QProgressDialog dlg(QString(), "Stop watching", 0, 0, parent);
    dlg.setWindowTitle(title);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);

    QElapsedTimer clock;
    clock.start();
    // The job has only just been handed to CUPS, so the printer is very likely
    // still idle for a moment. Wait for it to pick the job up before treating
    // "ready" as "finished" — otherwise we would report success instantly.
    bool started = false;
    int readyRun = 0, failRun = 0;
    const qint64 kStartTimeoutMs = 30000;

    while (!dlg.wasCanceled()) {
        QString name;
        const int code = dwripPrinterStatus(&name, usbSerial);
        const qint64 secs = clock.elapsed() / 1000;

        if (code == -2) {
            // Device in use: the job is still being handed over. That is
            // activity, not silence — and definitely not "finished".
            started = true;
            readyRun = 0;
            failRun = 0;
        } else if (code < 0) {
            if (++failRun >= kFailStreak)
                break;              // really gone — do not hang forever
        } else {
            failRun = 0;
            if (!started) {
                if (code != kReady)
                    started = true;
                else if (clock.elapsed() > kStartTimeoutMs)
                    break;          // never saw it start; assume it is over
            } else if (code == kReady) {
                if (++readyRun >= kReadyStreak)
                    break;          // steadily ready → genuinely finished
            } else {
                readyRun = 0;
            }
        }

        QString state = !started      ? QStringLiteral("waiting to start")
                        : failRun > 0 ? QStringLiteral("busy (not answering)")
                        : readyRun > 0
                            ? QStringLiteral("finishing up")
                            : name;
        dlg.setLabelText(QString("Printer: %1\nElapsed: %2:%3")
                             .arg(state)
                             .arg(secs / 60)
                             .arg(secs % 60, 2, 10, QChar('0')));
        // Poll gently: this is a minute-scale operation, not a progress bar.
        QElapsedTimer tick;
        tick.start();
        while (tick.elapsed() < 2000 && !dlg.wasCanceled())
            QApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

QList<WasteCounter> dwripReadWaste(const QString &usbSerial)
{
    QList<WasteCounter> out;
    const QString bin = locateDwrip();
    if (bin.isEmpty())
        return out;
    QStringList args{"--waste"};
    if (!usbSerial.isEmpty())
        args << "--device-serial" << usbSerial;
    QProcess p;
    p.start(bin, args);
    // D4 brings up a session and reads several EEPROM bytes; allow for it.
    if (!p.waitForFinished(20000)) {
        p.kill();
        return out;
    }
    // "WASTE 1 6898 55.29 Waste counter 1"
    const QRegularExpression re("WASTE\\s+\\d+\\s+(\\d+)\\s+([0-9.]+)\\s+(.+)");
    for (const QString &line : QString::fromUtf8(p.readAllStandardOutput())
                                   .split('\n')) {
        const auto m = re.match(line);
        if (!m.hasMatch())
            continue;
        WasteCounter w;
        w.value = m.captured(1).toInt();
        w.percent = m.captured(2).toDouble();
        w.name = m.captured(3).trimmed();
        out.append(w);
    }
    return out;
}

bool dwripResetWaste(const QString &usbSerial, QString *log)
{
    const QString bin = locateDwrip();
    if (bin.isEmpty())
        return false;
    QStringList args{"--waste-reset"};
    if (!usbSerial.isEmpty())
        args << "--device-serial" << usbSerial;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(bin, args);
    if (!p.waitForFinished(30000)) {
        p.kill();
        return false;
    }
    if (log)
        *log = QString::fromUtf8(p.readAll()).trimmed();
    return p.exitCode() == 0;
}
