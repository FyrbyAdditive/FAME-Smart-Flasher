// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef SERIALCONNECTION_H
#define SERIALCONNECTION_H

#include "models/SerialPort.h"
#include <QString>
#include <QByteArray>
#include <stdexcept>

/**
 * Errors that can occur during serial communication
 */
class SerialError : public std::runtime_error {
public:
    enum Type {
        CannotOpen,
        WriteFailed,
        ReadFailed,
        Timeout,
        InvalidConfiguration,
        NotConnected
    };

    SerialError(Type type, int errorCode = 0)
        : std::runtime_error(errorDescription(type, errorCode).toStdString())
        , m_type(type)
        , m_errorCode(errorCode)
    {}

    Type type() const { return m_type; }
    int errorCode() const { return m_errorCode; }

    static QString errorDescription(Type type, int errorCode);

private:
    Type m_type;
    int m_errorCode;
};

/**
 * POSIX-based serial port connection
 * Matches macOS SerialConnection.swift implementation exactly
 */
class SerialConnection {
public:
    SerialConnection();
    ~SerialConnection();

    // Non-copyable
    SerialConnection(const SerialConnection&) = delete;
    SerialConnection& operator=(const SerialConnection&) = delete;

    bool isConnected() const { return m_fd >= 0; }

    /**
     * Open a serial port
     * @param path Path to the serial port (e.g., /dev/ttyUSB0)
     */
    void open(const QString& path);

    /**
     * Close the serial port
     */
    void close();

    /**
     * Set the baud rate
     * @param rate New baud rate
     */
    void setBaudRate(BaudRate rate);

    /**
     * Write data to the serial port
     * @param data Data to write
     */
    void write(const QByteArray& data);

    /**
     * Read data from the serial port
     * @param timeout Read timeout in seconds
     * @return Data read from the port (empty on timeout)
     */
    QByteArray read(double timeout = 1.0);

    /**
     * Flush input and output buffers
     */
    void flush();

    /**
     * Set DTR (Data Terminal Ready) line state
     * Uses TIOCMBIS/TIOCMBIC like pyserial for better compatibility
     * @param value true to assert, false to deassert
     */
    void setDTR(bool value);

    /**
     * Set RTS (Request To Send) line state
     * Uses TIOCMBIS/TIOCMBIC like pyserial for better compatibility
     * @param value true to assert, false to deassert
     */
    void setRTS(bool value);

    /**
     * Set both DTR and RTS simultaneously
     * @param dtr DTR state
     * @param rts RTS state
     */
    void setDTRRTS(bool dtr, bool rts);

    /**
     * Enter bootloader mode using DTR/RTS reset sequence
     * @param isUSBJTAGSerial If true, uses USB-JTAG-Serial reset (ESP32-C3/S3 native USB).
     *                        If false, uses classic reset (USB-UART bridges).
     */
    void enterBootloaderMode(bool isUSBJTAGSerial = true);

    /**
     * Perform a hard reset to run the newly flashed firmware
     * For USB-JTAG-Serial devices, this triggers a proper chip reset
     * that will start the application (not bootloader mode)
     */
    void hardReset();

private:
    /**
     * USBJTAGSerialReset sequence - exact match of esptool implementation
     * For ESP32-C3/S3 with native USB-JTAG-Serial peripheral
     */
    void usbJtagSerialReset();

    /**
     * Classic reset sequence from esptool (ClassicReset)
     * For ESP32 with USB-UART bridge (CP2102, CH340, etc.)
     */
    void classicReset();

    /**
     * Sleep for milliseconds
     */
    static void sleepMs(int ms);

    int m_fd = -1;
    BaudRate m_currentBaudRate = BaudRate::Baud115200;
};

#endif // SERIALCONNECTION_H
