/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QMainWindow>
#include <QVector>
#include <QImage>
#include <QPointF>
#include <QString>
#include "AppSettings.h"

class PrintBedScene;
class PrintBedView;
class ImageItem;
class InfoPanel;
class PropertiesPanel;
class DwripPanel;
class ThumbnailStrip;
class QLabel;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Import an image from a path (no dialog); used by --open and tests.
    void openImageFile(const QString &path);

    // Toggle a preview mode programmatically (used by --white-preview/--color-preview/tests).
    void setWhitePreview(bool on) { if (m_actWhitePreview) m_actWhitePreview->setChecked(on); }
    void setColorPreview(bool on) { if (m_actColorPreview) m_actColorPreview->setChecked(on); }

protected:
    void showEvent(QShowEvent *e) override;
    void closeEvent(QCloseEvent *e) override;   // persist the session on exit

private slots:
    void importImage();
    void onMouseMoved(const QPointF &scenePos);
    void onStripRowChanged(int row);
    void removeDoc(int row);
    void closeAllImages();   // clear every imported image + reset the workspace
    void openSettings();
    void openExport();
    void exportPrinterProfile();
    void importPrinterProfile();
    void exportSettings();   // dump all QSettings to a chosen .ini file
    void importSettings();   // load QSettings from a chosen .ini file
    void openPrintQueue();   // show the CUPS print queue viewer
    void nozzleCheck();      // run dwrip --nozzle-check (printer maintenance)
    void headCleaning();     // run dwrip --clean LEVEL (printer maintenance)
    void readWasteInk();     // run dwrip --waste (manual, uses D4)
    void onPreviewToggled();   // White / Colour-limit preview actions (exclusive)

private:
    void buildActions();
    void buildMenu();
    void buildToolBar();
    void updateBedStatus();
    void updatePrinterStatus();   // read the printer once, refresh m_printerLabel
    int  whiteChokePx(const QImage &src) const;  // --white-choke mm → source px
    void fitBed();
    void applyPreviewToItem();   // (re)build & push the active preview pixmap

    enum class PreviewMode { None, White, ColorLimit };

    // One imported image = one print job. The canvas shows the active one only.
    // Transforms are non-destructive: stored here, applied to the ImageItem.
    struct ImageDoc {
        QImage  image;
        QString path;
        QPointF pos{0, 0};
        double  wMm = -1.0;       // -1 = native size
        double  hMm = -1.0;
        double  rotation = 0.0;
        bool    flipH = false;
        bool    flipV = false;
        int     exportCount = 0;   // times this image was exported/printed
    };
    void activateDoc(int index);     // swap the canvas to docs[index]
    void saveActiveState();          // persist pos/size of the on-canvas item
    void saveSession();              // write the doc list (paths + transforms) to QSettings
    void restoreSession();           // reload the doc list from QSettings at startup

    AppSettings m_settings;

    PrintBedScene *m_scene = nullptr;
    PrintBedView  *m_view = nullptr;
    InfoPanel     *m_info = nullptr;
    PropertiesPanel *m_props = nullptr;
    DwripPanel    *m_dwrip = nullptr;
    ThumbnailStrip *m_strip = nullptr;

    QVector<ImageDoc> m_docs;
    int        m_activeIndex = -1;
    ImageItem *m_item = nullptr;      // the single item currently on the canvas

    QAction *m_actImport   = nullptr;
    QAction *m_actCloseAll = nullptr;
    QAction *m_actExport   = nullptr;
    QAction *m_actExportProfile = nullptr;   // single printer profile → file
    QAction *m_actImportProfile = nullptr;   // file → NEW printer profile
    QAction *m_actExportSettings = nullptr;  // whole config backup
    QAction *m_actImportSettings = nullptr;  // whole config restore (destructive)
    QAction *m_actPrintQueue = nullptr;
    QAction *m_actNozzleCheck = nullptr;
    QAction *m_actHeadClean = nullptr;
    QAction *m_actWasteInk = nullptr;
    QAction *m_actZoomIn   = nullptr;
    QAction *m_actZoomOut  = nullptr;
    QAction *m_actFit      = nullptr;
    QAction *m_actWhitePreview = nullptr;
    QAction *m_actColorPreview = nullptr;
    QAction *m_actSettings = nullptr;

    PreviewMode m_preview = PreviewMode::None;   // canvas preview mode (transient)

    QLabel *m_posLabel = nullptr;   // cursor position (display unit)
    QLabel *m_bedLabel = nullptr;   // bed size (display unit)
    QLabel *m_printerLabel = nullptr;  // last-read printer state (ready/printing/…)
    class QToolButton *m_printerRefresh = nullptr;  // read the state now

    bool m_didInitialFit = false;
};
