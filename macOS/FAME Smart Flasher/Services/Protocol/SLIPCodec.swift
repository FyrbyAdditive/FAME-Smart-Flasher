import Foundation

/// SLIP (Serial Line Internet Protocol) encoder/decoder
/// Used for framing ESP32 bootloader packets
enum SLIPCodec {
    // SLIP special characters
    static let frameEnd: UInt8 = 0xC0
    static let frameEscape: UInt8 = 0xDB
    static let escapedEnd: UInt8 = 0xDC      // 0xC0 -> 0xDB 0xDC
    static let escapedEscape: UInt8 = 0xDD   // 0xDB -> 0xDB 0xDD

    /// Encode data with SLIP framing
    /// - Parameter data: Raw data to encode
    /// - Returns: SLIP-encoded packet with 0xC0 delimiters
    static func encode(_ data: Data) -> Data {
        var encoded = Data()
        encoded.reserveCapacity(data.count * 2 + 2)

        encoded.append(frameEnd)

        for byte in data {
            switch byte {
            case frameEnd:
                encoded.append(frameEscape)
                encoded.append(escapedEnd)
            case frameEscape:
                encoded.append(frameEscape)
                encoded.append(escapedEscape)
            default:
                encoded.append(byte)
            }
        }

        encoded.append(frameEnd)
        return encoded
    }

    /// Decode a SLIP-framed packet
    /// - Parameter slipPacket: SLIP-encoded packet (including delimiters)
    /// - Returns: Decoded raw data, or nil if invalid
    static func decode(_ slipPacket: Data) -> Data? {
        var decoded = Data()
        decoded.reserveCapacity(slipPacket.count)

        var inEscape = false
        var started = false

        for byte in slipPacket {
            if byte == frameEnd {
                if started && !decoded.isEmpty {
                    return decoded
                }
                started = true
                decoded.removeAll(keepingCapacity: true)
                continue
            }

            guard started else { continue }

            if inEscape {
                switch byte {
                case escapedEnd:
                    decoded.append(frameEnd)
                case escapedEscape:
                    decoded.append(frameEscape)
                default:
                    // Invalid escape sequence
                    decoded.append(byte)
                }
                inEscape = false
            } else if byte == frameEscape {
                inEscape = true
            } else {
                decoded.append(byte)
            }
        }

        return decoded.isEmpty ? nil : decoded
    }
}

/// Stateful SLIP decoder for streaming data
final class SLIPDecoder {
    private var buffer = Data()
    private var inEscape = false
    private var packetStarted = false

    /// Process incoming bytes and return complete packets
    /// - Parameter byte: Single byte to process
    /// - Returns: Complete decoded packet if one was received, nil otherwise
    func process(_ byte: UInt8) -> Data? {
        switch byte {
        case SLIPCodec.frameEnd:
            if packetStarted && !buffer.isEmpty {
                let packet = buffer
                reset()
                return packet
            }
            packetStarted = true
            buffer.removeAll(keepingCapacity: true)
            return nil

        case SLIPCodec.frameEscape:
            guard packetStarted else { return nil }
            inEscape = true
            return nil

        default:
            guard packetStarted else { return nil }

            if inEscape {
                inEscape = false
                switch byte {
                case SLIPCodec.escapedEnd:
                    buffer.append(SLIPCodec.frameEnd)
                case SLIPCodec.escapedEscape:
                    buffer.append(SLIPCodec.frameEscape)
                default:
                    buffer.append(byte)
                }
            } else {
                buffer.append(byte)
            }
            return nil
        }
    }

    /// Process multiple bytes at once
    /// - Parameter data: Data to process
    /// - Returns: Array of complete decoded packets
    func process(_ data: Data) -> [Data] {
        var packets: [Data] = []
        for byte in data {
            if let packet = process(byte) {
                packets.append(packet)
            }
        }
        return packets
    }

    /// Reset the decoder state
    func reset() {
        buffer.removeAll(keepingCapacity: true)
        inEscape = false
        packetStarted = false
    }
}
