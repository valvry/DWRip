/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QDialog>
#include <QString>
#include <QVector>

class QDoubleSpinBox;
class QLineEdit;

// Ink-cost parameters (persisted to QSettings group "inkcost"): per-channel
// price per mL (C/M/Y/K/W) + drop volumes (pL) for small/medium/large + a
// currency symbol. All calibratable by the user vs real ink consumption.
struct InkCostParams {
    // index 0..4 = C, M, Y, K, W
    double price[5] = {0, 0, 0, 0, 0};       // per mL, in `currency` units
    double pl[3] = {1.5, 3.7, 8.8};          // small, medium, large drop (pL)
    QString currency = "Rp";

    static InkCostParams load();
    void save() const;
    static int channelIndex(QChar inkLetter); // C/M/Y/K/W → 0..4, else -1
};

// One ink's computed usage.
struct InkUse {
    QString ink;      // C/M/Y/K/W
    double ml = 0;    // millilitres
    double cost = 0;  // ml × price
};

// Parse dwrip stdout lines "INK_REPORT <name> <small> <medium> <large>"
// (name = C/M/Y/K/W1/W2; W1+W2 fold into W), apply `p`, and return per-ink
// usage sorted C,M,Y,K,W. *totalCost gets the sum.
QVector<InkUse> inkCostCompute(const QString &dwripStdout,
                               const InkCostParams &p, double *totalCost);

// Modal editor for InkCostParams (prices + drop volumes + currency).
class InkCostDialog : public QDialog {
    Q_OBJECT
public:
    explicit InkCostDialog(QWidget *parent = nullptr);
private:
    void accept() override;   // save then close
    QDoubleSpinBox *m_price[5] = {};
    QDoubleSpinBox *m_pl[3] = {};
    QLineEdit *m_currency = nullptr;
};
