// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "FlashingService.h"
#include "protocol/SLIPCodec.h"
#include "protocol/ESP32Protocol.h"

#include <QDateTime>
#include <thread>
#include <chrono>

FlashingService::FlashingService(QObject* parent)
    : QObject(parent)
{
}

FlashingService::~FlashingService()
{
    cancel();
    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void FlashingService::flash(const FirmwareFile& firmware, const SerialPort& port, BaudRate baudRate)
{
    if (m_isFlashing) {
        return;
    }

    m_isCancelled = false;
    m_isFlashing = true;

    // Run flashing in a separate thread
    m_workerThread = QThread::create([this, firmware, port, baudRate]() {
        runFlashing(firmware, port, baudRate);
    });

    connect(m_workerThread, &QThread::finished, m_workerThread, &QThread::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_workerThread = nullptr;
    });

    m_workerThread->start();
}

void FlashingService::cancel()
{
    m_isCancelled = true;
}

void FlashingService::runFlashing(const FirmwareFile& firmware, const SerialPort& port, BaudRate baudRate)
{
    m_connection = std::make_unique<SerialConnection>();

    auto cleanup = [this]() {
        if (m_connection) {
            m_connection->close();
            m_connection.reset();
        }
        m_isFlashing = false;
    };

    try {
        // 1. Connect
        emit stateChanged(FlashingState::connecting());
        m_connection->open(port.path);

        // 2. Enter bootloader mode using DTR/RTS reset sequence
        // For ESP32-C3 USB-JTAG-Serial, this triggers the built-in reset logic
        // esptool uses only one reset strategy per device type - don't mix them
        bool isUSBJTAGSerial = port.isESP32C3();
        m_connection->enterBootloaderMode(isUSBJTAGSerial);

        // Wait a moment for the chip to enter bootloader
        // The USB-JTAG-Serial peripheral should stay connected
        sleepMs(500);

        // Flush any remaining boot messages
        m_connection->flush();

        // Try syncing without closing the port first
        // If that fails, we'll try the close/reopen approach
        bool syncSucceeded = false;

        try {
            emit stateChanged(FlashingState::syncing());
            syncWithRetry();
            syncSucceeded = true;

            // CRITICAL: Disable watchdogs IMMEDIATELY after first sync
            // For USB-JTAG-Serial devices, the RTC watchdog can cause resets
            // that interrupt flashing. We must disable it before doing anything else.
            if (isUSBJTAGSerial) {
                disableWatchdogs();
            }
        } catch (const std::exception&) {
            // First sync attempt failed
        }

        // If sync failed, try closing and reopening the port
        // This handles cases where USB-JTAG-Serial re-enumerates
        if (!syncSucceeded) {
            m_connection->close();

            // Wait for USB re-enumeration
            sleepMs(2000);

            // Try to reopen the port multiple times
            bool opened = false;
            for (int attempt = 1; attempt <= 5; ++attempt) {
                try {
                    m_connection->open(port.path);
                    opened = true;
                    break;
                } catch (const std::exception&) {
                    if (attempt < 5) {
                        sleepMs(500);
                    }
                }
            }

            if (!opened) {
                throw std::runtime_error("Could not reopen port after reset");
            }

            // Flush any garbage data
            m_connection->flush();

            // Try sync again
            emit stateChanged(FlashingState::syncing());
            syncWithRetry();

            // CRITICAL: Disable watchdogs IMMEDIATELY after sync
            if (isUSBJTAGSerial) {
                disableWatchdogs();
            }
        }

        // 4. Change baud rate if needed
        if (baudRate != BaudRate::Baud115200) {
            emit stateChanged(FlashingState::changingBaudRate());
            changeBaudRate(baudRate);
        }

        // 5. Attach SPI flash (required for ROM bootloader before flash operations)
        spiAttach();

        // 6. Flash all images in the firmware package
        int totalBytes = firmware.totalSize();
        int bytesFlashed = 0;

        for (const auto& image : firmware.images()) {
            if (m_isCancelled) {
                throw std::runtime_error("Cancelled");
            }

            int blockSize = ESP32Protocol::FLASH_BLOCK_SIZE;
            int numBlocks = (image.size() + blockSize - 1) / blockSize;

            // Begin flash for this image
            emit stateChanged(FlashingState::erasing());
            flashBegin(
                static_cast<uint32_t>(image.size()),
                static_cast<uint32_t>(numBlocks),
                static_cast<uint32_t>(blockSize),
                image.offset
            );

            // Send data blocks
            for (int blockNum = 0; blockNum < numBlocks; ++blockNum) {
                if (m_isCancelled) {
                    throw std::runtime_error("Cancelled");
                }

                int start = blockNum * blockSize;
                int end = qMin(start + blockSize, image.size());
                QByteArray blockData = image.data.mid(start, end - start);

                // Pad last block with 0xFF if needed
                if (blockData.size() < blockSize) {
                    blockData.append(QByteArray(blockSize - blockData.size(), static_cast<char>(0xFF)));
                }

                // Calculate overall progress across all images
                double imageProgress = static_cast<double>(blockNum + 1) / numBlocks;
                double overallProgress = (bytesFlashed + imageProgress * image.size()) / totalBytes;
                emit stateChanged(FlashingState::flashing(overallProgress));

                flashData(blockData, blockNum);

                // Small delay after each block to prevent USB-JTAG-Serial buffer overflow
                // The ROM bootloader (without stub) can overwhelm the USB peripheral
                // This is a known issue with ESP32-C3 USB-JTAG-Serial
                sleepMs(BLOCK_DELAY_MS);
            }

            bytesFlashed += image.size();
        }

        // 7. Verify (implicit - checksums validated per block)
        emit stateChanged(FlashingState::verifying());
        sleepMs(100);

        // 8. Complete flashing and reboot
        emit stateChanged(FlashingState::restarting());
        flashEnd(true, isUSBJTAGSerial);

        sleepMs(1000); // 1 second for device to restart

        emit stateChanged(FlashingState::complete());
        cleanup();
        emit finished(true);

    } catch (const std::exception& e) {
        cleanup();

        QString errorMsg = QString::fromStdString(e.what());

        if (m_isCancelled || errorMsg.contains("Cancelled")) {
            emit stateChanged(FlashingState::error(FlashingErrorType::Cancelled));
        } else if (errorMsg.contains("sync")) {
            emit stateChanged(FlashingState::error(FlashingErrorType::SyncFailed, errorMsg, SYNC_RETRIES));
        } else if (errorMsg.contains("Cannot open") || errorMsg.contains("reopen")) {
            emit stateChanged(FlashingState::error(FlashingErrorType::ConnectionFailed, errorMsg));
        } else {
            emit stateChanged(FlashingState::error(FlashingErrorType::ConnectionFailed, errorMsg));
        }

        emit finished(false);
    }
}

void FlashingService::syncWithRetry()
{
    for (int attempt = 1; attempt <= SYNC_RETRIES; ++attempt) {
        try {
            performSync();
            return; // Success
        } catch (const std::exception&) {
            if (attempt == SYNC_RETRIES) {
                throw std::runtime_error("Failed to sync with bootloader");
            }
            sleepMs(SYNC_RETRY_DELAY_MS);
        }
    }
}

void FlashingService::performSync()
{
    QByteArray syncCommand = ESP32Protocol::buildSyncCommand();
    QByteArray slipEncoded = SLIPCodec::encode(syncCommand);

    // Send ONE sync packet (not 7 like before!)
    // esptool sends 1 sync, then reads 7 additional responses to drain
    m_connection->write(slipEncoded);

    // Wait for first response
    ESP32Response response = waitForResponse(ESP32Command::Sync, 1.0);

    if (!response.isSuccess()) {
        throw std::runtime_error("Sync failed");
    }

    // Read 7 more responses to drain extra sync responses (like esptool does)
    // The ROM bootloader sends multiple responses to sync
    for (int i = 0; i < 7; ++i) {
        try {
            waitForResponse(ESP32Command::Sync, 0.1);
        } catch (...) {
            // Ignore drain timeouts
        }
    }

    // Flush any remaining data
    m_connection->flush();
}

void FlashingService::changeBaudRate(BaudRate rate)
{
    QByteArray command = ESP32Protocol::buildChangeBaudCommand(
        static_cast<uint32_t>(baudRateValue(rate)),
        115200
    );
    QByteArray encoded = SLIPCodec::encode(command);
    m_connection->write(encoded);

    // Brief delay then change host baud rate
    sleepMs(50);
    m_connection->setBaudRate(rate);
    sleepMs(50);

    // Sync again at new baud rate
    performSync();
}

void FlashingService::spiAttach()
{
    QByteArray command = ESP32Protocol::buildSpiAttachCommand();
    QByteArray encoded = SLIPCodec::encode(command);
    m_connection->write(encoded);

    ESP32Response response = waitForResponse(ESP32Command::SpiAttach, 3.0);
    if (!response.isSuccess()) {
        throw std::runtime_error(QString("SPI attach failed: status=%1, error=%2")
                                     .arg(response.status)
                                     .arg(response.error)
                                     .toStdString());
    }
}

void FlashingService::disableWatchdogs()
{
    // 1. Disable RTC Watchdog
    // First unlock the write protection
    writeReg(ESP32C3Registers::RTC_WDT_WPROTECT, ESP32C3Registers::RTC_WDT_WKEY);

    // Read current config and clear WDT_EN bit
    uint32_t wdtConfig = readReg(ESP32C3Registers::RTC_WDT_CONFIG0);
    uint32_t newWdtConfig = wdtConfig & ~ESP32C3Registers::WDT_EN_BIT;
    writeReg(ESP32C3Registers::RTC_WDT_CONFIG0, newWdtConfig);

    // Re-lock write protection
    writeReg(ESP32C3Registers::RTC_WDT_WPROTECT, 0);

    // 2. Enable Super Watchdog auto-feed (effectively disables it)
    // First unlock the write protection
    writeReg(ESP32C3Registers::SWD_WPROTECT, ESP32C3Registers::SWD_WKEY);

    // Read current config and set SWD_AUTO_FEED_EN bit
    uint32_t swdConfig = readReg(ESP32C3Registers::SWD_CONF);
    uint32_t newSwdConfig = swdConfig | ESP32C3Registers::SWD_AUTO_FEED_EN_BIT;
    writeReg(ESP32C3Registers::SWD_CONF, newSwdConfig);

    // Re-lock write protection
    writeReg(ESP32C3Registers::SWD_WPROTECT, 0);
}

uint32_t FlashingService::readReg(uint32_t address)
{
    QByteArray command = ESP32Protocol::buildReadRegCommand(address);
    QByteArray encoded = SLIPCodec::encode(command);
    m_connection->write(encoded);

    ESP32Response response = waitForResponse(ESP32Command::ReadReg, 1.0);
    if (!response.isSuccess()) {
        throw std::runtime_error(QString("READ_REG failed at 0x%1")
                                     .arg(address, 8, 16, QChar('0'))
                                     .toStdString());
    }
    return response.value;
}

void FlashingService::writeReg(uint32_t address, uint32_t value)
{
    QByteArray command = ESP32Protocol::buildWriteRegCommand(address, value);
    QByteArray encoded = SLIPCodec::encode(command);
    m_connection->write(encoded);

    ESP32Response response = waitForResponse(ESP32Command::WriteReg, 1.0);
    if (!response.isSuccess()) {
        throw std::runtime_error(QString("WRITE_REG failed at 0x%1")
                                     .arg(address, 8, 16, QChar('0'))
                                     .toStdString());
    }
}

void FlashingService::flashBegin(uint32_t size, uint32_t numBlocks, uint32_t blockSize, uint32_t offset)
{
    QByteArray command = ESP32Protocol::buildFlashBeginCommand(size, numBlocks, blockSize, offset);
    QByteArray encoded = SLIPCodec::encode(command);
    m_connection->write(encoded);

    ESP32Response response = waitForResponse(ESP32Command::FlashBegin, 30.0); // Erase can take time
    if (!response.isSuccess()) {
        throw std::runtime_error(QString("Flash begin failed: status=%1")
                                     .arg(response.status)
                                     .toStdString());
    }
}

void FlashingService::flashData(const QByteArray& block, int sequenceNumber)
{
    QByteArray command = ESP32Protocol::buildFlashDataCommand(block, static_cast<uint32_t>(sequenceNumber));
    QByteArray encoded = SLIPCodec::encode(command);
    m_connection->write(encoded);

    ESP32Response response = waitForResponse(ESP32Command::FlashData, RESPONSE_TIMEOUT);
    if (!response.isSuccess()) {
        throw std::runtime_error(QString("Flash data failed at block %1: status=%2")
                                     .arg(sequenceNumber)
                                     .arg(response.status)
                                     .toStdString());
    }
}

void FlashingService::flashEnd(bool reboot, bool isUSBJTAGSerial)
{
    QByteArray command = ESP32Protocol::buildFlashEndCommand(reboot);
    QByteArray encoded = SLIPCodec::encode(command);
    m_connection->write(encoded);

    // Flash end might not get a response if rebooting
    try {
        ESP32Response response = waitForResponse(ESP32Command::FlashEnd, 2.0);
        if (!response.isSuccess() && !reboot) {
            throw std::runtime_error("Flash end failed");
        }
    } catch (...) {
        // Expected if rebooting
        if (!reboot) {
            throw;
        }
    }

    // For USB-JTAG-Serial devices, the FLASH_END reboot flag often doesn't work
    // because the ROM bootloader's soft reset doesn't reset the USB peripheral.
    // We need to do a hard reset using DTR/RTS.
    if (reboot && isUSBJTAGSerial) {
        m_connection->hardReset();
    }
}

ESP32Response FlashingService::waitForResponse(ESP32Command command, double timeout)
{
    QDateTime deadline = QDateTime::currentDateTime().addMSecs(static_cast<qint64>(timeout * 1000));
    m_slipDecoder.reset();

    while (QDateTime::currentDateTime() < deadline) {
        if (m_isCancelled) {
            throw std::runtime_error("Cancelled");
        }

        try {
            QByteArray data = m_connection->read(0.1);

            std::vector<QByteArray> packets = m_slipDecoder.process(data);

            for (const QByteArray& packet : packets) {
                auto response = ESP32Response::parse(packet);
                if (response && response->command == static_cast<uint8_t>(command)) {
                    return *response;
                }
            }
        } catch (const SerialError& e) {
            if (e.type() == SerialError::Timeout) {
                continue;
            }
            throw;
        }
    }

    throw std::runtime_error(QString("Timeout waiting for %1 response")
                                 .arg(static_cast<int>(command))
                                 .toStdString());
}

void FlashingService::sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
