package com.fyrbyadditive.smartflasher.protocol.slip

/**
 * SLIP (Serial Line Internet Protocol) encoder/decoder
 * Used for framing ESP32 bootloader packets
 */
object SlipCodec {
    // SLIP special characters
    const val FRAME_END: Byte = 0xC0.toByte()
    const val FRAME_ESCAPE: Byte = 0xDB.toByte()
    const val ESCAPED_END: Byte = 0xDC.toByte()      // 0xC0 -> 0xDB 0xDC
    const val ESCAPED_ESCAPE: Byte = 0xDD.toByte()   // 0xDB -> 0xDB 0xDD

    /**
     * Encode data with SLIP framing
     * @param data Raw data to encode
     * @return SLIP-encoded packet with 0xC0 delimiters
     */
    fun encode(data: ByteArray): ByteArray {
        val encoded = mutableListOf<Byte>()

        encoded.add(FRAME_END)

        for (byte in data) {
            when (byte) {
                FRAME_END -> {
                    encoded.add(FRAME_ESCAPE)
                    encoded.add(ESCAPED_END)
                }
                FRAME_ESCAPE -> {
                    encoded.add(FRAME_ESCAPE)
                    encoded.add(ESCAPED_ESCAPE)
                }
                else -> encoded.add(byte)
            }
        }

        encoded.add(FRAME_END)
        return encoded.toByteArray()
    }

    /**
     * Decode a SLIP-framed packet
     * @param slipPacket SLIP-encoded packet (including delimiters)
     * @return Decoded raw data, or null if invalid
     */
    fun decode(slipPacket: ByteArray): ByteArray? {
        val decoded = mutableListOf<Byte>()
        var inEscape = false
        var started = false

        for (byte in slipPacket) {
            if (byte == FRAME_END) {
                if (started && decoded.isNotEmpty()) {
                    return decoded.toByteArray()
                }
                started = true
                decoded.clear()
                continue
            }

            if (!started) continue

            if (inEscape) {
                when (byte) {
                    ESCAPED_END -> decoded.add(FRAME_END)
                    ESCAPED_ESCAPE -> decoded.add(FRAME_ESCAPE)
                    else -> decoded.add(byte) // Invalid escape sequence
                }
                inEscape = false
            } else if (byte == FRAME_ESCAPE) {
                inEscape = true
            } else {
                decoded.add(byte)
            }
        }

        return if (decoded.isEmpty()) null else decoded.toByteArray()
    }
}

/**
 * Stateful SLIP decoder for streaming data
 */
class SlipDecoder {
    private val buffer = mutableListOf<Byte>()
    private var inEscape = false
    private var packetStarted = false

    /**
     * Process incoming bytes and return complete packets
     * @param byte Single byte to process
     * @return Complete decoded packet if one was received, null otherwise
     */
    fun process(byte: Byte): ByteArray? {
        return when (byte) {
            SlipCodec.FRAME_END -> {
                if (packetStarted && buffer.isNotEmpty()) {
                    val packet = buffer.toByteArray()
                    reset()
                    packet
                } else {
                    packetStarted = true
                    buffer.clear()
                    null
                }
            }
            SlipCodec.FRAME_ESCAPE -> {
                if (packetStarted) {
                    inEscape = true
                }
                null
            }
            else -> {
                if (!packetStarted) return null

                if (inEscape) {
                    inEscape = false
                    when (byte) {
                        SlipCodec.ESCAPED_END -> buffer.add(SlipCodec.FRAME_END)
                        SlipCodec.ESCAPED_ESCAPE -> buffer.add(SlipCodec.FRAME_ESCAPE)
                        else -> buffer.add(byte)
                    }
                } else {
                    buffer.add(byte)
                }
                null
            }
        }
    }

    /**
     * Process multiple bytes at once
     * @param data Data to process
     * @return List of complete decoded packets
     */
    fun process(data: ByteArray): List<ByteArray> {
        val packets = mutableListOf<ByteArray>()
        for (byte in data) {
            process(byte)?.let { packets.add(it) }
        }
        return packets
    }

    /**
     * Reset the decoder state
     */
    fun reset() {
        buffer.clear()
        inEscape = false
        packetStarted = false
    }
}
