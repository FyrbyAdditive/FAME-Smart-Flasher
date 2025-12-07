// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef SERIALPORTMANAGER_H
#define SERIALPORTMANAGER_H

#include "models/SerialPort.h"
#include <QObject>
#include <QTimer>
#include <vector>

struct udev;
struct udev_monitor;

/**
 * Manages serial port enumeration and monitoring
 * Uses libudev for device enumeration on Linux
 */
class SerialPortManager : public QObject {
    Q_OBJECT

public:
    explicit SerialPortManager(QObject* parent = nullptr);
    ~SerialPortManager();

    const std::vector<SerialPort>& availablePorts() const { return m_availablePorts; }
    bool isScanning() const { return m_isScanning; }

    /**
     * Refresh the list of available serial ports
     */
    void refreshPorts();

    /**
     * Start observing for port connect/disconnect events
     */
    void startObserving();

    /**
     * Stop observing for port events
     */
    void stopObserving();

    /**
     * Check if a serial port is an ESP32-C3 USB-JTAG-Serial device
     * @param port The serial port to check
     * @return true if the device is ESP32-C3 USB-JTAG-Serial
     */
    static bool isESP32USBJtagSerial(const SerialPort& port);

signals:
    void portsChanged();

private slots:
    void checkForDeviceChanges();

private:
    /**
     * Enumerate all available serial ports using libudev
     */
    std::vector<SerialPort> enumeratePorts();

    /**
     * Get USB VID/PID for a device path using sysfs
     */
    void getUSBInfo(const QString& devicePath, int& vendorId, int& productId);

    std::vector<SerialPort> m_availablePorts;
    bool m_isScanning = false;

    // libudev handles
    udev* m_udev = nullptr;
    udev_monitor* m_monitor = nullptr;
    int m_monitorFd = -1;

    // Polling timer for device changes
    QTimer* m_pollTimer = nullptr;
};

#endif // SERIALPORTMANAGER_H
