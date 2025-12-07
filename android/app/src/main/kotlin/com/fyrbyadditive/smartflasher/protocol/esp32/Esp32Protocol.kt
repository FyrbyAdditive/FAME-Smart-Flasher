package com.fyrbyadditive.smartflasher.protocol.esp32

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * ESP32 bootloader command opcodes
 */
enum class Esp32Command(val opcode: Byte) {
    SYNC(0x08),
    FLASH_BEGIN(0x02),
    FLASH_DATA(0x03),
    FLASH_END(0x04),
    CHANGE_BAUD_RATE(0x0F),
    READ_REG(0x0A),
    WRITE_REG(0x09),
    SPI_ATTACH(0x0D)
}

/**
 * ESP32-C3 register addresses for watchdog control
 */
object Esp32C3Registers {
    const val RTC_CNTL_BASE: Long = 0x60008000L

    // RTC Watchdog Config
    val RTC_WDT_CONFIG0: Long = RTC_CNTL_BASE + 0x0090
    val RTC_WDT_WPROTECT: Long = RTC_CNTL_BASE + 0x00A8
    const val RTC_WDT_WKEY: Long = 0x50D83AA1L

    // Super Watchdog Config
    val SWD_CONF: Long = RTC_CNTL_BASE + 0x00AC
    val SWD_WPROTECT: Long = RTC_CNTL_BASE + 0x00B0
    const val SWD_WKEY: Long = 0x8F1D312AL

    // Bit positions
    const val WDT_EN_BIT: Long = 1L shl 31
    const val SWD_AUTO_FEED_EN_BIT: Long = 1L shl 31
    const val SWD_DISABLE_BIT: Long = 1L shl 30
}

/**
 * ESP32 protocol packet builder
 */
object Esp32Protocol {
    /** Checksum seed value */
    const val CHECKSUM_SEED: Byte = 0xEF.toByte()

    /** Default block size for flash data */
    const val FLASH_BLOCK_SIZE: Int = 1024

    /**
     * Calculate XOR checksum for data
     * @param data Data to checksum
     * @return Checksum value
     */
    fun calculateChecksum(data: ByteArray): Long {
        var checksum = CHECKSUM_SEED.toInt() and 0xFF
        for (byte in data) {
            checksum = checksum xor (byte.toInt() and 0xFF)
        }
        return checksum.toLong()
    }

    /**
     * Build a command packet (before SLIP encoding)
     */
    private fun buildPacket(
        command: Esp32Command,
        data: ByteArray,
        checksum: Long = 0
    ): ByteArray {
        val packet = ByteBuffer.allocate(8 + data.size)
        packet.order(ByteOrder.LITTLE_ENDIAN)

        // Direction: 0x00 for request
        packet.put(0x00)
        // Command opcode
        packet.put(command.opcode)
        // Data size (little-endian 16-bit)
        packet.putShort(data.size.toShort())
        // Checksum (little-endian 32-bit)
        packet.putInt(checksum.toInt())
        // Payload
        packet.put(data)

        return packet.array()
    }

    /**
     * Build SYNC command packet
     * SYNC payload: 0x07 0x07 0x12 0x20 followed by 32 bytes of 0x55
     */
    fun buildSyncCommand(): ByteArray {
        val payload = ByteArray(36)
        payload[0] = 0x07
        payload[1] = 0x07
        payload[2] = 0x12
        payload[3] = 0x20
        for (i in 4 until 36) {
            payload[i] = 0x55
        }
        return buildPacket(Esp32Command.SYNC, payload)
    }

    /**
     * Build SPI_ATTACH command packet
     * Required before FLASH_BEGIN when using ROM bootloader (not stub)
     * @param config SPI configuration (0 = use default pins)
     */
    fun buildSpiAttachCommand(config: Long = 0): ByteArray {
        val payload = ByteBuffer.allocate(8)
        payload.order(ByteOrder.LITTLE_ENDIAN)
        // SPI configuration - 0 means use default SPI flash pins
        payload.putInt(config.toInt())
        // For ESP32-C3, we need 8 bytes total (second word is also 0)
        payload.putInt(0)
        return buildPacket(Esp32Command.SPI_ATTACH, payload.array())
    }

    /**
     * Build FLASH_BEGIN command packet
     */
    fun buildFlashBeginCommand(
        size: Long,
        numBlocks: Long,
        blockSize: Long,
        offset: Long,
        encrypted: Boolean = false
    ): ByteArray {
        val payload = ByteBuffer.allocate(20) // 5 x 32-bit words for ROM loader
        payload.order(ByteOrder.LITTLE_ENDIAN)

        // Erase size (little-endian)
        payload.putInt(size.toInt())
        // Number of blocks
        payload.putInt(numBlocks.toInt())
        // Block size
        payload.putInt(blockSize.toInt())
        // Offset
        payload.putInt(offset.toInt())
        // Encryption flag (ROM loader requires this 5th word)
        payload.putInt(if (encrypted) 1 else 0)

        return buildPacket(Esp32Command.FLASH_BEGIN, payload.array())
    }

    /**
     * Build FLASH_DATA command packet
     */
    fun buildFlashDataCommand(blockData: ByteArray, sequenceNumber: Long): ByteArray {
        val payload = ByteBuffer.allocate(16 + blockData.size)
        payload.order(ByteOrder.LITTLE_ENDIAN)

        // Data length (little-endian)
        payload.putInt(blockData.size)
        // Sequence number
        payload.putInt(sequenceNumber.toInt())
        // Reserved (8 bytes of zeros)
        payload.putLong(0)
        // Actual data
        payload.put(blockData)

        val checksum = calculateChecksum(blockData)
        return buildPacket(Esp32Command.FLASH_DATA, payload.array(), checksum)
    }

    /**
     * Build FLASH_END command packet
     * @param reboot Whether to reboot the device
     */
    fun buildFlashEndCommand(reboot: Boolean = true): ByteArray {
        val payload = ByteBuffer.allocate(4)
        payload.order(ByteOrder.LITTLE_ENDIAN)
        // 0 = reboot, 1 = stay in bootloader
        payload.putInt(if (reboot) 0 else 1)
        return buildPacket(Esp32Command.FLASH_END, payload.array())
    }

    /**
     * Build CHANGE_BAUDRATE command packet
     */
    fun buildChangeBaudCommand(newBaud: Long, oldBaud: Long = 0): ByteArray {
        val payload = ByteBuffer.allocate(8)
        payload.order(ByteOrder.LITTLE_ENDIAN)
        payload.putInt(newBaud.toInt())
        payload.putInt(oldBaud.toInt())
        return buildPacket(Esp32Command.CHANGE_BAUD_RATE, payload.array())
    }

    /**
     * Build READ_REG command packet
     * @param address Register address to read
     */
    fun buildReadRegCommand(address: Long): ByteArray {
        val payload = ByteBuffer.allocate(4)
        payload.order(ByteOrder.LITTLE_ENDIAN)
        payload.putInt(address.toInt())
        return buildPacket(Esp32Command.READ_REG, payload.array())
    }

    /**
     * Build WRITE_REG command packet
     */
    fun buildWriteRegCommand(
        address: Long,
        value: Long,
        mask: Long = 0xFFFFFFFFL,
        delayUs: Long = 0
    ): ByteArray {
        val payload = ByteBuffer.allocate(16)
        payload.order(ByteOrder.LITTLE_ENDIAN)
        payload.putInt(address.toInt())
        payload.putInt(value.toInt())
        payload.putInt(mask.toInt())
        payload.putInt(delayUs.toInt())
        return buildPacket(Esp32Command.WRITE_REG, payload.array())
    }
}

/**
 * ESP32 bootloader response
 */
data class Esp32Response(
    val direction: Byte,
    val command: Byte,
    val size: Int,
    val value: Long,
    val data: ByteArray,
    val status: Byte,
    val error: Byte
) {
    val isSuccess: Boolean
        get() = status.toInt() == 0 && error.toInt() == 0

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is Esp32Response) return false
        return direction == other.direction &&
                command == other.command &&
                size == other.size &&
                value == other.value &&
                data.contentEquals(other.data) &&
                status == other.status &&
                error == other.error
    }

    override fun hashCode(): Int {
        var result = direction.toInt()
        result = 31 * result + command.toInt()
        result = 31 * result + size
        result = 31 * result + value.hashCode()
        result = 31 * result + data.contentHashCode()
        result = 31 * result + status.toInt()
        result = 31 * result + error.toInt()
        return result
    }

    companion object {
        /**
         * Parse a decoded SLIP packet into a response
         * @param packet Decoded packet data
         * @return Parsed response, or null if invalid
         */
        fun parse(packet: ByteArray): Esp32Response? {
            if (packet.size < 8) return null

            val direction = packet[0]
            // Response direction should be 0x01
            if (direction != 0x01.toByte()) return null

            val command = packet[1]
            val size = (packet[2].toInt() and 0xFF) or ((packet[3].toInt() and 0xFF) shl 8)
            val value = (packet[4].toLong() and 0xFF) or
                    ((packet[5].toLong() and 0xFF) shl 8) or
                    ((packet[6].toLong() and 0xFF) shl 16) or
                    ((packet[7].toLong() and 0xFF) shl 24)

            val dataEndIndex = minOf(8 + size, packet.size)
            val data = if (packet.size > 8) packet.copyOfRange(8, dataEndIndex) else ByteArray(0)

            // Status bytes are at the START of the data section (not the end!)
            // Format: [status (1 byte)][error (1 byte)][optional additional data]
            val status = if (data.isNotEmpty()) data[0] else 0
            val error = if (data.size >= 2) data[1] else 0

            return Esp32Response(
                direction = direction,
                command = command,
                size = size,
                value = value,
                data = data,
                status = status,
                error = error
            )
        }
    }
}
