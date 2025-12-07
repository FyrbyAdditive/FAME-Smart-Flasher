// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef SERIALPORT_H
#define SERIALPORT_H

#include <QString>
#include <termios.h>

/**
 * Represents an available serial port
 */
struct SerialPort {
    QString id;
    QString name;
    QString path;
    int vendorId = -1;
    int productId = -1;

    QString displayName() const {
        return name.isEmpty() ? path : name;
    }

    /**
     * Check if this is an ESP32-C3 USB-JTAG-Serial device
     * ESP32-C3 USB CDC VID/PID: 0x303A:0x1001
     */
    bool isESP32C3() const {
        return vendorId == 0x303A && productId == 0x1001;
    }

    bool operator==(const SerialPort& other) const {
        return path == other.path;
    }

    bool operator!=(const SerialPort& other) const {
        return !(*this == other);
    }
};

/**
 * Supported baud rates for flashing
 */
enum class BaudRate {
    Baud115200 = 115200,
    Baud230400 = 230400,
    Baud460800 = 460800,
    Baud921600 = 921600
};

inline int baudRateValue(BaudRate rate)
{
    return static_cast<int>(rate);
}

inline QString baudRateDisplayName(BaudRate rate)
{
    switch (rate) {
    case BaudRate::Baud115200: return "115200";
    case BaudRate::Baud230400: return "230400";
    case BaudRate::Baud460800: return "460800";
    case BaudRate::Baud921600: return "921600";
    }
    return "Unknown";
}

inline speed_t baudRateConstant(BaudRate rate)
{
    switch (rate) {
    case BaudRate::Baud115200: return B115200;
    case BaudRate::Baud230400: return B230400;
    case BaudRate::Baud460800: return B460800;
    case BaudRate::Baud921600: return B921600;
    }
    return B115200;
}

constexpr BaudRate ALL_BAUD_RATES[] = {
    BaudRate::Baud115200,
    BaudRate::Baud230400,
    BaudRate::Baud460800,
    BaudRate::Baud921600
};

#endif // SERIALPORT_H
