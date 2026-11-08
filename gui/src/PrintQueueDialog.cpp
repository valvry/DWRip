/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include "PrintQueueDialog.h"
#include "CupsSpooler.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QMessageBox>

PrintQueueDialog::PrintQueueDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Print Queue");
    setModal(false);
    resize(560, 320);

    auto *root = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({"Job ID", "Status", "User", "Size", "Submitted"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    root->addWidget(m_table, 1);

    m_status = new QLabel(this);
    root->addWidget(m_status);

    auto *btnRow = new QHBoxLayout;
    auto *refreshBtn = new QPushButton("Refresh", this);
    auto *cancelBtn  = new QPushButton("Cancel job", this);
    auto *closeBtn   = new QPushButton("Close", this);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(cancelBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(refreshBtn, &QPushButton::clicked, this, &PrintQueueDialog::refresh);
    connect(cancelBtn,  &QPushButton::clicked, this, &PrintQueueDialog::cancelSelected);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::accept);

    // Live view: poll the queue while the window is open.
    m_timer = new QTimer(this);
    m_timer->setInterval(2000);
    connect(m_timer, &QTimer::timeout, this, &PrintQueueDialog::refresh);
    m_timer->start();

    refresh();
}

void PrintQueueDialog::refresh()
{
    const QList<CupsJob> jobs = cupsListJobs();

    // Preserve the current selection across refresh.
    QString selId;
    if (m_table->currentRow() >= 0 && m_table->item(m_table->currentRow(), 0))
        selId = m_table->item(m_table->currentRow(), 0)->text();

    m_table->setRowCount(jobs.size());
    int printing = 0;
    for (int r = 0; r < jobs.size(); ++r) {
        const CupsJob &j = jobs.at(r);
        const QString cols[5] = { j.id, j.state, j.user, j.size, j.when };
        for (int c = 0; c < 5; ++c)
            m_table->setItem(r, c, new QTableWidgetItem(cols[c]));
        if (j.state == "Processing") ++printing;
        if (j.id == selId)
            m_table->selectRow(r);
    }

    m_status->setText(jobs.isEmpty()
        ? QStringLiteral("Queue empty.")
        : QString("%1 job(s) in queue — %2 printing.").arg(jobs.size()).arg(printing));
}

void PrintQueueDialog::cancelSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0)) {
        QMessageBox::information(this, "Print Queue", "Select a job to cancel.");
        return;
    }
    const QString id = m_table->item(row, 0)->text();
    QString err;
    if (!cupsCancel(id, &err))
        QMessageBox::warning(this, "Print Queue",
            QString("Failed to cancel %1:\n%2").arg(id, err));
    refresh();
}
