/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#pragma once
#include <QDialog>

class QTableWidget;
class QTimer;
class QLabel;

// Non-modal viewer over the CUPS print queue (`lpstat -o`). Lets the user watch
// pending/printing jobs and cancel one. Auto-refreshes while open.
class PrintQueueDialog : public QDialog {
    Q_OBJECT
public:
    explicit PrintQueueDialog(QWidget *parent = nullptr);

private slots:
    void refresh();
    void cancelSelected();

private:
    QTableWidget *m_table = nullptr;
    QLabel       *m_status = nullptr;
    QTimer       *m_timer = nullptr;
};
