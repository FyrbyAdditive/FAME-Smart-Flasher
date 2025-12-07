// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

/**
 * About dialog showing application information
 */
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog() = default;

private:
    void setupUi();
};

#endif // ABOUTDIALOG_H
