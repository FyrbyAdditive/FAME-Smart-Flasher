// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "SLIPCodec.h"

namespace SLIPCodec {

QByteArray encode(const QByteArray& data)
{
    QByteArray encoded;
    encoded.reserve(data.size() * 2 + 2);

    encoded.append(static_cast<char>(FRAME_END));

    for (int i = 0; i < data.size(); ++i) {
        uint8_t byte = static_cast<uint8_t>(data[i]);
        switch (byte) {
        case FRAME_END:
            encoded.append(static_cast<char>(FRAME_ESCAPE));
            encoded.append(static_cast<char>(ESCAPED_END));
            break;
        case FRAME_ESCAPE:
            encoded.append(static_cast<char>(FRAME_ESCAPE));
            encoded.append(static_cast<char>(ESCAPED_ESCAPE));
            break;
        default:
            encoded.append(static_cast<char>(byte));
            break;
        }
    }

    encoded.append(static_cast<char>(FRAME_END));
    return encoded;
}

QByteArray decode(const QByteArray& slipPacket)
{
    QByteArray decoded;
    decoded.reserve(slipPacket.size());

    bool inEscape = false;
    bool started = false;

    for (int i = 0; i < slipPacket.size(); ++i) {
        uint8_t byte = static_cast<uint8_t>(slipPacket[i]);

        if (byte == FRAME_END) {
            if (started && !decoded.isEmpty()) {
                return decoded;
            }
            started = true;
            decoded.clear();
            continue;
        }

        if (!started) {
            continue;
        }

        if (inEscape) {
            switch (byte) {
            case ESCAPED_END:
                decoded.append(static_cast<char>(FRAME_END));
                break;
            case ESCAPED_ESCAPE:
                decoded.append(static_cast<char>(FRAME_ESCAPE));
                break;
            default:
                // Invalid escape sequence
                decoded.append(static_cast<char>(byte));
                break;
            }
            inEscape = false;
        } else if (byte == FRAME_ESCAPE) {
            inEscape = true;
        } else {
            decoded.append(static_cast<char>(byte));
        }
    }

    return decoded.isEmpty() ? QByteArray() : decoded;
}

} // namespace SLIPCodec

std::vector<QByteArray> SLIPDecoder::process(const QByteArray& data)
{
    std::vector<QByteArray> packets;
    for (int i = 0; i < data.size(); ++i) {
        QByteArray packet = processByte(static_cast<uint8_t>(data[i]));
        if (!packet.isEmpty()) {
            packets.push_back(packet);
        }
    }
    return packets;
}

QByteArray SLIPDecoder::processByte(uint8_t byte)
{
    switch (byte) {
    case SLIPCodec::FRAME_END:
        if (m_packetStarted && !m_buffer.isEmpty()) {
            QByteArray packet = m_buffer;
            reset();
            return packet;
        }
        m_packetStarted = true;
        m_buffer.clear();
        return QByteArray();

    case SLIPCodec::FRAME_ESCAPE:
        if (!m_packetStarted) {
            return QByteArray();
        }
        m_inEscape = true;
        return QByteArray();

    default:
        if (!m_packetStarted) {
            return QByteArray();
        }

        if (m_inEscape) {
            m_inEscape = false;
            switch (byte) {
            case SLIPCodec::ESCAPED_END:
                m_buffer.append(static_cast<char>(SLIPCodec::FRAME_END));
                break;
            case SLIPCodec::ESCAPED_ESCAPE:
                m_buffer.append(static_cast<char>(SLIPCodec::FRAME_ESCAPE));
                break;
            default:
                m_buffer.append(static_cast<char>(byte));
                break;
            }
        } else {
            m_buffer.append(static_cast<char>(byte));
        }
        return QByteArray();
    }
}

void SLIPDecoder::reset()
{
    m_buffer.clear();
    m_inEscape = false;
    m_packetStarted = false;
}
