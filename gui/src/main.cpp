/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QPalette>
#include <QStyleFactory>
#include <QProxyStyle>
#include <QIcon>
#include "MainWindow.h"
#include "AppSettings.h"
#include "canvas/PrintBedScene.h"
#include "canvas/ImageItem.h"
#include "PropertiesPanel.h"
#include "Compositor.h"
#include "SpotMask.h"
#include "SpotMaskDialog.h"
#include <QSet>
#include "dwrip/DwripRunner.h"
#include "dwrip/DwripPanel.h"
#include "SettingsDialog.h"
#include "PrinterProfile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "InkSettingsDialog.h"
#include "ExportDialog.h"
#include "InkCost.h"
#include "Compositor.h"
#include "Theme.h"
#include <QImage>
#include <QImageReader>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QAbstractScrollArea>
#include <QWheelEvent>
#include <QSettings>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cmath>

// Stop the mouse wheel from changing spin box / combo values. Instead forward
// the scroll to the nearest scroll area so the panel still scrolls.
class WheelGuard : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() != QEvent::Wheel)
            return false;
        if (qobject_cast<QAbstractSpinBox *>(o) || qobject_cast<QComboBox *>(o)) {
            for (QWidget *p = qobject_cast<QWidget *>(o)->parentWidget(); p; p = p->parentWidget())
                if (auto *sa = qobject_cast<QAbstractScrollArea *>(p)) {
                    QCoreApplication::sendEvent(sa->viewport(), e);
                    break;
                }
            return true;   // consume on the spin/combo → value unchanged
        }
        return false;
    }
};

// Fusion renders combo popups as a scrolling "menu" centred on the current item
// (SH_ComboBox_Popup=1): it shows only a couple of rows with up/down scroll
// arrows even for a 3-item list. Force the plain list-dropdown popup instead.
class ComboListPopupStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;   // inherit the QProxyStyle(QString baseKey) ctor
    int styleHint(StyleHint hint, const QStyleOption *opt = nullptr,
                  const QWidget *w = nullptr,
                  QStyleHintReturn *ret = nullptr) const override
    {
        if (hint == SH_ComboBox_Popup)
            return 0;
        return QProxyStyle::styleHint(hint, opt, w, ret);
    }
};

// Apply the modern light theme app-wide (Fusion + palette + font + QSS) so the
// GUI looks the same regardless of the host's desktop theme.
static void applyLightTheme(QApplication &app)
{
    app.setStyle(new ComboListPopupStyle("Fusion"));   // Fusion base, list popups
    app.setFont(Theme::pickUiFont());

    QPalette p;
    p.setColor(QPalette::Window,          Theme::bgApp());
    p.setColor(QPalette::WindowText,      Theme::text());
    p.setColor(QPalette::Base,            Theme::surface());
    p.setColor(QPalette::AlternateBase,   QColor("#f1f4f9"));
    p.setColor(QPalette::Text,            Theme::text());
    p.setColor(QPalette::Button,          Theme::surface());
    p.setColor(QPalette::ButtonText,      Theme::text());
    p.setColor(QPalette::ToolTipBase,     Theme::text());
    p.setColor(QPalette::ToolTipText,     QColor("#ffffff"));
    p.setColor(QPalette::Highlight,       Theme::accent());
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::PlaceholderText, Theme::textMuted());
    app.setPalette(p);

    app.setStyleSheet(Theme::modernStyleSheet());
}

// --self-test: construct the full UI then exit 0. Lets CI / the headless
// sandbox verify linking and startup with QT_QPA_PLATFORM=offscreen, and
// sanity-checks the dwrip arg-builder seam without printing anything.
static int selfTest()
{
    MainWindow w;
    w.show();

    // 1) dwrip arg-builder seam.
    JobConfig cfg;
    cfg.inputPath = "design.png";
    cfg.outputPath = "out.prn";
    cfg.channels = "WWYKMC";
    cfg.resolution = "1440x720";
    cfg.whiteFirst = true;
    cfg.sizeWmm = 40.0;
    cfg.sizeHmm = 40.0;
    const QStringList args = DwripRunner::buildArgs(cfg);
    const int ri = args.indexOf("-R");
    const bool argsOk = args.contains("-i") && args.contains("design.png") &&
                        args.contains("-W") && args.contains("-S") &&
                        args.contains("40x40") &&
                        ri >= 0 && args.value(ri + 1) == "1440x720";

    // 2) Defaults: bed 300x450 mm, unit Cm.
    const AppSettings def;
    const bool defOk = def.bedWmm == 300.0 && def.bedHmm == 450.0 &&
                       def.unit == Unit::Cm;

    // 3) SVG icons load via the svg icon engine.
    bool iconsOk = true;
    for (const char *name : {"import", "zoom-in", "zoom-out", "fit", "white-layer", "ink-limit", "settings"}) {
        QIcon ic(QString(":/icons/%1.svg").arg(name));
        if (ic.isNull() || ic.pixmap(20, 20).isNull()) {
            qWarning().noquote() << "[self-test] icon failed:" << name;
            iconsOk = false;
        }
    }

    // 4) Snap-to-canvas: dragging an item past an edge clamps it inside the bed.
    bool snapOk = false;
    {
        PrintBedScene scene(300.0, 450.0, Unit::Cm);
        QImage img(100, 100, QImage::Format_ARGB32);
        img.fill(Qt::red);
        auto *item = new ImageItem(img, "test");   // owned by scene after addItem
        scene.addItem(item);
        const double w = item->widthMm(), h = item->heightMm();
        item->setTopLeftMm(QPointF(9999, 9999));   // far past bottom-right
        const QPointF p1 = item->topLeftMm();
        item->setTopLeftMm(QPointF(-500, -500));   // far past top-left
        const QPointF p2 = item->topLeftMm();
        snapOk = std::abs(p1.x() - (300.0 - w)) < 0.5 &&
                 std::abs(p1.y() - (450.0 - h)) < 0.5 &&
                 std::abs(p2.x()) < 0.5 && std::abs(p2.y()) < 0.5;
    }

    // 5) Transforms: resize / rotate (90° swaps bbox W/H) / mirror.
    bool xformOk = false;
    {
        PrintBedScene scene(300.0, 450.0, Unit::Cm);
        QImage img(200, 100, QImage::Format_ARGB32);   // 2:1 aspect
        img.fill(Qt::green);
        auto *item = new ImageItem(img, "t");
        scene.addItem(item);

        item->setSizeMm(100.0, 50.0);
        const bool sizeOk = std::abs(item->widthMm() - 100.0) < 0.01 &&
                            std::abs(item->heightMm() - 50.0) < 0.01;

        item->rotateBy(90);
        const QRectF bb = item->sceneBoundingRect();   // 90° → bbox ≈ 50 × 100
        const bool rotOk = std::abs(item->rotationDeg() - 90.0) < 0.01 &&
                           std::abs(bb.width() - 50.0) < 1.0 &&
                           std::abs(bb.height() - 100.0) < 1.0;

        item->setRotationDeg(0);
        item->setFlipH(true);
        const bool mirOk = item->flipH() &&
                           std::abs(item->widthMm() - 100.0) < 0.01;   // size unchanged

        // The Width/Height fields describe the bed, so a quarter turn has to
        // send them to the opposite axis of the item — otherwise typing a width
        // after rotating 90° changes the height.
        const bool swapOk = !PropertiesPanel::axesSwappedFor(0) &&
                            PropertiesPanel::axesSwappedFor(90) &&
                            !PropertiesPanel::axesSwappedFor(180) &&
                            PropertiesPanel::axesSwappedFor(270) &&
                            PropertiesPanel::axesSwappedFor(-90) &&
                            !PropertiesPanel::axesSwappedFor(360);

        xformOk = sizeOk && rotOk && mirOk && swapOk;
    }

    // 6) dwrip panel: QSettings auto-load → applyTo → buildArgs mapping (-D/-l/-L).
    //    Isolate to a test app name so the user's real config is untouched.
    bool dwripOk = false;
    {
        const QString realApp = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("dwrip-gui-selftest");
        {
            QSettings s;
            s.beginGroup("dwrip");
            s.setValue("dropMode", "M");
            s.setValue("resolution", "1440x720");
            s.setValue("inkColor", 80);   // DwripPanel now owns colour density (TAC moved out)
            s.setValue("whiteLimit", 50);
            s.endGroup();
            s.sync();
        }
        DwripPanel panel;            // should auto-load the values above
        JobConfig cfg;
        panel.applyTo(cfg);
        const QStringList a = DwripRunner::buildArgs(cfg);
        const int di = a.indexOf("-D"), ii = a.indexOf("--ink-color"),
                  Li = a.indexOf("-L"), Ri = a.indexOf("-R");
        dwripOk = di >= 0 && a.value(di + 1) == "M" &&
                  ii >= 0 && a.value(ii + 1) == "80" &&
                  Li >= 0 && a.value(Li + 1) == "50" &&
                  Ri >= 0 && a.value(Ri + 1) == "1440x720" &&
                  a.indexOf("-l") < 0;   // TAC no longer emitted by the dwrip panel
        QSettings().clear();         // wipe the isolated test config
        QCoreApplication::setApplicationName(realApp);
    }

    // 7) App Settings: AppSettings round-trip + Tier-1 dwrip mapping (isolated).
    bool settingsOk = false;
    {
        const QString realApp = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("dwrip-gui-selftest");

        AppSettings a;
        a.bedWmm = 200; a.bedHmm = 320; a.unit = Unit::Mm;
        a.save();
        AppSettings b;            // defaults
        b.load();
        const bool appOk = b.bedWmm == 200 && b.bedHmm == 320 && b.unit == Unit::Mm;

        // Legacy-migration path: write the OLD flat layout, then let the
        // migration fold it into the first printer profile. This is the exact
        // upgrade an existing install goes through, so it is worth asserting.
        channelProfilesSave({{"DTF", "WWYKMC"}});
        {
            QSettings s;
            s.beginGroup("dwrip");
            s.setValue("channelProfile", "DTF");
            s.setValue("cupsQueue", "L1800");
            s.setValue("margin", 4.0);
            s.setValue("intent", "relative-bpc");
            s.setValue("colorLimit", 65);
            s.endGroup();
            s.sync();
        }
        printersMigrateLegacy();
        const bool migrateOk =
            printersLoad().size() == 1 &&
            printersLoad().first().name == "L1800" &&          // named after the queue
            printerValue("channelOrder").toString() == "WWYKMC" &&  // name → order
            printerValue("margin").toDouble() == 4.0 &&
            printerValue("colorLimit").toInt() == 65;

        SettingsDialog dlg(AppSettings{});
        JobConfig cfg;
        dlg.applyTo(cfg);
        const QStringList a2 = DwripRunner::buildArgs(cfg);
        const int ci = a2.indexOf("-c"), mi = a2.indexOf("-M"), ii = a2.indexOf("--intent");
        const bool tier1Ok = ci >= 0 && a2.value(ci + 1) == "WWYKMC" &&   // from profile
                             mi >= 0 && a2.value(mi + 1) == "4" &&
                             ii >= 0 && a2.value(ii + 1) == "relative-bpc";
        // TAC (Ink Settings) flows through the free applyPrinterSettings() used
        // by the export path, not SettingsDialog::applyTo.
        JobConfig tcfg;
        applyPrinterSettings(tcfg);
        const QStringList a3 = DwripRunner::buildArgs(tcfg);
        const int li = a3.indexOf("-l");
        const bool tacOk = li >= 0 && a3.value(li + 1) == "65";

        // Two profiles must not share state, and each must remember its own
        // resolution (the per-printer stickiness the plan calls for).
        const QString p1 = activePrinterId();
        printerSetValue("resolution", "1440x720");
        const QString p2 = printerDuplicate(p1, "Second");
        setActivePrinter(p2);
        printerSetValue("resolution", "720x720");
        printerSetValue("channelOrder", "CMYKWW");
        setActivePrinter(p1);
        const bool stickyA = printerValue("resolution").toString() == "1440x720" &&
                             printerValue("channelOrder").toString() == "WWYKMC";
        setActivePrinter(p2);
        const bool stickyB = printerValue("resolution").toString() == "720x720" &&
                             printerValue("channelOrder").toString() == "CMYKWW" &&
                             // duplicate carried the calibration across
                             printerValue("colorLimit").toInt() == 65;
        JobConfig bcfg;
        applyPrinterSettings(bcfg);
        const QStringList a4 = DwripRunner::buildArgs(bcfg);
        const int ri = a4.indexOf("-R"), ci2 = a4.indexOf("-c");
        const bool switchOk = ri >= 0 && a4.value(ri + 1) == "720x720" &&
                              ci2 >= 0 && a4.value(ci2 + 1) == "CMYKWW";
        // Deleting down to one profile must be refused, never leaving zero.
        setActivePrinter(p1);
        const bool guardOk = printerRemove(p2) && !printerRemove(p1);

        // Export → import round-trip: every setting must survive, the import
        // must ADD (never overwrite), and the ICC must be re-pointed at a file
        // that exists on this machine.
        bool ioOk = false;
        {
            const QString dir = QDir::temp().filePath("dwrip_profile_test");
            QDir().mkpath(dir);
            const QString icc = dir + "/fake.icm";
            { QFile f(icc); f.open(QIODevice::WriteOnly); f.write("ICCDATA"); }
            setActivePrinter(p1);
            printerSetValue("icc", icc);
            printerSetValue("trimY", 7);
            printerSetValue("uvTail", 3.5);

            const QString file = dir + "/round.dwrip-profile";
            QString err, warn;
            const int before = printersLoad().size();
            const bool expOk = printerExport(p1, file, &err);
            const QString newId = printerImport(file, &err, &warn);
            const bool added = printersLoad().size() == before + 1 && !newId.isEmpty();
            setActivePrinter(newId);
            const bool valuesOk =
                printerValue("channelOrder").toString() == "WWYKMC" &&
                printerValue("colorLimit").toInt() == 65 &&
                printerValue("trimY").toInt() == 7 &&
                qFuzzyCompare(printerValue("uvTail").toDouble(), 3.5) &&
                printerValue("resolution").toString() == "1440x720";
            // ICC must point at a real file, and NOT at the original path.
            const QString gotIcc = printerValue("icc").toString();
            const bool iccOk = !gotIcc.isEmpty() && gotIcc != icc &&
                               QFileInfo::exists(gotIcc);
            // Name must be de-duplicated, not silently reused.
            const bool nameOk = printerName(newId) != printerName(p1);
            // A junk file must be refused without creating anything.
            const QString bad = dir + "/bad.dwrip-profile";
            { QFile f(bad); f.open(QIODevice::WriteOnly); f.write("not a profile"); }
            const int n2 = printersLoad().size();
            const bool rejectOk = printerImport(bad, &err, &warn).isEmpty() &&
                                  printersLoad().size() == n2;
            ioOk = expOk && added && valuesOk && iccOk && nameOk && rejectOk;
            setActivePrinter(p1);
            printerRemove(newId);
            QDir(dir).removeRecursively();
        }

        settingsOk = appOk && migrateOk && tier1Ok && tacOk &&
                     stickyA && stickyB && switchOk && guardOk && ioOk;

        QSettings().clear();
        QCoreApplication::setApplicationName(realApp);
    }

    // 8) Export: composite size/transparency + flag assembly + end-to-end PRN.
    bool exportOk = false;
    {
        const QString realApp = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("dwrip-gui-selftest");

        // Composite: image at X=50,Y=30mm → canvas (50+w)×(30+h), padding transparent.
        PrintBedScene scene(300.0, 450.0, Unit::Cm);
        QImage img(120, 120, QImage::Format_ARGB32);
        img.fill(Qt::blue);
        auto *item = new ImageItem(img, "t");
        scene.addItem(item);
        item->setTopLeftMm(QPointF(50, 30));
        QSizeF sz;
        const QImage comp = renderActiveImage(item, 300.0, sz);
        const double w = item->widthMm(), h = item->heightMm();
        const bool compOk = std::abs(sz.width() - (50 + w)) < 0.5 &&
                            std::abs(sz.height() - (30 + h)) < 0.5 &&
                            qAlpha(comp.pixel(2, 2)) == 0 &&                       // top-left padding
                            qAlpha(comp.pixel(comp.width() - 3, comp.height() - 3)) > 0; // image

        // Flag assembly: printer profile + Tier-2 (export dialog).
        channelProfilesSave({{"DTF", "WWYKMC"}});
        { QSettings s; s.beginGroup("dwrip");
          s.setValue("channelProfile", "DTF"); s.setValue("margin", 4.0);
          s.endGroup(); s.sync(); }
        printersMigrateLegacy();     // fold the flat keys into profile p1
        JobConfig cfg;
        applyPrinterSettings(cfg);
        cfg.mode = "white"; cfg.whiteFirst = true; cfg.zones = 3; cfg.outputPath = "/tmp/x.prn";
        const QStringList fa = DwripRunner::buildArgs(cfg);
        auto has = [&](const QString &f, const QString &v) {
            const int i = fa.indexOf(f); return i >= 0 && fa.value(i + 1) == v; };
        const bool flagOk = has("-c", "WWYKMC") && fa.contains("-m") && has("-m", "white") &&
                            fa.contains("-W") && has("-Z", "3") && has("-M", "4") &&
                            has("-o", "/tmp/x.prn");

        // End-to-end (if ./dwrip is built): render → run → .prn exists.
        bool e2eOk = true;
        const QString dwrip = QFile::exists("dwrip") ? QFileInfo("dwrip").absoluteFilePath()
                            : QFile::exists("../dwrip") ? QFileInfo("../dwrip").absoluteFilePath()
                            : QString();
        if (!dwrip.isEmpty()) {
            const QString tmpIn = QDir::temp().filePath("dwrip_selftest_in.png");
            const QString tmpOut = QDir::temp().filePath("dwrip_selftest_out.prn");
            QFile::remove(tmpOut);
            comp.save(tmpIn, "PNG");
            JobConfig j; j.inputPath = tmpIn; j.outputPath = tmpOut;
            j.sizeWmm = sz.width(); j.sizeHmm = sz.height();
            QProcess proc;
            proc.start(dwrip, DwripRunner::buildArgs(j));
            proc.waitForFinished(20000);
            e2eOk = QFileInfo(tmpOut).size() > 0;
            QFile::remove(tmpIn); QFile::remove(tmpOut);
        }
        exportOk = compOk && flagOk && e2eOk;

        QSettings().clear();
        QCoreApplication::setApplicationName(realApp);
    }

    // 9) White-layer derivation matches dwrip (white = alpha × whiteLimit/100).
    bool whiteOk = false;
    {
        QImage src(4, 1, QImage::Format_ARGB32);
        src.setPixel(0, 0, qRgba(10, 20, 30, 0));
        src.setPixel(1, 0, qRgba(10, 20, 30, 128));
        src.setPixel(2, 0, qRgba(10, 20, 30, 255));
        src.setPixel(3, 0, qRgba(10, 20, 30, 200));
        const QImage full = buildWhiteLayer(src, 100);
        const QImage half = buildWhiteLayer(src, 50);
        whiteOk =
            qAlpha(full.pixel(0, 0)) == 0   && qAlpha(full.pixel(1, 0)) == 128 &&
            qAlpha(full.pixel(2, 0)) == 255 && qAlpha(full.pixel(3, 0)) == 200 &&
            qAlpha(half.pixel(1, 0)) == 64  && qAlpha(half.pixel(2, 0)) == 128 &&
            qRed(full.pixel(2, 0)) == 255 && qGreen(full.pixel(2, 0)) == 255 &&
            qBlue(full.pixel(2, 0)) == 255;   // RGB forced white
    }

    // 10) Colour ink-limit soft-proof matches the engine UCR + TAC cap.
    bool colorOk = false;
    {
        QImage src(2, 1, QImage::Format_ARGB32);
        src.setPixel(0, 0, qRgba(255, 0, 0, 255));   // pure red: UCR C0 M255 Y255 K0, sum 510
        src.setPixel(1, 0, qRgba(200, 200, 200, 255)); // light grey: sum well under cap
        const QImage cap25 = buildColorLimitProof(src, 25);   // cap=255 → s=0.5 on red
        const QImage cap100 = buildColorLimitProof(src, 100); // no-op
        const QRgb red = cap25.pixel(0, 0);
        colorOk =
            qRed(red) == 255 && std::abs(qGreen(red) - 127) <= 1 &&
            qBlue(red) == qGreen(red) && qAlpha(red) == 255 &&          // red → soft pink
            cap100.pixel(0, 0) == qRgba(255, 0, 0, 255) &&              // 100% = identity
            cap25.pixel(1, 0) == src.pixel(1, 0);                       // under cap: unchanged
    }

    // 11) Per-channel trim + --chart flag mapping; trim spec from QSettings.
    bool trimOk = false;
    {
        JobConfig t; t.trim = "C:-12,Y:-5";
        const QStringList a = DwripRunner::buildArgs(t);
        const int ti = a.indexOf("--trim");
        const bool argOk = ti >= 0 && a.value(ti + 1) == "C:-12,Y:-5";

        JobConfig ct; ct.chart = true;
        const bool chartOk = DwripRunner::buildArgs(ct).contains("--chart");

        JobConfig rt; rt.regV = 6;
        const QStringList ra = DwripRunner::buildArgs(rt);
        const int ri = ra.indexOf("--reg-v");
        const bool regArgOk = ri >= 0 && ra.value(ri + 1) == "6";

        const QString realApp = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("dwrip-gui-selftest");
        { QSettings s; s.beginGroup("dwrip");
          s.setValue("trimC", -12); s.setValue("trimM", 0);
          s.setValue("trimY", -5);  s.setValue("trimK", 0);
          s.setValue("regV", 6);
          s.endGroup(); s.sync(); }
        const bool specOk = trimSpecFromSettings() == "C:-12,Y:-5";
        JobConfig tcfg; applyPrinterSettings(tcfg);
        const bool regReadOk = tcfg.regV.has_value() && *tcfg.regV == 6;
        QSettings().clear();
        QCoreApplication::setApplicationName(realApp);

        trimOk = argOk && chartOk && specOk && regArgOk && regReadOk;
    }

    // 12) Ink-cost: parse INK_REPORT, fold W1+W2 → W, apply pL + price.
    bool inkCostOk = false;
    {
        InkCostParams p;
        p.pl[0] = 1; p.pl[1] = 2; p.pl[2] = 3;   // small/med/large pL
        for (double &x : p.price) x = 0;
        p.price[0] = 10;   // C per mL
        p.price[4] = 5;    // W per mL
        const QString rep =
            "INK_REPORT C 1000000000 0 0\n"   // 1e9 small × 1pL = 1e9 pL = 1 mL
            "INK_REPORT W1 0 500000000 0\n"   // W1+W2 medium × 2pL = 2e9 pL = 2 mL
            "INK_REPORT W2 0 500000000 0\n";
        double total = 0;
        const QVector<InkUse> r = inkCostCompute(rep, p, &total);
        inkCostOk = r.size() == 5 &&
                    std::abs(r[0].ml - 1.0) < 1e-6 && std::abs(r[0].cost - 10.0) < 1e-6 &&
                    std::abs(r[4].ml - 2.0) < 1e-6 && std::abs(r[4].cost - 10.0) < 1e-6 &&
                    std::abs(total - 20.0) < 1e-6;
    }

    // 15) The dwrip panel must not rewrite settings while it is loading them.
    //     An unguarded save-on-change turned a stored "luminance" back into
    //     "alpha" the next time the panel reloaded, so the engine printed full
    //     white while the preview showed almost none.
    bool panelLoadOk = false;
    {
        const QString realApp = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("dwrip-gui-selftest-load");
        QSettings().clear();
        printersMigrateLegacy();
        printerSetValue("whiteMode", "luminance");
        printerSetValue("whiteDarkPct", 0);
        printerSetValue("whiteCurveX", 30);
        printerSetValue("whiteCurveDelta", 15);

        { DwripPanel panel; }        // constructing it loads — and must not save

        const bool kept = printerValue("whiteMode").toString() == "luminance" &&
                          printerValue("whiteDarkPct").toInt() == 0 &&
                          printerValue("whiteCurveX").toInt() == 30 &&
                          printerValue("whiteCurveDelta").toInt() == 15;

        // …and what it loaded must reach the engine.
        DwripPanel panel2;
        JobConfig cfg;
        panel2.applyTo(cfg);
        const QStringList a2 = DwripRunner::buildArgs(cfg);
        const int mi = a2.indexOf("--white-mode"), ci = a2.indexOf("--white-curve");
        panelLoadOk = kept && mi >= 0 && a2.value(mi + 1) == "luminance" &&
                      a2.contains("--white-dark-pct") &&
                      ci >= 0 && a2.value(ci + 1) == "30,15";

        QSettings().clear();
        QCoreApplication::setApplicationName(realApp);
    }

    // 14) Spot mask generator: the invariants that matter for an emboss job.
    bool spotOk = false;
    {
        // Left half opaque mid-grey, right half transparent.
        QImage art(64, 32, QImage::Format_ARGB32);
        for (int y = 0; y < art.height(); ++y)
            for (int x = 0; x < art.width(); ++x)
                art.setPixel(x, y, x < 32 ? qRgba(128, 128, 128, 255)
                                          : qRgba(128, 128, 128, 0));

        auto raisedOutside = [](const QImage &m) {
            for (int y = 0; y < m.height(); ++y)
                for (int x = 32; x < m.width(); ++x)
                    if (qRed(m.pixel(x, y)) != 0)
                        return true;
            return false;
        };

        SpotParams p;
        const QImage lum = buildSpotMask(art, p);
        p.method = SpotMethod::Shape;
        const QImage shape = buildSpotMask(art, p);
        p.method = SpotMethod::Outline; p.outlinePx = 3;
        const QImage outline = buildSpotMask(art, p);

        // Nothing may be raised where the artwork is transparent: white ink
        // there would sit on the media with no colour over it.
        const bool clipped = !raisedOutside(lum) && !raisedOutside(shape) &&
                             !raisedOutside(outline);
        // The mask must be opaque, or the engine switches to its alpha
        // convention and reads the mask inside-out.
        const bool opaque = qAlpha(shape.pixel(0, 0)) == 255 &&
                            qAlpha(shape.pixel(63, 0)) == 255;
        const bool shapeFull = qRed(shape.pixel(0, 0)) == 255;
        const bool lumMid = qRed(lum.pixel(0, 0)) == 128;

        // Quantising to N layers may only produce N+1 distinct heights.
        p = SpotParams(); p.levels = 4;
        const QImage q = buildSpotMask(art, p);
        QSet<int> heights;
        for (int x = 0; x < 32; ++x)
            heights.insert(qRed(q.pixel(x, 0)));
        spotOk = clipped && opaque && shapeFull && lumMid && heights.size() <= 5;
    }

    // 13) White base: flags appear only when they would change something, so a
    //     default job keeps the argument list it has always had.
    bool whiteBaseOk = false;
    {
        JobConfig c;
        c.whiteMode = "alpha";
        c.whiteDarkPct = 100;
        c.whiteCurveDelta = 0;
        const QStringList def = DwripRunner::buildArgs(c);
        const bool quiet = !def.contains("--white-mode") &&
                           !def.contains("--white-dark-pct") &&
                           !def.contains("--white-curve");

        c.whiteMode = "luminance";
        c.whiteDarkPct = 55;
        c.whiteCurveX = 30;
        c.whiteCurveDelta = 20;
        const QStringList on = DwripRunner::buildArgs(c);
        const int mi = on.indexOf("--white-mode");
        const int di = on.indexOf("--white-dark-pct");
        const int ci = on.indexOf("--white-curve");
        whiteBaseOk = quiet && mi >= 0 && on.value(mi + 1) == "luminance" &&
                  di >= 0 && on.value(di + 1) == "55" &&
                  ci >= 0 && on.value(ci + 1) == "30,20";

        // luminance at 100% thins nothing: the mode still goes out (it is what
        // the user chose) but the redundant dark-pct does not.
        c.whiteDarkPct = 100;
        c.whiteCurveDelta = 0;
        const QStringList mid = DwripRunner::buildArgs(c);
        whiteBaseOk = whiteBaseOk && mid.contains("--white-mode") &&
                  !mid.contains("--white-dark-pct") &&
                  !mid.contains("--white-curve") &&
                  !mid.contains("--white-choke");

        c.whiteChokeMm = 0.25;
        const QStringList ch = DwripRunner::buildArgs(c);
        const int chi = ch.indexOf("--white-choke");
        whiteBaseOk = whiteBaseOk && chi >= 0 && ch.value(chi + 1) == "0.25";

        // …and the preview must actually pull the white in, or it would promise
        // an edge the print does not have.
        QImage art(41, 41, QImage::Format_ARGB32);
        for (int y = 0; y < 41; ++y)
            for (int x = 0; x < 41; ++x)
                art.setPixel(x, y, (x >= 10 && x < 31 && y >= 10 && y < 31)
                                       ? qRgba(0, 0, 0, 255) : qRgba(0, 0, 0, 0));
        auto span = [](const QImage &m) {
            int n = 0;
            for (int x = 0; x < m.width(); ++x)
                if (qAlpha(m.pixel(x, 20)) > 127) n++;
            return n;
        };
        const int wide = span(buildWhiteLayer(art, 100));
        const int narrow = span(buildWhiteLayer(art, 100, false, 100, 50, 0, 3));
        whiteBaseOk = whiteBaseOk && wide == 21 && narrow == 21 - 6;
    }

    // 13) Head cleaning: --clean only appears when a level is set.
    bool cleanOk = false;
    {
        JobConfig c;
        const bool absent = !DwripRunner::buildArgs(c).contains("--clean");
        c.cleanLevel = "gentle";
        const QStringList a2 = DwripRunner::buildArgs(c);
        const int i = a2.indexOf("--clean");
        cleanOk = absent && i >= 0 && a2.value(i + 1) == "gentle";
    }

    const bool ok = argsOk && defOk && iconsOk && snapOk && xformOk && dwripOk &&
                    settingsOk && exportOk && whiteOk && colorOk && trimOk && inkCostOk && cleanOk && whiteBaseOk && spotOk &&
                    panelLoadOk;
    qInfo().noquote() << "[self-test] argv:" << args.join(' ');
    qInfo().noquote() << "[self-test] defaults bed/unit:" << (defOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] icons:" << (iconsOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] snap-to-canvas:" << (snapOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] transforms:" << (xformOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] dwrip panel/save:" << (dwripOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] app settings:" << (settingsOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] export:" << (exportOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] white-layer:" << (whiteOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] colour-limit:" << (colorOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] trim/chart:" << (trimOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] ink-cost:" << (inkCostOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] head-cleaning:" << (cleanOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] white base:" << (whiteBaseOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] spot mask:" << (spotOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test] panel load/save:" << (panelLoadOk ? "ok" : "FAIL");
    qInfo().noquote() << "[self-test]" << (ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("DWRip");   // QSettings location
    QApplication::setApplicationName("dwrip-gui");
    // App/window icon. setWindowIcon covers X11 and the window-manager fallback;
    // setDesktopFileName ties the running window to the installed dwrip.desktop
    // so Wayland (GNOME) shows the launcher icon in the dock instead of a
    // generic gear. Must match the .desktop basename ("dwrip").
    QApplication::setWindowIcon(QIcon(":/icons/appicon.svg"));
    QGuiApplication::setDesktopFileName("dwrip");
    // Lift Qt6's 256 MB QImageReader decode cap — DTF artwork is routinely
    // huge (e.g. 6732×10984 RGBA ≈ 282 MB) and would otherwise fail to import
    // with a null QImage. 0 = no limit (this is a trusted local RIP tool).
    QImageReader::setAllocationLimit(0);
    applyLightTheme(app);
    app.installEventFilter(new WheelGuard(&app));   // spinbox/combo ignore mouse wheel

    QString screenshotPath;
    QStringList openPaths;
    bool showSettings = false;
    bool showProfileEditor = false;
    bool showExportDialog = false;
    bool showInkDialog = false;
    QString spotDialogImage;
    bool whitePreview = false;
    bool colorPreview = false;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--self-test") {
            int rc = selfTest();
            QTimer::singleShot(0, &app, [rc]() { QApplication::exit(rc); });
            return app.exec();
        }
        if (QString(argv[i]) == "--screenshot" && i + 1 < argc)
            screenshotPath = argv[++i];
        else if (QString(argv[i]) == "--open" && i + 1 < argc)
            openPaths << argv[++i];
        else if (QString(argv[i]) == "--settings")
            showSettings = true;
        else if (QString(argv[i]) == "--profile-editor")
            showProfileEditor = true;
        else if (QString(argv[i]) == "--export-dialog")
            showExportDialog = true;
        else if (QString(argv[i]) == "--ink-dialog")
            showInkDialog = true;
        else if (QString(argv[i]) == "--spot-dialog" && i + 1 < argc)
            spotDialogImage = argv[++i];
        else if (QString(argv[i]) == "--white-preview")
            whitePreview = true;
        else if (QString(argv[i]) == "--color-preview")
            colorPreview = true;
    }

    // Upgrade a pre-profile install: fold the flat "dwrip/" settings into the
    // first printer profile. Idempotent and non-destructive. Placed AFTER the
    // argument loop so --self-test (which returns early with its own isolated
    // settings) never rewrites the user's real config.
    printersMigrateLegacy();

    MainWindow w;
    w.resize(1100, 760);
    w.show();
    for (const QString &p : openPaths)
        w.openImageFile(p);
    if (whitePreview)
        w.setWhitePreview(true);
    else if (colorPreview)
        w.setColorPreview(true);

    // For screenshots only: pop the settings dialog non-modally so it can be grabbed.
    QWidget *shotTarget = &w;
    SettingsDialog *dlg = nullptr;
    if (showSettings) {
        dlg = new SettingsDialog(AppSettings{}, &w);
        dlg->setModal(false);
        dlg->show();
        if (!screenshotPath.isEmpty())
            shotTarget = dlg;
    }
    ChannelProfileDialog *pdlg = nullptr;
    if (showProfileEditor) {
        pdlg = new ChannelProfileDialog({}, &w);
        pdlg->setModal(false);
        pdlg->show();
        if (!screenshotPath.isEmpty())
            shotTarget = pdlg;
    }
    SpotMaskDialog *sdlg = nullptr;
    if (!spotDialogImage.isEmpty()) {
        const QImage art(spotDialogImage);
        if (!art.isNull()) {
            sdlg = new SpotMaskDialog(art, spotDialogImage, 8, &w);
            sdlg->setModal(false);
            sdlg->show();
            if (!screenshotPath.isEmpty())
                shotTarget = sdlg;
        }
    }
    ExportDialog *edlg = nullptr;
    if (showExportDialog) {
        JobConfig base;
        base.channels = "WWYKMC"; base.resolution = "1440x720"; base.dropMode = "ML";
        base.colorLimitPct = 70; base.whiteLimitPct = 90;
        base.marginMm = 4.0; base.trim = "C:-12"; base.regV = 4;
        // Set by name, not positionally: this struct gains fields, and a
        // brace-list silently shifts everything when it does.
        ExportImageInfo info;
        info.name = "design.png";
        info.wpx = 400; info.hpx = 300;
        info.wmm = 30.0; info.hmm = 22.5; info.dpi = 300.0;
        edlg = new ExportDialog(QDir::home().filePath("output.prn"), true, base, info, &w);
        edlg->setModal(false);
        edlg->show();
        if (!screenshotPath.isEmpty())
            shotTarget = edlg;
    }
    InkSettingsDialog *idlg = nullptr;
    if (showInkDialog) {
        idlg = new InkSettingsDialog(&w);
        idlg->setModal(false);
        idlg->show();
        if (!screenshotPath.isEmpty())
            shotTarget = idlg;
    }

    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(300, &app, [shotTarget, screenshotPath]() {
            shotTarget->grab().save(screenshotPath);
            QApplication::quit();
        });
    }
    return app.exec();
}
