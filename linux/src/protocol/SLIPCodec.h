// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef SLIPCODEC_H
#define SLIPCODEC_H

#include <QByteArray>
#include <vector>
#include <cstdint>

/**
 * SLIP (Serial Line Internet Protocol) encoder/decoder
 * Used for framing ESP32 bootloader packets
 */
namespace SLIPCodec {

// SLIP special characters
constexpr uint8_t FRAME_END = 0xC0;
constexpr uint8_t FRAME_ESCAPE = 0xDB;
constexpr uint8_t ESCAPED_END = 0xDC;      // 0xC0 -> 0xDB 0xDC
constexpr uint8_t ESCAPED_ESCAPE = 0xDD;   // 0xDB -> 0xDB 0xDD

/**
 * Encode data with SLIP framing
 * @param data Raw data to encode
 * @return SLIP-encoded packet with 0xC0 delimiters
 */
QByteArray encode(const QByteArray& data);

/**
 * Decode a SLIP-framed packet
 * @param slipPacket SLIP-encoded packet (including delimiters)
 * @return Decoded raw data, or empty if invalid
 */
QByteArray decode(const QByteArray& slipPacket);

} // namespace SLIPCodec

/**
 * Stateful SLIP decoder for streaming data
 */
class SLIPDecoder {
public:
    SLIPDecoder() = default;

    /**
     * Process incoming bytes and return complete packets
     * @param data Data to process
     * @return Vector of complete decoded packets
     */
    std::vector<QByteArray> process(const QByteArray& data);

    /**
     * Process a single byte
     * @param byte Single byte to process
     * @return Complete decoded packet if one was received, empty otherwise
     */
    QByteArray processByte(uint8_t byte);

    /**
     * Reset the decoder state
     */
    void reset();

private:
    QByteArray m_buffer;
    bool m_inEscape = false;
    bool m_packetStarted = false;
};

#endif // SLIPCODEC_H
