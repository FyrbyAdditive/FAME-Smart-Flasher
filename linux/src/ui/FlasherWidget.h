// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef FLASHERWIDGET_H
#define FLASHERWIDGET_H

#include "models/SerialPort.h"
#include "models/FirmwareFile.h"
#include "models/FlashingState.h"
#include "serial/SerialPortManager.h"
#include "services/FlashingService.h"

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QCheckBox>
#include <QGroupBox>
#include <memory>
#include <optional>

/**
 * Main flashing interface widget
 */
class FlasherWidget : public QWidget {
    Q_OBJECT

public:
    explicit FlasherWidget(QWidget* parent = nullptr);
    ~FlasherWidget();

signals:
    void serialMonitorToggled(bool enabled);
    void portChanged(const SerialPort& port);
    void flashingStarted();
    void flashingFinished();

private slots:
    void refreshPorts();
    void onPortSelectionChanged(int index);
    void onBaudRateChanged(int index);
    void selectFirmware();
    void startFlashing();
    void cancelFlashing();
    void onFlashingStateChanged(FlashingState state);
    void onSerialMonitorToggled(bool checked);

private:
    void setupUi();
    void updateFlashButtonState();
    void updateStatusDisplay(const FlashingState& state);

    // UI components
    QComboBox* m_portComboBox = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_firmwareButton = nullptr;
    QLabel* m_firmwareSizeLabel = nullptr;
    QComboBox* m_baudRateComboBox = nullptr;
    QGroupBox* m_advancedGroupBox = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_percentLabel = nullptr;
    QWidget* m_statusWidget = nullptr;
    QLabel* m_statusIconLabel = nullptr;
    QLabel* m_statusTextLabel = nullptr;
    QPushButton* m_flashButton = nullptr;
    QCheckBox* m_serialMonitorCheckBox = nullptr;

    // State
    SerialPortManager* m_portManager = nullptr;
    FlashingService* m_flashingService = nullptr;
    std::optional<SerialPort> m_selectedPort;
    BaudRate m_selectedBaudRate = BaudRate::Baud115200;
    std::optional<FirmwareFile> m_firmwareFile;
    FlashingState m_currentState;

    // Auto-reconnect: remember last selected port path to reconnect after reset
    QString m_lastSelectedPortPath;
};

#endif // FLASHERWIDGET_H
