// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef FLASHINGSERVICE_H
#define FLASHINGSERVICE_H

#include "models/SerialPort.h"
#include "models/FirmwareFile.h"
#include "models/FlashingState.h"
#include "serial/SerialConnection.h"
#include "protocol/SLIPCodec.h"
#include "protocol/ESP32Protocol.h"

#include <QObject>
#include <QThread>
#include <functional>
#include <atomic>
#include <memory>

/**
 * Service that orchestrates the ESP32 flashing process
 * Runs flashing operations in a worker thread
 */
class FlashingService : public QObject {
    Q_OBJECT

public:
    explicit FlashingService(QObject* parent = nullptr);
    ~FlashingService();

    /**
     * Flash firmware to an ESP32 device
     * This method is asynchronous - it starts flashing in a worker thread
     * @param firmware Firmware file to flash (can contain multiple images at different offsets)
     * @param port Serial port to use
     * @param baudRate Target baud rate for flashing
     */
    void flash(const FirmwareFile& firmware, const SerialPort& port, BaudRate baudRate);

    /**
     * Cancel the current flash operation
     */
    void cancel();

    bool isFlashing() const { return m_isFlashing; }

signals:
    void stateChanged(FlashingState state);
    void finished(bool success);

private:
    void runFlashing(const FirmwareFile& firmware, const SerialPort& port, BaudRate baudRate);

    /**
     * Perform sync with bootloader with retries
     */
    void syncWithRetry();

    /**
     * Perform a single sync attempt
     */
    void performSync();

    /**
     * Change baud rate
     */
    void changeBaudRate(BaudRate rate);

    /**
     * Send SPI_ATTACH command
     */
    void spiAttach();

    /**
     * Disable RTC and Super watchdogs for USB-JTAG-Serial devices
     */
    void disableWatchdogs();

    /**
     * Read a register
     */
    uint32_t readReg(uint32_t address);

    /**
     * Write a register
     */
    void writeReg(uint32_t address, uint32_t value);

    /**
     * Begin flash operation for an image
     */
    void flashBegin(uint32_t size, uint32_t numBlocks, uint32_t blockSize, uint32_t offset);

    /**
     * Flash a single data block
     */
    void flashData(const QByteArray& block, int sequenceNumber);

    /**
     * End flash operation
     */
    void flashEnd(bool reboot, bool isUSBJTAGSerial);

    /**
     * Wait for a response from the bootloader
     */
    ESP32Response waitForResponse(ESP32Command command, double timeout);

    /**
     * Sleep for milliseconds
     */
    static void sleepMs(int ms);

    std::unique_ptr<SerialConnection> m_connection;
    SLIPDecoder m_slipDecoder;
    std::atomic<bool> m_isCancelled{false};
    std::atomic<bool> m_isFlashing{false};

    // Constants matching macOS implementation exactly
    static constexpr int SYNC_RETRIES = 20;
    static constexpr double RESPONSE_TIMEOUT = 5.0;
    static constexpr int BLOCK_DELAY_MS = 5;
    static constexpr int SYNC_RETRY_DELAY_MS = 50;

    QThread* m_workerThread = nullptr;
};

#endif // FLASHINGSERVICE_H
