/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "dwrip/DwripRunner.h"
#include <QProcess>

DwripRunner::DwripRunner(QObject *parent) : QObject(parent) {}

QStringList DwripRunner::buildArgs(const JobConfig &cfg)
{
    QStringList a;

    // Core I/O
    if (!cfg.inputPath.isEmpty())  a << "-i" << cfg.inputPath;
    if (!cfg.whitePath.isEmpty())  a << "-w" << cfg.whitePath;
    if (!cfg.outputPath.isEmpty()) a << "-o" << cfg.outputPath;

    // Channels / quality
    if (!cfg.channels.isEmpty()) a << "-c" << cfg.channels;
    if (!cfg.resolution.isEmpty()) a << "-R" << cfg.resolution;
    if (!cfg.dropMode.isEmpty()) a << "-D" << cfg.dropMode;
    if (cfg.inkColorPct)   a << "--ink-color" << QString::number(*cfg.inkColorPct);
    if (cfg.colorLimitPct) a << "-l" << QString::number(*cfg.colorLimitPct);
    if (cfg.whiteLimitPct) a << "-L" << QString::number(*cfg.whiteLimitPct);
    if (!cfg.trim.isEmpty()) a << "--trim" << cfg.trim;
    if (!cfg.curveC.isEmpty()) a << "--curve-c" << cfg.curveC;
    if (!cfg.curveM.isEmpty()) a << "--curve-m" << cfg.curveM;
    if (!cfg.curveY.isEmpty()) a << "--curve-y" << cfg.curveY;
    if (!cfg.curveK.isEmpty()) a << "--curve-k" << cfg.curveK;
    if (!cfg.curvePointC.isEmpty()) a << "--curve-point-c" << cfg.curvePointC;
    if (!cfg.curvePointM.isEmpty()) a << "--curve-point-m" << cfg.curvePointM;
    if (!cfg.curvePointY.isEmpty()) a << "--curve-point-y" << cfg.curvePointY;
    if (!cfg.curvePointK.isEmpty()) a << "--curve-point-k" << cfg.curvePointK;
    if (!cfg.structDitherMethod.isEmpty()) a << "--struct-dither" << cfg.structDitherMethod;
    if (cfg.estimate) a << "--estimate";
    if (cfg.embossLayers > 0) {
        a << "--emboss" << QString::number(cfg.embossLayers);
        if (!cfg.spotPath.isEmpty()) a << "--spot" << cfg.spotPath;
        a << "--underbase" << (cfg.underbase ? "on" : "off");
        a << "--emboss-height" << (cfg.embossGrayscale ? "grayscale" : "binary");
        a << "--emboss-mode" << (cfg.embossSplit ? "split" : "single");
        if (cfg.embossSplit) {
            a << "--emboss-white-runs" << QString::number(cfg.embossWhiteRuns);
            a << "--emboss-color-reg" << QString::number(cfg.embossColorReg);
        }
    }
    if (cfg.chart) a << "--chart";
    if (cfg.chartGray) a << "--chart-gray";
    if (cfg.regTest) a << "--reg-test";
    if (cfg.nozzleCheck) a << "--nozzle-check";
    // White base shaping — emitted only when it would change anything, so a
    // default job keeps producing the exact argument list it always did.
    if (cfg.whiteMode == "luminance") {
        a << "--white-mode" << "luminance";
        if (cfg.whiteDarkPct && *cfg.whiteDarkPct != 100)
            a << "--white-dark-pct" << QString::number(*cfg.whiteDarkPct);
    }
    if (cfg.whiteCurveDelta && *cfg.whiteCurveDelta != 0)
        a << "--white-curve"
          << QString("%1,%2").arg(cfg.whiteCurveX ? *cfg.whiteCurveX : 50)
                             .arg(*cfg.whiteCurveDelta);
    if (cfg.whiteChokeMm && *cfg.whiteChokeMm > 0.0)
        a << "--white-choke" << QString::number(*cfg.whiteChokeMm, 'f', 2);
    if (!cfg.cleanLevel.isEmpty()) a << "--clean" << cfg.cleanLevel;
    if (cfg.regV) a << "--reg-v" << QString::number(*cfg.regV);

    // Print mechanics
    if (cfg.whiteFirst) a << "-W";
    a << (cfg.bidi ? "-b" : "-u");
    if (!cfg.mode.isEmpty()) a << "-m" << cfg.mode;
    if (!cfg.singleWeave.isEmpty() && cfg.singleWeave != "fast")
        a << "--single-weave" << cfg.singleWeave;
    a << "-Z" << QString::number(cfg.zones);
    // Zone-3 source (only meaningful with -Z 3 both-mode; engine guards it).
    if (!cfg.z3Mode.isEmpty() && cfg.z3Mode != "same") {
        a << "--z3-mode" << cfg.z3Mode;
        if (cfg.z3Mode == "image" && !cfg.z3ImagePath.isEmpty())
            a << "--z3-image" << cfg.z3ImagePath;
    }

    // Size & margins
    if (cfg.sizeWmm) {
        QString s = QString::number(*cfg.sizeWmm, 'g', 6);
        if (cfg.sizeHmm)
            s += "x" + QString::number(*cfg.sizeHmm, 'g', 6);
        a << "-S" << s;
    }
    if (cfg.marginMm)   a << "-M" << QString::number(*cfg.marginMm, 'g', 6);
    if (cfg.uvTailMm)   a << "-U" << QString::number(*cfg.uvTailMm, 'g', 6);
    if (cfg.uvTailSpeed) a << "--uv-tail-speed" << QString::number(*cfg.uvTailSpeed);
    if (cfg.leadInFeedStep) a << "--lead-in-feed-step" << QString::number(*cfg.leadInFeedStep);
    if (!cfg.leadInSweep.isEmpty() && cfg.leadInSweep != "off")
        a << "--lead-in-sweep" << cfg.leadInSweep;

    // ICC
    if (!cfg.iccPath.isEmpty()) a << "-I" << cfg.iccPath;
    if (!cfg.intent.isEmpty())  a << "--intent" << cfg.intent;

    // Direct send
    if (!cfg.sendDevice.isEmpty()) a << "-s" << cfg.sendDevice;

    return a;
}

bool DwripRunner::isRunning() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void DwripRunner::run(const JobConfig &cfg)
{
    if (isRunning()) {
        emit log("[dwrip] already running, ignoring request");
        return;
    }
    if (!m_proc) {
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
            emit log(QString::fromLocal8Bit(m_proc->readAllStandardOutput()).trimmed());
        });
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) { emit finished(code); });
    }

    const QStringList args = buildArgs(cfg);
    emit log("[dwrip] " + m_binary + " " + args.join(' '));
    m_proc->start(m_binary, args);
}
