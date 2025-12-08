// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

namespace FAMESmartFlasher.Protocol;

/// <summary>
/// SLIP (Serial Line Internet Protocol) encoder/decoder
/// Used for framing ESP32 bootloader packets
/// </summary>
public static class SLIPCodec
{
    // SLIP special characters
    private const byte FrameEnd = 0xC0;
    private const byte FrameEscape = 0xDB;
    private const byte EscapedEnd = 0xDC;      // 0xC0 -> 0xDB 0xDC
    private const byte EscapedEscape = 0xDD;   // 0xDB -> 0xDB 0xDD

    /// <summary>
    /// Encode data with SLIP framing
    /// </summary>
    /// <param name="data">Raw data to encode</param>
    /// <returns>SLIP-encoded packet with 0xC0 delimiters</returns>
    public static byte[] Encode(byte[] data)
    {
        var encoded = new List<byte>(data.Length * 2 + 2);

        encoded.Add(FrameEnd);

        foreach (var b in data)
        {
            switch (b)
            {
                case FrameEnd:
                    encoded.Add(FrameEscape);
                    encoded.Add(EscapedEnd);
                    break;
                case FrameEscape:
                    encoded.Add(FrameEscape);
                    encoded.Add(EscapedEscape);
                    break;
                default:
                    encoded.Add(b);
                    break;
            }
        }

        encoded.Add(FrameEnd);
        return encoded.ToArray();
    }

    /// <summary>
    /// Decode a SLIP-framed packet
    /// </summary>
    /// <param name="slipPacket">SLIP-encoded packet (including delimiters)</param>
    /// <returns>Decoded raw data, or null if invalid</returns>
    public static byte[]? Decode(byte[] slipPacket)
    {
        var decoded = new List<byte>(slipPacket.Length);
        var inEscape = false;
        var started = false;

        foreach (var b in slipPacket)
        {
            if (b == FrameEnd)
            {
                if (started && decoded.Count > 0)
                {
                    return decoded.ToArray();
                }
                started = true;
                decoded.Clear();
                continue;
            }

            if (!started) continue;

            if (inEscape)
            {
                switch (b)
                {
                    case EscapedEnd:
                        decoded.Add(FrameEnd);
                        break;
                    case EscapedEscape:
                        decoded.Add(FrameEscape);
                        break;
                    default:
                        // Invalid escape sequence
                        decoded.Add(b);
                        break;
                }
                inEscape = false;
            }
            else if (b == FrameEscape)
            {
                inEscape = true;
            }
            else
            {
                decoded.Add(b);
            }
        }

        return decoded.Count == 0 ? null : decoded.ToArray();
    }
}

/// <summary>
/// Stateful SLIP decoder for streaming data
/// </summary>
public class SLIPDecoder
{
    private readonly List<byte> _buffer = new();
    private bool _inEscape;
    private bool _packetStarted;

    /// <summary>
    /// Process incoming bytes and return complete packets
    /// </summary>
    /// <param name="byte">Single byte to process</param>
    /// <returns>Complete decoded packet if one was received, null otherwise</returns>
    public byte[]? Process(byte @byte)
    {
        switch (@byte)
        {
            case 0xC0: // FrameEnd
                if (_packetStarted && _buffer.Count > 0)
                {
                    var packet = _buffer.ToArray();
                    Reset();
                    return packet;
                }
                _packetStarted = true;
                _buffer.Clear();
                return null;

            case 0xDB: // FrameEscape
                if (!_packetStarted) return null;
                _inEscape = true;
                return null;

            default:
                if (!_packetStarted) return null;

                if (_inEscape)
                {
                    _inEscape = false;
                    switch (@byte)
                    {
                        case 0xDC: // EscapedEnd
                            _buffer.Add(0xC0);
                            break;
                        case 0xDD: // EscapedEscape
                            _buffer.Add(0xDB);
                            break;
                        default:
                            _buffer.Add(@byte);
                            break;
                    }
                }
                else
                {
                    _buffer.Add(@byte);
                }
                return null;
        }
    }

    /// <summary>
    /// Process multiple bytes at once
    /// </summary>
    /// <param name="data">Data to process</param>
    /// <returns>List of complete decoded packets</returns>
    public List<byte[]> Process(byte[] data)
    {
        var packets = new List<byte[]>();
        foreach (var b in data)
        {
            var packet = Process(b);
            if (packet != null)
            {
                packets.Add(packet);
            }
        }
        return packets;
    }

    /// <summary>
    /// Reset the decoder state
    /// </summary>
    public void Reset()
    {
        _buffer.Clear();
        _inEscape = false;
        _packetStarted = false;
    }
}
