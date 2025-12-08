// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

using System.Buffers.Binary;

namespace FAMESmartFlasher.Protocol;

/// <summary>
/// ESP32 bootloader command opcodes
/// </summary>
public enum ESP32Command : byte
{
    Sync = 0x08,
    FlashBegin = 0x02,
    FlashData = 0x03,
    FlashEnd = 0x04,
    ChangeBaudRate = 0x0F,
    ReadReg = 0x0A,
    WriteReg = 0x09,
    SpiAttach = 0x0D
}

/// <summary>
/// ESP32-C3 register addresses for watchdog control
/// </summary>
public static class ESP32C3Registers
{
    public const uint RtcCntlBase = 0x60008000;

    // RTC Watchdog Config
    public const uint RtcWdtConfig0 = RtcCntlBase + 0x0090;
    public const uint RtcWdtWprotect = RtcCntlBase + 0x00A8;
    public const uint RtcWdtWkey = 0x50D83AA1;

    // Super Watchdog Config
    public const uint SwdConf = RtcCntlBase + 0x00AC;
    public const uint SwdWprotect = RtcCntlBase + 0x00B0;
    public const uint SwdWkey = 0x8F1D312A;

    // Bit positions
    public const uint WdtEnBit = 1u << 31;
    public const uint SwdAutoFeedEnBit = 1u << 31;
    public const uint SwdDisableBit = 1u << 30;
}

/// <summary>
/// ESP32 protocol packet builder
/// </summary>
public static class ESP32Protocol
{
    /// <summary>
    /// Checksum seed value
    /// </summary>
    public const byte ChecksumSeed = 0xEF;

    /// <summary>
    /// Default block size for flash data
    /// </summary>
    public const int FlashBlockSize = 1024;

    /// <summary>
    /// Calculate XOR checksum for data
    /// </summary>
    /// <param name="data">Data to checksum</param>
    /// <returns>Checksum value</returns>
    public static uint CalculateChecksum(byte[] data)
    {
        byte checksum = ChecksumSeed;
        foreach (var b in data)
        {
            checksum ^= b;
        }
        return checksum;
    }

    /// <summary>
    /// Build a command packet (before SLIP encoding)
    /// </summary>
    private static byte[] BuildPacket(ESP32Command command, byte[] data, uint checksum = 0)
    {
        var packet = new List<byte>(8 + data.Length);

        // Direction: 0x00 for request
        packet.Add(0x00);
        // Command opcode
        packet.Add((byte)command);
        // Data size (little-endian 16-bit)
        packet.Add((byte)(data.Length & 0xFF));
        packet.Add((byte)((data.Length >> 8) & 0xFF));
        // Checksum (little-endian 32-bit)
        packet.Add((byte)(checksum & 0xFF));
        packet.Add((byte)((checksum >> 8) & 0xFF));
        packet.Add((byte)((checksum >> 16) & 0xFF));
        packet.Add((byte)((checksum >> 24) & 0xFF));
        // Payload
        packet.AddRange(data);

        return packet.ToArray();
    }

    /// <summary>
    /// Build SYNC command packet
    /// SYNC payload: 0x07 0x07 0x12 0x20 followed by 32 bytes of 0x55
    /// </summary>
    public static byte[] BuildSyncCommand()
    {
        var payload = new List<byte> { 0x07, 0x07, 0x12, 0x20 };
        for (int i = 0; i < 32; i++)
        {
            payload.Add(0x55);
        }
        return BuildPacket(ESP32Command.Sync, payload.ToArray());
    }

    /// <summary>
    /// Build SPI_ATTACH command packet
    /// Required before FLASH_BEGIN when using ROM bootloader (not stub)
    /// </summary>
    /// <param name="config">SPI configuration (0 = use default pins)</param>
    public static byte[] BuildSpiAttachCommand(uint config = 0)
    {
        var payload = new byte[8];
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(0, 4), config);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4, 4), 0);
        return BuildPacket(ESP32Command.SpiAttach, payload);
    }

    /// <summary>
    /// Build FLASH_BEGIN command packet
    /// </summary>
    public static byte[] BuildFlashBeginCommand(
        uint size,
        uint numBlocks,
        uint blockSize,
        uint offset,
        bool encrypted = false)
    {
        var payload = new byte[20]; // 5 x 32-bit words for ROM loader

        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(0, 4), size);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4, 4), numBlocks);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8, 4), blockSize);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(12, 4), offset);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(16, 4), encrypted ? 1u : 0u);

        return BuildPacket(ESP32Command.FlashBegin, payload);
    }

    /// <summary>
    /// Build FLASH_DATA command packet
    /// </summary>
    public static byte[] BuildFlashDataCommand(byte[] blockData, uint sequenceNumber)
    {
        var payload = new byte[16 + blockData.Length];

        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(0, 4), (uint)blockData.Length);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4, 4), sequenceNumber);
        // Reserved (8 bytes of zeros) - already zero-initialized
        Array.Copy(blockData, 0, payload, 16, blockData.Length);

        var checksum = CalculateChecksum(blockData);
        return BuildPacket(ESP32Command.FlashData, payload, checksum);
    }

    /// <summary>
    /// Build FLASH_END command packet
    /// </summary>
    public static byte[] BuildFlashEndCommand(bool reboot = true)
    {
        var payload = new byte[4];
        // 0 = reboot, 1 = stay in bootloader
        BinaryPrimitives.WriteUInt32LittleEndian(payload, reboot ? 0u : 1u);
        return BuildPacket(ESP32Command.FlashEnd, payload);
    }

    /// <summary>
    /// Build CHANGE_BAUDRATE command packet
    /// </summary>
    public static byte[] BuildChangeBaudCommand(uint newBaud, uint oldBaud = 0)
    {
        var payload = new byte[8];
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(0, 4), newBaud);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4, 4), oldBaud);
        return BuildPacket(ESP32Command.ChangeBaudRate, payload);
    }

    /// <summary>
    /// Build READ_REG command packet
    /// </summary>
    public static byte[] BuildReadRegCommand(uint address)
    {
        var payload = new byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(payload, address);
        return BuildPacket(ESP32Command.ReadReg, payload);
    }

    /// <summary>
    /// Build WRITE_REG command packet
    /// </summary>
    public static byte[] BuildWriteRegCommand(
        uint address,
        uint value,
        uint mask = 0xFFFFFFFF,
        uint delayUs = 0)
    {
        var payload = new byte[16];
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(0, 4), address);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(4, 4), value);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(8, 4), mask);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(12, 4), delayUs);
        return BuildPacket(ESP32Command.WriteReg, payload);
    }
}

/// <summary>
/// ESP32 bootloader response parser
/// </summary>
public class ESP32Response
{
    public byte Direction { get; init; }
    public byte Command { get; init; }
    public ushort Size { get; init; }
    public uint Value { get; init; }
    public byte[] Data { get; init; } = Array.Empty<byte>();
    public byte Status { get; init; }
    public byte Error { get; init; }

    public bool IsSuccess => Status == 0 && Error == 0;

    /// <summary>
    /// Parse a decoded SLIP packet into a response
    /// </summary>
    public static ESP32Response? Parse(byte[] packet)
    {
        if (packet.Length < 8) return null;

        var direction = packet[0];
        // Response direction should be 0x01
        if (direction != 0x01) return null;

        var command = packet[1];
        var size = BinaryPrimitives.ReadUInt16LittleEndian(packet.AsSpan(2, 2));
        var value = BinaryPrimitives.ReadUInt32LittleEndian(packet.AsSpan(4, 4));

        // Extract data (if any) and status bytes
        var dataLength = size > 2 ? size - 2 : 0;
        var data = new byte[dataLength];
        if (dataLength > 0 && packet.Length >= 8 + dataLength)
        {
            Array.Copy(packet, 8, data, 0, dataLength);
        }

        var statusIndex = 8 + dataLength;
        var status = packet.Length > statusIndex ? packet[statusIndex] : (byte)0;
        var error = packet.Length > statusIndex + 1 ? packet[statusIndex + 1] : (byte)0;

        return new ESP32Response
        {
            Direction = direction,
            Command = command,
            Size = size,
            Value = value,
            Data = data,
            Status = status,
            Error = error
        };
    }
}
