/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "ChannelProfileDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPainter>
#include <QPixmap>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QSettings>

// Physical slot order (fixed) → ESC/i code, for the tooltip hint. See src/channel.c.
static const char *kSlotCode[6] = {"0x04", "0x00", "0x12", "0x11", "0x01", "0x02"};

// Swatch colour representing each ink.
QColor inkColor(char c)
{
    switch (c) {
    case 'C': return QColor("#13b5ea");   // cyan
    case 'M': return QColor("#ec008c");   // magenta
    case 'Y': return QColor("#ffd400");   // yellow
    case 'K': return QColor("#1f2937");   // black
    case 'W': return QColor("#ffffff");   // white
    }
    return QColor("#cbd5e1");
}

static QPixmap chip(const QColor &col, int w, int h)
{
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(col);
    p.setPen(QPen(QColor(15, 23, 42, 55)));
    p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 1), 3, 3);
    return pm;
}

QIcon inkIcon(char c)
{
    return QIcon(chip(inkColor(c), 20, 14));
}

QIcon channelOrderIcon(const QString &order)
{
    const int sq = 13, gap = 2, n = order.size();
    QPixmap pm(n * sq + (n - 1) * gap, sq);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < n; ++i) {
        p.setBrush(inkColor(order[i].toLatin1()));
        p.setPen(QPen(QColor(15, 23, 42, 55)));
        p.drawRoundedRect(QRectF(i * (sq + gap) + 0.5, 0.5, sq - 1, sq - 1), 3, 3);
    }
    return QIcon(pm);
}

// ── Persistence ──────────────────────────────────────────────

QList<ChannelProfile> channelProfilesLoad()
{
    QList<ChannelProfile> out;
    QSettings s;
    const int n = s.beginReadArray("channelProfiles");
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        ChannelProfile p;
        p.name = s.value("name").toString();
        p.order = s.value("order").toString();
        if (!p.name.isEmpty() && p.order.size() == 6)
            out.append(p);
    }
    s.endArray();
    return out;
}

void channelProfilesSave(const QList<ChannelProfile> &profiles)
{
    QSettings s;
    s.beginWriteArray("channelProfiles");
    for (int i = 0; i < profiles.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("name", profiles[i].name);
        s.setValue("order", profiles[i].order);
    }
    s.endArray();
}

// ── Editor dialog ────────────────────────────────────────────

ChannelProfileDialog::ChannelProfileDialog(const ChannelProfile &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(initial.name.isEmpty() ? "New Channel Profile" : "Edit Channel Profile");
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);

    // Name row.
    auto *nameRow = new QHBoxLayout;
    auto *nameLbl = new QLabel("Name:", this);
    m_name = new QLineEdit(this);
    m_name->setText(initial.name);
    m_name->setPlaceholderText("profile name");
    nameRow->addWidget(nameLbl);
    nameRow->addWidget(m_name, 1);
    root->addLayout(nameRow);

    auto *hint = new QLabel("Ink installed per slot (physical head order):", this);
    hint->setStyleSheet("color:#64748b; font-size:12px;");
    root->addWidget(hint);

    // Slots laid out HORIZONTALLY: each = label + colour swatch + dropdown.
    auto *slotsRow = new QHBoxLayout;
    slotsRow->setSpacing(10);
    const QString def = initial.order.size() == 6 ? initial.order : QStringLiteral("CMYKWW");
    for (int i = 0; i < 6; ++i) {
        auto *col = new QVBoxLayout;
        col->setSpacing(5);
        col->setAlignment(Qt::AlignHCenter);

        auto *lbl = new QLabel(QString("Slot %1").arg(i + 1), this);
        lbl->setAlignment(Qt::AlignHCenter);
        lbl->setStyleSheet("color:#64748b; font-size:11px; font-weight:600;");
        lbl->setToolTip(QString("Physical slot %1 (code %2)").arg(i + 1).arg(kSlotCode[i]));

        m_swatch[i] = new QLabel(this);
        m_swatch[i]->setFixedSize(46, 28);

        m_slot[i] = new QComboBox(this);
        m_slot[i]->setIconSize(QSize(20, 14));
        m_slot[i]->addItem(inkIcon('C'), "Cyan", 'C');
        m_slot[i]->addItem(inkIcon('M'), "Magenta", 'M');
        m_slot[i]->addItem(inkIcon('Y'), "Yellow", 'Y');
        m_slot[i]->addItem(inkIcon('K'), "Black", 'K');
        m_slot[i]->addItem(inkIcon('W'), "White", 'W');
        m_slot[i]->setFixedWidth(112);
        const int idx = m_slot[i]->findData(QChar(def[i]).toLatin1());
        m_slot[i]->setCurrentIndex(idx >= 0 ? idx : 0);
        m_slot[i]->setToolTip(QString("Physical slot %1 (code %2)").arg(i + 1).arg(kSlotCode[i]));
        connect(m_slot[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, i](int) { updateSwatch(i); });
        updateSwatch(i);

        col->addWidget(lbl, 0, Qt::AlignHCenter);
        col->addWidget(m_swatch[i], 0, Qt::AlignHCenter);
        col->addWidget(m_slot[i], 0, Qt::AlignHCenter);
        slotsRow->addLayout(col);
    }
    root->addLayout(slotsRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Save");
    buttons->button(QDialogButtonBox::Cancel)->setText("Cancel");
    connect(buttons, &QDialogButtonBox::accepted, this, &ChannelProfileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ChannelProfileDialog::reject);
    root->addWidget(buttons);

    root->setSizeConstraint(QLayout::SetFixedSize);   // fixed size, no resize
}

void ChannelProfileDialog::updateSwatch(int i)
{
    const char c = char(m_slot[i]->currentData().toInt());
    const QColor col = inkColor(c);
    // White needs a stronger border to be visible on a light dialog.
    const QString border = (c == 'W') ? "#cbd5e1" : "rgba(15,23,42,0.18)";
    m_swatch[i]->setStyleSheet(
        QString("background:%1; border:1px solid %2; border-radius:6px;")
            .arg(col.name(), border));
}

ChannelProfile ChannelProfileDialog::result() const
{
    QString order;
    for (int i = 0; i < 6; ++i)
        order += QChar(m_slot[i]->currentData().toInt());
    return {m_name->text().trimmed(), order};
}

void ChannelProfileDialog::accept()
{
    if (m_name->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Profile", "Profile name cannot be empty.");
        return;
    }
    QDialog::accept();
}
