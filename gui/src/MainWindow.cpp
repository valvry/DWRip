/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "MainWindow.h"
#include "canvas/PrintBedScene.h"
#include "canvas/PrintBedView.h"
#include "canvas/ImageItem.h"
#include "InfoPanel.h"
#include "PropertiesPanel.h"
#include "dwrip/DwripPanel.h"
#include "ThumbnailStrip.h"
#include "SettingsDialog.h"
#include "PrinterProfile.h"
#include "InkCost.h"
#include "ExportDialog.h"
#include "PrintQueueDialog.h"
#include <QCloseEvent>
#include "Compositor.h"
#include "Theme.h"
#include "dwrip/DwripRunner.h"
#include "dwrip/DwripRun.h"
#include "CupsSpooler.h"
#include "dwrip/JobConfig.h"

#include <QWidget>
#include <QPixmap>
#include <QTabWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFrame>
#include <QLabel>
#include <algorithm>


// Wrap a panel in a scroll area so it never forces a tall window minimum
// (keeps the app maximizable/fits on small screens).
static QScrollArea *scrollWrap(QWidget *inner)
{
    auto *sa = new QScrollArea;
    sa->setWidget(inner);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    return sa;
}

// Wrap a panel in a floating white "card" (rounded + soft shadow) with margin
// around it so the card reads as elevated over the app background.
static QWidget *cardWrap(QWidget *inner, int margin = 10)
{
    auto *holder = new QWidget;
    auto *lay = new QVBoxLayout(holder);
    lay->setContentsMargins(margin, 4, margin, margin);
    auto *card = new QFrame;
    card->setObjectName("card");
    auto *cl = new QVBoxLayout(card);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(inner);
    Theme::applyShadow(card, 22, 5, 22);
    lay->addWidget(card);
    return holder;
}
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QActionGroup>
#include <QStatusBar>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QDockWidget>
#include <QShowEvent>
#include <QEvent>
#include <QPushButton>
#include <QTimer>
#include <QIcon>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QTime>
#include <QPlainTextEdit>
#include <QApplication>
#include <QProcess>
#include <QInputDialog>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QCoreApplication>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("DWRip GUI — Print Bed");

    m_settings.load();   // restore persisted bed size & unit before building the UI

    m_scene = new PrintBedScene(m_settings.bedWmm, m_settings.bedHmm,
                                m_settings.unit, this);
    // The view hosts its own rulers (integrated into the viewport margins).
    m_view  = new PrintBedView(m_scene, m_settings.unit, this);
    setCentralWidget(m_view);

    // Left dock — info for the loaded/selected image (scrollable).
    auto *infoDock = new QDockWidget("Image", this);
    infoDock->setObjectName("infoDock");
    m_info = new InfoPanel(infoDock);
    infoDock->setWidget(cardWrap(scrollWrap(m_info)));
    infoDock->setMinimumWidth(232);
    addDockWidget(Qt::LeftDockWidgetArea, infoDock);

    // Right dock — tabbed: "Layout" (image transform) + "dwrip" (future flags).
    auto *dock = new QDockWidget("Properties", this);
    dock->setObjectName("propertiesDock");
    auto *tabs = new QTabWidget(dock);
    tabs->setObjectName("propertiesTabs");
    m_props = new PropertiesPanel(tabs);
    m_props->setUnit(m_settings.unit);
    m_dwrip = new DwripPanel(tabs);
    // Live-refresh the active preview when an ink limit (-L / -l) changes.
    connect(m_dwrip, &DwripPanel::changed, this, [this]() {
        if (m_preview != PreviewMode::None)
            applyPreviewToItem();
    });
    tabs->addTab(scrollWrap(m_props), "Layout");   // scrollable → small min height
    tabs->addTab(scrollWrap(m_dwrip), "dwrip");
    dock->setWidget(cardWrap(tabs));
    dock->setMinimumWidth(262);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // Bottom dock — thumbnail strip / print queue (full width).
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    auto *stripDock = new QDockWidget("Images", this);
    stripDock->setObjectName("stripDock");
    m_strip = new ThumbnailStrip(stripDock);
    stripDock->setWidget(cardWrap(m_strip));
    addDockWidget(Qt::BottomDockWidgetArea, stripDock);

    connect(m_view, &PrintBedView::mouseMovedScene, this, &MainWindow::onMouseMoved);
    connect(m_view, &PrintBedView::itemEdited, this, [this]() {
        if (m_item) { m_props->refresh(); m_info->showItem(m_item); }
    });
    connect(m_strip, &ThumbnailStrip::currentChanged, this, &MainWindow::onStripRowChanged);
    connect(m_strip, &ThumbnailStrip::removeRequested, this, &MainWindow::removeDoc);

    buildActions();
    buildMenu();
    buildToolBar();

    m_posLabel = new QLabel(this);
    m_bedLabel = new QLabel(this);
    m_printerLabel = new QLabel(this);
    m_printerLabel->setToolTip(
        "State of the printer selected in the active profile, as of the time\n"
        "shown. Click ⟳ to read it again.\n"
        "Only local USB queues can be read; a queue on another machine\n"
        "shows \"remote\".\n\n"
        "\"no reply\" after using WIC Reset Utility usually means the printer is\n"
        "left in IEEE-1284.4 mode — power-cycle it to restore communication.");
    m_printerRefresh = new QToolButton(this);
    m_printerRefresh->setText("⟳");
    m_printerRefresh->setAutoRaise(true);
    m_printerRefresh->setToolTip("Read the printer's state now");
    connect(m_printerRefresh, &QToolButton::clicked,
            this, &MainWindow::updatePrinterStatus);
    statusBar()->addPermanentWidget(m_posLabel);
    statusBar()->addPermanentWidget(m_bedLabel);
    statusBar()->addPermanentWidget(m_printerLabel);
    statusBar()->addPermanentWidget(m_printerRefresh);
    updateBedStatus();

    // Read the printer ONCE at startup, then only on demand.
    //
    // There is deliberately no timer here. The printer device allows a single
    // opener, so anything that polls it in the background locks everyone else
    // out — an earlier 5-second poll held the device ~60% of the time and left
    // WIC Reset Utility unable to talk to the printer at all ("Could not open
    // the device. Device or resource busy" in its own log). A status that
    // costs other tools their access is not worth having; the state is
    // refreshed where it actually matters instead (startup, opening Export,
    // after a cleaning) plus the ⟳ button.
    QTimer::singleShot(0, this, &MainWindow::updatePrinterStatus);

    // Cleaner, fixed panels: no float/close buttons on the docks.
    for (QDockWidget *d : findChildren<QDockWidget *>())
        d->setFeatures(QDockWidget::DockWidgetMovable);

    restoreSession();   // bring back images imported in the previous run

    resize(1100, 760);
}

void MainWindow::buildActions()
{
    m_actImport = new QAction(QIcon(":/icons/import.svg"), "&Import PNG…", this);
    m_actImport->setShortcut(QKeySequence("Ctrl+I"));
    m_actImport->setToolTip("Import a PNG onto the bed");
    connect(m_actImport, &QAction::triggered, this, &MainWindow::importImage);

    m_actCloseAll = new QAction("Close &All Images", this);
    m_actCloseAll->setShortcut(QKeySequence("Ctrl+Shift+W"));
    m_actCloseAll->setToolTip("Remove every imported image and clear the workspace");
    connect(m_actCloseAll, &QAction::triggered, this, &MainWindow::closeAllImages);

    m_actExport = new QAction(QIcon(":/icons/export.svg"), "&Export PRN…", this);
    m_actExport->setShortcut(QKeySequence("Ctrl+E"));
    m_actExport->setToolTip("Composite the image → run dwrip → .prn");
    connect(m_actExport, &QAction::triggered, this, &MainWindow::openExport);

    m_actExportProfile = new QAction("Export Printer &Profile…", this);
    m_actExportProfile->setToolTip(
        "Save the ACTIVE printer profile (device, ink layout, colour, calibration,\n"
        "UV, mechanics) plus a copy of its ICC — portable to another machine");
    connect(m_actExportProfile, &QAction::triggered, this, &MainWindow::exportPrinterProfile);

    m_actImportProfile = new QAction("Import Printer Pro&file…", this);
    m_actImportProfile->setToolTip(
        "Load a printer profile file. Always ADDS a new profile — your existing\n"
        "printers and their calibration are never touched");
    connect(m_actImportProfile, &QAction::triggered, this, &MainWindow::importPrinterProfile);

    m_actExportSettings = new QAction("&Backup All Settings…", this);
    m_actExportSettings->setToolTip("Save the whole app configuration (every printer "
                                    "profile, bed, ink prices) to an .ini file");
    connect(m_actExportSettings, &QAction::triggered, this, &MainWindow::exportSettings);

    m_actImportSettings = new QAction("&Restore All Settings…", this);
    m_actImportSettings->setToolTip("REPLACE the whole configuration from a backup "
                                    "— removes every current printer profile");
    connect(m_actImportSettings, &QAction::triggered, this, &MainWindow::importSettings);

    m_actPrintQueue = new QAction("Print &Queue…", this);
    m_actPrintQueue->setToolTip("View / cancel jobs in the CUPS print queue");
    connect(m_actPrintQueue, &QAction::triggered, this, &MainWindow::openPrintQueue);

    m_actNozzleCheck = new QAction("&Nozzle Check…", this);
    m_actNozzleCheck->setToolTip("Print a nozzle-check pattern (a gap = clogged nozzle)");
    connect(m_actNozzleCheck, &QAction::triggered, this, &MainWindow::nozzleCheck);

    m_actHeadClean = new QAction("Head &Cleaning…", this);
    m_actHeadClean->setToolTip("Run the printer's head cleaning — uses ink and "
                               "raises the waste-ink counter");
    connect(m_actHeadClean, &QAction::triggered, this, &MainWindow::headCleaning);

    m_actWasteInk = new QAction("&Waste Ink Counters…", this);
    m_actWasteInk->setToolTip("Read the waste-ink pad counters from the printer");
    connect(m_actWasteInk, &QAction::triggered, this, &MainWindow::readWasteInk);

    m_actZoomIn = new QAction(QIcon(":/icons/zoom-in.svg"), "Zoom &In", this);
    m_actZoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(m_actZoomIn, &QAction::triggered, m_view, &PrintBedView::zoomIn);

    m_actZoomOut = new QAction(QIcon(":/icons/zoom-out.svg"), "Zoom &Out", this);
    m_actZoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(m_actZoomOut, &QAction::triggered, m_view, &PrintBedView::zoomOut);

    m_actFit = new QAction(QIcon(":/icons/fit.svg"), "&Fit Bed", this);
    m_actFit->setShortcut(QKeySequence("Ctrl+0"));
    connect(m_actFit, &QAction::triggered, this, &MainWindow::fitBed);

    m_actWhitePreview = new QAction(QIcon(":/icons/white-layer.svg"), "&White Preview", this);
    m_actWhitePreview->setCheckable(true);
    m_actWhitePreview->setShortcut(QKeySequence("Ctrl+W"));
    m_actWhitePreview->setToolTip("Preview the white-ink layer on a black bed");
    m_actWhitePreview->setEnabled(false);   // needs an active image
    connect(m_actWhitePreview, &QAction::toggled, this, &MainWindow::onPreviewToggled);

    m_actColorPreview = new QAction(QIcon(":/icons/ink-limit.svg"), "&Colour-limit Preview", this);
    m_actColorPreview->setCheckable(true);
    m_actColorPreview->setShortcut(QKeySequence("Ctrl+L"));
    m_actColorPreview->setToolTip("Preview the colour with the ink limit (TAC) applied");
    m_actColorPreview->setEnabled(false);   // needs an active image
    connect(m_actColorPreview, &QAction::toggled, this, &MainWindow::onPreviewToggled);

    // The two preview modes are mutually exclusive (one alternate pixmap at a
    // time); ExclusiveOptional still lets the user switch a mode fully off.
    auto *previewGroup = new QActionGroup(this);
    previewGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    previewGroup->addAction(m_actWhitePreview);
    previewGroup->addAction(m_actColorPreview);

    m_actSettings = new QAction(QIcon(":/icons/settings.svg"), "&Settings…", this);
    m_actSettings->setShortcut(QKeySequence("Ctrl+,"));
    m_actSettings->setToolTip("App settings (bed, unit, printer/media)");
    connect(m_actSettings, &QAction::triggered, this, &MainWindow::openSettings);
}

void MainWindow::buildMenu()
{
    QMenu *file = menuBar()->addMenu("&File");
    file->addAction(m_actImport);
    file->addAction(m_actCloseAll);
    file->addAction(m_actExport);
    file->addSeparator();
    file->addAction(m_actPrintQueue);
    file->addAction(m_actNozzleCheck);
    file->addAction(m_actHeadClean);
    file->addAction(m_actWasteInk);
    file->addSeparator();
    file->addAction(m_actExportProfile);
    file->addAction(m_actImportProfile);
    file->addSeparator();
    file->addAction(m_actExportSettings);
    file->addAction(m_actImportSettings);
    file->addSeparator();
    QAction *quit = file->addAction("&Quit");
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu *view = menuBar()->addMenu("&View");
    view->addAction(m_actZoomIn);
    view->addAction(m_actZoomOut);
    view->addAction(m_actFit);
    view->addSeparator();
    view->addAction(m_actWhitePreview);
    view->addAction(m_actColorPreview);

    QMenu *edit = menuBar()->addMenu("&Edit");
    edit->addAction(m_actSettings);
    edit->addAction("Ink &Cost…", this, [this]() { InkCostDialog(this).exec(); });
}

void MainWindow::buildToolBar()
{
    auto *tb = addToolBar("Main");
    tb->setObjectName("mainToolBar");
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(22, 22));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    setContextMenuPolicy(Qt::NoContextMenu);   // no toolbar right-click toggle menu

    // Brand block (left).
    auto *brand = new QWidget(tb);
    auto *bl = new QVBoxLayout(brand);
    bl->setContentsMargins(4, 0, 12, 0);
    bl->setSpacing(0);
    auto *title = new QLabel("DWRip", brand);
    title->setStyleSheet("font-size:16px; font-weight:800; color:#1e293b;");
    auto *sub = new QLabel("UV RIP", brand);
    sub->setStyleSheet("font-size:10px; font-weight:600; letter-spacing:2px; color:#94a3b8;");
    bl->addWidget(title);
    bl->addWidget(sub);
    tb->addWidget(brand);

    tb->addSeparator();
    m_actImport->setText("Import");
    tb->addAction(m_actImport);
    tb->widgetForAction(m_actImport)->setProperty("class", "tbtext");

    tb->addSeparator();
    tb->addAction(m_actZoomOut);
    tb->addAction(m_actZoomIn);
    tb->addAction(m_actFit);
    tb->addSeparator();
    tb->addAction(m_actWhitePreview);
    tb->addAction(m_actColorPreview);

    // Flexible spacer pushes the rest to the right.
    auto *spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    tb->addAction(m_actSettings);

    // Primary Export button (accent fill, text beside icon).
    m_actExport->setText("Export");
    tb->addAction(m_actExport);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(m_actExport))) {
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setProperty("primary", true);
    }
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(m_actImport)))
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    Theme::applyShadow(tb, 18, 3, 18);   // subtle header elevation
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    // Fit the bed once, after the viewport has a real size.
    if (!m_didInitialFit) {
        m_didInitialFit = true;
        QTimer::singleShot(0, this, &MainWindow::fitBed);
    }
}

void MainWindow::fitBed()
{
    m_view->fitBed(QRectF(0, 0, m_scene->bedWidthMm(), m_scene->bedHeightMm()));
}

void MainWindow::importImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Import PNG", QString(), "Images (*.png *.jpg *.jpeg)");
    if (!path.isEmpty())
        openImageFile(path);
}

void MainWindow::openImageFile(const QString &path)
{
    // Same image already in the list → just activate it (don't add a duplicate).
    for (int i = 0; i < m_docs.size(); ++i) {
        if (m_docs[i].path == path) {
            activateDoc(i);
            statusBar()->showMessage(
                QFileInfo(path).fileName() + " is already in the list — activated.", 4000);
            return;
        }
    }

    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, "Import", "Failed to load image:\n" + path);
        return;
    }

    // One import = one queue entry (job). It does NOT stack on the canvas; it
    // becomes the new active image.
    m_docs.push_back({img, path});

    const QPixmap thumb = QPixmap::fromImage(img).scaled(72, 72, Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation);
    m_strip->addThumb(thumb, QFileInfo(path).fileName());
    activateDoc(m_docs.size() - 1);
}

void MainWindow::saveActiveState()
{
    if (m_item && m_activeIndex >= 0 && m_activeIndex < m_docs.size()) {
        ImageDoc &d = m_docs[m_activeIndex];
        d.pos      = m_item->topLeftMm();
        d.wMm      = m_item->widthMm();
        d.hMm      = m_item->heightMm();
        d.rotation = m_item->rotationDeg();
        d.flipH    = m_item->flipH();
        d.flipV    = m_item->flipV();
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSession();
    QMainWindow::closeEvent(e);
}

// Persist the imported-image list so it survives an app restart. We store each
// entry's source path + non-destructive transform (NOT the pixels) — the image
// is re-loaded from disk on restore. Small, and always matches the source file.
void MainWindow::saveSession()
{
    saveActiveState();   // flush the on-canvas item's transform into m_docs

    QSettings s;
    s.beginGroup("session");
    s.remove("");        // drop any previous session before rewriting
    s.beginWriteArray("docs", m_docs.size());
    for (int i = 0; i < m_docs.size(); ++i) {
        const ImageDoc &d = m_docs[i];
        s.setArrayIndex(i);
        s.setValue("path", d.path);
        s.setValue("posX", d.pos.x());
        s.setValue("posY", d.pos.y());
        s.setValue("wMm", d.wMm);
        s.setValue("hMm", d.hMm);
        s.setValue("rot", d.rotation);
        s.setValue("flipH", d.flipH);
        s.setValue("flipV", d.flipV);
        s.setValue("exportCount", d.exportCount);
    }
    s.endArray();
    s.setValue("activeIndex", m_activeIndex);
    s.endGroup();
}

void MainWindow::restoreSession()
{
    QSettings s;
    s.beginGroup("session");
    const int n = s.beginReadArray("docs");
    int missing = 0;
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        const QString path = s.value("path").toString();
        QImage img(path);
        if (path.isEmpty() || img.isNull()) {   // moved / deleted since last run
            ++missing;
            continue;
        }
        ImageDoc d;
        d.image    = img;
        d.path     = path;
        d.pos      = QPointF(s.value("posX", 0.0).toDouble(), s.value("posY", 0.0).toDouble());
        d.wMm      = s.value("wMm", -1.0).toDouble();
        d.hMm      = s.value("hMm", -1.0).toDouble();
        d.rotation = s.value("rot", 0.0).toDouble();
        d.flipH    = s.value("flipH", false).toBool();
        d.flipV    = s.value("flipV", false).toBool();
        d.exportCount = s.value("exportCount", 0).toInt();
        m_docs.push_back(d);

        const QPixmap thumb = QPixmap::fromImage(img).scaled(72, 72, Qt::KeepAspectRatio,
                                                             Qt::SmoothTransformation);
        const int row = m_strip->addThumb(thumb, QFileInfo(path).fileName());
        m_strip->setBadge(row, d.exportCount);
    }
    int active = s.value("activeIndex", -1).toInt();
    s.endArray();
    s.endGroup();

    if (!m_docs.isEmpty()) {
        if (active < 0 || active >= m_docs.size())
            active = 0;
        activateDoc(active);
    }
    if (missing > 0)
        statusBar()->showMessage(
            QString("%1 image(s) from the last session could not be found.").arg(missing), 6000);
}

// Ink limits the dwrip panel persists, so the preview matches what will print.
static int inkLimitFromSettings(const char *key)
{
    QSettings s;
    s.beginGroup("dwrip");
    const int v = s.value(key, 100).toInt();
    s.endGroup();
    return v;
}

void MainWindow::onPreviewToggled()
{
    m_preview = m_actWhitePreview->isChecked() ? PreviewMode::White
              : m_actColorPreview->isChecked() ? PreviewMode::ColorLimit
                                               : PreviewMode::None;
    m_scene->setWhitePreview(m_preview == PreviewMode::White);   // black bed only for white
    applyPreviewToItem();
}

void MainWindow::applyPreviewToItem()
{
    if (!m_item)
        return;
    if (m_preview != PreviewMode::None && m_activeIndex >= 0) {
        const QImage &src = m_docs[m_activeIndex].image;
        const QImage pv = (m_preview == PreviewMode::White)
            ? buildWhiteLayer(src, inkLimitFromSettings("whiteLimit"),
                              printerValue("whiteMode", "alpha").toString()
                                  == "luminance",
                              printerValue("whiteDarkPct", 100).toInt(),
                              printerValue("whiteCurveX", 50).toInt(),
                              printerValue("whiteCurveDelta", 0).toInt(),
                              whiteChokePx(src))
            : buildColorLimitProof(src, inkLimitFromSettings("colorLimit"));
        m_item->setPreviewPixmap(QPixmap::fromImage(pv));
    }
    m_item->setPreviewActive(m_preview != PreviewMode::None);
}

void MainWindow::activateDoc(int index)
{
    if (index < 0 || index >= m_docs.size())
        return;

    saveActiveState();
    if (m_item) {
        m_scene->removeItem(m_item);
        delete m_item;
        m_item = nullptr;
    }
    m_activeIndex = index;
    const ImageDoc &d = m_docs[index];
    m_item = new ImageItem(d.image, d.path);
    // Restore the non-destructive transform stored for this entry.
    if (d.wMm > 0 && d.hMm > 0)
        m_item->setSizeMm(d.wMm, d.hMm);
    m_item->setFlipH(d.flipH);
    m_item->setFlipV(d.flipV);
    m_item->setRotationDeg(d.rotation);
    m_scene->addItem(m_item);
    m_item->setTopLeftMm(d.pos);      // d.pos is the bbox top-left (default 0,0)
    m_scene->clearSelection();
    m_item->setSelected(true);

    m_strip->setCurrentIndex(index);  // sync queue highlight (no signal)
    m_info->showItem(m_item);         // info panel follows the ACTIVE image
    m_props->bind(m_item);            // transform controls drive the ACTIVE image
    m_view->setActiveItem(m_item);    // show resize/rotate handles on canvas
    // Refresh position fields live while the image is dragged (after bind).
    m_item->setOnChanged([this]() { m_props->refreshPosition(); });

    m_actWhitePreview->setEnabled(true);
    m_actColorPreview->setEnabled(true);
    applyPreviewToItem();              // build preview pixmap if a mode is on

    statusBar()->showMessage(
        QString("Active: %1 — %2 × %3 @ %4 dpi")
            .arg(QFileInfo(d.path).fileName())
            .arg(fmtUnit(m_item->widthMm(), m_settings.unit))
            .arg(fmtUnit(m_item->heightMm(), m_settings.unit))
            .arg(m_item->dpi(), 0, 'f', 0),
        6000);
}

void MainWindow::onStripRowChanged(int row)
{
    if (row < 0 || row == m_activeIndex)
        return;
    activateDoc(row);
}

void MainWindow::removeDoc(int row)
{
    if (row < 0 || row >= m_docs.size())
        return;

    const bool removingActive = (row == m_activeIndex);
    if (removingActive && m_item) {
        m_scene->removeItem(m_item);
        delete m_item;
        m_item = nullptr;
        m_activeIndex = -1;
    } else if (row < m_activeIndex) {
        --m_activeIndex;              // indices shift down
    }

    m_strip->removeAt(row);
    m_docs.removeAt(row);

    if (m_docs.isEmpty()) {
        m_activeIndex = -1;
        m_info->showItem(nullptr);    // canvas empty → clear the info panel
        m_props->bind(nullptr);
        m_view->setActiveItem(nullptr);
        m_strip->setCurrentIndex(-1);
        m_actWhitePreview->setChecked(false);  // leave preview cleanly (resets bed)
        m_actColorPreview->setChecked(false);
        m_actWhitePreview->setEnabled(false);
        m_actColorPreview->setEnabled(false);
        return;
    }
    if (removingActive)
        activateDoc(std::min(row, int(m_docs.size()) - 1));
    else
        m_strip->setCurrentIndex(m_activeIndex);
}

void MainWindow::closeAllImages()
{
    if (m_docs.isEmpty())
        return;
    if (QMessageBox::question(
            this, "Close All Images",
            QString("Close all %1 image(s) and clear the workspace?")
                .arg(m_docs.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    if (m_item) {
        m_scene->removeItem(m_item);
        delete m_item;
        m_item = nullptr;
    }
    m_activeIndex = -1;
    m_docs.clear();
    while (m_strip->count() > 0)          // no clear() API — drop cards one by one
        m_strip->removeAt(0);

    // Same empty-state teardown as removeDoc()'s last-image branch.
    m_info->showItem(nullptr);
    m_props->bind(nullptr);
    m_view->setActiveItem(nullptr);
    m_strip->setCurrentIndex(-1);
    m_actWhitePreview->setChecked(false);
    m_actColorPreview->setChecked(false);
    m_actWhitePreview->setEnabled(false);
    m_actColorPreview->setEnabled(false);
}

void MainWindow::onMouseMoved(const QPointF &scenePos)
{
    // Ruler cursor markers are updated inside PrintBedView; here we only mirror
    // the position into the status bar.
    m_posLabel->setText(QString("  X: %1   Y: %2  ")
                            .arg(fmtUnit(scenePos.x(), m_settings.unit))
                            .arg(fmtUnit(scenePos.y(), m_settings.unit)));
}

void MainWindow::updateBedStatus()
{
    m_bedLabel->setText(QString("  Bed: %1 × %2  ")
                            .arg(fmtUnit(m_scene->bedWidthMm(), m_settings.unit, 0))
                            .arg(fmtUnit(m_scene->bedHeightMm(), m_settings.unit, 0)));
}

void MainWindow::updatePrinterStatus()
{
    // Follow the printer the user actually selected, not "whatever Epson is
    // plugged in" — with two machines those are different answers, and a status
    // for the wrong one is worse than no status at all.
    const QString queue = printerValue("cupsQueue").toString();
    if (queue.isEmpty()) {
        m_printerLabel->setText("  Printer: none selected  ");
        return;
    }

    // Only local USB printers can be asked directly. A queue on another machine
    // (smb:, ipp:) has no device node here, so say so rather than reporting some
    // other printer's state.
    const QString serial = cupsUsbSerial(queue);
    if (serial.isEmpty()) {
        m_printerLabel->setText("  Printer: remote (no status)  ");
        return;
    }

    // Never touch the device while CUPS is feeding it a job: usblp allows a
    // single opener, and the queue already tells us what is happening.
    if (!cupsListJobs().isEmpty()) {
        m_printerLabel->setText("  Printer: sending job  ");
        return;
    }

    QString name;
    const int code = dwripPrinterStatus(&name, serial);
    QString text;
    if (code == -2)             text = "busy";
    else if (code < 0)          text = "no reply";
    else                        text = name;
    // Stamp it: without a timer the reading ages, and a stale state presented
    // as current is worse than an obviously old one.
    m_printerLabel->setText(QString("  Printer: %1 (%2)  ")
                                .arg(text, QTime::currentTime().toString("HH:mm")));
}

// The choke is set in millimetres, but the preview works in source pixels —
// convert through the item's print size. The engine does its own conversion
// against the raster, which is not square; this image is, so one radius covers
// both axes here.
int MainWindow::whiteChokePx(const QImage &src) const
{
    const double mm = printerValue("whiteChoke", 0).toInt() / 100.0;
    if (mm <= 0 || !m_item || m_item->widthMm() <= 0 || src.isNull())
        return 0;
    const double pxPerMm = src.width() / m_item->widthMm();
    return int(mm * pxPerMm + 0.5);
}

// Dedicated folder for generated PRN files. Defaults to ~/DWRip/PRN and then
// follows the last folder the user actually saved into (paths/prnDir). Created
// on demand so the export default never dumps .prn files loose in $HOME.
static QString prnOutputDir()
{
    QSettings s;
    QString dir = s.value("paths/prnDir").toString();
    if (dir.isEmpty())
        dir = QDir::home().filePath("DWRip/PRN");
    QDir().mkpath(dir);
    return dir;
}

void MainWindow::openExport()
{
    if (!m_item) {
        QMessageBox::information(this, "Export", "No active image to export.");
        return;
    }

    if (locateDwrip().isEmpty()) {
        QMessageBox::warning(this, "Export",
            "dwrip binary not found.\nBuild it at the repo root first:  make");
        return;
    }

    // Default output name from the active image.
    const QString base = QFileInfo(m_docs[m_activeIndex].path).completeBaseName();
    const QString defOut = QDir(prnOutputDir())
                               .filePath((base.isEmpty() ? "output" : base) + ".prn");

    // About to print: a fresh reading is worth one device access here.
    updatePrinterStatus();

    // ── Assemble Tier1+Tier3 up front (for the summary + reuse on accept) ──
    JobConfig baseCfg;
    applyPrinterSettings(baseCfg);  // whole active printer profile: channels, resolution,
                                    // drop, ink limits, ICC, UV, mechanics, calibration
    // applyPrinterSettings() already covers what the dwrip panel edits (it reads
    // the same profile keys, and the panel auto-saves), so no second pass here.
    const bool sendConfigured = !baseCfg.cupsQueue.isEmpty();

    const ImageDoc &d = m_docs[m_activeIndex];
    ExportImageInfo info;
    info.name = QFileInfo(d.path).fileName();
    info.path = d.path;
    info.image = d.image;
    info.wpx = d.image.width();
    info.hpx = d.image.height();
    info.wmm = m_item->widthMm();
    info.hmm = m_item->heightMm();
    info.dpi = m_item->dpi();

    // ── Composite the active image to a temp PNG up front, so the dialog's
    // "Estimate ink & cost" button can run dwrip --estimate on the same input.
    QSizeF sizeMm;
    const QImage comp = renderActiveImage(m_item, 300.0, sizeMm);
    const QString tmpPng = QDir::temp().filePath("dwrip_gui_composite.png");
    if (comp.isNull() || !comp.save(tmpPng, "PNG")) {
        QMessageBox::warning(this, "Export", "Failed to composite the image.");
        return;
    }
    baseCfg.inputPath = tmpPng;
    baseCfg.sizeWmm = sizeMm.width();
    baseCfg.sizeHmm = sizeMm.height();

    ExportDialog dlg(defOut, sendConfigured, baseCfg, info, this);
    const int dlgRc = dlg.exec();
    // The dialog can switch the active printer and edit resolution/drop mode,
    // so re-sync the panel that shows those either way (accepted or cancelled —
    // a printer switch persists as soon as it is made).
    m_dwrip->reload();
    if (dlgRc != QDialog::Accepted) {
        QFile::remove(tmpPng);
        return;
    }
    // Re-read the profile in case the dialog switched printers mid-flight.
    {
        const QString keepInput = baseCfg.inputPath;
        const auto keepW = baseCfg.sizeWmm, keepH = baseCfg.sizeHmm;
        baseCfg = JobConfig();
        applyPrinterSettings(baseCfg);
        baseCfg.inputPath = keepInput;
        baseCfg.sizeWmm = keepW;
        baseCfg.sizeHmm = keepH;
    }

    // ── Final JobConfig: Tier1+Tier3 (base) + Tier2 (dialog) ──
    JobConfig cfg = baseCfg;
    dlg.applyTo(cfg);            // mode, white-first, zones, output, send-toggle

    // Print-only: no output path but a queue is set → write to a temp PRN,
    // spool it, then delete it (nothing kept on disk). Otherwise remember the
    // folder the user saved into so the next export defaults there.
    bool ephemeralPrn = false;
    if (cfg.outputPath.isEmpty() && !cfg.cupsQueue.isEmpty()) {
        cfg.outputPath = QDir::temp().filePath(
            "dwrip_print_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".prn");
        ephemeralPrn = true;
    } else if (!cfg.outputPath.isEmpty()) {
        const QString d = QFileInfo(cfg.outputPath).absolutePath();
        if (!d.isEmpty())
            QSettings().setValue("paths/prnDir", d);
    }

    // ── Run dwrip, streaming log into a modal progress dialog ──
    const int idx = m_activeIndex;   // active doc is fixed during the modal export
    const QString ephemeral = ephemeralPrn ? cfg.outputPath : QString();
    runDwripWithLog(this, cfg, [this, tmpPng, idx, ephemeral](int code) {
        QFile::remove(tmpPng);
        if (!ephemeral.isEmpty())    // spooling already happened before this callback
            QFile::remove(ephemeral);
        if (code == 0 && idx >= 0 && idx < m_docs.size()) {
            m_docs[idx].exportCount += 1;                       // count this export/print
            m_strip->setBadge(idx, m_docs[idx].exportCount);
        }
    });
}

// Copy every key from one QSettings store into another (values verbatim).
static void copyAllSettings(QSettings &src, QSettings &dst)
{
    const QStringList keys = src.allKeys();
    for (const QString &k : keys)
        dst.setValue(k, src.value(k));
    dst.sync();
}

void MainWindow::exportPrinterProfile()
{
    const QString id = activePrinterId();
    if (id.isEmpty()) {
        QMessageBox::warning(this, "Export Printer Profile", "No printer profile to export.");
        return;
    }
    const QString name = printerName(id);
    QString path = QFileDialog::getSaveFileName(
        this, "Export Printer Profile",
        QDir::home().filePath(name + ".dwrip-profile"),
        "DWRip printer profile (*.dwrip-profile)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".dwrip-profile", Qt::CaseInsensitive))
        path += ".dwrip-profile";

    QString note;
    if (!printerExport(id, path, &note)) {
        QMessageBox::warning(this, "Export Printer Profile", note);
        return;
    }
    QString msg = "Profile \"" + name + "\" saved to:\n" + path;
    if (!note.isEmpty())
        msg += "\n\n" + note;          // e.g. the ICC file was missing
    else
        msg += "\n\nA copy of the ICC profile was written next to it — keep the "
               "two files together when sharing.";
    QMessageBox::information(this, "Export Printer Profile", msg);
}

void MainWindow::importPrinterProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Import Printer Profile", QDir::homePath(),
        "DWRip printer profile (*.dwrip-profile);;All files (*)");
    if (path.isEmpty())
        return;

    QString err, warn;
    const QString id = printerImport(path, &err, &warn);
    if (id.isEmpty()) {
        QMessageBox::warning(this, "Import Printer Profile", err);
        return;
    }
    const QString name = printerName(id);
    QString msg = "Added printer profile \"" + name + "\".\n"
                  "Your existing profiles were not changed.";
    if (!warn.isEmpty())
        msg += "\n\n" + warn;
    msg += "\n\nMake it the active printer now?";
    if (QMessageBox::question(this, "Import Printer Profile", msg,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::Yes) == QMessageBox::Yes) {
        setActivePrinter(id);
        m_dwrip->reload();
    }
}

void MainWindow::exportSettings()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Backup All Settings",
        QDir::home().filePath("dwrip-settings.ini"),
        "Settings (*.ini)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".ini", Qt::CaseInsensitive))
        path += ".ini";

    QSettings src;                                  // native app store
    QSettings dst(path, QSettings::IniFormat);      // target file
    dst.clear();                                    // don't merge into a stale file
    copyAllSettings(src, dst);

    if (dst.status() != QSettings::NoError) {
        QMessageBox::warning(this, "Backup All Settings",
            "Failed to write:\n" + path);
        return;
    }
    QMessageBox::information(this, "Backup All Settings",
        "Backup saved to:\n" + path);
}

void MainWindow::importSettings()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Restore All Settings", QDir::homePath(), "Settings (*.ini)");
    if (path.isEmpty())
        return;

    QSettings src(path, QSettings::IniFormat);
    if (src.status() != QSettings::NoError || src.allKeys().isEmpty()) {
        QMessageBox::warning(this, "Restore All Settings",
            "Not a valid settings file:\n" + path);
        return;
    }

    if (QMessageBox::question(this, "Restore All Settings",
            "This REPLACES your whole configuration, including every printer "
            "profile and its calibration.\n\nRestore from:\n" + path + " ?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QSettings dst;                 // native app store
    dst.clear();                   // full replace, not a merge
    // Skip the machine-local groups: which documents were open and where the
    // window sat say nothing about a printer and only cause surprises when a
    // backup is restored onto a different machine.
    for (const QString &k : src.allKeys()) {
        if (k.startsWith("session/") || k.startsWith("ui/"))
            continue;
        dst.setValue(k, src.value(k));
    }
    dst.sync();

    // Refresh everything that caches settings in memory.
    m_settings.load();
    m_scene->setUnit(m_settings.unit);
    m_view->setUnit(m_settings.unit);
    m_props->setUnit(m_settings.unit);
    m_info->setUnit(m_settings.unit);
    m_scene->setBedSize(m_settings.bedWmm, m_settings.bedHmm);
    m_view->fitBed(QRectF(0, 0, m_settings.bedWmm, m_settings.bedHmm));
    if (m_item)
        m_item->setTopLeftMm(m_item->topLeftMm());   // re-clamp inside new bed
    updateBedStatus();
    m_dwrip->reload();

    QMessageBox::information(this, "Restore All Settings",
        "Configuration restored from:\n" + path);
}

void MainWindow::openPrintQueue()
{
    // Non-modal, owned by this window; reuse a single instance if already open.
    auto *dlg = findChild<PrintQueueDialog *>();
    if (!dlg) {
        dlg = new PrintQueueDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
    }
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::nozzleCheck()
{
    if (locateDwrip().isEmpty()) {
        QMessageBox::warning(this, "Nozzle Check",
            "dwrip binary not found.\nBuild it at the repo root first:  make");
        return;
    }

    // No image needed — dwrip synthesises the pattern. Use Tier-1 device
    // settings (resolution/bidi/printer); ICC irrelevant.
    JobConfig cfg;
    applyPrinterSettings(cfg);
    cfg.iccPath.clear();
    cfg.intent.clear();
    cfg.nozzleCheck = true;

    if (cfg.cupsQueue.isEmpty()) {
        const QString p = QFileDialog::getSaveFileName(
            this, "Save nozzle-check PRN", QDir(prnOutputDir()).filePath("dwrip_nozzle.prn"),
            "PRN (*.prn)");
        if (p.isEmpty())
            return;
        cfg.outputPath = p;
    } else {
        cfg.outputPath = QDir::temp().filePath("dwrip_nozzle.prn");
    }

    runDwripWithLog(this, cfg);
}

void MainWindow::headCleaning()
{
    const QString bin = locateDwrip();
    if (bin.isEmpty()) {
        QMessageBox::warning(this, "Head Cleaning",
            "dwrip binary not found.\nBuild it at the repo root first:  make");
        return;
    }

    // Ask the engine which levels exist rather than repeating the table here —
    // it owns the captured command bytes, so it owns the list too.
    QProcess p;
    p.start(bin, {"--clean", "list"});
    p.waitForFinished(5000);
    QList<QPair<QString, QString>> levels;   // name, duration text
    const QRegularExpression re("^\\s+(\\w+)\\s+(~\\d+\\s*s)");
    for (const QString &line : QString::fromUtf8(p.readAllStandardOutput())
                                   .split('\n')) {
        const auto m = re.match(line);
        if (m.hasMatch())
            levels.append({m.captured(1), m.captured(2)});
    }
    if (levels.isEmpty()) {
        QMessageBox::warning(this, "Head Cleaning",
            "dwrip reported no cleaning levels.");
        return;
    }

    QStringList items;
    for (const auto &l : levels)
        items << QString("%1  (%2)").arg(l.first, l.second);
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, "Head Cleaning",
        "Cleaning level — stronger clears more, but uses more ink:",
        items, 0, false, &ok);
    if (!ok)
        return;
    const QString level = levels.at(items.indexOf(pick)).first;

    // Cleaning is irreversible spending: ink out of the tanks and one more tick
    // on a waste counter that cannot be un-ticked. Make that explicit.
    if (QMessageBox::warning(this, "Head Cleaning",
            QString("Run a '%1' head cleaning now?\n\n"
                    "This uses ink and raises the printer's waste-ink counter.\n"
                    "Do not switch the printer off while it runs.").arg(level),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes)
        return;

    // From here it is the nozzle-check flow: no image, device settings only.
    JobConfig cfg;
    applyPrinterSettings(cfg);
    cfg.iccPath.clear();
    cfg.intent.clear();
    cfg.cleanLevel = level;

    if (cfg.cupsQueue.isEmpty()) {
        const QString f = QFileDialog::getSaveFileName(
            this, "Save cleaning command",
            QDir(prnOutputDir()).filePath("dwrip_clean_" + level + ".prn"),
            "PRN (*.prn)");
        if (f.isEmpty())
            return;
        cfg.outputPath = f;
    } else {
        cfg.outputPath = QDir::temp().filePath("dwrip_clean.prn");
    }

    runDwripWithLog(this, cfg);

    // Spooling only proves CUPS accepted the bytes; the cleaning itself runs
    // for another minute. Watch the printer's own status so the user knows when
    // it is safe to print (or to run a nozzle check).
    if (!cfg.cupsQueue.isEmpty()) {
        waitWhilePrinterBusy(this, "Head Cleaning",
                             cupsUsbSerial(cfg.cupsQueue));
        updatePrinterStatus();
    }
}

void MainWindow::readWasteInk()
{
    if (locateDwrip().isEmpty()) {
        QMessageBox::warning(this, "Waste Ink Counters",
            "dwrip binary not found.\nBuild it at the repo root first:  make");
        return;
    }
    const QString queue = printerValue("cupsQueue").toString();
    const QString serial = cupsUsbSerial(queue);
    if (serial.isEmpty()) {
        QMessageBox::information(this, "Waste Ink Counters",
            "The selected printer is not a local USB queue, so its counters "
            "cannot be read from here.");
        return;
    }

    // Reading needs IEEE-1284.4, which switches the printer into a mode where
    // plain ESC/P goes unanswered until it is power-cycled. Nothing breaks —
    // printing is unaffected and the status reader falls back to D4 — but the
    // user should not meet that as a surprise.
    if (QMessageBox::question(this, "Waste Ink Counters",
            "Read the waste-ink counters from the printer?\n\n"
            "This switches the printer into IEEE-1284.4 mode (the same mode "
            "WIC Reset Utility uses). Printing is unaffected; the printer "
            "returns to normal mode when it is next switched off and on.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes)
        != QMessageBox::Yes)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<WasteCounter> counters = dwripReadWaste(serial);
    QApplication::restoreOverrideCursor();

    if (counters.isEmpty()) {
        QMessageBox::warning(this, "Waste Ink Counters",
            "Could not read the counters from the printer.");
        return;
    }

    QString text = "<table cellpadding=4>";
    for (const WasteCounter &w : counters)
        text += QString("<tr><td>%1</td><td align=right><b>%2%</b></td>"
                        "<td align=right>%3</td></tr>")
                    .arg(w.name).arg(w.percent, 0, 'f', 2).arg(w.value);
    text += "</table>";
    QMessageBox box(QMessageBox::Information, "Waste Ink Counters", text,
                    QMessageBox::Ok, this);
    box.setInformativeText("The printer stops when a counter reaches 100%.");
    // Reset lives here rather than in the menu on purpose: you have to read the
    // counters, and see them, before you can zero them.
    QPushButton *reset = box.addButton("Reset counter…", QMessageBox::ResetRole);
    box.exec();
    if (box.clickedButton() != reset) {
        updatePrinterStatus();
        return;
    }

    // Writing the EEPROM cannot be undone, and zeroing the counter does not
    // empty the pad — on a printer with the original pad that is how ink ends
    // up on the desk.
    if (QMessageBox::warning(this, "Reset Waste Counter",
            "Set waste counter 1 back to zero?\n\n"
            "This writes the printer's EEPROM and CANNOT be undone.\n\n"
            "It does not empty the waste pad. Only reset after the pad has "
            "been serviced, or if the printer drains to an external tank.",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes) {
        updatePrinterStatus();
        return;
    }

    QString log;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = dwripResetWaste(serial, &log);
    QApplication::restoreOverrideCursor();

    QMessageBox result(ok ? QMessageBox::Information : QMessageBox::Warning,
                       "Reset Waste Counter",
                       ok ? "Waste counter reset."
                          : "The printer did not accept the reset.",
                       QMessageBox::Ok, this);
    if (!log.isEmpty())
        result.setDetailedText(log);
    result.exec();

    updatePrinterStatus();
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_settings, this);
    const int rc = dlg.exec();
    // The dialog can switch the active printer (which persists immediately) and
    // its Quality tab edits the same settings the dwrip panel shows, so re-sync
    // the panel either way.
    m_dwrip->reload();
    if (rc != QDialog::Accepted)
        return;   // Cancel → discard (profile fields are only written on OK)

    const AppSettings s = dlg.settings();
    const bool unitChanged = (s.unit != m_settings.unit);
    const bool bedChanged  = (s.bedWmm != m_settings.bedWmm || s.bedHmm != m_settings.bedHmm);
    m_settings = s;

    if (unitChanged) {
        m_scene->setUnit(s.unit);
        m_view->setUnit(s.unit);
        m_props->setUnit(s.unit);
        m_info->setUnit(s.unit);
    }
    if (bedChanged) {
        m_scene->setBedSize(s.bedWmm, s.bedHmm);
        m_view->fitBed(QRectF(0, 0, s.bedWmm, s.bedHmm));
        if (m_item)
            m_item->setTopLeftMm(m_item->topLeftMm());   // re-clamp inside new bed
    }
    updateBedStatus();
}
