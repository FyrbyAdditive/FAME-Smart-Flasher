// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "SerialPortManager.h"

#include <libudev.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

// ESP32 USB identifiers
static constexpr int kESP32VendorID = 0x303A;
static constexpr int kESP32C3ProductID = 0x1001;  // USB-JTAG-Serial

SerialPortManager::SerialPortManager(QObject* parent)
    : QObject(parent)
{
    m_udev = udev_new();

    // Initial port scan
    refreshPorts();

    // Set up polling timer for device changes
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &SerialPortManager::checkForDeviceChanges);
}

SerialPortManager::~SerialPortManager()
{
    stopObserving();

    if (m_monitor) {
        udev_monitor_unref(m_monitor);
        m_monitor = nullptr;
    }

    if (m_udev) {
        udev_unref(m_udev);
        m_udev = nullptr;
    }
}

void SerialPortManager::refreshPorts()
{
    m_isScanning = true;
    m_availablePorts = enumeratePorts();
    m_isScanning = false;
    emit portsChanged();
}

std::vector<SerialPort> SerialPortManager::enumeratePorts()
{
    std::vector<SerialPort> ports;

    if (!m_udev) {
        return ports;
    }

    // Enumerate tty devices
    struct udev_enumerate* enumerate = udev_enumerate_new(m_udev);
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry* entry;

    udev_list_entry_foreach(entry, devices) {
        const char* path = udev_list_entry_get_name(entry);
        struct udev_device* device = udev_device_new_from_syspath(m_udev, path);

        if (!device) {
            continue;
        }

        const char* devNode = udev_device_get_devnode(device);
        if (!devNode) {
            udev_device_unref(device);
            continue;
        }

        QString devicePath = QString::fromUtf8(devNode);

        // Only include /dev/ttyUSB* and /dev/ttyACM* devices
        if (!devicePath.startsWith("/dev/ttyUSB") && !devicePath.startsWith("/dev/ttyACM")) {
            udev_device_unref(device);
            continue;
        }

        // Get the USB parent device for VID/PID
        struct udev_device* usbDevice = udev_device_get_parent_with_subsystem_devtype(
            device, "usb", "usb_device"
        );

        int vendorId = -1;
        int productId = -1;
        QString deviceName;

        if (usbDevice) {
            const char* vidStr = udev_device_get_sysattr_value(usbDevice, "idVendor");
            const char* pidStr = udev_device_get_sysattr_value(usbDevice, "idProduct");
            const char* product = udev_device_get_sysattr_value(usbDevice, "product");
            const char* manufacturer = udev_device_get_sysattr_value(usbDevice, "manufacturer");

            if (vidStr) {
                vendorId = QString::fromUtf8(vidStr).toInt(nullptr, 16);
            }
            if (pidStr) {
                productId = QString::fromUtf8(pidStr).toInt(nullptr, 16);
            }

            // Build device name
            if (manufacturer && product) {
                deviceName = QString("%1 %2").arg(manufacturer).arg(product);
            } else if (product) {
                deviceName = QString::fromUtf8(product);
            } else if (manufacturer) {
                deviceName = QString::fromUtf8(manufacturer);
            }
        }

        // Fallback name from device path
        if (deviceName.isEmpty()) {
            deviceName = QFileInfo(devicePath).fileName();
        }

        SerialPort port;
        port.id = devicePath;
        port.name = deviceName;
        port.path = devicePath;
        port.vendorId = vendorId;
        port.productId = productId;

        ports.push_back(port);

        udev_device_unref(device);
    }

    udev_enumerate_unref(enumerate);

    // Sort ports with ESP32 devices first
    std::sort(ports.begin(), ports.end(), [](const SerialPort& a, const SerialPort& b) {
        if (a.isESP32C3() != b.isESP32C3()) {
            return a.isESP32C3();
        }
        return a.name < b.name;
    });

    return ports;
}

void SerialPortManager::getUSBInfo(const QString& devicePath, int& vendorId, int& productId)
{
    vendorId = -1;
    productId = -1;

    // Find the USB device in sysfs
    // /sys/class/tty/ttyUSB0/device/../../../idVendor
    QString ttyName = QFileInfo(devicePath).fileName();
    QString sysPath = QString("/sys/class/tty/%1/device").arg(ttyName);

    QDir sysDir(sysPath);
    if (!sysDir.exists()) {
        return;
    }

    // Walk up to find idVendor and idProduct
    QString currentPath = sysDir.absolutePath();
    for (int i = 0; i < 5; ++i) {
        QFile vidFile(currentPath + "/idVendor");
        QFile pidFile(currentPath + "/idProduct");

        if (vidFile.exists() && pidFile.exists()) {
            if (vidFile.open(QIODevice::ReadOnly)) {
                QString vid = vidFile.readAll().trimmed();
                vendorId = vid.toInt(nullptr, 16);
                vidFile.close();
            }
            if (pidFile.open(QIODevice::ReadOnly)) {
                QString pid = pidFile.readAll().trimmed();
                productId = pid.toInt(nullptr, 16);
                pidFile.close();
            }
            break;
        }

        // Go up one directory
        currentPath = QFileInfo(currentPath).dir().absolutePath();
    }
}

void SerialPortManager::startObserving()
{
    if (!m_udev) {
        return;
    }

    // Create udev monitor
    if (!m_monitor) {
        m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
        if (m_monitor) {
            udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "tty", nullptr);
            udev_monitor_enable_receiving(m_monitor);
            m_monitorFd = udev_monitor_get_fd(m_monitor);
        }
    }

    // Start polling timer (500ms interval)
    if (!m_pollTimer->isActive()) {
        m_pollTimer->start(500);
    }
}

void SerialPortManager::stopObserving()
{
    if (m_pollTimer && m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }
}

void SerialPortManager::checkForDeviceChanges()
{
    if (!m_monitor || m_monitorFd < 0) {
        // Fallback: just refresh periodically
        refreshPorts();
        return;
    }

    // Check if there are pending events
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(m_monitorFd, &readSet);

    struct timeval tv = {0, 0}; // Non-blocking
    int result = select(m_monitorFd + 1, &readSet, nullptr, nullptr, &tv);

    if (result > 0 && FD_ISSET(m_monitorFd, &readSet)) {
        // Process all pending events
        while (true) {
            struct udev_device* device = udev_monitor_receive_device(m_monitor);
            if (!device) {
                break;
            }

            const char* action = udev_device_get_action(device);
            if (action) {
                QString actionStr = QString::fromUtf8(action);
                if (actionStr == "add" || actionStr == "remove") {
                    // Device added or removed, refresh ports
                    udev_device_unref(device);
                    refreshPorts();
                    return;
                }
            }

            udev_device_unref(device);
        }
    }
}

bool SerialPortManager::isESP32USBJtagSerial(const SerialPort& port)
{
    return port.vendorId == kESP32VendorID && port.productId == kESP32C3ProductID;
}
