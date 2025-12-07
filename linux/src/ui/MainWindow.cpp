// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "MainWindow.h"
#include "FlasherWidget.h"
#include "SerialMonitorWidget.h"
#include "AboutDialog.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QVBoxLayout>
#include <QApplication>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    setupMenuBar();

    setWindowTitle("FAME Smart Flasher");
    setMinimumSize(450, 450);
    resize(500, 650);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create splitter for flasher and serial monitor
    m_splitter = new QSplitter(Qt::Vertical, m_centralWidget);
    mainLayout->addWidget(m_splitter);

    // Flasher widget
    m_flasherWidget = new FlasherWidget(m_splitter);
    m_splitter->addWidget(m_flasherWidget);

    // Serial monitor widget (initially hidden)
    m_serialMonitorWidget = new SerialMonitorWidget(m_splitter);
    m_serialMonitorWidget->hide();
    m_splitter->addWidget(m_serialMonitorWidget);

    // Connect serial monitor toggle
    connect(m_flasherWidget, &FlasherWidget::serialMonitorToggled,
            this, &MainWindow::toggleSerialMonitor);

    // Connect serial port to serial monitor
    connect(m_flasherWidget, &FlasherWidget::portChanged,
            m_serialMonitorWidget, &SerialMonitorWidget::setPort);

    // Connect flashing state to serial monitor
    connect(m_flasherWidget, &FlasherWidget::flashingStarted,
            m_serialMonitorWidget, &SerialMonitorWidget::onFlashingStarted);
    connect(m_flasherWidget, &FlasherWidget::flashingFinished,
            m_serialMonitorWidget, &SerialMonitorWidget::onFlashingFinished);

    // Set initial splitter sizes
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
}

void MainWindow::setupMenuBar()
{
    QMenuBar* menuBar = this->menuBar();

    // File menu
    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    // Help menu
    QMenu* helpMenu = menuBar->addMenu("&Help");

    QAction* aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::showAboutDialog()
{
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::toggleSerialMonitor(bool show)
{
    if (show) {
        m_serialMonitorWidget->show();
        m_splitter->setSizes({400, 180});
        setMinimumHeight(600);
    } else {
        m_serialMonitorWidget->hide();
        setMinimumHeight(450);
    }
}
