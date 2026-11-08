/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "ThumbnailStrip.h"
#include "FlowLayout.h"

#include <QVBoxLayout>
#include <QTimer>
#include <QLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStyle>
#include <QFontMetrics>

// ───────────────────────── ThumbCard ─────────────────────────

ThumbCard::ThumbCard(const QPixmap &thumb, const QString &name, QWidget *parent)
    : QFrame(parent)
{
    setObjectName("thumbCard");
    setFixedSize(104, 112);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet(
        "#thumbCard { background:#ffffff; border:1px solid #d8dee7; border-radius:7px; }"
        "#thumbCard:hover { background:#f8fbff; border-color:#b8cff0; }"
        "#thumbCard[sel=\"true\"] { border:2px solid #2f7de1; background:#eaf3ff; }");

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 6, 6, 6);
    v->setSpacing(4);

    auto *img = new QLabel(this);
    img->setAlignment(Qt::AlignCenter);
    img->setFixedHeight(74);
    img->setPixmap(thumb);
    v->addWidget(img);

    auto *lbl = new QLabel(this);
    QFont nf = lbl->font();
    nf.setPixelSize(10);
    lbl->setFont(nf);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setText(QFontMetrics(nf).elidedText(name, Qt::ElideMiddle, 90));
    lbl->setToolTip(name);
    v->addWidget(lbl);

    m_close = new QToolButton(this);
    m_close->setText("×");
    m_close->setFixedSize(18, 18);
    m_close->setCursor(Qt::PointingHandCursor);
    m_close->setToolTip("Remove from list");
    m_close->setStyleSheet(
        "QToolButton { border:none; background:rgba(255,255,255,210); border-radius:9px; color:#667085; font:bold 13px; padding:0; }"
        "QToolButton:hover { background:#feecee; color:#c52233; }");
    connect(m_close, &QToolButton::clicked, this, &ThumbCard::closeClicked);

    // Export/print counter badge, top-left corner. Hidden until count > 0.
    m_badge = new QLabel(this);
    m_badge->setAlignment(Qt::AlignCenter);
    m_badge->setMinimumWidth(16);
    m_badge->setStyleSheet(
        "background:#2f7de1; color:white; font:bold 10px; border-radius:8px; padding:1px 4px;");
    m_badge->setToolTip("Times exported / printed");
    m_badge->hide();
}

void ThumbCard::setBadge(int count)
{
    if (count <= 0) {
        m_badge->hide();
        return;
    }
    m_badge->setText(QString::number(count));
    m_badge->adjustSize();
    m_badge->show();
    placeBadge();
}

void ThumbCard::placeBadge()
{
    if (m_badge && m_badge->isVisible()) {
        m_badge->move(4, 4);   // top-left, over the thumbnail corner
        m_badge->raise();
    }
}

void ThumbCard::setSelected(bool sel)
{
    setProperty("sel", sel);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void ThumbCard::mousePressEvent(QMouseEvent *e)
{
    emit clicked();
    QFrame::mousePressEvent(e);
}

void ThumbCard::resizeEvent(QResizeEvent *e)
{
    QFrame::resizeEvent(e);
    m_close->move(width() - m_close->width() - 2, 2);
    m_close->raise();
    placeBadge();
}

// ──────────────────────── ThumbnailStrip ─────────────────────

ThumbnailStrip::ThumbnailStrip(QWidget *parent) : QScrollArea(parent)
{
    // ~1 row tall by default; wraps to more rows and scrolls vertically when full.
    setMinimumHeight(136);
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_content = new QWidget(this);
    m_content->setStyleSheet("background:transparent;");
    m_flow = new FlowLayout(m_content, 10, 10, 10);
    setWidget(m_content);

    // Empty-state hint (centred over the viewport when the queue is empty).
    m_emptyHint = new QLabel("No images yet — Import to add", viewport());
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet("color:#94a3b8; font-size:13px;");
    updateEmptyHint();
}

void ThumbnailStrip::updateEmptyHint()
{
    if (!m_emptyHint)
        return;
    const bool empty = m_cards.isEmpty();
    m_emptyHint->setVisible(empty);
    if (empty) {
        m_emptyHint->adjustSize();
        const QSize vp = viewport()->size();
        const QSize hs = m_emptyHint->size();
        m_emptyHint->move((vp.width() - hs.width()) / 2, (vp.height() - hs.height()) / 2);
        m_emptyHint->raise();
    }
}

void ThumbnailStrip::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    updateEmptyHint();
}

int ThumbnailStrip::addThumb(const QPixmap &thumb, const QString &name)
{
    auto *card = new ThumbCard(thumb, name, m_content);
    m_cards.append(card);
    m_flow->addWidget(card);

    connect(card, &ThumbCard::clicked, this, [this, card]() {
        const int i = m_cards.indexOf(card);
        if (i >= 0) {
            setCurrentIndex(i);
            emit currentChanged(i);
        }
    });
    connect(card, &ThumbCard::closeClicked, this, [this, card]() {
        const int i = m_cards.indexOf(card);
        if (i >= 0)
            emit removeRequested(i);
    });
    updateEmptyHint();
    return m_cards.size() - 1;
}

void ThumbnailStrip::setBadge(int index, int count)
{
    if (index >= 0 && index < m_cards.size())
        m_cards[index]->setBadge(count);
}

void ThumbnailStrip::removeAt(int index)
{
    if (index < 0 || index >= m_cards.size())
        return;
    ThumbCard *c = m_cards.takeAt(index);
    m_flow->removeWidget(c);
    c->deleteLater();
    if (m_current == index)
        m_current = -1;
    else if (m_current > index)
        --m_current;
    updateEmptyHint();
}

void ThumbnailStrip::setCurrentIndex(int index)
{
    m_current = index;
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setSelected(i == index);
    if (index < 0 || index >= m_cards.size())
        return;

    // Scrolling to a card that was added a moment ago needs the flow layout to
    // have placed it first — until then its geometry is still empty and the
    // scroll goes nowhere, which is why a newly imported image stayed out of
    // sight on the second row even though it was selected.
    if (QLayout *l = m_content->layout())
        l->activate();
    ensureWidgetVisible(m_cards[index]);

    // And once more after the event loop has run: the card is only given its
    // final row when the strip has been laid out at its real width.
    QTimer::singleShot(0, this, [this, index]() {
        if (index >= 0 && index < m_cards.size() && index == m_current)
            ensureWidgetVisible(m_cards[index]);
    });
}

void ThumbnailStrip::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Delete && m_current >= 0) {
        emit removeRequested(m_current);
        e->accept();
        return;
    }
    QScrollArea::keyPressEvent(e);
}
