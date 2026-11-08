/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QObject>
#include <QStringList>
#include "dwrip/JobConfig.h"

class QProcess;

// DwripRunner is the ONLY place the GUI touches the dwrip engine.
// It translates a JobConfig into argv and runs the `dwrip` binary as a subprocess,
// streaming stdout/stderr back via signals.
class DwripRunner : public QObject {
    Q_OBJECT
public:
    explicit DwripRunner(QObject *parent = nullptr);

    // Path to the dwrip binary. Defaults to "./dwrip" relative to CWD;
    // callers may override (e.g. to the repo root build output).
    void setBinaryPath(const QString &path) { m_binary = path; }
    QString binaryPath() const { return m_binary; }

    // Pure function (also used in tests): JobConfig -> argv. No side effects.
    static QStringList buildArgs(const JobConfig &cfg);

    // Launch dwrip with cfg. Emits log()/finished().
    void run(const JobConfig &cfg);

    bool isRunning() const;

signals:
    void log(const QString &line);
    void finished(int exitCode);

private:
    QString m_binary = "./dwrip";
    QProcess *m_proc = nullptr;
};
