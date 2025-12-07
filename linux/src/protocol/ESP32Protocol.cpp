// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "ESP32Protocol.h"

namespace {

// Helper to append little-endian 16-bit value
void appendLE16(QByteArray& data, uint16_t value)
{
    data.append(static_cast<char>(value & 0xFF));
    data.append(static_cast<char>((value >> 8) & 0xFF));
}

// Helper to append little-endian 32-bit value
void appendLE32(QByteArray& data, uint32_t value)
{
    data.append(static_cast<char>(value & 0xFF));
    data.append(static_cast<char>((value >> 8) & 0xFF));
    data.append(static_cast<char>((value >> 16) & 0xFF));
    data.append(static_cast<char>((value >> 24) & 0xFF));
}

// Helper to read little-endian 16-bit value
uint16_t readLE16(const QByteArray& data, int offset)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(data[offset])) |
           (static_cast<uint16_t>(static_cast<uint8_t>(data[offset + 1])) << 8);
}

// Helper to read little-endian 32-bit value
uint32_t readLE32(const QByteArray& data, int offset)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(data[offset])) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 3])) << 24);
}

/**
 * Build a command packet (before SLIP encoding)
 * @param command Command opcode
 * @param payload Command payload
 * @param checksum Checksum value (for data commands)
 * @return Raw command packet
 */
QByteArray buildPacket(ESP32Command command, const QByteArray& payload, uint32_t checksum = 0)
{
    QByteArray packet;
    packet.reserve(8 + payload.size());

    // Direction: 0x00 for request
    packet.append(static_cast<char>(0x00));
    // Command opcode
    packet.append(static_cast<char>(static_cast<uint8_t>(command)));
    // Data size (little-endian 16-bit)
    appendLE16(packet, static_cast<uint16_t>(payload.size()));
    // Checksum (little-endian 32-bit)
    appendLE32(packet, checksum);
    // Payload
    packet.append(payload);

    return packet;
}

} // anonymous namespace

std::optional<ESP32Response> ESP32Response::parse(const QByteArray& packet)
{
    if (packet.size() < 8) {
        return std::nullopt;
    }

    uint8_t direction = static_cast<uint8_t>(packet[0]);
    // Response direction should be 0x01
    if (direction != 0x01) {
        return std::nullopt;
    }

    ESP32Response response;
    response.direction = direction;
    response.command = static_cast<uint8_t>(packet[1]);
    response.size = readLE16(packet, 2);
    response.value = readLE32(packet, 4);

    int dataEndIndex = qMin(8 + static_cast<int>(response.size), packet.size());
    if (packet.size() > 8) {
        response.data = packet.mid(8, dataEndIndex - 8);
    }

    // Status bytes are at the START of the data section (not the end!)
    // Format: [status (1 byte)][error (1 byte)][optional additional data]
    response.status = response.data.size() >= 1 ? static_cast<uint8_t>(response.data[0]) : 0;
    response.error = response.data.size() >= 2 ? static_cast<uint8_t>(response.data[1]) : 0;

    return response;
}

namespace ESP32Protocol {

uint32_t calculateChecksum(const QByteArray& data)
{
    uint8_t checksum = CHECKSUM_SEED;
    for (int i = 0; i < data.size(); ++i) {
        checksum ^= static_cast<uint8_t>(data[i]);
    }
    return static_cast<uint32_t>(checksum);
}

QByteArray buildSyncCommand()
{
    QByteArray payload;
    payload.append(static_cast<char>(0x07));
    payload.append(static_cast<char>(0x07));
    payload.append(static_cast<char>(0x12));
    payload.append(static_cast<char>(0x20));

    // 32 bytes of 0x55
    for (int i = 0; i < 32; ++i) {
        payload.append(static_cast<char>(0x55));
    }

    return buildPacket(ESP32Command::Sync, payload);
}

QByteArray buildSpiAttachCommand(uint32_t config)
{
    QByteArray payload;
    // SPI configuration - 0 means use default SPI flash pins
    appendLE32(payload, config);
    // For ESP32-C3, we need 8 bytes total (second word is also 0)
    appendLE32(payload, 0);
    return buildPacket(ESP32Command::SpiAttach, payload);
}

QByteArray buildFlashBeginCommand(
    uint32_t size,
    uint32_t numBlocks,
    uint32_t blockSize,
    uint32_t offset,
    bool encrypted)
{
    QByteArray payload;
    payload.reserve(20);  // 5 x 32-bit words for ROM loader

    // Erase size (little-endian)
    appendLE32(payload, size);
    // Number of blocks
    appendLE32(payload, numBlocks);
    // Block size
    appendLE32(payload, blockSize);
    // Offset
    appendLE32(payload, offset);
    // Encryption flag (ROM loader requires this 5th word)
    // 0 = not encrypted, 1 = encrypted
    appendLE32(payload, encrypted ? 1 : 0);

    return buildPacket(ESP32Command::FlashBegin, payload);
}

QByteArray buildFlashDataCommand(const QByteArray& blockData, uint32_t sequenceNumber)
{
    QByteArray payload;
    payload.reserve(16 + blockData.size());

    // Data length (little-endian)
    appendLE32(payload, static_cast<uint32_t>(blockData.size()));
    // Sequence number
    appendLE32(payload, sequenceNumber);
    // Reserved (8 bytes of zeros)
    for (int i = 0; i < 8; ++i) {
        payload.append(static_cast<char>(0));
    }
    // Actual data
    payload.append(blockData);

    uint32_t checksum = calculateChecksum(blockData);
    return buildPacket(ESP32Command::FlashData, payload, checksum);
}

QByteArray buildFlashEndCommand(bool reboot)
{
    QByteArray payload;
    // 0 = reboot, 1 = stay in bootloader
    uint32_t flag = reboot ? 0 : 1;
    appendLE32(payload, flag);

    return buildPacket(ESP32Command::FlashEnd, payload);
}

QByteArray buildChangeBaudCommand(uint32_t newBaud, uint32_t oldBaud)
{
    QByteArray payload;
    appendLE32(payload, newBaud);
    appendLE32(payload, oldBaud);

    return buildPacket(ESP32Command::ChangeBaudRate, payload);
}

QByteArray buildReadRegCommand(uint32_t address)
{
    QByteArray payload;
    appendLE32(payload, address);
    return buildPacket(ESP32Command::ReadReg, payload);
}

QByteArray buildWriteRegCommand(uint32_t address, uint32_t value, uint32_t mask, uint32_t delayUs)
{
    QByteArray payload;
    appendLE32(payload, address);
    appendLE32(payload, value);
    appendLE32(payload, mask);
    appendLE32(payload, delayUs);
    return buildPacket(ESP32Command::WriteReg, payload);
}

} // namespace ESP32Protocol
