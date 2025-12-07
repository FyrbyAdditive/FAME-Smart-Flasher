import Foundation

/// ESP32 bootloader command opcodes
enum ESP32Command: UInt8 {
    case sync = 0x08
    case flashBegin = 0x02
    case flashData = 0x03
    case flashEnd = 0x04
    case changeBaudRate = 0x0F
    case readReg = 0x0A
    case writeReg = 0x09
    case spiAttach = 0x0D
}

/// ESP32-C3 register addresses for watchdog control
enum ESP32C3Registers {
    static let rtcCntlBase: UInt32 = 0x60008000

    // RTC Watchdog Config
    static let rtcWdtConfig0: UInt32 = rtcCntlBase + 0x0090
    static let rtcWdtWprotect: UInt32 = rtcCntlBase + 0x00A8
    static let rtcWdtWkey: UInt32 = 0x50D83AA1

    // Super Watchdog Config
    static let swdConf: UInt32 = rtcCntlBase + 0x00AC
    static let swdWprotect: UInt32 = rtcCntlBase + 0x00B0
    static let swdWkey: UInt32 = 0x8F1D312A

    // Bit positions
    static let wdtEnBit: UInt32 = 1 << 31
    static let swdAutoFeedEnBit: UInt32 = 1 << 31
    static let swdDisableBit: UInt32 = 1 << 30
}

/// ESP32 protocol packet builder
enum ESP32Protocol {
    /// Checksum seed value
    static let checksumSeed: UInt8 = 0xEF

    /// Default block size for flash data
    static let flashBlockSize: Int = 1024

    /// Calculate XOR checksum for data
    /// - Parameter data: Data to checksum
    /// - Returns: Checksum value
    static func calculateChecksum(_ data: Data) -> UInt32 {
        var checksum = checksumSeed
        for byte in data {
            checksum ^= byte
        }
        return UInt32(checksum)
    }

    /// Build a command packet (before SLIP encoding)
    /// - Parameters:
    ///   - command: Command opcode
    ///   - data: Command payload
    ///   - checksum: Checksum value (for data commands)
    /// - Returns: Raw command packet
    private static func buildPacket(
        command: ESP32Command,
        data: Data,
        checksum: UInt32 = 0
    ) -> Data {
        var packet = Data()
        packet.reserveCapacity(8 + data.count)

        // Direction: 0x00 for request
        packet.append(0x00)
        // Command opcode
        packet.append(command.rawValue)
        // Data size (little-endian 16-bit)
        packet.append(UInt8(data.count & 0xFF))
        packet.append(UInt8((data.count >> 8) & 0xFF))
        // Checksum (little-endian 32-bit)
        packet.append(UInt8(checksum & 0xFF))
        packet.append(UInt8((checksum >> 8) & 0xFF))
        packet.append(UInt8((checksum >> 16) & 0xFF))
        packet.append(UInt8((checksum >> 24) & 0xFF))
        // Payload
        packet.append(data)

        return packet
    }

    /// Build SYNC command packet
    /// SYNC payload: 0x07 0x07 0x12 0x20 followed by 32 bytes of 0x55
    static func buildSyncCommand() -> Data {
        var payload = Data([0x07, 0x07, 0x12, 0x20])
        payload.append(contentsOf: [UInt8](repeating: 0x55, count: 32))
        return buildPacket(command: .sync, data: payload)
    }

    /// Build SPI_ATTACH command packet
    /// Required before FLASH_BEGIN when using ROM bootloader (not stub)
    /// - Parameter config: SPI configuration (0 = use default pins)
    /// - Returns: Command packet
    static func buildSpiAttachCommand(config: UInt32 = 0) -> Data {
        var payload = Data()
        // SPI configuration - 0 means use default SPI flash pins
        // This is what esptool sends for normal flash operations
        payload.append(contentsOf: config.littleEndianBytes)
        // For ESP32-C3, we need 8 bytes total (second word is also 0)
        payload.append(contentsOf: UInt32(0).littleEndianBytes)
        return buildPacket(command: .spiAttach, data: payload)
    }

    /// Build FLASH_BEGIN command packet
    /// - Parameters:
    ///   - size: Total firmware size
    ///   - numBlocks: Number of data blocks
    ///   - blockSize: Size of each block
    ///   - offset: Flash address offset
    ///   - encrypted: Whether to use encrypted flash (ROM loader only)
    /// - Returns: Command packet
    static func buildFlashBeginCommand(
        size: UInt32,
        numBlocks: UInt32,
        blockSize: UInt32,
        offset: UInt32,
        encrypted: Bool = false
    ) -> Data {
        var payload = Data()
        payload.reserveCapacity(20)  // 5 x 32-bit words for ROM loader

        // Erase size (little-endian)
        payload.append(contentsOf: size.littleEndianBytes)
        // Number of blocks
        payload.append(contentsOf: numBlocks.littleEndianBytes)
        // Block size
        payload.append(contentsOf: blockSize.littleEndianBytes)
        // Offset
        payload.append(contentsOf: offset.littleEndianBytes)
        // Encryption flag (ROM loader requires this 5th word)
        // 0 = not encrypted, 1 = encrypted
        payload.append(contentsOf: (encrypted ? UInt32(1) : UInt32(0)).littleEndianBytes)

        return buildPacket(command: .flashBegin, data: payload)
    }

    /// Build FLASH_DATA command packet
    /// - Parameters:
    ///   - data: Block data to flash
    ///   - sequenceNumber: Block sequence number
    /// - Returns: Command packet
    static func buildFlashDataCommand(
        data blockData: Data,
        sequenceNumber: UInt32
    ) -> Data {
        var payload = Data()
        payload.reserveCapacity(16 + blockData.count)

        // Data length (little-endian)
        payload.append(contentsOf: UInt32(blockData.count).littleEndianBytes)
        // Sequence number
        payload.append(contentsOf: sequenceNumber.littleEndianBytes)
        // Reserved (8 bytes of zeros)
        payload.append(contentsOf: [UInt8](repeating: 0, count: 8))
        // Actual data
        payload.append(blockData)

        let checksum = calculateChecksum(blockData)
        return buildPacket(command: .flashData, data: payload, checksum: checksum)
    }

    /// Build FLASH_END command packet
    /// - Parameter reboot: Whether to reboot the device
    /// - Returns: Command packet
    static func buildFlashEndCommand(reboot: Bool = true) -> Data {
        var payload = Data()
        // 0 = reboot, 1 = stay in bootloader
        let flag: UInt32 = reboot ? 0 : 1
        payload.append(contentsOf: flag.littleEndianBytes)

        return buildPacket(command: .flashEnd, data: payload)
    }

    /// Build CHANGE_BAUDRATE command packet
    /// - Parameters:
    ///   - newBaud: New baud rate
    ///   - oldBaud: Current baud rate (0 for ROM default)
    /// - Returns: Command packet
    static func buildChangeBaudCommand(newBaud: UInt32, oldBaud: UInt32 = 0) -> Data {
        var payload = Data()
        payload.append(contentsOf: newBaud.littleEndianBytes)
        payload.append(contentsOf: oldBaud.littleEndianBytes)

        return buildPacket(command: .changeBaudRate, data: payload)
    }

    /// Build READ_REG command packet
    /// - Parameter address: Register address to read
    /// - Returns: Command packet
    static func buildReadRegCommand(address: UInt32) -> Data {
        var payload = Data()
        payload.append(contentsOf: address.littleEndianBytes)
        return buildPacket(command: .readReg, data: payload)
    }

    /// Build WRITE_REG command packet
    /// - Parameters:
    ///   - address: Register address to write
    ///   - value: Value to write
    ///   - mask: Bit mask (0xFFFFFFFF for full write)
    ///   - delayUs: Delay after write in microseconds
    /// - Returns: Command packet
    static func buildWriteRegCommand(
        address: UInt32,
        value: UInt32,
        mask: UInt32 = 0xFFFFFFFF,
        delayUs: UInt32 = 0
    ) -> Data {
        var payload = Data()
        payload.append(contentsOf: address.littleEndianBytes)
        payload.append(contentsOf: value.littleEndianBytes)
        payload.append(contentsOf: mask.littleEndianBytes)
        payload.append(contentsOf: delayUs.littleEndianBytes)
        return buildPacket(command: .writeReg, data: payload)
    }
}

/// ESP32 bootloader response parser
struct ESP32Response {
    let direction: UInt8
    let command: UInt8
    let size: UInt16
    let value: UInt32
    let data: Data
    let status: UInt8
    let error: UInt8

    var isSuccess: Bool {
        status == 0 && error == 0
    }

    /// Parse a decoded SLIP packet into a response
    /// - Parameter packet: Decoded packet data
    /// - Returns: Parsed response, or nil if invalid
    static func parse(_ packet: Data) -> ESP32Response? {
        guard packet.count >= 8 else { return nil }

        let direction = packet[0]
        // Response direction should be 0x01
        guard direction == 0x01 else { return nil }

        let command = packet[1]
        let size = UInt16(packet[2]) | (UInt16(packet[3]) << 8)
        let value = UInt32(packet[4]) |
                    (UInt32(packet[5]) << 8) |
                    (UInt32(packet[6]) << 16) |
                    (UInt32(packet[7]) << 24)

        let dataEndIndex = min(8 + Int(size), packet.count)
        let data = packet.count > 8 ? Data(packet[8..<dataEndIndex]) : Data()

        // Status bytes are at the START of the data section (not the end!)
        // Format: [status (1 byte)][error (1 byte)][optional additional data]
        let status = data.count >= 1 ? data[0] : 0
        let error = data.count >= 2 ? data[1] : 0

        return ESP32Response(
            direction: direction,
            command: command,
            size: size,
            value: value,
            data: data,
            status: status,
            error: error
        )
    }
}

// MARK: - Helper Extensions

extension UInt32 {
    var littleEndianBytes: [UInt8] {
        [
            UInt8(self & 0xFF),
            UInt8((self >> 8) & 0xFF),
            UInt8((self >> 16) & 0xFF),
            UInt8((self >> 24) & 0xFF)
        ]
    }
}

extension UInt16 {
    var littleEndianBytes: [UInt8] {
        [
            UInt8(self & 0xFF),
            UInt8((self >> 8) & 0xFF)
        ]
    }
}
