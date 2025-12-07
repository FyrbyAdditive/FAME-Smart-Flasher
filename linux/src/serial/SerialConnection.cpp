// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "SerialConnection.h"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/select.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>

#ifdef Q_OS_LINUX
#include <linux/serial.h>
#endif

QString SerialError::errorDescription(Type type, int errorCode)
{
    switch (type) {
    case CannotOpen:
        return QString("Cannot open port: %1").arg(strerror(errorCode));
    case WriteFailed:
        return QString("Write failed: %1").arg(strerror(errorCode));
    case ReadFailed:
        return QString("Read failed: %1").arg(strerror(errorCode));
    case Timeout:
        return "Operation timed out";
    case InvalidConfiguration:
        return "Invalid serial configuration";
    case NotConnected:
        return "Not connected";
    }
    return "Unknown error";
}

SerialConnection::SerialConnection()
{
}

SerialConnection::~SerialConnection()
{
    close();
}

void SerialConnection::open(const QString& path)
{
    // Open the port with O_NOCTTY to prevent terminal from taking control
    // and O_NONBLOCK to avoid blocking on modem lines
    // NOTE: pyserial keeps O_NONBLOCK active, so we do the same
    m_fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) {
        int err = errno;
        throw SerialError(SerialError::CannotOpen, err);
    }

    // Use flock() for exclusive access like pyserial does
    if (flock(m_fd, LOCK_EX | LOCK_NB) == -1) {
        int err = errno;
        ::close(m_fd);
        m_fd = -1;
        throw SerialError(SerialError::CannotOpen, err);
    }

    // Configure as raw terminal
    struct termios options;
    tcgetattr(m_fd, &options);
    cfmakeraw(&options);

    // Set initial baud rate (115200)
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    m_currentBaudRate = BaudRate::Baud115200;

    // 8N1 configuration
    options.c_cflag |= CS8;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;

    // Enable receiver, ignore modem control lines
    options.c_cflag |= (CREAD | CLOCAL);

    // Disable HUPCL - don't drop DTR on close
    // This is important for USB-JTAG-Serial devices
    options.c_cflag &= ~HUPCL;

    // Disable hardware flow control (CRTSCTS)
    options.c_cflag &= ~CRTSCTS;

    // Disable software flow control
    options.c_iflag &= ~(IXON | IXOFF | IXANY);

    // Set timeout (VMIN=0, VTIME=10 = 1 second timeout)
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;

    tcsetattr(m_fd, TCSANOW, &options);

    // DON'T touch DTR/RTS on port open - this can trigger a reset on ESP32-C3
    // The USB-JTAG-Serial peripheral monitors these lines and changing them
    // (even to deassert) can cause the chip to reset.
    // Only manipulate DTR/RTS explicitly when entering bootloader mode.

    // Flush any pending data
    tcflush(m_fd, TCIOFLUSH);
}

void SerialConnection::close()
{
    if (m_fd >= 0) {
        // Release the flock
        flock(m_fd, LOCK_UN);
        ::close(m_fd);
        m_fd = -1;
    }
}

void SerialConnection::setBaudRate(BaudRate rate)
{
    if (m_fd < 0) {
        throw SerialError(SerialError::NotConnected);
    }

    struct termios options;
    tcgetattr(m_fd, &options);

    // Set baud rate
    speed_t speedConst = baudRateConstant(rate);
    cfsetispeed(&options, speedConst);
    cfsetospeed(&options, speedConst);

    int result = tcsetattr(m_fd, TCSANOW, &options);
    if (result != 0) {
        throw SerialError(SerialError::InvalidConfiguration);
    }

    m_currentBaudRate = rate;
    tcflush(m_fd, TCIOFLUSH);
}

void SerialConnection::write(const QByteArray& data)
{
    if (m_fd < 0) {
        throw SerialError(SerialError::NotConnected);
    }

    int totalWritten = 0;
    int count = data.size();

    while (totalWritten < count) {
        ssize_t result = ::write(
            m_fd,
            data.constData() + totalWritten,
            count - totalWritten
        );

        if (result < 0) {
            // With O_NONBLOCK, EAGAIN means buffer is full, retry
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Brief delay then retry
                usleep(1000); // 1ms
                continue;
            }
            throw SerialError(SerialError::WriteFailed, errno);
        }

        totalWritten += result;
    }

    // Note: We don't call tcdrain() here anymore as it can cause issues
    // with USB-JTAG-Serial devices. The data is written successfully via
    // the write() loop, and responses confirm receipt.
}

QByteArray SerialConnection::read(double timeout)
{
    if (m_fd < 0) {
        throw SerialError(SerialError::NotConnected);
    }

    // Use select() for timeout handling
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(m_fd, &readSet);

    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout);
    tv.tv_usec = static_cast<long>((timeout - tv.tv_sec) * 1000000);

    int selectResult = select(m_fd + 1, &readSet, nullptr, nullptr, &tv);

    if (selectResult < 0) {
        throw SerialError(SerialError::ReadFailed, errno);
    }

    if (selectResult == 0) {
        // Timeout, return empty
        return QByteArray();
    }

    char buffer[4096];
    ssize_t bytesRead = ::read(m_fd, buffer, sizeof(buffer));

    if (bytesRead < 0) {
        // With O_NONBLOCK, EAGAIN means no data available
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return QByteArray();
        }
        throw SerialError(SerialError::ReadFailed, errno);
    }

    return QByteArray(buffer, bytesRead);
}

void SerialConnection::flush()
{
    if (m_fd >= 0) {
        tcflush(m_fd, TCIOFLUSH);
    }
}

void SerialConnection::setDTR(bool value)
{
    if (m_fd < 0) {
        throw SerialError(SerialError::NotConnected);
    }

    int bits = TIOCM_DTR;

    if (value) {
        // Use TIOCMBIS to SET the DTR bit
        ioctl(m_fd, TIOCMBIS, &bits);
    } else {
        // Use TIOCMBIC to CLEAR the DTR bit
        ioctl(m_fd, TIOCMBIC, &bits);
    }
}

void SerialConnection::setRTS(bool value)
{
    if (m_fd < 0) {
        throw SerialError(SerialError::NotConnected);
    }

    int bits = TIOCM_RTS;

    if (value) {
        // Use TIOCMBIS to SET the RTS bit
        ioctl(m_fd, TIOCMBIS, &bits);
    } else {
        // Use TIOCMBIC to CLEAR the RTS bit
        ioctl(m_fd, TIOCMBIC, &bits);
    }
}

void SerialConnection::setDTRRTS(bool dtr, bool rts)
{
    if (m_fd < 0) {
        throw SerialError(SerialError::NotConnected);
    }

    // Set DTR
    int dtrBits = TIOCM_DTR;
    if (dtr) {
        ioctl(m_fd, TIOCMBIS, &dtrBits);
    } else {
        ioctl(m_fd, TIOCMBIC, &dtrBits);
    }

    // Set RTS
    int rtsBits = TIOCM_RTS;
    if (rts) {
        ioctl(m_fd, TIOCMBIS, &rtsBits);
    } else {
        ioctl(m_fd, TIOCMBIC, &rtsBits);
    }
}

void SerialConnection::enterBootloaderMode(bool isUSBJTAGSerial)
{
    if (isUSBJTAGSerial) {
        // USB-JTAG-Serial reset (for ESP32-C3/S3 with native USB)
        // esptool uses ONLY this strategy for USB-JTAG-Serial devices
        usbJtagSerialReset();
    } else {
        // Classic reset for USB-UART bridges (CP2102, CH340, etc.)
        classicReset();
    }

    flush();
}

void SerialConnection::usbJtagSerialReset()
{
    // USBJTAGSerialReset sequence - exact match of esptool implementation
    // For ESP32-C3/S3 with native USB-JTAG-Serial peripheral
    // Source: esptool/reset.py USBJTAGSerialReset class
    //
    // The USB-JTAG-Serial peripheral on ESP32-C3 monitors DTR/RTS signals
    // in a specific way that's different from classic USB-UART bridges.
    //
    // Exact esptool sequence from reset.py:
    // self._setRTS(False)
    // self._setDTR(False)  # Idle
    // time.sleep(0.1)
    // self._setDTR(True)   # Set IO0
    // self._setRTS(False)
    // time.sleep(0.1)
    // self._setRTS(True)   # Reset
    // self._setDTR(False)
    // self._setRTS(True)   # RTS set as Windows only propagates DTR on RTS setting
    // time.sleep(0.1)
    // self._setDTR(False)
    // self._setRTS(False)  # Chip out of reset

    // Step 1: Idle state - both lines deasserted
    setRTS(false);
    setDTR(false);
    sleepMs(100);

    // Step 2: Set IO0 (GPIO9 low for boot mode)
    setDTR(true);
    setRTS(false);
    sleepMs(100);

    // Step 3: Reset sequence
    setRTS(true);   // Assert reset
    setDTR(false);  // Release IO0
    setRTS(true);   // Set RTS again (Windows driver quirk)
    sleepMs(100);

    // Step 4: Chip out of reset - both lines deasserted
    setDTR(false);
    setRTS(false);

    // Give the chip time to start the bootloader
    // The USB-JTAG-Serial peripheral needs time to reinitialize
    sleepMs(50);
}

void SerialConnection::classicReset()
{
    // Classic reset sequence from esptool (ClassicReset)
    // For ESP32 with USB-UART bridge (CP2102, CH340, etc.)
    // The bridge circuit typically has:
    // - DTR -> GPIO0 (inverted)
    // - RTS -> EN (inverted)

    // Step 1: Assert RTS (EN=LOW, chip in reset), deassert DTR (GPIO0=HIGH)
    setDTRRTS(false, true);
    sleepMs(100);

    // Step 2: Assert DTR (GPIO0=LOW for boot mode), deassert RTS (EN=HIGH, run)
    // Chip comes out of reset with GPIO0 low -> bootloader mode
    setDTRRTS(true, false);
    sleepMs(50);

    // Step 3: Deassert DTR (GPIO0=HIGH, release boot pin)
    setDTR(false);
    sleepMs(50);
}

void SerialConnection::hardReset()
{
    // For USB-JTAG-Serial, RTS controls the reset line (active high = reset asserted)
    // We pulse RTS without touching DTR (GPIO9) so the chip boots normally
    // DTR=false means GPIO9=HIGH which means normal boot (not bootloader mode)

    // Ensure DTR is low (GPIO9 high = normal boot mode)
    setDTR(false);
    sleepMs(50);

    // Pulse RTS to trigger reset
    setRTS(true);
    sleepMs(100);

    // Release reset - chip starts running
    setRTS(false);
    sleepMs(100);
}

void SerialConnection::sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
