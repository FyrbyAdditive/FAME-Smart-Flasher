// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "FlasherWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStyle>

FlasherWidget::FlasherWidget(QWidget* parent)
    : QWidget(parent)
    , m_currentState(FlashingState::idle())
{
    m_portManager = new SerialPortManager(this);
    m_flashingService = new FlashingService(this);

    setupUi();

    // Connect signals
    connect(m_portManager, &SerialPortManager::portsChanged,
            this, &FlasherWidget::refreshPorts);

    connect(m_flashingService, &FlashingService::stateChanged,
            this, &FlasherWidget::onFlashingStateChanged);

    // Start port monitoring
    m_portManager->startObserving();

    // Initial UI update
    refreshPorts();
    updateFlashButtonState();
}

FlasherWidget::~FlasherWidget()
{
    m_portManager->stopObserving();
}

void FlasherWidget::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Port Selection
    QHBoxLayout* portLayout = new QHBoxLayout();
    QLabel* portLabel = new QLabel("USB Port", this);
    portLabel->setFixedWidth(80);
    portLayout->addWidget(portLabel);

    m_portComboBox = new QComboBox(this);
    m_portComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    portLayout->addWidget(m_portComboBox);

    m_refreshButton = new QPushButton(this);
    m_refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip("Refresh ports");
    m_refreshButton->setFixedWidth(32);
    portLayout->addWidget(m_refreshButton);

    mainLayout->addLayout(portLayout);

    connect(m_portComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FlasherWidget::onPortSelectionChanged);
    connect(m_refreshButton, &QPushButton::clicked, [this]() {
        m_portManager->refreshPorts();
    });

    // Firmware Selection
    QHBoxLayout* firmwareLayout = new QHBoxLayout();
    QLabel* firmwareLabel = new QLabel("Firmware", this);
    firmwareLabel->setFixedWidth(80);
    firmwareLayout->addWidget(firmwareLabel);

    QVBoxLayout* firmwareInnerLayout = new QVBoxLayout();
    firmwareInnerLayout->setSpacing(4);

    m_firmwareButton = new QPushButton("Select File...", this);
    m_firmwareButton->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    connect(m_firmwareButton, &QPushButton::clicked, this, &FlasherWidget::selectFirmware);
    firmwareInnerLayout->addWidget(m_firmwareButton);

    m_firmwareSizeLabel = new QLabel(this);
    m_firmwareSizeLabel->setStyleSheet("color: gray; font-size: 11px;");
    m_firmwareSizeLabel->hide();
    firmwareInnerLayout->addWidget(m_firmwareSizeLabel);

    firmwareLayout->addLayout(firmwareInnerLayout);
    mainLayout->addLayout(firmwareLayout);

    // Advanced Settings (collapsible)
    m_advancedGroupBox = new QGroupBox("Advanced Settings", this);
    m_advancedGroupBox->setCheckable(true);
    m_advancedGroupBox->setChecked(false);

    QHBoxLayout* advancedLayout = new QHBoxLayout(m_advancedGroupBox);
    QLabel* baudLabel = new QLabel("Baud Rate", this);
    baudLabel->setFixedWidth(80);
    advancedLayout->addWidget(baudLabel);

    m_baudRateComboBox = new QComboBox(this);
    for (BaudRate rate : ALL_BAUD_RATES) {
        m_baudRateComboBox->addItem(baudRateDisplayName(rate), static_cast<int>(rate));
    }
    m_baudRateComboBox->setCurrentIndex(0);
    advancedLayout->addWidget(m_baudRateComboBox);
    advancedLayout->addStretch();

    connect(m_baudRateComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FlasherWidget::onBaudRateChanged);

    mainLayout->addWidget(m_advancedGroupBox);

    // Spacer
    mainLayout->addSpacing(8);

    // Progress Section
    QVBoxLayout* progressLayout = new QVBoxLayout();
    progressLayout->setSpacing(8);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    progressLayout->addWidget(m_progressBar);

    m_percentLabel = new QLabel(this);
    m_percentLabel->setAlignment(Qt::AlignCenter);
    m_percentLabel->setStyleSheet("color: gray; font-size: 11px;");
    m_percentLabel->hide();
    progressLayout->addWidget(m_percentLabel);

    mainLayout->addLayout(progressLayout);

    // Status Message
    m_statusWidget = new QWidget(this);
    m_statusWidget->setAutoFillBackground(true);

    QHBoxLayout* statusLayout = new QHBoxLayout(m_statusWidget);
    statusLayout->setContentsMargins(12, 8, 12, 8);

    m_statusIconLabel = new QLabel(this);
    m_statusIconLabel->setFixedSize(16, 16);
    statusLayout->addWidget(m_statusIconLabel);

    m_statusTextLabel = new QLabel("Ready", this);
    statusLayout->addWidget(m_statusTextLabel);
    statusLayout->addStretch();

    mainLayout->addWidget(m_statusWidget);
    updateStatusDisplay(m_currentState);

    // Spacer
    mainLayout->addStretch();

    // Flash Button
    m_flashButton = new QPushButton("Flash Firmware", this);
    m_flashButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_flashButton->setMinimumHeight(40);

    connect(m_flashButton, &QPushButton::clicked, [this]() {
        if (m_currentState.isActive()) {
            cancelFlashing();
        } else {
            startFlashing();
        }
    });

    mainLayout->addWidget(m_flashButton);

    // Serial Monitor Toggle
    m_serialMonitorCheckBox = new QCheckBox("Show Serial Monitor", this);
    connect(m_serialMonitorCheckBox, &QCheckBox::toggled,
            this, &FlasherWidget::onSerialMonitorToggled);
    mainLayout->addWidget(m_serialMonitorCheckBox);
}

void FlasherWidget::refreshPorts()
{
    m_portComboBox->blockSignals(true);

    // Remember current selection path, or use last selected path for auto-reconnect
    QString targetPath;
    if (m_selectedPort) {
        targetPath = m_selectedPort->path;
    } else if (!m_lastSelectedPortPath.isEmpty()) {
        // Try to reconnect to the last selected port
        targetPath = m_lastSelectedPortPath;
    }

    m_portComboBox->clear();
    m_portComboBox->addItem("Select port...", QVariant());

    const auto& ports = m_portManager->availablePorts();
    int newIndex = 0;

    for (size_t i = 0; i < ports.size(); ++i) {
        const auto& port = ports[i];
        QString displayText = port.displayName();
        if (port.isESP32C3()) {
            displayText += " (ESP32-C3)";
        }
        m_portComboBox->addItem(displayText, port.path);

        if (port.path == targetPath) {
            newIndex = static_cast<int>(i) + 1;
        }
    }

    m_portComboBox->setCurrentIndex(newIndex);
    m_portComboBox->blockSignals(false);

    // Update selected port
    if (newIndex > 0 && static_cast<size_t>(newIndex - 1) < ports.size()) {
        m_selectedPort = ports[newIndex - 1];
        // Emit portChanged if we auto-reconnected to a previously disconnected port
        if (!targetPath.isEmpty() && m_selectedPort->path == targetPath) {
            emit portChanged(*m_selectedPort);
        }
    } else {
        m_selectedPort.reset();
    }

    updateFlashButtonState();
}

void FlasherWidget::onPortSelectionChanged(int index)
{
    const auto& ports = m_portManager->availablePorts();

    if (index <= 0 || static_cast<size_t>(index - 1) >= ports.size()) {
        m_selectedPort.reset();
        // Don't clear m_lastSelectedPortPath - keep it for auto-reconnect
    } else {
        m_selectedPort = ports[index - 1];
        m_lastSelectedPortPath = m_selectedPort->path;  // Save for auto-reconnect
        emit portChanged(*m_selectedPort);
    }

    updateFlashButtonState();
}

void FlasherWidget::onBaudRateChanged(int index)
{
    if (index >= 0) {
        int value = m_baudRateComboBox->itemData(index).toInt();
        m_selectedBaudRate = static_cast<BaudRate>(value);
    }
}

void FlasherWidget::selectFirmware()
{
    QFileDialog dialog(this, "Select Firmware File");
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter("Firmware Files (*.bin);;All Files (*)");
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    if (!dialog.exec()) {
        return;
    }

    QStringList files = dialog.selectedFiles();
    if (files.isEmpty()) {
        return;
    }

    QString path = files.first();

    if (path.isEmpty()) {
        return;
    }

    try {
        m_firmwareFile = FirmwareFile::loadFromFile(path);

        m_firmwareButton->setText(m_firmwareFile->fileName());
        m_firmwareSizeLabel->setText(m_firmwareFile->sizeDescription());
        m_firmwareSizeLabel->show();

        if (!m_firmwareFile->isValid()) {
            m_currentState = FlashingState::error(
                FlashingErrorType::InvalidFirmware,
                "Missing ESP32 magic byte"
            );
            updateStatusDisplay(m_currentState);
        } else {
            m_currentState = FlashingState::idle();
            updateStatusDisplay(m_currentState);
        }
    } catch (const FirmwareLoadError& e) {
        m_firmwareFile.reset();
        m_firmwareButton->setText("Select File...");
        m_firmwareSizeLabel->hide();

        m_currentState = FlashingState::error(
            FlashingErrorType::InvalidFirmware,
            e.message()
        );
        updateStatusDisplay(m_currentState);
    }

    updateFlashButtonState();
}

void FlasherWidget::startFlashing()
{
    if (!m_selectedPort || !m_firmwareFile) {
        return;
    }

    emit flashingStarted();

    m_flashingService->flash(*m_firmwareFile, *m_selectedPort, m_selectedBaudRate);
}

void FlasherWidget::cancelFlashing()
{
    m_flashingService->cancel();
    m_currentState = FlashingState::idle();
    updateStatusDisplay(m_currentState);
    updateFlashButtonState();
}

void FlasherWidget::onFlashingStateChanged(FlashingState state)
{
    m_currentState = state;
    updateStatusDisplay(state);
    updateFlashButtonState();

    // Update progress bar
    if (state.type == FlashingStateType::Flashing) {
        int percent = static_cast<int>(state.progress * 100);
        m_progressBar->setValue(percent);
        m_percentLabel->setText(QString("%1%").arg(percent));
        m_percentLabel->show();
    } else if (state.type == FlashingStateType::Complete) {
        m_progressBar->setValue(100);
        m_percentLabel->setText("100%");
        emit flashingFinished();
    } else if (state.type == FlashingStateType::Idle) {
        m_progressBar->setValue(0);
        m_percentLabel->hide();
    } else if (state.type == FlashingStateType::Error) {
        emit flashingFinished();
    }
}

void FlasherWidget::onSerialMonitorToggled(bool checked)
{
    emit serialMonitorToggled(checked);
}

void FlasherWidget::updateFlashButtonState()
{
    bool canFlash = m_selectedPort.has_value() &&
                    m_firmwareFile.has_value() &&
                    !m_currentState.isActive();

    if (m_currentState.isActive()) {
        m_flashButton->setText("Cancel");
        m_flashButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
        m_flashButton->setEnabled(true);
        m_flashButton->setStyleSheet("background-color: #c0392b; color: white;");
    } else {
        m_flashButton->setText("Flash Firmware");
        m_flashButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_flashButton->setEnabled(canFlash);
        m_flashButton->setStyleSheet("");
    }

    // Disable controls during flashing
    bool isFlashing = m_currentState.isActive();
    m_portComboBox->setEnabled(!isFlashing);
    m_refreshButton->setEnabled(!isFlashing);
    m_firmwareButton->setEnabled(!isFlashing);
    m_baudRateComboBox->setEnabled(!isFlashing);
    m_advancedGroupBox->setEnabled(!isFlashing);
    m_serialMonitorCheckBox->setEnabled(!isFlashing);
}

void FlasherWidget::updateStatusDisplay(const FlashingState& state)
{
    QString iconText;
    QString statusColor;
    QString bgColor;

    switch (state.type) {
    case FlashingStateType::Idle:
        iconText = "\u25CB";  // Circle
        statusColor = "black";
        bgColor = "#e0e0e0";
        break;
    case FlashingStateType::Connecting:
    case FlashingStateType::Syncing:
    case FlashingStateType::ChangingBaudRate:
        iconText = "\u25CE";  // Bullseye
        statusColor = "black";
        bgColor = "#e0e0e0";
        break;
    case FlashingStateType::Erasing:
        iconText = "\u2716";  // X mark
        statusColor = "black";
        bgColor = "#e0e0e0";
        break;
    case FlashingStateType::Flashing:
        iconText = "\u26A1";  // Lightning
        statusColor = "black";
        bgColor = "#e0e0e0";
        break;
    case FlashingStateType::Verifying:
        iconText = "\u2714";  // Check
        statusColor = "black";
        bgColor = "#e0e0e0";
        break;
    case FlashingStateType::Restarting:
        iconText = "\u21BB";  // Clockwise arrow
        statusColor = "black";
        bgColor = "#e0e0e0";
        break;
    case FlashingStateType::Complete:
        iconText = "\u2714";  // Check
        statusColor = "#27ae60";
        bgColor = "#d5f5e3";
        break;
    case FlashingStateType::Error:
        iconText = "\u26A0";  // Warning
        statusColor = "#c0392b";
        bgColor = "#fadbd8";
        break;
    }

    m_statusIconLabel->setText(iconText);
    m_statusTextLabel->setText(state.statusMessage());

    QString styleSheet = QString(
        "background-color: %1; border-radius: 4px;"
    ).arg(bgColor);
    m_statusWidget->setStyleSheet(styleSheet);

    QString textStyle = QString("color: %1;").arg(statusColor);
    m_statusIconLabel->setStyleSheet(textStyle);
    m_statusTextLabel->setStyleSheet(textStyle);
}
