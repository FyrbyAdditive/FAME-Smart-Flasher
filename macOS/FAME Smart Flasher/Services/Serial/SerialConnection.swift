import Foundation
import Darwin

// TIOCMBIS and TIOCMBIC are not exposed in Swift, but we need them
// for proper DTR/RTS control (pyserial uses these instead of TIOCMSET)
// These set/clear individual modem bits without affecting others
private let TIOCMBIS: UInt = 0x8004746C  // _IOW('t', 108, int) - bis (set) modem bits
private let TIOCMBIC: UInt = 0x8004746B  // _IOW('t', 107, int) - bic (clear) modem bits

/// Errors that can occur during serial communication
enum SerialError: Error, LocalizedError {
    case cannotOpen(Int32)
    case writeFailed(Int32)
    case readFailed(Int32)
    case timeout
    case invalidConfiguration
    case notConnected

    var errorDescription: String? {
        switch self {
        case .cannotOpen(let errno):
            return "Cannot open port: \(String(cString: strerror(errno)))"
        case .writeFailed(let errno):
            return "Write failed: \(String(cString: strerror(errno)))"
        case .readFailed(let errno):
            return "Read failed: \(String(cString: strerror(errno)))"
        case .timeout:
            return "Operation timed out"
        case .invalidConfiguration:
            return "Invalid serial configuration"
        case .notConnected:
            return "Not connected"
        }
    }
}

/// POSIX-based serial port connection
actor SerialConnection {
    private var fileDescriptor: Int32 = -1
    private var currentBaudRate: BaudRate = .baud115200

    var isConnected: Bool {
        fileDescriptor >= 0
    }

    /// Open a serial port
    /// - Parameter path: Path to the serial port (e.g., /dev/cu.usbserial-0001)
    func open(path: String) throws {
        #if DEBUG
        print("[SerialConnection] Opening port: \(path)")
        #endif

        // Open the port with O_NOCTTY to prevent terminal from taking control
        // and O_NONBLOCK to avoid blocking on modem lines
        // NOTE: pyserial keeps O_NONBLOCK active, so we do the same
        fileDescriptor = Darwin.open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard fileDescriptor >= 0 else {
            let err = errno
            #if DEBUG
            print("[SerialConnection] Failed to open port: \(String(cString: strerror(err)))")
            #endif
            throw SerialError.cannotOpen(err)
        }

        // Use flock() for exclusive access like pyserial does
        // (instead of TIOCEXCL which is terminal-specific)
        if flock(fileDescriptor, LOCK_EX | LOCK_NB) == -1 {
            let err = errno
            #if DEBUG
            print("[SerialConnection] Failed to get exclusive lock: \(String(cString: strerror(err)))")
            #endif
            Darwin.close(fileDescriptor)
            fileDescriptor = -1
            throw SerialError.cannotOpen(err)
        }

        // Configure as raw terminal
        var options = termios()
        tcgetattr(fileDescriptor, &options)
        cfmakeraw(&options)

        // Set initial baud rate (115200)
        cfsetispeed(&options, speed_t(B115200))
        cfsetospeed(&options, speed_t(B115200))
        currentBaudRate = .baud115200

        // 8N1 configuration
        options.c_cflag |= UInt(CS8)
        options.c_cflag &= ~UInt(PARENB)
        options.c_cflag &= ~UInt(CSTOPB)

        // Enable receiver, ignore modem control lines
        options.c_cflag |= UInt(CREAD | CLOCAL)

        // Disable HUPCL - don't drop DTR on close
        // This is important for USB-JTAG-Serial devices
        options.c_cflag &= ~UInt(HUPCL)

        // Disable hardware flow control (CRTSCTS)
        options.c_cflag &= ~UInt(CRTSCTS)

        // Disable software flow control
        options.c_iflag &= ~UInt(IXON | IXOFF | IXANY)

        // Set timeout (VMIN=0, VTIME=10 = 1 second timeout)
        options.c_cc.16 = 0   // VMIN - minimum characters
        options.c_cc.17 = 10  // VTIME - timeout in deciseconds

        tcsetattr(fileDescriptor, TCSANOW, &options)

        // DON'T touch DTR/RTS on port open - this can trigger a reset on ESP32-C3
        // The USB-JTAG-Serial peripheral monitors these lines and changing them
        // (even to deassert) can cause the chip to reset.
        // Only manipulate DTR/RTS explicitly when entering bootloader mode.

        // Flush any pending data
        tcflush(fileDescriptor, TCIOFLUSH)

        #if DEBUG
        print("[SerialConnection] Port opened successfully")
        #endif
    }

    /// Close the serial port
    func close() {
        guard fileDescriptor >= 0 else { return }
        // Release the flock
        _ = flock(fileDescriptor, LOCK_UN)
        Darwin.close(fileDescriptor)
        fileDescriptor = -1
    }

    /// Set the baud rate
    /// - Parameter rate: New baud rate
    func setBaudRate(_ rate: BaudRate) throws {
        guard fileDescriptor >= 0 else {
            throw SerialError.notConnected
        }

        var options = termios()
        tcgetattr(fileDescriptor, &options)

        // Set baud rate
        cfsetispeed(&options, rate.speedConstant)
        cfsetospeed(&options, rate.speedConstant)

        let result = tcsetattr(fileDescriptor, TCSANOW, &options)
        guard result == 0 else {
            throw SerialError.invalidConfiguration
        }

        currentBaudRate = rate
        tcflush(fileDescriptor, TCIOFLUSH)
    }

    /// Write data to the serial port
    /// - Parameter data: Data to write
    func write(_ data: Data) throws {
        guard fileDescriptor >= 0 else {
            throw SerialError.notConnected
        }

        var totalWritten = 0
        let count = data.count

        while totalWritten < count {
            let result = data.withUnsafeBytes { buffer in
                Darwin.write(
                    fileDescriptor,
                    buffer.baseAddress! + totalWritten,
                    count - totalWritten
                )
            }

            if result < 0 {
                // With O_NONBLOCK, EAGAIN means buffer is full, retry
                if errno == EAGAIN || errno == EWOULDBLOCK {
                    // Brief delay then retry
                    usleep(1000) // 1ms
                    continue
                }
                throw SerialError.writeFailed(errno)
            }

            totalWritten += result
        }

        // Note: We don't call tcdrain() here anymore as it can cause issues
        // with USB-JTAG-Serial devices. The data is written successfully via
        // the write() loop, and responses confirm receipt.
    }

    /// Read data from the serial port
    /// - Parameter timeout: Read timeout in seconds
    /// - Returns: Data read from the port
    func read(timeout: TimeInterval = 1.0) throws -> Data {
        guard fileDescriptor >= 0 else {
            throw SerialError.notConnected
        }

        // Use select() for timeout handling
        var readSet = fd_set()
        __darwin_fd_zero(&readSet)
        __darwin_fd_set(fileDescriptor, &readSet)

        var tv = timeval(
            tv_sec: Int(timeout),
            tv_usec: Int32((timeout.truncatingRemainder(dividingBy: 1)) * 1_000_000)
        )

        let selectResult = Darwin.select(fileDescriptor + 1, &readSet, nil, nil, &tv)

        guard selectResult > 0 else {
            if selectResult == 0 {
                return Data() // Timeout, return empty
            }
            throw SerialError.readFailed(errno)
        }

        var buffer = [UInt8](repeating: 0, count: 4096)
        let bytesRead = Darwin.read(fileDescriptor, &buffer, buffer.count)

        if bytesRead < 0 {
            // With O_NONBLOCK, EAGAIN means no data available (shouldn't happen after select)
            if errno == EAGAIN || errno == EWOULDBLOCK {
                return Data()
            }
            throw SerialError.readFailed(errno)
        }

        return Data(buffer[0..<bytesRead])
    }

    /// Read data with a deadline
    /// - Parameters:
    ///   - deadline: Absolute deadline
    ///   - minBytes: Minimum bytes to read before returning
    /// - Returns: Data read from the port
    func read(until deadline: Date, minBytes: Int = 1) throws -> Data {
        var result = Data()

        while Date() < deadline {
            let remaining = deadline.timeIntervalSinceNow
            guard remaining > 0 else { break }

            let data = try read(timeout: min(remaining, 0.1))
            result.append(data)

            if result.count >= minBytes {
                break
            }
        }

        return result
    }

    /// Flush input and output buffers
    func flush() {
        guard fileDescriptor >= 0 else { return }
        tcflush(fileDescriptor, TCIOFLUSH)
    }

    /// Set DTR (Data Terminal Ready) line state
    /// Uses TIOCMBIS/TIOCMBIC like pyserial for better compatibility
    /// - Parameter value: true to assert, false to deassert
    func setDTR(_ value: Bool) throws {
        guard fileDescriptor >= 0 else {
            throw SerialError.notConnected
        }

        var bits = TIOCM_DTR

        if value {
            // Use TIOCMBIS to SET the DTR bit
            let result = ioctl(fileDescriptor, TIOCMBIS, &bits)
            #if DEBUG
            print("[SerialConnection] setDTR(true) via TIOCMBIS, result: \(result)")
            #endif
        } else {
            // Use TIOCMBIC to CLEAR the DTR bit
            let result = ioctl(fileDescriptor, TIOCMBIC, &bits)
            #if DEBUG
            print("[SerialConnection] setDTR(false) via TIOCMBIC, result: \(result)")
            #endif
        }
    }

    /// Set RTS (Request To Send) line state
    /// Uses TIOCMBIS/TIOCMBIC like pyserial for better compatibility
    /// - Parameter value: true to assert, false to deassert
    func setRTS(_ value: Bool) throws {
        guard fileDescriptor >= 0 else {
            throw SerialError.notConnected
        }

        var bits = TIOCM_RTS

        if value {
            // Use TIOCMBIS to SET the RTS bit
            let result = ioctl(fileDescriptor, TIOCMBIS, &bits)
            #if DEBUG
            print("[SerialConnection] setRTS(true) via TIOCMBIS, result: \(result)")
            #endif
        } else {
            // Use TIOCMBIC to CLEAR the RTS bit
            let result = ioctl(fileDescriptor, TIOCMBIC, &bits)
            #if DEBUG
            print("[SerialConnection] setRTS(false) via TIOCMBIC, result: \(result)")
            #endif
        }
    }

    /// Set both DTR and RTS simultaneously
    /// - Parameters:
    ///   - dtr: DTR state
    ///   - rts: RTS state
    func setDTRRTS(dtr: Bool, rts: Bool) throws {
        guard fileDescriptor >= 0 else {
            throw SerialError.notConnected
        }

        // Set DTR
        var dtrBits = TIOCM_DTR
        if dtr {
            _ = ioctl(fileDescriptor, TIOCMBIS, &dtrBits)
        } else {
            _ = ioctl(fileDescriptor, TIOCMBIC, &dtrBits)
        }

        // Set RTS
        var rtsBits = TIOCM_RTS
        if rts {
            _ = ioctl(fileDescriptor, TIOCMBIS, &rtsBits)
        } else {
            _ = ioctl(fileDescriptor, TIOCMBIC, &rtsBits)
        }

        #if DEBUG
        print("[SerialConnection] setDTRRTS(dtr: \(dtr), rts: \(rts))")
        #endif
    }

    /// Enter bootloader mode using DTR/RTS reset sequence
    /// - Parameter isUSBJTAGSerial: If true, uses USB-JTAG-Serial reset (ESP32-C3/S3 native USB).
    ///                               If false, uses classic reset (USB-UART bridges).
    func enterBootloaderMode(isUSBJTAGSerial: Bool = true) async throws {
        #if DEBUG
        print("[SerialConnection] Entering bootloader mode (USB-JTAG-Serial: \(isUSBJTAGSerial))...")
        #endif

        if isUSBJTAGSerial {
            // USB-JTAG-Serial reset (for ESP32-C3/S3 with native USB)
            // esptool uses ONLY this strategy for USB-JTAG-Serial devices
            try await usbJtagSerialReset()
        } else {
            // Classic reset for USB-UART bridges (CP2102, CH340, etc.)
            try await classicReset()
        }

        flush()
    }

    /// USBJTAGSerialReset sequence - exact match of esptool implementation
    /// For ESP32-C3/S3 with native USB-JTAG-Serial peripheral
    /// Source: esptool/reset.py USBJTAGSerialReset class
    ///
    /// The USB-JTAG-Serial peripheral on ESP32-C3 monitors DTR/RTS signals
    /// in a specific way that's different from classic USB-UART bridges.
    ///
    /// Exact esptool sequence from reset.py:
    /// self._setRTS(False)
    /// self._setDTR(False)  # Idle
    /// time.sleep(0.1)
    /// self._setDTR(True)   # Set IO0
    /// self._setRTS(False)
    /// time.sleep(0.1)
    /// self._setRTS(True)   # Reset
    /// self._setDTR(False)
    /// self._setRTS(True)   # RTS set as Windows only propagates DTR on RTS setting
    /// time.sleep(0.1)
    /// self._setDTR(False)
    /// self._setRTS(False)  # Chip out of reset
    private func usbJtagSerialReset() async throws {
        #if DEBUG
        print("[SerialConnection] USB-JTAG-Serial reset sequence starting (esptool-exact)")
        #endif

        // Step 1: Idle state - both lines deasserted
        try setRTS(false)
        try setDTR(false)
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms

        // Step 2: Set IO0 (GPIO9 low for boot mode)
        try setDTR(true)
        try setRTS(false)
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms

        // Step 3: Reset sequence
        try setRTS(true)   // Assert reset
        try setDTR(false)  // Release IO0
        try setRTS(true)   // Set RTS again (Windows driver quirk)
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms

        // Step 4: Chip out of reset - both lines deasserted
        try setDTR(false)
        try setRTS(false)

        // Give the chip time to start the bootloader
        // The USB-JTAG-Serial peripheral needs time to reinitialize
        try await Task.sleep(nanoseconds: 50_000_000) // 50ms

        #if DEBUG
        print("[SerialConnection] USB-JTAG-Serial reset sequence complete")
        #endif
    }

    /// Classic reset sequence from esptool (ClassicReset)
    /// For ESP32 with USB-UART bridge (CP2102, CH340, etc.)
    /// The bridge circuit typically has:
    /// - DTR -> GPIO0 (inverted)
    /// - RTS -> EN (inverted)
    private func classicReset() async throws {
        #if DEBUG
        print("[SerialConnection] Classic reset sequence starting")
        #endif

        // Step 1: Assert RTS (EN=LOW, chip in reset), deassert DTR (GPIO0=HIGH)
        try setDTRRTS(dtr: false, rts: true)
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms

        // Step 2: Assert DTR (GPIO0=LOW for boot mode), deassert RTS (EN=HIGH, run)
        // Chip comes out of reset with GPIO0 low -> bootloader mode
        try setDTRRTS(dtr: true, rts: false)
        try await Task.sleep(nanoseconds: 50_000_000) // 50ms

        // Step 3: Deassert DTR (GPIO0=HIGH, release boot pin)
        try setDTR(false)
        try await Task.sleep(nanoseconds: 50_000_000) // 50ms

        #if DEBUG
        print("[SerialConnection] Classic reset sequence complete")
        #endif
    }

    /// Reset the device
    func resetDevice() async throws {
        try setDTR(true)
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms
        try setDTR(false)
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms
    }

    /// Perform a hard reset to run the newly flashed firmware
    /// For USB-JTAG-Serial devices, this triggers a proper chip reset
    /// that will start the application (not bootloader mode)
    func hardReset() async throws {
        #if DEBUG
        print("[SerialConnection] Performing hard reset (RTS pulse)")
        #endif

        // For USB-JTAG-Serial, RTS controls the reset line (active high = reset asserted)
        // We pulse RTS without touching DTR (GPIO9) so the chip boots normally
        // DTR=false means GPIO9=HIGH which means normal boot (not bootloader mode)

        // Ensure DTR is low (GPIO9 high = normal boot mode)
        try setDTR(false)
        try await Task.sleep(nanoseconds: 50_000_000) // 50ms

        // Pulse RTS to trigger reset
        try setRTS(true)
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms

        // Release reset - chip starts running
        try setRTS(false)
        try await Task.sleep(nanoseconds: 100_000_000) // 100ms

        #if DEBUG
        print("[SerialConnection] Hard reset complete")
        #endif
    }
}

// MARK: - fd_set Helpers

private func __darwin_fd_zero(_ set: inout fd_set) {
    set.fds_bits = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
}

private func __darwin_fd_set(_ fd: Int32, _ set: inout fd_set) {
    let intOffset = Int(fd / 32)
    let bitOffset = Int(fd % 32)
    let mask = Int32(1 << bitOffset)

    switch intOffset {
    case 0: set.fds_bits.0 |= mask
    case 1: set.fds_bits.1 |= mask
    case 2: set.fds_bits.2 |= mask
    case 3: set.fds_bits.3 |= mask
    case 4: set.fds_bits.4 |= mask
    case 5: set.fds_bits.5 |= mask
    case 6: set.fds_bits.6 |= mask
    case 7: set.fds_bits.7 |= mask
    default: break
    }
}
