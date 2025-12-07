// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application metadata
    app.setApplicationName("FAME Smart Flasher");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Fyrby Additive Manufacturing & Engineering");
    app.setOrganizationDomain("fyrbyadditive.com");

    // Create and show main window
    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
