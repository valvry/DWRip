/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "InfoPanel.h"
#include "canvas/ImageItem.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFileInfo>
#include <QPixmap>
#include <algorithm>

static QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    const double kb = bytes / 1024.0;
    if (kb < 1024.0)
        return QString::number(kb, 'f', 1) + " KB";
    return QString::number(kb / 1024.0, 'f', 2) + " MB";
}

InfoPanel::InfoPanel(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(10);

    m_thumb = new QLabel(this);
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setFixedHeight(160);
    m_thumb->setStyleSheet(
        "background:#f8fafc; border:1px dashed #cfd8e6; border-radius:10px;"
        " color:#94a3b8; font-size:12px;");
    v->addWidget(m_thumb);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);

    m_name  = new QLabel(this);  m_name->setWordWrap(true);
    m_location = new QLabel(this);  m_location->setWordWrap(true);
    m_location->setStyleSheet("color:#64748b;");   // muted, secondary detail
    m_size  = new QLabel(this);
    m_dims  = new QLabel(this);
    m_print = new QLabel(this);
    m_dpi   = new QLabel(this);
    m_mode  = new QLabel(this);
    for (QLabel *l : {m_name, m_location, m_size, m_dims, m_print, m_dpi, m_mode})
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow("Name:",       m_name);
    form->addRow("Location:",   m_location);
    form->addRow("File size:", m_size);
    form->addRow("Dimensions:",    m_dims);
    form->addRow("Print:",      m_print);
    form->addRow("DPI:",        m_dpi);
    form->addRow("Colour:",      m_mode);
    v->addLayout(form);
    v->addStretch(1);

    refresh();
}

void InfoPanel::setUnit(Unit u)
{
    m_unit = u;
    refresh();
}

void InfoPanel::showItem(const ImageItem *item)
{
    m_item = item;
    refresh();
}

void InfoPanel::refresh()
{
    if (!m_item) {
        m_thumb->setPixmap(QPixmap());
        m_thumb->setText("No image selected");
        for (QLabel *l : {m_name, m_location, m_size, m_dims, m_print, m_dpi, m_mode})
            l->setText("—");
        return;
    }

    const QPixmap &pm = m_item->pixmap();

    // Thumbnail.
    if (!pm.isNull()) {
        const int w = std::max(16, m_thumb->width() - 8);
        const int h = std::max(16, m_thumb->height() - 8);
        m_thumb->setPixmap(pm.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // File name, full location & size.
    const QFileInfo fi(m_item->sourcePath());
    m_name->setText(fi.fileName().isEmpty() ? m_item->sourcePath() : fi.fileName());
    m_name->setToolTip(m_item->sourcePath());
    const QString full = m_item->sourcePath();
    m_location->setText(full.isEmpty() ? QStringLiteral("—") : full);
    m_location->setToolTip(full);
    m_size->setText(fi.exists() ? humanSize(fi.size()) : "—");

    // Pixel dimensions.
    m_dims->setText(QString("%1 × %2 px").arg(pm.width()).arg(pm.height()));

    // Physical print size on the bed (active unit).
    m_print->setText(QString("%1 × %2")
                         .arg(fmtUnit(m_item->widthMm(), m_unit))
                         .arg(fmtUnit(m_item->heightMm(), m_unit)));

    // DPI.
    m_dpi->setText(QString::number(m_item->dpi(), 'f', 0) + " dpi");

    // Colour mode (approximate from pixmap).
    m_mode->setText(QString("%1 · %2-bit")
                        .arg(pm.hasAlphaChannel() ? "RGBA" : "RGB")
                        .arg(pm.depth()));
}
