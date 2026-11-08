/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QWidget>
#include "Units.h"

class ImageItem;
class QLabel;

// Left-side panel showing metadata for the loaded/selected image:
// thumbnail, file name, file size, pixel dimensions, print size, DPI, colour mode.
class InfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit InfoPanel(QWidget *parent = nullptr);
    void setUnit(Unit u);

public slots:
    void showItem(const ImageItem *item);   // nullptr → "no image" state

private:
    void refresh();

    Unit m_unit = Unit::Cm;
    const ImageItem *m_item = nullptr;

    QLabel *m_thumb = nullptr;
    QLabel *m_name  = nullptr;
    QLabel *m_location = nullptr;   // full source path (disambiguates same-named files)
    QLabel *m_size  = nullptr;
    QLabel *m_dims  = nullptr;
    QLabel *m_print = nullptr;
    QLabel *m_dpi   = nullptr;
    QLabel *m_mode  = nullptr;
};
