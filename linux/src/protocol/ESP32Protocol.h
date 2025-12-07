// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef ESP32PROTOCOL_H
#define ESP32PROTOCOL_H

#include <QByteArray>
#include <cstdint>
#include <optional>

/**
 * ESP32 bootloader command opcodes
 */
enum class ESP32Command : uint8_t {
    Sync = 0x08,
    FlashBegin = 0x02,
    FlashData = 0x03,
    FlashEnd = 0x04,
    ChangeBaudRate = 0x0F,
    ReadReg = 0x0A,
    WriteReg = 0x09,
    SpiAttach = 0x0D
};

/**
 * ESP32-C3 register addresses for watchdog control
 */
namespace ESP32C3Registers {
    constexpr uint32_t RTC_CNTL_BASE = 0x60008000;

    // RTC Watchdog Config
    constexpr uint32_t RTC_WDT_CONFIG0 = RTC_CNTL_BASE + 0x0090;
    constexpr uint32_t RTC_WDT_WPROTECT = RTC_CNTL_BASE + 0x00A8;
    constexpr uint32_t RTC_WDT_WKEY = 0x50D83AA1;

    // Super Watchdog Config
    constexpr uint32_t SWD_CONF = RTC_CNTL_BASE + 0x00AC;
    constexpr uint32_t SWD_WPROTECT = RTC_CNTL_BASE + 0x00B0;
    constexpr uint32_t SWD_WKEY = 0x8F1D312A;

    // Bit positions
    constexpr uint32_t WDT_EN_BIT = 1 << 31;
    constexpr uint32_t SWD_AUTO_FEED_EN_BIT = 1 << 31;
    constexpr uint32_t SWD_DISABLE_BIT = 1 << 30;
}

/**
 * ESP32 bootloader response
 */
struct ESP32Response {
    uint8_t direction;
    uint8_t command;
    uint16_t size;
    uint32_t value;
    QByteArray data;
    uint8_t status;
    uint8_t error;

    bool isSuccess() const { return status == 0 && error == 0; }

    /**
     * Parse a decoded SLIP packet into a response
     * @param packet Decoded packet data
     * @return Parsed response, or nullopt if invalid
     */
    static std::optional<ESP32Response> parse(const QByteArray& packet);
};

/**
 * ESP32 protocol packet builder
 */
namespace ESP32Protocol {

/// Checksum seed value
constexpr uint8_t CHECKSUM_SEED = 0xEF;

/// Default block size for flash data
constexpr int FLASH_BLOCK_SIZE = 1024;

/**
 * Calculate XOR checksum for data
 * @param data Data to checksum
 * @return Checksum value
 */
uint32_t calculateChecksum(const QByteArray& data);

/**
 * Build SYNC command packet
 * SYNC payload: 0x07 0x07 0x12 0x20 followed by 32 bytes of 0x55
 */
QByteArray buildSyncCommand();

/**
 * Build SPI_ATTACH command packet
 * Required before FLASH_BEGIN when using ROM bootloader (not stub)
 * @param config SPI configuration (0 = use default pins)
 * @return Command packet
 */
QByteArray buildSpiAttachCommand(uint32_t config = 0);

/**
 * Build FLASH_BEGIN command packet
 * @param size Total firmware size
 * @param numBlocks Number of data blocks
 * @param blockSize Size of each block
 * @param offset Flash address offset
 * @param encrypted Whether to use encrypted flash (ROM loader only)
 * @return Command packet
 */
QByteArray buildFlashBeginCommand(
    uint32_t size,
    uint32_t numBlocks,
    uint32_t blockSize,
    uint32_t offset,
    bool encrypted = false
);

/**
 * Build FLASH_DATA command packet
 * @param blockData Block data to flash
 * @param sequenceNumber Block sequence number
 * @return Command packet
 */
QByteArray buildFlashDataCommand(const QByteArray& blockData, uint32_t sequenceNumber);

/**
 * Build FLASH_END command packet
 * @param reboot Whether to reboot the device
 * @return Command packet
 */
QByteArray buildFlashEndCommand(bool reboot = true);

/**
 * Build CHANGE_BAUDRATE command packet
 * @param newBaud New baud rate
 * @param oldBaud Current baud rate (0 for ROM default)
 * @return Command packet
 */
QByteArray buildChangeBaudCommand(uint32_t newBaud, uint32_t oldBaud = 0);

/**
 * Build READ_REG command packet
 * @param address Register address to read
 * @return Command packet
 */
QByteArray buildReadRegCommand(uint32_t address);

/**
 * Build WRITE_REG command packet
 * @param address Register address to write
 * @param value Value to write
 * @param mask Bit mask (0xFFFFFFFF for full write)
 * @param delayUs Delay after write in microseconds
 * @return Command packet
 */
QByteArray buildWriteRegCommand(
    uint32_t address,
    uint32_t value,
    uint32_t mask = 0xFFFFFFFF,
    uint32_t delayUs = 0
);

} // namespace ESP32Protocol

#endif // ESP32PROTOCOL_H
