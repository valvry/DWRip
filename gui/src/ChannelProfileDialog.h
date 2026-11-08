/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QDialog>
#include <QString>
#include <QList>
#include <QColor>
#include <QIcon>

class QLineEdit;
class QComboBox;
class QLabel;

// A channel profile maps each of the 6 physical ink slots to an ink colour,
// producing dwrip's 6-char channel order string (e.g. "WWYKMC").
struct ChannelProfile {
    QString name;
    QString order;   // exactly 6 chars, each ∈ {C,M,Y,K,W}
};

// Persistence (QSettings array group "channelProfiles/").
QList<ChannelProfile> channelProfilesLoad();
void channelProfilesSave(const QList<ChannelProfile> &profiles);

// Colour helpers (shared so the editor and the profile selector match).
QColor inkColor(char c);                            // C/M/Y/K/W → swatch colour
QIcon  inkIcon(char c);                             // single colour chip
QIcon  channelOrderIcon(const QString &order);      // row of 6 chips for a profile

// Editor dialog: name + 6 colour dropdowns (one per physical slot).
class ChannelProfileDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChannelProfileDialog(const ChannelProfile &initial = {}, QWidget *parent = nullptr);
    ChannelProfile result() const;

private slots:
    void accept() override;

private:
    void updateSwatch(int i);

    QLineEdit *m_name = nullptr;
    QComboBox *m_slot[6] = {nullptr};
    QLabel    *m_swatch[6] = {nullptr};
};
