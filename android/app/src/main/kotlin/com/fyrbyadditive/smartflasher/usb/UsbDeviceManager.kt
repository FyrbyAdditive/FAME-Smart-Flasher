package com.fyrbyadditive.smartflasher.usb

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import com.hoho.android.usbserial.driver.CdcAcmSerialDriver
import com.hoho.android.usbserial.driver.Ch34xSerialDriver
import com.hoho.android.usbserial.driver.Cp21xxSerialDriver
import com.hoho.android.usbserial.driver.FtdiSerialDriver
import com.hoho.android.usbserial.driver.ProlificSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Serial port information
 */
data class SerialPortInfo(
    val device: UsbDevice,
    val driver: UsbSerialDriver,
    val portNumber: Int = 0
) {
    val name: String
        get() = device.deviceName

    val displayName: String
        get() {
            val manufacturer = device.manufacturerName ?: ""
            val product = device.productName ?: ""
            return when {
                product.isNotBlank() -> product
                manufacturer.isNotBlank() -> "$manufacturer (${device.deviceId})"
                else -> "USB Serial (${device.deviceId})"
            }
        }

    val isEsp32C3: Boolean
        get() = device.vendorId == ESP32_C3_VID && device.productId == ESP32_C3_PID

    val vendorId: Int
        get() = device.vendorId

    val productId: Int
        get() = device.productId

    companion object {
        // ESP32-C3 USB-JTAG-Serial (native USB CDC)
        const val ESP32_C3_VID = 0x303A
        const val ESP32_C3_PID = 0x1001
    }
}

/**
 * Manages USB serial device enumeration and monitoring
 */
class UsbDeviceManager(private val context: Context) {
    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager

    private val _availablePorts = MutableStateFlow<List<SerialPortInfo>>(emptyList())
    val availablePorts: StateFlow<List<SerialPortInfo>> = _availablePorts.asStateFlow()

    private val prober: UsbSerialProber by lazy {
        // Create a custom prober that includes all drivers we support
        val probeTable = UsbSerialProber.getDefaultProbeTable()

        // Add ESP32-C3 native USB (CDC ACM)
        probeTable.addProduct(SerialPortInfo.ESP32_C3_VID, SerialPortInfo.ESP32_C3_PID, CdcAcmSerialDriver::class.java)

        UsbSerialProber(probeTable)
    }

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED,
                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    Log.d(TAG, "USB device ${intent.action}")
                    refreshPorts()
                }
            }
        }
    }

    private var isObserving = false

    /**
     * Start observing USB device changes
     */
    fun startObserving() {
        if (isObserving) return
        isObserving = true

        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(usbReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            context.registerReceiver(usbReceiver, filter)
        }

        refreshPorts()
    }

    /**
     * Stop observing USB device changes
     */
    fun stopObserving() {
        if (!isObserving) return
        isObserving = false

        try {
            context.unregisterReceiver(usbReceiver)
        } catch (e: IllegalArgumentException) {
            // Receiver not registered
        }
    }

    /**
     * Refresh the list of available serial ports
     */
    fun refreshPorts() {
        val drivers = prober.findAllDrivers(usbManager)
        val ports = mutableListOf<SerialPortInfo>()

        for (driver in drivers) {
            for (portIndex in driver.ports.indices) {
                ports.add(SerialPortInfo(driver.device, driver, portIndex))
            }
        }

        // Sort ESP32-C3 devices first
        val sorted = ports.sortedWith(compareByDescending<SerialPortInfo> { it.isEsp32C3 }
            .thenBy { it.displayName })

        Log.d(TAG, "Found ${sorted.size} serial port(s)")
        _availablePorts.value = sorted
    }

    /**
     * Check if we have permission to access a device
     */
    fun hasPermission(portInfo: SerialPortInfo): Boolean {
        return try {
            usbManager.hasPermission(portInfo.device)
        } catch (e: Exception) {
            Log.e(TAG, "Error checking permission", e)
            false
        }
    }

    /**
     * Open a serial port connection
     * @param portInfo The port to open
     * @param baudRate Baud rate
     * @return The opened UsbSerialPort, or null if failed
     */
    fun openPort(portInfo: SerialPortInfo, baudRate: Int = 115200): UsbSerialPort? {
        try {
            if (!hasPermission(portInfo)) {
                Log.e(TAG, "No permission to access device")
                return null
            }

            val connection = usbManager.openDevice(portInfo.device)
            if (connection == null) {
                Log.e(TAG, "Failed to open device connection")
                return null
            }

            val port = portInfo.driver.ports.getOrNull(portInfo.portNumber)
            if (port == null) {
                Log.e(TAG, "Invalid port number: ${portInfo.portNumber}")
                connection.close()
                return null
            }

            try {
                port.open(connection)
                port.setParameters(
                    baudRate,
                    UsbSerialPort.DATABITS_8,
                    UsbSerialPort.STOPBITS_1,
                    UsbSerialPort.PARITY_NONE
                )
                val portName = try { portInfo.displayName } catch (e: Exception) { "unknown" }
                Log.d(TAG, "Opened port $portName at $baudRate baud")
                return port
            } catch (e: Exception) {
                Log.e(TAG, "Failed to open port", e)
                try {
                    port.close()
                } catch (ignored: Exception) {}
                try {
                    connection.close()
                } catch (ignored: Exception) {}
                return null
            }
        } catch (e: Exception) {
            Log.e(TAG, "Unexpected error opening port", e)
            return null
        }
    }

    companion object {
        private const val TAG = "UsbDeviceManager"
    }
}
