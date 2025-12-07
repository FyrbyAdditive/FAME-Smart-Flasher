package com.fyrbyadditive.smartflasher.usb

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.util.Log
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume

/**
 * Handles USB permission requests
 */
class UsbPermissionHandler(private val context: Context) {
    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager

    /**
     * Request permission to access a USB device
     * @param portInfo The port to request permission for
     * @return true if permission was granted, false otherwise
     */
    suspend fun requestPermission(portInfo: SerialPortInfo): Boolean {
        try {
            if (usbManager.hasPermission(portInfo.device)) {
                return true
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error checking permission", e)
            return false
        }

        return try {
            suspendCancellableCoroutine { continuation ->
                val receiver = object : BroadcastReceiver() {
                    override fun onReceive(ctx: Context, intent: Intent) {
                        if (intent.action == ACTION_USB_PERMISSION) {
                            val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                            } else {
                                @Suppress("DEPRECATION")
                                intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                            }

                            val targetDeviceId = try { portInfo.device.deviceId } catch (e: Exception) { -1 }
                            if (device?.deviceId == targetDeviceId) {
                                try {
                                    context.unregisterReceiver(this)
                                } catch (e: IllegalArgumentException) {
                                    // Already unregistered
                                }

                                val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                                val portName = try { portInfo.displayName } catch (e: Exception) { "unknown" }
                                Log.d(TAG, "Permission ${if (granted) "granted" else "denied"} for $portName")

                                if (continuation.isActive) {
                                    continuation.resume(granted)
                                }
                            }
                        }
                    }
                }

                val filter = IntentFilter(ACTION_USB_PERMISSION)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
                } else {
                    context.registerReceiver(receiver, filter)
                }

                val permissionIntent = PendingIntent.getBroadcast(
                    context,
                    0,
                    Intent(ACTION_USB_PERMISSION),
                    PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
                )

                val portName = try { portInfo.displayName } catch (e: Exception) { "unknown" }
                Log.d(TAG, "Requesting permission for $portName")
                usbManager.requestPermission(portInfo.device, permissionIntent)

                continuation.invokeOnCancellation {
                    try {
                        context.unregisterReceiver(receiver)
                    } catch (e: IllegalArgumentException) {
                        // Already unregistered
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error requesting permission", e)
            false
        }
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

    companion object {
        private const val TAG = "UsbPermissionHandler"
        const val ACTION_USB_PERMISSION = "com.fyrbyadditive.smartflasher.USB_PERMISSION"
    }
}
