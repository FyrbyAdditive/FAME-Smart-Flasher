// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef SERIALMONITORWIDGET_H
#define SERIALMONITORWIDGET_H

#include "models/SerialPort.h"
#include "serial/SerialConnection.h"

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QThread>
#include <QTimer>
#include <memory>
#include <atomic>
#include <optional>

/**
 * Serial monitor panel for viewing device output
 */
class SerialMonitorWidget : public QWidget {
    Q_OBJECT

public:
    explicit SerialMonitorWidget(QWidget* parent = nullptr);
    ~SerialMonitorWidget();

public slots:
    void setPort(const SerialPort& port);
    void onFlashingStarted();
    void onFlashingFinished();

private slots:
    void clearOutput();
    void connectToPort();
    void disconnectFromPort();
    void processIncomingData();
    void onReconnectTimer();

private:
    void setupUi();
    void appendText(const QString& text);
    void updateConnectionStatus(bool connected);
    void startReading();
    void stopReading();

    // UI components
    QLabel* m_titleLabel = nullptr;
    QLabel* m_statusIndicator = nullptr;
    QPushButton* m_clearButton = nullptr;
    QTextEdit* m_outputText = nullptr;

    // Serial connection
    std::unique_ptr<SerialConnection> m_connection;
    std::optional<SerialPort> m_currentPort;
    QThread* m_readThread = nullptr;
    QTimer* m_readTimer = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    std::atomic<bool> m_isReading{false};
    std::atomic<bool> m_isFlashing{false};
    bool m_wasConnectedBeforeFlash = false;

    // Buffer for batching updates
    QString m_pendingText;
    QTimer* m_updateTimer = nullptr;
    static constexpr int MAX_OUTPUT_SIZE = 50000;
    static constexpr int TRIM_TO_SIZE = 40000;
};

#endif // SERIALMONITORWIDGET_H
