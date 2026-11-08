/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "InkCost.h"

#include <QSettings>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QDialogButtonBox>
#include <QRegularExpression>

static const char *const kInk[5] = {"C", "M", "Y", "K", "W"};

int InkCostParams::channelIndex(QChar c)
{
    switch (c.toUpper().toLatin1()) {
    case 'C': return 0;
    case 'M': return 1;
    case 'Y': return 2;
    case 'K': return 3;
    case 'W': return 4;
    default:  return -1;
    }
}

InkCostParams InkCostParams::load()
{
    InkCostParams p;
    QSettings s;
    s.beginGroup("inkcost");
    for (int i = 0; i < 5; i++)
        p.price[i] = s.value(QString("price%1").arg(kInk[i]), 0.0).toDouble();
    p.pl[0] = s.value("plSmall",  p.pl[0]).toDouble();
    p.pl[1] = s.value("plMedium", p.pl[1]).toDouble();
    p.pl[2] = s.value("plLarge",  p.pl[2]).toDouble();
    p.currency = s.value("currency", p.currency).toString();
    s.endGroup();
    return p;
}

void InkCostParams::save() const
{
    QSettings s;
    s.beginGroup("inkcost");
    for (int i = 0; i < 5; i++)
        s.setValue(QString("price%1").arg(kInk[i]), price[i]);
    s.setValue("plSmall",  pl[0]);
    s.setValue("plMedium", pl[1]);
    s.setValue("plLarge",  pl[2]);
    s.setValue("currency", currency);
    s.endGroup();
}

QVector<InkUse> inkCostCompute(const QString &out, const InkCostParams &p,
                               double *totalCost)
{
    // Accumulate drop counts per ink letter (W1+W2 → W).
    double small[5] = {}, medium[5] = {}, large[5] = {};
    static const QRegularExpression re(
        "INK_REPORT\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)");
    auto it = re.globalMatch(out);
    while (it.hasNext()) {
        const auto m = it.next();
        const int idx = InkCostParams::channelIndex(m.captured(1).at(0));
        if (idx < 0)
            continue;
        small[idx]  += m.captured(2).toDouble();
        medium[idx] += m.captured(3).toDouble();
        large[idx]  += m.captured(4).toDouble();
    }

    QVector<InkUse> rows;
    double total = 0;
    for (int i = 0; i < 5; i++) {
        const double pL = small[i] * p.pl[0] + medium[i] * p.pl[1]
                        + large[i] * p.pl[2];
        InkUse u;
        u.ink = kInk[i];
        u.ml = pL / 1.0e9;            // 1 mL = 1e9 pL
        u.cost = u.ml * p.price[i];
        total += u.cost;
        rows.push_back(u);
    }
    if (totalCost)
        *totalCost = total;
    return rows;
}

// ── editor ───────────────────────────────────────────────────────────────
InkCostDialog::InkCostDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Ink Cost");
    setModal(true);
    const InkCostParams p = InkCostParams::load();

    auto *root = new QVBoxLayout(this);

    m_currency = new QLineEdit(p.currency, this);
    m_currency->setMaximumWidth(80);
    {
        auto *cf = new QFormLayout;
        cf->addRow("Currency symbol:", m_currency);
        root->addLayout(cf);
    }

    auto *priceBox = new QGroupBox("Price per mL", this);
    auto *pf = new QFormLayout(priceBox);
    const char *names[5] = {"Cyan", "Magenta", "Yellow", "Black", "White"};
    for (int i = 0; i < 5; i++) {
        m_price[i] = new QDoubleSpinBox(priceBox);
        m_price[i]->setRange(0, 1e9);
        m_price[i]->setDecimals(2);
        m_price[i]->setValue(p.price[i]);
        pf->addRow(QString("%1:").arg(names[i]), m_price[i]);
    }
    root->addWidget(priceBox);

    auto *dropBox = new QGroupBox("Drop volume (pL) — calibrate vs real usage", this);
    auto *df = new QFormLayout(dropBox);
    const char *dl[3] = {"Small", "Medium", "Large"};
    for (int i = 0; i < 3; i++) {
        m_pl[i] = new QDoubleSpinBox(dropBox);
        m_pl[i]->setRange(0, 100);
        m_pl[i]->setDecimals(2);
        m_pl[i]->setSuffix(" pL");
        m_pl[i]->setValue(p.pl[i]);
        df->addRow(QString("%1 dot:").arg(dl[i]), m_pl[i]);
    }
    root->addWidget(dropBox);

    auto *hint = new QLabel(
        "Estimate only. Calibrate: print a job, weigh ink used, then adjust "
        "the drop volumes / prices until the estimate matches reality.", this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#64748b; font-size:12px;");
    root->addWidget(hint);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &InkCostDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &InkCostDialog::reject);
    root->addWidget(bb);
    root->setSizeConstraint(QLayout::SetFixedSize);
}

void InkCostDialog::accept()
{
    InkCostParams p;
    for (int i = 0; i < 5; i++)
        p.price[i] = m_price[i]->value();
    for (int i = 0; i < 3; i++)
        p.pl[i] = m_pl[i]->value();
    p.currency = m_currency->text().trimmed();
    if (p.currency.isEmpty())
        p.currency = "Rp";
    p.save();
    QDialog::accept();
}
