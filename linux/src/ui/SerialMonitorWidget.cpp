// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "SerialMonitorWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>

SerialMonitorWidget::SerialMonitorWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    // Timer for batching text updates (100ms)
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(100);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() {
        if (!m_pendingText.isEmpty()) {
            QString text = m_pendingText;
            m_pendingText.clear();

            QString currentText = m_outputText->toPlainText();
            currentText += text;

            // Limit buffer size
            if (currentText.size() > MAX_OUTPUT_SIZE) {
                currentText = currentText.right(TRIM_TO_SIZE);
            }

            m_outputText->setPlainText(currentText);

            // Scroll to bottom
            QScrollBar* scrollBar = m_outputText->verticalScrollBar();
            scrollBar->setValue(scrollBar->maximum());
        }
    });
    m_updateTimer->start();

    // Timer for reading serial data
    m_readTimer = new QTimer(this);
    m_readTimer->setInterval(50);
    connect(m_readTimer, &QTimer::timeout, this, &SerialMonitorWidget::processIncomingData);

    // Timer for reconnection attempts
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SerialMonitorWidget::onReconnectTimer);
}

SerialMonitorWidget::~SerialMonitorWidget()
{
    stopReading();
    m_updateTimer->stop();
    m_reconnectTimer->stop();
}

void SerialMonitorWidget::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header
    QWidget* headerWidget = new QWidget(this);
    headerWidget->setStyleSheet("background-color: #e0e0e0;");

    QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(12, 8, 12, 8);

    m_titleLabel = new QLabel("Serial Monitor", this);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    headerLayout->addWidget(m_titleLabel);

    headerLayout->addStretch();

    // Connection status indicator
    m_statusIndicator = new QLabel(this);
    m_statusIndicator->setFixedSize(8, 8);
    m_statusIndicator->setStyleSheet(
        "background-color: gray; border-radius: 4px;"
    );
    headerLayout->addWidget(m_statusIndicator);

    headerLayout->addSpacing(8);

    // Clear button
    m_clearButton = new QPushButton(this);
    m_clearButton->setText("\u2716"); // X mark
    m_clearButton->setToolTip("Clear output");
    m_clearButton->setFixedSize(24, 24);
    m_clearButton->setFlat(true);
    connect(m_clearButton, &QPushButton::clicked, this, &SerialMonitorWidget::clearOutput);
    headerLayout->addWidget(m_clearButton);

    mainLayout->addWidget(headerWidget);

    // Output text area
    m_outputText = new QTextEdit(this);
    m_outputText->setReadOnly(true);
    m_outputText->setFont(QFont("Monospace", 9));
    m_outputText->setStyleSheet(
        "background-color: white; color: #333333; border: none;"
    );
    m_outputText->setPlaceholderText("No output yet...");

    mainLayout->addWidget(m_outputText);
}

void SerialMonitorWidget::setPort(const SerialPort& port)
{
    // Disconnect from current port
    stopReading();

    m_currentPort = port;

    // Connect to new port if not flashing
    if (!m_isFlashing) {
        connectToPort();
    }
}

void SerialMonitorWidget::onFlashingStarted()
{
    m_isFlashing = true;
    m_wasConnectedBeforeFlash = m_connection && m_connection->isConnected();

    appendText("[Disconnecting for flash...]\n");
    stopReading();
}

void SerialMonitorWidget::onFlashingFinished()
{
    m_isFlashing = false;

    // Reconnect if we were connected before
    if (m_wasConnectedBeforeFlash && m_currentPort) {
        // Wait a bit for the device to restart
        QTimer::singleShot(1000, this, &SerialMonitorWidget::connectToPort);
    }
}

void SerialMonitorWidget::clearOutput()
{
    m_outputText->clear();
    m_pendingText.clear();
}

void SerialMonitorWidget::connectToPort()
{
    if (!m_currentPort || m_isFlashing) {
        return;
    }

    stopReading();
    m_reconnectTimer->stop();

    m_connection = std::make_unique<SerialConnection>();

    try {
        m_connection->open(m_currentPort->path);
        m_connection->setBaudRate(BaudRate::Baud115200);

        appendText(QString("[Connected to %1]\n").arg(m_currentPort->name));
        updateConnectionStatus(true);

        startReading();
    } catch (const SerialError& e) {
        appendText(QString("[Connection failed: %1]\n")
                       .arg(QString::fromStdString(e.what())));
        updateConnectionStatus(false);

        // Start reconnection attempts
        if (!m_isFlashing) {
            m_reconnectTimer->start();
        }
    }
}

void SerialMonitorWidget::disconnectFromPort()
{
    stopReading();
    m_reconnectTimer->stop();

    if (m_connection) {
        m_connection->close();
        m_connection.reset();
    }

    updateConnectionStatus(false);
}

void SerialMonitorWidget::processIncomingData()
{
    if (!m_connection || !m_connection->isConnected() || m_isFlashing) {
        return;
    }

    try {
        QByteArray data = m_connection->read(0.05);
        if (!data.isEmpty()) {
            // Try UTF-8 first, fall back to Latin1
            QString text = QString::fromUtf8(data);
            if (text.contains(QChar::ReplacementCharacter)) {
                text = QString::fromLatin1(data);
            }
            m_pendingText += text;
        }
    } catch (const SerialError& e) {
        if (e.type() != SerialError::Timeout) {
            appendText(QString("[Disconnected: %1]\n")
                           .arg(QString::fromStdString(e.what())));
            stopReading();
            updateConnectionStatus(false);

            // Start reconnection attempts
            if (!m_isFlashing) {
                m_reconnectTimer->start();
            }
        }
    }
}

void SerialMonitorWidget::onReconnectTimer()
{
    if (m_isFlashing || !m_currentPort) {
        m_reconnectTimer->stop();
        return;
    }

    if (m_connection && m_connection->isConnected()) {
        m_reconnectTimer->stop();
        return;
    }

    appendText("[Attempting to reconnect...]\n");
    connectToPort();
}

void SerialMonitorWidget::appendText(const QString& text)
{
    m_pendingText += text;
}

void SerialMonitorWidget::updateConnectionStatus(bool connected)
{
    if (connected) {
        m_statusIndicator->setStyleSheet(
            "background-color: #27ae60; border-radius: 4px;"
        );
    } else {
        m_statusIndicator->setStyleSheet(
            "background-color: gray; border-radius: 4px;"
        );
    }
}

void SerialMonitorWidget::startReading()
{
    if (!m_isReading) {
        m_isReading = true;
        m_readTimer->start();
    }
}

void SerialMonitorWidget::stopReading()
{
    m_isReading = false;
    m_readTimer->stop();

    if (m_connection) {
        m_connection->close();
    }
}
