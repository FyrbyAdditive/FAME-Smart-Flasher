import Foundation
import IOKit
import IOKit.serial
import IOKit.usb

// USB property keys
private let kUSBVendorID = "idVendor"
private let kUSBProductID = "idProduct"

// ESP32 USB identifiers
private let kESP32VendorID: Int = 0x303A
private let kESP32C3ProductID: Int = 0x1001  // USB-JTAG-Serial

/// Manages serial port enumeration and monitoring
@MainActor
@Observable
final class SerialPortManager {
    private(set) var availablePorts: [SerialPort] = []
    private(set) var isScanning = false

    // These properties need nonisolated(unsafe) to be accessible from deinit
    // They are only modified on MainActor during normal operation
    nonisolated(unsafe) private var notificationPort: IONotificationPortRef?
    nonisolated(unsafe) private var addedIterator: io_iterator_t = 0
    nonisolated(unsafe) private var removedIterator: io_iterator_t = 0

    init() {
        refreshPorts()
    }

    deinit {
        // Clean up IOKit resources
        if addedIterator != 0 {
            IOObjectRelease(addedIterator)
        }
        if removedIterator != 0 {
            IOObjectRelease(removedIterator)
        }
        if let port = notificationPort {
            IONotificationPortDestroy(port)
        }
    }

    /// Refresh the list of available serial ports
    func refreshPorts() {
        isScanning = true
        availablePorts = enumeratePorts()
        isScanning = false
    }

    /// Enumerate all available serial ports
    private func enumeratePorts() -> [SerialPort] {
        var ports: [SerialPort] = []

        let matchingDict = IOServiceMatching(kIOSerialBSDServiceValue) as NSMutableDictionary
        matchingDict[kIOSerialBSDTypeKey] = kIOSerialBSDRS232Type

        var iterator: io_iterator_t = 0
        let result = IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iterator)

        guard result == KERN_SUCCESS else { return [] }
        defer { IOObjectRelease(iterator) }

        var service: io_object_t = IOIteratorNext(iterator)
        while service != 0 {
            defer {
                IOObjectRelease(service)
                service = IOIteratorNext(iterator)
            }

            // Get the callout device path
            guard let pathProperty = IORegistryEntryCreateCFProperty(
                service,
                kIOCalloutDeviceKey as CFString,
                kCFAllocatorDefault,
                0
            )?.takeRetainedValue() as? String else { continue }

            // Filter to cu.* devices (not tty.*)
            guard pathProperty.contains("/dev/cu.") else { continue }

            // Get display name
            let name = pathProperty
                .replacingOccurrences(of: "/dev/cu.", with: "")
                .replacingOccurrences(of: "usbserial-", with: "USB ")
                .replacingOccurrences(of: "usbmodem", with: "USB Modem ")

            // Try to get USB VID/PID
            var vendorId: Int? = nil
            var productId: Int? = nil

            if let parent = getUSBParent(for: service) {
                vendorId = getIntProperty(for: parent, key: kUSBVendorID)
                productId = getIntProperty(for: parent, key: kUSBProductID)
                IOObjectRelease(parent)
            }

            let port = SerialPort(
                id: pathProperty,
                name: name,
                path: pathProperty,
                vendorId: vendorId,
                productId: productId
            )
            ports.append(port)
        }

        // Sort ports with ESP32 devices first
        return ports.sorted { lhs, rhs in
            if lhs.isESP32C3 != rhs.isESP32C3 {
                return lhs.isESP32C3
            }
            return lhs.name < rhs.name
        }
    }

    /// Get the USB parent device for a serial port
    private func getUSBParent(for service: io_object_t) -> io_object_t? {
        var parent: io_object_t = 0
        var current = service

        // Walk up the IOKit tree to find USB device
        while IORegistryEntryGetParentEntry(current, kIOServicePlane, &parent) == KERN_SUCCESS {
            if current != service {
                IOObjectRelease(current)
            }
            current = parent

            var className = [CChar](repeating: 0, count: 128)
            IOObjectGetClass(current, &className)
            let classString = String(cString: className)

            if classString.contains("USB") {
                return current
            }
        }

        return nil
    }

    /// Get an integer property from an IOKit registry entry
    private func getIntProperty(for entry: io_object_t, key: String) -> Int? {
        guard let property = IORegistryEntryCreateCFProperty(
            entry,
            key as CFString,
            kCFAllocatorDefault,
            0
        )?.takeRetainedValue() else { return nil }

        return (property as? NSNumber)?.intValue
    }

    /// Start observing for port connect/disconnect events
    func startObserving() {
        guard notificationPort == nil else { return }

        notificationPort = IONotificationPortCreate(kIOMainPortDefault)
        guard let notificationPort = notificationPort else { return }

        let runLoopSource = IONotificationPortGetRunLoopSource(notificationPort).takeUnretainedValue()
        CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, .defaultMode)

        // Set up matching notification for serial devices
        let matchingDict = IOServiceMatching(kIOSerialBSDServiceValue) as NSMutableDictionary
        matchingDict[kIOSerialBSDTypeKey] = kIOSerialBSDRS232Type

        // Create a copy for the second notification
        let matchingDictCopy = matchingDict.mutableCopy() as! NSMutableDictionary

        // Add notification for device additions
        let selfPtr = Unmanaged.passUnretained(self).toOpaque()

        IOServiceAddMatchingNotification(
            notificationPort,
            kIOPublishNotification,
            matchingDict,
            { refcon, iterator in
                guard let refcon = refcon else { return }
                let manager = Unmanaged<SerialPortManager>.fromOpaque(refcon).takeUnretainedValue()
                // Drain the iterator
                var entry: io_object_t = IOIteratorNext(iterator)
                while entry != 0 {
                    IOObjectRelease(entry)
                    entry = IOIteratorNext(iterator)
                }
                Task { @MainActor in
                    manager.refreshPorts()
                }
            },
            selfPtr,
            &addedIterator
        )

        // Drain the initial iterator
        var entry: io_object_t = IOIteratorNext(addedIterator)
        while entry != 0 {
            IOObjectRelease(entry)
            entry = IOIteratorNext(addedIterator)
        }

        // Add notification for device removals
        IOServiceAddMatchingNotification(
            notificationPort,
            kIOTerminatedNotification,
            matchingDictCopy,
            { refcon, iterator in
                guard let refcon = refcon else { return }
                let manager = Unmanaged<SerialPortManager>.fromOpaque(refcon).takeUnretainedValue()
                // Drain the iterator
                var entry: io_object_t = IOIteratorNext(iterator)
                while entry != 0 {
                    IOObjectRelease(entry)
                    entry = IOIteratorNext(iterator)
                }
                Task { @MainActor in
                    manager.refreshPorts()
                }
            },
            selfPtr,
            &removedIterator
        )

        // Drain the initial iterator
        entry = IOIteratorNext(removedIterator)
        while entry != 0 {
            IOObjectRelease(entry)
            entry = IOIteratorNext(removedIterator)
        }
    }

    /// Stop observing for port events
    func stopObserving() {
        if addedIterator != 0 {
            IOObjectRelease(addedIterator)
            addedIterator = 0
        }
        if removedIterator != 0 {
            IOObjectRelease(removedIterator)
            removedIterator = 0
        }
        if let port = notificationPort {
            IONotificationPortDestroy(port)
            notificationPort = nil
        }
    }

    /// Find the IOKit USB device for a serial port
    /// - Parameter portPath: The serial port path (e.g., /dev/cu.usbmodem01)
    /// - Returns: The USB device io_service_t or 0 if not found
    func findUSBDevice(forPort portPath: String) -> io_service_t {
        let matchingDict = IOServiceMatching(kIOSerialBSDServiceValue) as NSMutableDictionary
        matchingDict[kIOSerialBSDTypeKey] = kIOSerialBSDRS232Type

        var iterator: io_iterator_t = 0
        let result = IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iterator)

        guard result == KERN_SUCCESS else { return 0 }
        defer { IOObjectRelease(iterator) }

        var service: io_object_t = IOIteratorNext(iterator)
        while service != 0 {
            defer {
                IOObjectRelease(service)
                service = IOIteratorNext(iterator)
            }

            guard let pathProperty = IORegistryEntryCreateCFProperty(
                service,
                kIOCalloutDeviceKey as CFString,
                kCFAllocatorDefault,
                0
            )?.takeRetainedValue() as? String else { continue }

            if pathProperty == portPath {
                // Found the serial device, now find its USB parent
                if let usbDevice = getUSBParent(for: service) {
                    return usbDevice
                }
            }
        }

        return 0
    }

    /// Check if a serial port is an ESP32-C3 USB-JTAG-Serial device
    /// - Parameter port: The serial port to check
    /// - Returns: true if the device is ESP32-C3 USB-JTAG-Serial
    func isESP32USBJtagSerial(_ port: SerialPort) -> Bool {
        return port.vendorId == kESP32VendorID && port.productId == kESP32C3ProductID
    }
}
