// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QSplitter>

class FlasherWidget;
class SerialMonitorWidget;
class AboutDialog;

/**
 * Main application window
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void showAboutDialog();
    void toggleSerialMonitor(bool show);

private:
    void setupUi();
    void setupMenuBar();

    FlasherWidget* m_flasherWidget = nullptr;
    SerialMonitorWidget* m_serialMonitorWidget = nullptr;
    QSplitter* m_splitter = nullptr;
    QWidget* m_centralWidget = nullptr;
};

#endif // MAINWINDOW_H
