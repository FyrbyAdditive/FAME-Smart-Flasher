// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "AboutDialog.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("About FAME Smart Flasher");
    setFixedSize(400, 480);
    setModal(true);

    setupUi();
}

void AboutDialog::setupUi()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(20);
    layout->setContentsMargins(32, 32, 32, 32);

    // Company logo
    QLabel* logoLabel = new QLabel(this);
    QPixmap logo(":/images/company-logo.png");
    if (!logo.isNull()) {
        // Scale to fit within bounds while maintaining aspect ratio
        QPixmap scaled = logo.scaled(320, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        logoLabel->setPixmap(scaled);
        logoLabel->setFixedHeight(scaled.height());
    } else {
        logoLabel->setText("FAME");
        logoLabel->setStyleSheet("font-size: 32px; font-weight: bold; color: #2c3e50;");
    }
    logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(logoLabel);

    // App name and version
    QLabel* appNameLabel = new QLabel("FAME Smart Flasher", this);
    appNameLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    appNameLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(appNameLabel);

    QLabel* versionLabel = new QLabel("Version 1.0.0", this);
    versionLabel->setStyleSheet("color: gray;");
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    // Description
    QLabel* descLabel = new QLabel("ESP32-C3 Firmware Flasher", this);
    descLabel->setStyleSheet("color: gray;");
    descLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(descLabel);

    // Links
    QLabel* websiteLink = new QLabel(
        "<a href=\"https://fyrbyadditive.com\">fyrbyadditive.com</a>",
        this
    );
    websiteLink->setOpenExternalLinks(true);
    websiteLink->setAlignment(Qt::AlignCenter);
    layout->addWidget(websiteLink);

    QLabel* githubLink = new QLabel(
        "<a href=\"https://github.com/FyrbyAdditive/FAME-Smart-Flasher\">GitHub</a>",
        this
    );
    githubLink->setOpenExternalLinks(true);
    githubLink->setAlignment(Qt::AlignCenter);
    layout->addWidget(githubLink);

    // Copyright
    QLabel* copyrightLabel = new QLabel(this);
    copyrightLabel->setText("Copyright 2025\nFyrby Additive Manufacturing & Engineering");
    copyrightLabel->setStyleSheet("color: gray; font-size: 11px;");
    copyrightLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(copyrightLabel);

    layout->addStretch();

    // OK button
    QPushButton* okButton = new QPushButton("OK", this);
    okButton->setDefault(true);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(okButton);
}
