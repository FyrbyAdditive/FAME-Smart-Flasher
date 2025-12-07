package com.fyrbyadditive.smartflasher.ui.screens

import android.app.Application
import android.net.Uri
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.fyrbyadditive.smartflasher.protocol.esp32.Esp32C3Registers
import com.fyrbyadditive.smartflasher.protocol.esp32.Esp32Command
import com.fyrbyadditive.smartflasher.protocol.esp32.Esp32Protocol
import com.fyrbyadditive.smartflasher.protocol.esp32.Esp32Response
import com.fyrbyadditive.smartflasher.protocol.slip.SlipCodec
import com.fyrbyadditive.smartflasher.protocol.slip.SlipDecoder
import com.fyrbyadditive.smartflasher.usb.SerialPortInfo
import com.fyrbyadditive.smartflasher.usb.UsbDeviceManager
import com.fyrbyadditive.smartflasher.usb.UsbPermissionHandler
import com.hoho.android.usbserial.driver.UsbSerialPort
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import java.io.IOException

/**
 * Flashing state
 */
sealed class FlashingState {
    object Idle : FlashingState()
    object Connecting : FlashingState()
    object Syncing : FlashingState()
    object ChangingBaudRate : FlashingState()
    object Erasing : FlashingState()
    data class Flashing(val progress: Float) : FlashingState()
    object Verifying : FlashingState()
    object Restarting : FlashingState()
    object Complete : FlashingState()
    data class Error(val message: String) : FlashingState()

    val isActive: Boolean
        get() = this !is Idle && this !is Complete && this !is Error

    val statusMessage: String
        get() = when (this) {
            is Idle -> "Ready"
            is Connecting -> "Connecting to device..."
            is Syncing -> "Syncing with bootloader..."
            is ChangingBaudRate -> "Changing baud rate..."
            is Erasing -> "Erasing flash..."
            is Flashing -> "Flashing... ${(progress * 100).toInt()}%"
            is Verifying -> "Verifying..."
            is Restarting -> "Restarting device..."
            is Complete -> "Flash complete!"
            is Error -> "Error: $message"
        }
}

/**
 * Baud rate options
 */
enum class BaudRate(val value: Int) {
    BAUD_115200(115200),
    BAUD_230400(230400),
    BAUD_460800(460800),
    BAUD_921600(921600);

    companion object {
        fun fromValue(value: Int): BaudRate = entries.find { it.value == value } ?: BAUD_115200
    }
}

/**
 * Firmware file information
 */
data class FirmwareFile(
    val uri: Uri,
    val fileName: String,
    val data: ByteArray,
    val offset: Long = 0x0
) {
    val size: Int get() = data.size

    val sizeDescription: String
        get() {
            val kb = size / 1024.0
            return if (kb >= 1024) {
                String.format("%.2f MB", kb / 1024)
            } else {
                String.format("%.2f KB", kb)
            }
        }

    val isValid: Boolean
        get() = data.isNotEmpty() && data[0] == 0xE9.toByte()

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is FirmwareFile) return false
        return uri == other.uri && fileName == other.fileName && data.contentEquals(other.data)
    }

    override fun hashCode(): Int {
        var result = uri.hashCode()
        result = 31 * result + fileName.hashCode()
        result = 31 * result + data.contentHashCode()
        return result
    }
}

/**
 * Main ViewModel for the home screen
 */
class HomeViewModel(application: Application) : AndroidViewModel(application) {
    private val context = application.applicationContext

    // USB management
    val usbDeviceManager = UsbDeviceManager(context)
    private val permissionHandler = UsbPermissionHandler(context)

    // State
    private val _selectedPort = MutableStateFlow<SerialPortInfo?>(null)
    val selectedPort: StateFlow<SerialPortInfo?> = _selectedPort.asStateFlow()

    private val _selectedBaudRate = MutableStateFlow(BaudRate.BAUD_115200)
    val selectedBaudRate: StateFlow<BaudRate> = _selectedBaudRate.asStateFlow()

    private val _firmwareFile = MutableStateFlow<FirmwareFile?>(null)
    val firmwareFile: StateFlow<FirmwareFile?> = _firmwareFile.asStateFlow()

    private val _flashingState = MutableStateFlow<FlashingState>(FlashingState.Idle)
    val flashingState: StateFlow<FlashingState> = _flashingState.asStateFlow()

    private val _progress = MutableStateFlow(0f)
    val progress: StateFlow<Float> = _progress.asStateFlow()

    // Serial monitor
    private val _isSerialMonitorEnabled = MutableStateFlow(false)
    val isSerialMonitorEnabled: StateFlow<Boolean> = _isSerialMonitorEnabled.asStateFlow()

    private val _serialMonitorOutput = MutableStateFlow("")
    val serialMonitorOutput: StateFlow<String> = _serialMonitorOutput.asStateFlow()

    private val _isSerialMonitorConnected = MutableStateFlow(false)
    val isSerialMonitorConnected: StateFlow<Boolean> = _isSerialMonitorConnected.asStateFlow()

    // Private state
    private var serialPort: UsbSerialPort? = null
    private var serialMonitorPort: UsbSerialPort? = null  // Separate port for serial monitor
    private var flashingJob: Job? = null
    private var serialMonitorJob: Job? = null
    private var autoReconnectJob: Job? = null
    private var isCancelled = false

    // Protocol
    private val slipDecoder = SlipDecoder()
    private val syncRetries = 20
    private val responseTimeout = 5000L // 5 seconds
    private val autoReconnectIntervalMs = 2000L

    val canFlash: Boolean
        get() = _selectedPort.value != null &&
                _firmwareFile.value != null &&
                !_flashingState.value.isActive

    init {
        usbDeviceManager.startObserving()
    }

    override fun onCleared() {
        super.onCleared()
        try {
            usbDeviceManager.stopObserving()
        } catch (e: Exception) {
            Log.e(TAG, "Error stopping USB observer", e)
        }
        try {
            stopAutoReconnect()
            disconnectSerialMonitor()
        } catch (e: Exception) {
            Log.e(TAG, "Error disconnecting serial monitor", e)
        }
        try {
            closePort()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing port", e)
        }
    }

    fun selectPort(port: SerialPortInfo?) {
        _selectedPort.value = port
        if (_isSerialMonitorEnabled.value && !_flashingState.value.isActive) {
            viewModelScope.launch {
                try {
                    reconnectSerialMonitor()
                } catch (e: Exception) {
                    Log.e(TAG, "Error reconnecting serial monitor after port selection", e)
                }
            }
        }
    }

    /**
     * Refresh port selection after device reconnect
     * Finds the same device by VID/PID after re-enumeration
     */
    private fun refreshSelectedPort(): SerialPortInfo? {
        val currentPort = _selectedPort.value ?: return null

        // Store VID/PID before refresh (device reference may become invalid)
        val targetVid = try { currentPort.vendorId } catch (e: Exception) { return null }
        val targetPid = try { currentPort.productId } catch (e: Exception) { return null }

        try {
            usbDeviceManager.refreshPorts()
        } catch (e: Exception) {
            Log.e(TAG, "Error refreshing ports", e)
            return null
        }

        // Find the same device by VID/PID
        val refreshedPort = usbDeviceManager.availablePorts.value.find {
            try {
                it.vendorId == targetVid && it.productId == targetPid
            } catch (e: Exception) {
                false
            }
        }

        if (refreshedPort != null) {
            _selectedPort.value = refreshedPort
            return refreshedPort
        }

        // Device no longer available
        return null
    }

    fun selectBaudRate(baudRate: Int) {
        _selectedBaudRate.value = BaudRate.fromValue(baudRate)
    }

    fun setFirmware(uri: Uri, fileName: String, data: ByteArray) {
        val firmware = FirmwareFile(uri, fileName, data)
        _firmwareFile.value = firmware

        if (!firmware.isValid) {
            _flashingState.value = FlashingState.Error("Invalid firmware: Missing ESP32 magic byte")
        } else {
            _flashingState.value = FlashingState.Idle
        }
    }

    fun startFlashing() {
        val port = _selectedPort.value ?: return
        val firmware = _firmwareFile.value ?: return

        flashingJob = viewModelScope.launch {
            try {
                flash(port, firmware)
            } catch (e: CancellationException) {
                _flashingState.value = FlashingState.Error("Cancelled")
            } catch (e: Exception) {
                Log.e(TAG, "Flashing failed", e)
                _flashingState.value = FlashingState.Error(e.message ?: "Unknown error")
            } finally {
                closePort()
            }
        }
    }

    fun cancelFlashing() {
        isCancelled = true
        flashingJob?.cancel()
        _flashingState.value = FlashingState.Idle
        _progress.value = 0f
    }

    fun toggleSerialMonitor() {
        viewModelScope.launch {
            if (_isSerialMonitorEnabled.value) {
                disconnectSerialMonitor()
                _isSerialMonitorEnabled.value = false
            } else {
                _isSerialMonitorEnabled.value = true
                connectSerialMonitor()
            }
        }
    }

    fun clearSerialOutput() {
        _serialMonitorOutput.value = ""
    }

    // MARK: - Flashing Implementation

    private suspend fun flash(portInfo: SerialPortInfo, firmware: FirmwareFile) {
        isCancelled = false
        val wasSerialMonitorEnabled = _isSerialMonitorEnabled.value

        // Disconnect serial monitor before flashing
        if (wasSerialMonitorEnabled || _isSerialMonitorConnected.value) {
            appendSerialOutput("[Disconnecting for flash...]\n")
            disconnectSerialMonitor()
            delay(500)
        }

        try {
            // 1. Request permission and connect
            _flashingState.value = FlashingState.Connecting

            if (!permissionHandler.hasPermission(portInfo)) {
                if (!permissionHandler.requestPermission(portInfo)) {
                    throw IOException("USB permission denied")
                }
            }

            serialPort = usbDeviceManager.openPort(portInfo, 115200)
                ?: throw IOException("Failed to open port")

            val isEsp32C3 = portInfo.isEsp32C3
            Log.d(TAG, "Connected to ${portInfo.displayName}, ESP32-C3: $isEsp32C3")

            // 2. Enter bootloader mode
            enterBootloaderMode(isEsp32C3)
            delay(500)

            // Flush any boot output
            flushInput()

            // 3. Sync with device
            _flashingState.value = FlashingState.Syncing
            syncWithRetry()

            // CRITICAL: Disable watchdogs IMMEDIATELY after sync
            if (isEsp32C3) {
                Log.d(TAG, "Disabling watchdogs immediately after sync")
                disableWatchdogs()
            }

            // 4. Change baud rate if needed
            val targetBaud = _selectedBaudRate.value
            if (targetBaud != BaudRate.BAUD_115200) {
                _flashingState.value = FlashingState.ChangingBaudRate
                changeBaudRate(targetBaud)
            }

            // 5. Attach SPI flash
            Log.d(TAG, "Sending SPI_ATTACH command")
            spiAttach()

            // 6. Flash the firmware
            val blockSize = Esp32Protocol.FLASH_BLOCK_SIZE
            val numBlocks = (firmware.size + blockSize - 1) / blockSize

            Log.d(TAG, "Flashing ${firmware.fileName}: ${firmware.size} bytes at offset 0x${firmware.offset.toString(16)}")

            // Begin flash
            _flashingState.value = FlashingState.Erasing
            flashBegin(
                size = firmware.size.toLong(),
                numBlocks = numBlocks.toLong(),
                blockSize = blockSize.toLong(),
                offset = firmware.offset
            )

            // Send data blocks
            for (blockNum in 0 until numBlocks) {
                if (isCancelled) throw CancellationException("Cancelled")

                val start = blockNum * blockSize
                val end = minOf(start + blockSize, firmware.size)
                var blockData = firmware.data.copyOfRange(start, end)

                // Pad last block with 0xFF
                if (blockData.size < blockSize) {
                    val padded = ByteArray(blockSize) { 0xFF.toByte() }
                    blockData.copyInto(padded)
                    blockData = padded
                }

                val currentProgress = (blockNum + 1).toFloat() / numBlocks
                _flashingState.value = FlashingState.Flashing(currentProgress)
                _progress.value = currentProgress

                flashData(blockData, blockNum)

                // 5ms delay between blocks to prevent USB buffer overflow
                delay(5)
            }

            // 7. Verify (implicit via block checksums)
            _flashingState.value = FlashingState.Verifying
            delay(100)

            // 8. Complete and reboot
            _flashingState.value = FlashingState.Restarting
            flashEnd(reboot = true, isEsp32C3 = isEsp32C3)

            delay(1000)

            _flashingState.value = FlashingState.Complete
            _progress.value = 1f

        } finally {
            closePort()

            // Reconnect serial monitor if it was enabled
            if (wasSerialMonitorEnabled) {
                // Wait longer for device to reboot and USB to re-enumerate
                delay(2000)
                // Refresh port reference after device reboot
                refreshSelectedPort()
                connectSerialMonitor()
            }
        }
    }

    /**
     * Enter bootloader mode using DTR/RTS reset sequence
     */
    private suspend fun enterBootloaderMode(isEsp32C3: Boolean) = withContext(Dispatchers.IO) {
        val port = serialPort ?: return@withContext

        Log.d(TAG, "Entering bootloader mode (ESP32-C3: $isEsp32C3)")

        if (isEsp32C3) {
            // USB-JTAG-Serial reset sequence (exact esptool match)
            // Step 1: Idle state
            port.setRTS(false)
            port.setDTR(false)
            delay(100)

            // Step 2: Set IO0 (GPIO9 low for boot mode)
            port.setDTR(true)
            port.setRTS(false)
            delay(100)

            // Step 3: Reset sequence
            port.setRTS(true)   // Assert reset
            port.setDTR(false)  // Release IO0
            port.setRTS(true)   // Set RTS again (Windows quirk)
            delay(100)

            // Step 4: Chip out of reset
            port.setDTR(false)
            port.setRTS(false)
            delay(50)
        } else {
            // Classic reset for USB-UART bridges
            port.setDTR(false)
            port.setRTS(true)
            delay(100)

            port.setDTR(true)
            port.setRTS(false)
            delay(50)

            port.setDTR(false)
            delay(50)
        }

        Log.d(TAG, "Bootloader reset sequence complete")
    }

    /**
     * Perform hard reset to run flashed firmware
     */
    private suspend fun hardReset() = withContext(Dispatchers.IO) {
        val port = serialPort ?: return@withContext

        Log.d(TAG, "Performing hard reset")

        // Ensure DTR is low (normal boot)
        port.setDTR(false)
        delay(50)

        // Pulse RTS to trigger reset
        port.setRTS(true)
        delay(100)

        port.setRTS(false)
        delay(100)
    }

    private suspend fun syncWithRetry() {
        for (attempt in 1..syncRetries) {
            try {
                performSync()
                return
            } catch (e: Exception) {
                if (attempt == syncRetries) {
                    throw IOException("Failed to sync after $syncRetries attempts")
                }
                delay(50)
            }
        }
    }

    private suspend fun performSync() = withContext(Dispatchers.IO) {
        val syncCommand = Esp32Protocol.buildSyncCommand()
        val slipEncoded = SlipCodec.encode(syncCommand)

        Log.d(TAG, "Sending SYNC command")
        writeData(slipEncoded)

        val response = waitForResponse(Esp32Command.SYNC, 1000)
        if (!response.isSuccess) {
            throw IOException("Sync failed: status=${response.status}, error=${response.error}")
        }

        // Drain 7 extra sync responses
        Log.d(TAG, "Draining extra sync responses")
        repeat(7) {
            try {
                waitForResponse(Esp32Command.SYNC, 100)
            } catch (e: Exception) {
                // Ignore timeout
            }
        }

        flushInput()
    }

    private suspend fun changeBaudRate(rate: BaudRate) = withContext(Dispatchers.IO) {
        val command = Esp32Protocol.buildChangeBaudCommand(rate.value.toLong(), 115200)
        val encoded = SlipCodec.encode(command)
        writeData(encoded)

        delay(50)
        serialPort?.setParameters(
            rate.value,
            UsbSerialPort.DATABITS_8,
            UsbSerialPort.STOPBITS_1,
            UsbSerialPort.PARITY_NONE
        )
        delay(50)

        // Sync at new baud rate
        performSync()
    }

    private suspend fun spiAttach() = withContext(Dispatchers.IO) {
        val command = Esp32Protocol.buildSpiAttachCommand()
        val encoded = SlipCodec.encode(command)
        writeData(encoded)

        val response = waitForResponse(Esp32Command.SPI_ATTACH, 3000)
        if (!response.isSuccess) {
            throw IOException("SPI attach failed: status=${response.status}, error=${response.error}")
        }
    }

    private suspend fun disableWatchdogs() = withContext(Dispatchers.IO) {
        // 1. Disable RTC Watchdog
        writeReg(Esp32C3Registers.RTC_WDT_WPROTECT, Esp32C3Registers.RTC_WDT_WKEY)
        val wdtConfig = readReg(Esp32C3Registers.RTC_WDT_CONFIG0)
        val newWdtConfig = wdtConfig and Esp32C3Registers.WDT_EN_BIT.inv()
        writeReg(Esp32C3Registers.RTC_WDT_CONFIG0, newWdtConfig)
        writeReg(Esp32C3Registers.RTC_WDT_WPROTECT, 0)

        Log.d(TAG, "RTC WDT disabled (was 0x${wdtConfig.toString(16)}, now 0x${newWdtConfig.toString(16)})")

        // 2. Enable Super Watchdog auto-feed
        writeReg(Esp32C3Registers.SWD_WPROTECT, Esp32C3Registers.SWD_WKEY)
        val swdConfig = readReg(Esp32C3Registers.SWD_CONF)
        val newSwdConfig = swdConfig or Esp32C3Registers.SWD_AUTO_FEED_EN_BIT
        writeReg(Esp32C3Registers.SWD_CONF, newSwdConfig)
        writeReg(Esp32C3Registers.SWD_WPROTECT, 0)

        Log.d(TAG, "SWD auto-feed enabled (was 0x${swdConfig.toString(16)}, now 0x${newSwdConfig.toString(16)})")
    }

    private suspend fun readReg(address: Long): Long = withContext(Dispatchers.IO) {
        val command = Esp32Protocol.buildReadRegCommand(address)
        val encoded = SlipCodec.encode(command)
        writeData(encoded)

        val response = waitForResponse(Esp32Command.READ_REG, 1000)
        if (!response.isSuccess) {
            throw IOException("READ_REG failed at 0x${address.toString(16)}")
        }
        response.value
    }

    private suspend fun writeReg(address: Long, value: Long) = withContext(Dispatchers.IO) {
        val command = Esp32Protocol.buildWriteRegCommand(address, value)
        val encoded = SlipCodec.encode(command)
        writeData(encoded)

        val response = waitForResponse(Esp32Command.WRITE_REG, 1000)
        if (!response.isSuccess) {
            throw IOException("WRITE_REG failed at 0x${address.toString(16)}")
        }
    }

    private suspend fun flashBegin(size: Long, numBlocks: Long, blockSize: Long, offset: Long) = withContext(Dispatchers.IO) {
        val command = Esp32Protocol.buildFlashBeginCommand(size, numBlocks, blockSize, offset)
        val encoded = SlipCodec.encode(command)
        writeData(encoded)

        val response = waitForResponse(Esp32Command.FLASH_BEGIN, 30000) // 30s for erase
        Log.d(TAG, "FLASH_BEGIN response: status=${response.status}, error=${response.error}")
        if (!response.isSuccess) {
            throw IOException("Flash begin failed: status=${response.status}")
        }
    }

    private suspend fun flashData(block: ByteArray, sequenceNumber: Int) = withContext(Dispatchers.IO) {
        val command = Esp32Protocol.buildFlashDataCommand(block, sequenceNumber.toLong())
        val encoded = SlipCodec.encode(command)
        writeData(encoded)

        val response = waitForResponse(Esp32Command.FLASH_DATA, responseTimeout)
        if (!response.isSuccess) {
            throw IOException("Flash data failed at block $sequenceNumber: status=${response.status}")
        }
    }

    private suspend fun flashEnd(reboot: Boolean, isEsp32C3: Boolean) = withContext(Dispatchers.IO) {
        val command = Esp32Protocol.buildFlashEndCommand(reboot)
        val encoded = SlipCodec.encode(command)
        writeData(encoded)

        Log.d(TAG, "Sent FLASH_END command (reboot=$reboot)")

        try {
            val response = waitForResponse(Esp32Command.FLASH_END, 2000)
            Log.d(TAG, "FLASH_END response: status=${response.status}, error=${response.error}")
        } catch (e: Exception) {
            // Expected if rebooting
            if (!reboot) throw e
        }

        // For ESP32-C3, do hard reset
        if (reboot && isEsp32C3) {
            Log.d(TAG, "Performing hard reset for ESP32-C3")
            hardReset()
        }
    }

    private suspend fun waitForResponse(command: Esp32Command, timeoutMs: Long): Esp32Response = withContext(Dispatchers.IO) {
        slipDecoder.reset()
        val buffer = ByteArray(4096)
        val startTime = System.currentTimeMillis()

        while (System.currentTimeMillis() - startTime < timeoutMs) {
            if (isCancelled) throw CancellationException("Cancelled")

            val bytesRead = try {
                serialPort?.read(buffer, 100) ?: 0
            } catch (e: Exception) {
                0
            }

            if (bytesRead > 0) {
                val data = buffer.copyOf(bytesRead)
                val packets = slipDecoder.process(data)

                for (packet in packets) {
                    val response = Esp32Response.parse(packet)
                    if (response != null && response.command == command.opcode) {
                        return@withContext response
                    }
                }
            }
        }

        throw IOException("Timeout waiting for ${command.name} response")
    }

    private fun writeData(data: ByteArray) {
        serialPort?.write(data, 1000)
    }

    private fun flushInput() {
        val buffer = ByteArray(4096)
        try {
            while (true) {
                val read = serialPort?.read(buffer, 10) ?: 0
                if (read <= 0) break
            }
        } catch (e: Exception) {
            // Ignore
        }
    }

    private fun closePort() {
        try {
            serialPort?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing port", e)
        }
        serialPort = null
    }

    // MARK: - Serial Monitor

    private suspend fun connectSerialMonitor() {
        // Don't connect if flashing is active
        if (_flashingState.value.isActive) {
            startAutoReconnect()
            return
        }

        // Refresh port reference in case device was reconnected
        val port = try {
            refreshSelectedPort()
        } catch (e: Exception) {
            Log.e(TAG, "Error refreshing port", e)
            null
        }

        if (port == null) {
            appendSerialOutput("[No port selected]\n")
            startAutoReconnect()
            return
        }

        val hasPermission = try {
            permissionHandler.hasPermission(port)
        } catch (e: Exception) {
            Log.e(TAG, "Error checking permission", e)
            false
        }

        if (!hasPermission) {
            val gotPermission = try {
                permissionHandler.requestPermission(port)
            } catch (e: Exception) {
                Log.e(TAG, "Error requesting permission", e)
                false
            }
            if (!gotPermission) {
                appendSerialOutput("[USB permission denied]\n")
                startAutoReconnect()
                return
            }
        }

        val usbPort = try {
            usbDeviceManager.openPort(port, 115200)
        } catch (e: Exception) {
            Log.e(TAG, "Error opening port", e)
            null
        }

        if (usbPort == null) {
            appendSerialOutput("[Failed to open port - retrying...]\n")
            startAutoReconnect()
            return
        }

        // Use separate port for serial monitor
        serialMonitorPort = usbPort
        val portName = try { port.displayName } catch (e: Exception) { "device" }
        appendSerialOutput("[Connected to $portName]\n")
        _isSerialMonitorConnected.value = true
        stopAutoReconnect()

        serialMonitorJob = viewModelScope.launch(Dispatchers.IO) {
            val buffer = ByteArray(4096)
            val pendingText = StringBuilder()
            var lastUpdateTime = System.currentTimeMillis()

            try {
                while (isActive && _isSerialMonitorEnabled.value) {
                    val bytesRead = try {
                        usbPort.read(buffer, 100)
                    } catch (e: Exception) {
                        // USB disconnected or other error
                        Log.d(TAG, "Serial read error: ${e.message}")
                        -1
                    }

                    if (bytesRead < 0) {
                        // Error or disconnect - exit loop
                        break
                    }

                    if (bytesRead > 0) {
                        val text = try {
                            String(buffer, 0, bytesRead, Charsets.UTF_8)
                        } catch (e: Exception) {
                            ""
                        }
                        if (text.isNotEmpty()) {
                            pendingText.append(text)
                        }
                    }

                    // Batch updates every 100ms
                    val now = System.currentTimeMillis()
                    if (pendingText.isNotEmpty() && now - lastUpdateTime >= 100) {
                        val textToAdd = pendingText.toString()
                        pendingText.clear()
                        lastUpdateTime = now

                        try {
                            withContext(NonCancellable + Dispatchers.Main) {
                                appendSerialOutput(textToAdd)
                            }
                        } catch (e: Exception) {
                            // Ignore UI update errors
                        }
                    }
                }
            } catch (e: CancellationException) {
                // Normal cancellation, don't log as error
                Log.d(TAG, "Serial monitor cancelled")
            } catch (e: Exception) {
                Log.e(TAG, "Serial monitor error", e)
            }

            // Cleanup and notify disconnection
            try {
                withContext(NonCancellable + Dispatchers.Main) {
                    if (_isSerialMonitorEnabled.value) {
                        appendSerialOutput("[Disconnected]\n")
                    }
                    _isSerialMonitorConnected.value = false
                    // Start auto-reconnect if still enabled and not flashing
                    if (_isSerialMonitorEnabled.value && !_flashingState.value.isActive) {
                        startAutoReconnect()
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error updating UI on disconnect", e)
            }

            // Close port
            try {
                usbPort.close()
            } catch (e: Exception) {
                // Ignore close errors
            }
            serialMonitorPort = null
        }
    }

    private fun disconnectSerialMonitor() {
        stopAutoReconnect()
        serialMonitorJob?.cancel()
        serialMonitorJob = null
        try {
            serialMonitorPort?.close()
        } catch (e: Exception) {
            Log.d(TAG, "Error closing serial monitor port: ${e.message}")
        }
        serialMonitorPort = null
        _isSerialMonitorConnected.value = false
    }

    private suspend fun reconnectSerialMonitor() {
        disconnectSerialMonitor()
        if (_selectedPort.value != null && _isSerialMonitorEnabled.value) {
            delay(500)
            connectSerialMonitor()
        }
    }

    private fun startAutoReconnect() {
        if (autoReconnectJob?.isActive == true) return
        if (!_isSerialMonitorEnabled.value) return

        autoReconnectJob = viewModelScope.launch {
            try {
                while (isActive && _isSerialMonitorEnabled.value) {
                    delay(autoReconnectIntervalMs)

                    if (!_isSerialMonitorEnabled.value) break
                    if (_isSerialMonitorConnected.value) break
                    if (_flashingState.value.isActive) continue  // Wait if flashing

                    Log.d(TAG, "Auto-reconnecting serial monitor...")
                    try {
                        appendSerialOutput("[Attempting to reconnect...]\n")
                        connectSerialMonitor()
                    } catch (e: Exception) {
                        Log.e(TAG, "Auto-reconnect attempt failed", e)
                    }

                    if (_isSerialMonitorConnected.value) break
                }
            } catch (e: CancellationException) {
                // Normal cancellation
            } catch (e: Exception) {
                Log.e(TAG, "Auto-reconnect loop error", e)
            }
            autoReconnectJob = null
        }
    }

    private fun stopAutoReconnect() {
        autoReconnectJob?.cancel()
        autoReconnectJob = null
    }

    private fun appendSerialOutput(text: String) {
        val current = _serialMonitorOutput.value
        val newOutput = current + text
        // Limit buffer size
        _serialMonitorOutput.value = if (newOutput.length > 50000) {
            newOutput.takeLast(40000)
        } else {
            newOutput
        }
    }

    companion object {
        private const val TAG = "HomeViewModel"
    }
}
