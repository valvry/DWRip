/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "PrinterProfile.h"
#include "ChannelProfileDialog.h"   // channelProfilesLoad() — used once, by the migration
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

namespace {

const int kSchema = 1;

QString listKey()   { return QStringLiteral("printers/list"); }
QString activeKey() { return QStringLiteral("printers/active"); }
QString schemaKey() { return QStringLiteral("printers/schema"); }

QStringList idList()
{
    return QSettings().value(listKey()).toStringList();
}

void setIdList(const QStringList &ids)
{
    QSettings().setValue(listKey(), ids);
}

// Next free id. Scans the existing ids rather than counting them, so deleting
// a middle profile can never hand out an id that is already taken.
QString nextId()
{
    const QStringList ids = idList();
    int max = 0;
    for (const QString &id : ids) {
        if (!id.startsWith('p')) continue;
        const int n = id.mid(1).toInt();
        if (n > max) max = n;
    }
    return QStringLiteral("p%1").arg(max + 1);
}

}  // namespace

QStringList printerSettingKeys()
{
    // Mirrors the table in the plan. Order is irrelevant; completeness is not —
    // a key missing here is a key that will not follow a Duplicate.
    return {
        // device
        "cupsQueue",
        // ink layout
        "channelOrder",
        // colour
        "icc", "intent",
        // quality (resolution/dropMode are also editable from the Export dialog)
        "resolution", "dropMode", "structPattern", "bidi",
        // density calibration — the expensive part
        "inkColor", "colorLimit", "whiteLimit",
        // white base generation
        "whiteMode", "whiteDarkPct", "whiteCurveX", "whiteCurveDelta",
        "whiteChoke",
        "trimC", "trimM", "trimY", "trimK",
        "curveCPointX", "curveCPointDelta",
        "curveMPointX", "curveMPointDelta",
        "curveYPointX", "curveYPointDelta",
        "curveKPointX", "curveKPointDelta",
        // UV / curing
        "margin", "uvTail", "uvTailSpeed",
        // mechanics
        "regV", "leadInFeedStep", "leadInSweep",
    };
}

QList<PrinterMeta> printersLoad()
{
    QList<PrinterMeta> out;
    QSettings s;
    for (const QString &id : idList()) {
        PrinterMeta m;
        m.id = id;
        m.name = s.value(printerGroup(id) + "/name").toString();
        if (m.name.isEmpty())
            m.name = id;
        out.append(m);
    }
    return out;
}

QString printerName(const QString &id)
{
    const QString n = QSettings().value(printerGroup(id) + "/name").toString();
    return n.isEmpty() ? id : n;
}

void printerRename(const QString &id, const QString &name)
{
    if (id.isEmpty() || name.trimmed().isEmpty())
        return;
    QSettings().setValue(printerGroup(id) + "/name", name.trimmed());
}

QString printerGroup(const QString &id)
{
    return QStringLiteral("printers/") + id;
}

QString activePrinterId()
{
    const QStringList ids = idList();
    if (ids.isEmpty())
        return {};
    const QString a = QSettings().value(activeKey()).toString();
    return ids.contains(a) ? a : ids.first();
}

void setActivePrinter(const QString &id)
{
    if (id.isEmpty() || !idList().contains(id))
        return;
    QSettings().setValue(activeKey(), id);
}

QString activePrinterGroup()
{
    return printerGroup(activePrinterId());
}

QString printerCreate(const QString &name)
{
    const QString id = nextId();
    QSettings s;
    s.setValue(printerGroup(id) + "/name",
               name.trimmed().isEmpty() ? id : name.trimmed());
    QStringList ids = idList();
    ids.append(id);
    setIdList(ids);
    return id;
}

QString printerDuplicate(const QString &srcId, const QString &newName)
{
    const QString id = printerCreate(newName);
    QSettings s;
    const QString from = printerGroup(srcId), to = printerGroup(id);
    for (const QString &k : printerSettingKeys()) {
        const QVariant v = s.value(from + "/" + k);
        if (v.isValid())
            s.setValue(to + "/" + k, v);
    }
    return id;
}

bool printerRemove(const QString &id)
{
    QStringList ids = idList();
    if (ids.size() <= 1 || !ids.contains(id))
        return false;   // there must always be a profile to fall back on
    QSettings s;
    s.remove(printerGroup(id));
    ids.removeAll(id);
    setIdList(ids);
    if (s.value(activeKey()).toString() == id)
        s.setValue(activeKey(), ids.first());
    return true;
}

QVariant printerValue(const QString &key, const QVariant &def)
{
    QSettings s;
    const QString id = activePrinterId();
    if (!id.isEmpty()) {
        const QVariant v = s.value(printerGroup(id) + "/" + key);
        if (v.isValid())
            return v;
    }
    return s.value("dwrip/" + key, def);   // pre-migration fallback
}

void printerSetValue(const QString &key, const QVariant &value)
{
    const QString id = activePrinterId();
    if (id.isEmpty())
        return;
    QSettings().setValue(printerGroup(id) + "/" + key, value);
}

bool printerContains(const QString &key)
{
    QSettings s;
    const QString id = activePrinterId();
    if (!id.isEmpty() && s.contains(printerGroup(id) + "/" + key))
        return true;
    return s.contains("dwrip/" + key);
}

void printersMigrateLegacy()
{
    QSettings s;
    if (s.value(schemaKey()).toInt() >= kSchema && !idList().isEmpty())
        return;   // already migrated

    // Name the first profile after the CUPS queue it was set up for; that is
    // the most recognisable label for someone upgrading from the flat layout.
    const QString queue = s.value("dwrip/cupsQueue").toString().trimmed();
    const QString id = printerCreate(queue.isEmpty() ? QStringLiteral("Printer 1")
                                                     : queue);

    // Copy every owned key that the flat group actually held. Absent keys are
    // left unset so the engine/UI defaults still apply.
    for (const QString &k : printerSettingKeys()) {
        const QVariant v = s.value("dwrip/" + k);
        if (v.isValid())
            s.setValue(printerGroup(id) + "/" + k, v);
    }

    // Lead-in sweep was a bool before it became a 3-way mode; applyPrinterSettings
    // no longer carries that fallback, so resolve it here instead.
    if (!s.contains("dwrip/leadInSweep") && s.contains("dwrip/leadInDummySweep")) {
        s.setValue(printerGroup(id) + "/leadInSweep",
                   s.value("dwrip/leadInDummySweep").toBool()
                       ? QStringLiteral("full") : QStringLiteral("off"));
    }

    // The ink layout used to be an indirection: dwrip/channelProfile held the
    // NAME of an entry in the channelProfiles array. Resolve it once, here, so
    // profiles can carry the 6-char order directly from now on.
    const QString activeChannelProfile =
        s.value("dwrip/channelProfile").toString();
    if (!activeChannelProfile.isEmpty()) {
        for (const ChannelProfile &p : channelProfilesLoad()) {
            if (p.name == activeChannelProfile) {
                s.setValue(printerGroup(id) + "/channelOrder", p.order);
                break;
            }
        }
    }

    setActivePrinter(id);
    s.setValue(schemaKey(), kSchema);
    // "dwrip/" is deliberately NOT removed: it stays as a read fallback and as
    // a manual recovery path if anything about this migration turns out wrong.
}

// ── Portable single-profile files ───────────────────────────────────────────

namespace {

const int kFileFormat = 1;

// Where imported ICC profiles are kept, so a profile never points at a path
// that only existed on the machine it came from.
QString managedIccDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + "/icc";
    QDir().mkpath(d);
    return d;
}

// "L1800" → "L1800 (2)" when the name is already taken.
QString uniqueProfileName(const QString &wanted)
{
    QStringList taken;
    for (const PrinterMeta &m : printersLoad())
        taken << m.name;
    if (!taken.contains(wanted))
        return wanted;
    for (int n = 2; n < 1000; n++) {
        const QString cand = QStringLiteral("%1 (%2)").arg(wanted).arg(n);
        if (!taken.contains(cand))
            return cand;
    }
    return wanted;
}

}  // namespace

bool printerExport(const QString &id, const QString &path, QString *error)
{
    auto fail = [&](const QString &m) { if (error) *error = m; return false; };
    if (id.isEmpty() || !printersLoad().size())
        return fail("No printer profile to export.");

    QSettings src;
    const QString group = printerGroup(id);

    // Copy the ICC next to the profile file, and record only its basename —
    // absolute paths do not survive the trip to another machine.
    QString iccBase;
    const QString iccPath = src.value(group + "/icc").toString().trimmed();
    if (!iccPath.isEmpty()) {
        QFileInfo fi(iccPath);
        if (fi.exists()) {
            iccBase = QFileInfo(path).completeBaseName() + "." + fi.suffix();
            const QString dstIcc = QFileInfo(path).absolutePath() + "/" + iccBase;
            QFile::remove(dstIcc);
            if (!QFile::copy(iccPath, dstIcc))
                return fail("Could not copy the ICC profile to:\n" + dstIcc);
        } else if (error) {
            // Not fatal: export the settings, tell the caller the ICC is gone.
            *error = "ICC file not found, exported without it:\n" + iccPath;
        }
    }

    QFile::remove(path);                       // never merge into a stale file
    QSettings out(path, QSettings::IniFormat);
    out.setValue("profile/format", kFileFormat);
    out.setValue("profile/name", printerName(id));
    out.setValue("profile/iccFile", iccBase);
    for (const QString &k : printerSettingKeys()) {
        if (k == "icc")
            continue;                          // replaced by profile/iccFile
        const QVariant v = src.value(group + "/" + k);
        if (v.isValid())
            out.setValue("settings/" + k, v);
    }
    out.sync();
    if (out.status() != QSettings::NoError)
        return fail("Could not write:\n" + path);
    return true;
}

QString printerImport(const QString &path, QString *error, QString *warning)
{
    auto fail = [&](const QString &m) { if (error) *error = m; return QString(); };

    QSettings in(path, QSettings::IniFormat);
    if (in.status() != QSettings::NoError)
        return fail("Could not read:\n" + path);
    if (in.value("profile/format").toInt() != kFileFormat)
        return fail("Not a DWRip printer profile (or a newer format):\n" + path);

    const QString name = uniqueProfileName(
        in.value("profile/name").toString().trimmed().isEmpty()
            ? QStringLiteral("Imported printer")
            : in.value("profile/name").toString().trimmed());
    const QString id = printerCreate(name);

    QSettings dst;
    for (const QString &k : printerSettingKeys()) {
        const QVariant v = in.value("settings/" + k);
        if (v.isValid())
            dst.setValue(printerGroup(id) + "/" + k, v);
    }

    // Pull the sibling ICC into the managed folder so the imported profile
    // points somewhere that exists on THIS machine.
    const QString iccBase = in.value("profile/iccFile").toString().trimmed();
    if (!iccBase.isEmpty()) {
        const QString srcIcc = QFileInfo(path).absolutePath() + "/" + iccBase;
        if (QFileInfo::exists(srcIcc)) {
            const QString dstIcc = managedIccDir() + "/" + iccBase;
            QFile::remove(dstIcc);
            if (QFile::copy(srcIcc, dstIcc))
                dst.setValue(printerGroup(id) + "/icc", dstIcc);
            else if (warning)
                *warning = "Could not copy the ICC profile into:\n" + managedIccDir();
        } else if (warning) {
            *warning = "The ICC file '" + iccBase + "' was not next to the profile.\n"
                       "Set the ICC profile manually in App Settings → Colour.";
        }
    }
    return id;
}
