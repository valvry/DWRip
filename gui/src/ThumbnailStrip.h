/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QScrollArea>
#include <QFrame>
#include <QList>
#include <QPixmap>
#include <QString>

class QToolButton;
class QLabel;
class FlowLayout;

// One thumbnail card in the queue: preview + name + a close (×) button.
class ThumbCard : public QFrame {
    Q_OBJECT
public:
    ThumbCard(const QPixmap &thumb, const QString &name, QWidget *parent = nullptr);
    void setSelected(bool sel);
    void setBadge(int count);   // export/print counter overlay (hidden when 0)

signals:
    void clicked();
    void closeClicked();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void placeBadge();
    QToolButton *m_close = nullptr;
    QLabel      *m_badge = nullptr;
};

// Horizontal strip of thumbnail cards (the print queue), AcroRIP-style.
// currentChanged() fires only on user interaction; setCurrentIndex() is silent.
class ThumbnailStrip : public QScrollArea {
    Q_OBJECT
public:
    explicit ThumbnailStrip(QWidget *parent = nullptr);

    int  addThumb(const QPixmap &thumb, const QString &name);   // returns new index
    void setBadge(int index, int count);                        // export counter on a card
    void removeAt(int index);
    void setCurrentIndex(int index);                            // visual only
    int  count() const { return m_cards.size(); }

signals:
    void currentChanged(int index);   // user clicked a card
    void removeRequested(int index);  // user pressed the card's × or Delete

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void updateEmptyHint();

    QWidget    *m_content = nullptr;
    FlowLayout *m_flow = nullptr;
    QLabel     *m_emptyHint = nullptr;
    QList<ThumbCard *> m_cards;
    int m_current = -1;
};
