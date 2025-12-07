import Foundation

/// Service that orchestrates the ESP32 flashing process
actor FlashingService {
    private var connection: SerialConnection?
    private var slipDecoder = SLIPDecoder()
    private var isCancelled = false

    private let syncRetries = 20
    private let responseTimeout: TimeInterval = 5.0

    /// Flash firmware to an ESP32 device
    /// - Parameters:
    ///   - firmware: Firmware file to flash (can contain multiple images at different offsets)
    ///   - port: Serial port to use
    ///   - baudRate: Target baud rate for flashing
    ///   - onProgress: Progress callback
    func flash(
        firmware: FirmwareFile,
        port: SerialPort,
        baudRate: BaudRate,
        onProgress: @escaping (FlashingState) -> Void
    ) async throws {
        isCancelled = false
        connection = SerialConnection()

        defer {
            Task {
                await connection?.close()
            }
        }

        // 1. Connect
        await MainActor.run { onProgress(.connecting) }
        try await connection?.open(path: port.path)

        // 2. Enter bootloader mode using DTR/RTS reset sequence
        // For ESP32-C3 USB-JTAG-Serial, this triggers the built-in reset logic
        // esptool uses only one reset strategy per device type - don't mix them
        let isUSBJTAGSerial = port.isESP32C3
        #if DEBUG
        print("[FlashingService] Triggering bootloader entry via DTR/RTS (USB-JTAG-Serial: \(isUSBJTAGSerial))")
        #endif
        try await connection?.enterBootloaderMode(isUSBJTAGSerial: isUSBJTAGSerial)

        // Wait a moment for the chip to enter bootloader
        // The USB-JTAG-Serial peripheral should stay connected
        try await Task.sleep(nanoseconds: 500_000_000) // 500ms

        // Read any boot log that might have been output (like esptool does)
        // This helps diagnose whether the chip entered bootloader mode
        #if DEBUG
        if let bootData = try? await connection?.read(timeout: 0.1), !bootData.isEmpty {
            if let bootLog = String(data: bootData, encoding: .utf8) {
                print("[FlashingService] Boot output after reset: \(bootLog)")
            } else {
                print("[FlashingService] Boot output (hex): \(bootData.map { String(format: "%02X", $0) }.joined(separator: " "))")
            }
        }
        #endif

        // Flush any remaining boot messages
        await connection?.flush()

        // Try syncing without closing the port first
        // If that fails, we'll try the close/reopen approach
        var syncSucceeded = false

        #if DEBUG
        print("[FlashingService] Attempting sync without port close/reopen")
        #endif

        do {
            await MainActor.run { onProgress(.syncing) }
            try await syncWithRetry()
            syncSucceeded = true
            #if DEBUG
            print("[FlashingService] Sync succeeded without port reopen")
            #endif

            // CRITICAL: Disable watchdogs IMMEDIATELY after first sync
            // For USB-JTAG-Serial devices, the RTC watchdog can cause resets
            // that interrupt flashing. We must disable it before doing anything else.
            if isUSBJTAGSerial {
                #if DEBUG
                print("[FlashingService] Disabling watchdogs immediately after sync")
                #endif
                try await disableWatchdogs()
            }
        } catch {
            #if DEBUG
            print("[FlashingService] First sync attempt failed: \(error), trying with port reopen")
            #endif
        }

        // If sync failed, try closing and reopening the port
        // This handles cases where USB-JTAG-Serial re-enumerates
        if !syncSucceeded {
            await connection?.close()

            // Wait for USB re-enumeration
            try await Task.sleep(nanoseconds: 2_000_000_000) // 2 seconds

            // Try to reopen the port multiple times
            var opened = false
            for attempt in 1...5 {
                do {
                    try await connection?.open(path: port.path)
                    opened = true
                    #if DEBUG
                    print("[FlashingService] Port reopened on attempt \(attempt)")
                    #endif
                    break
                } catch {
                    #if DEBUG
                    print("[FlashingService] Port open attempt \(attempt) failed: \(error)")
                    #endif
                    if attempt < 5 {
                        try await Task.sleep(nanoseconds: 500_000_000) // 500ms
                    }
                }
            }

            guard opened else {
                throw FlashingError.connectionFailed("Could not reopen port after reset")
            }

            // Flush any garbage data
            await connection?.flush()

            // Try sync again
            await MainActor.run { onProgress(.syncing) }
            try await syncWithRetry()

            // CRITICAL: Disable watchdogs IMMEDIATELY after sync
            if isUSBJTAGSerial {
                #if DEBUG
                print("[FlashingService] Disabling watchdogs immediately after sync (retry path)")
                #endif
                try await disableWatchdogs()
            }
        }

        // 4. Change baud rate if needed
        if baudRate != .baud115200 {
            await MainActor.run { onProgress(.changingBaudRate) }
            try await changeBaudRate(to: baudRate)
        }

        // Note: Watchdogs already disabled immediately after sync (see above)

        // 5. Attach SPI flash (required for ROM bootloader before flash operations)
        #if DEBUG
        print("[FlashingService] Sending SPI_ATTACH command")
        #endif
        try await spiAttach()

        // 6. Flash all images in the firmware package
        let totalBytes = firmware.images.reduce(0) { $0 + $1.size }
        var bytesFlashed = 0

        #if DEBUG
        print("[FlashingService] Flashing \(firmware.images.count) image(s), total size: \(totalBytes) bytes")
        #endif

        for image in firmware.images {
            guard !isCancelled else {
                throw FlashingError.cancelled
            }

            let blockSize = ESP32Protocol.flashBlockSize
            let numBlocks = (image.size + blockSize - 1) / blockSize

            #if DEBUG
            print("[FlashingService] Flashing \(image.fileName): \(image.size) bytes at offset 0x\(String(image.offset, radix: 16))")
            let header = image.data.prefix(16)
            print("[FlashingService] Header: \(header.map { String(format: "%02X", $0) }.joined(separator: " "))")
            if image.data[0] == 0xE9 {
                print("[FlashingService] Valid ESP32 image detected (magic: 0xE9)")
            }
            #endif

            // Begin flash for this image
            await MainActor.run { onProgress(.erasing) }
            try await flashBegin(
                size: UInt32(image.size),
                numBlocks: UInt32(numBlocks),
                blockSize: UInt32(blockSize),
                offset: image.offset
            )

            // Send data blocks
            for blockNum in 0..<numBlocks {
                guard !isCancelled else {
                    throw FlashingError.cancelled
                }

                let start = blockNum * blockSize
                let end = min(start + blockSize, image.size)
                var blockData = Data(image.data[start..<end])

                // Pad last block with 0xFF if needed
                if blockData.count < blockSize {
                    blockData.append(contentsOf: [UInt8](repeating: 0xFF, count: blockSize - blockData.count))
                }

                // Calculate overall progress across all images
                let imageProgress = Double(blockNum + 1) / Double(numBlocks)
                let overallProgress = (Double(bytesFlashed) + (imageProgress * Double(image.size))) / Double(totalBytes)
                await MainActor.run { onProgress(.flashing(progress: overallProgress)) }

                try await flashData(block: blockData, sequenceNumber: blockNum)

                // Small delay after each block to prevent USB-JTAG-Serial buffer overflow
                // The ROM bootloader (without stub) can overwhelm the USB peripheral
                // This is a known issue with ESP32-C3 USB-JTAG-Serial
                try await Task.sleep(nanoseconds: 5_000_000) // 5ms between blocks
            }

            bytesFlashed += image.size
        }

        // 7. Verify (implicit - checksums validated per block)
        await MainActor.run { onProgress(.verifying) }
        try await Task.sleep(nanoseconds: 100_000_000) // Brief pause

        // 8. Complete flashing and reboot
        await MainActor.run { onProgress(.restarting) }
        try await flashEnd(reboot: true, isUSBJTAGSerial: isUSBJTAGSerial)

        try await Task.sleep(nanoseconds: 1_000_000_000) // 1 second for device to restart

        await MainActor.run { onProgress(.complete) }
    }

    /// Cancel the current flash operation
    func cancel() {
        isCancelled = true
    }

    // MARK: - Private Methods

    private func syncWithRetry() async throws {
        for attempt in 1...syncRetries {
            do {
                try await performSync()
                return // Success
            } catch {
                if attempt == syncRetries {
                    throw FlashingError.syncFailed(attempts: syncRetries)
                }
                try await Task.sleep(nanoseconds: 50_000_000) // 50ms
            }
        }
    }

    private func performSync() async throws {
        let syncCommand = ESP32Protocol.buildSyncCommand()
        let slipEncoded = SLIPCodec.encode(syncCommand)

        #if DEBUG
        print("[FlashingService] Sending SYNC command, \(slipEncoded.count) bytes")
        print("[FlashingService] SLIP encoded: \(slipEncoded.map { String(format: "%02X", $0) }.joined(separator: " "))")
        #endif

        // Send ONE sync packet (not 7 like before!)
        // esptool sends 1 sync, then reads 7 additional responses to drain
        try await connection?.write(slipEncoded)
        #if DEBUG
        print("[FlashingService] Sent sync packet")
        #endif

        // Wait for first response
        let response = try await waitForResponse(
            command: .sync,
            timeout: 1.0
        )

        #if DEBUG
        print("[FlashingService] Got response: command=\(response.command), status=\(response.status), error=\(response.error)")
        #endif

        guard response.isSuccess else {
            throw FlashingError.syncFailed(attempts: 1)
        }

        // Read 7 more responses to drain extra sync responses (like esptool does)
        // The ROM bootloader sends multiple responses to sync
        #if DEBUG
        print("[FlashingService] Draining extra sync responses...")
        #endif
        for _ in 0..<7 {
            _ = try? await waitForResponse(command: .sync, timeout: 0.1)
        }

        // Flush any remaining data
        await connection?.flush()
    }

    private func changeBaudRate(to rate: BaudRate) async throws {
        let command = ESP32Protocol.buildChangeBaudCommand(
            newBaud: UInt32(rate.rawValue),
            oldBaud: 115200
        )
        let encoded = SLIPCodec.encode(command)
        try await connection?.write(encoded)

        // Brief delay then change host baud rate
        try await Task.sleep(nanoseconds: 50_000_000) // 50ms
        try await connection?.setBaudRate(rate)
        try await Task.sleep(nanoseconds: 50_000_000) // 50ms

        // Sync again at new baud rate
        try await performSync()
    }

    private func spiAttach() async throws {
        let command = ESP32Protocol.buildSpiAttachCommand()
        let encoded = SLIPCodec.encode(command)
        try await connection?.write(encoded)

        let response = try await waitForResponse(command: .spiAttach, timeout: 3.0)
        #if DEBUG
        print("[FlashingService] SPI_ATTACH response: status=\(response.status), error=\(response.error)")
        #endif
        guard response.isSuccess else {
            throw FlashingError.connectionFailed("SPI attach failed: status=\(response.status), error=\(response.error)")
        }
    }

    /// Disable RTC and Super watchdogs for USB-JTAG-Serial devices
    /// This prevents the chip from resetting during long flash operations
    /// Based on esptool's watchdog handling for ESP32-C3
    private func disableWatchdogs() async throws {
        // 1. Disable RTC Watchdog
        // First unlock the write protection
        try await writeReg(
            address: ESP32C3Registers.rtcWdtWprotect,
            value: ESP32C3Registers.rtcWdtWkey
        )

        // Read current config and clear WDT_EN bit
        let wdtConfig = try await readReg(address: ESP32C3Registers.rtcWdtConfig0)
        let newWdtConfig = wdtConfig & ~ESP32C3Registers.wdtEnBit
        try await writeReg(address: ESP32C3Registers.rtcWdtConfig0, value: newWdtConfig)

        // Re-lock write protection
        try await writeReg(address: ESP32C3Registers.rtcWdtWprotect, value: 0)

        #if DEBUG
        print("[FlashingService] RTC WDT disabled (was 0x\(String(wdtConfig, radix: 16)), now 0x\(String(newWdtConfig, radix: 16)))")
        #endif

        // 2. Enable Super Watchdog auto-feed (effectively disables it)
        // First unlock the write protection
        try await writeReg(
            address: ESP32C3Registers.swdWprotect,
            value: ESP32C3Registers.swdWkey
        )

        // Read current config and set SWD_AUTO_FEED_EN bit
        let swdConfig = try await readReg(address: ESP32C3Registers.swdConf)
        let newSwdConfig = swdConfig | ESP32C3Registers.swdAutoFeedEnBit
        try await writeReg(address: ESP32C3Registers.swdConf, value: newSwdConfig)

        // Re-lock write protection
        try await writeReg(address: ESP32C3Registers.swdWprotect, value: 0)

        #if DEBUG
        print("[FlashingService] SWD auto-feed enabled (was 0x\(String(swdConfig, radix: 16)), now 0x\(String(newSwdConfig, radix: 16)))")
        #endif
    }

    private func readReg(address: UInt32) async throws -> UInt32 {
        let command = ESP32Protocol.buildReadRegCommand(address: address)
        let encoded = SLIPCodec.encode(command)
        try await connection?.write(encoded)

        let response = try await waitForResponse(command: .readReg, timeout: 1.0)
        guard response.isSuccess else {
            throw FlashingError.connectionFailed("READ_REG failed at 0x\(String(address, radix: 16))")
        }
        return response.value
    }

    private func writeReg(address: UInt32, value: UInt32) async throws {
        let command = ESP32Protocol.buildWriteRegCommand(address: address, value: value)
        let encoded = SLIPCodec.encode(command)
        try await connection?.write(encoded)

        let response = try await waitForResponse(command: .writeReg, timeout: 1.0)
        guard response.isSuccess else {
            throw FlashingError.connectionFailed("WRITE_REG failed at 0x\(String(address, radix: 16))")
        }
    }

    private func flashBegin(
        size: UInt32,
        numBlocks: UInt32,
        blockSize: UInt32,
        offset: UInt32
    ) async throws {
        let command = ESP32Protocol.buildFlashBeginCommand(
            size: size,
            numBlocks: numBlocks,
            blockSize: blockSize,
            offset: offset
        )
        let encoded = SLIPCodec.encode(command)
        try await connection?.write(encoded)

        let response = try await waitForResponse(command: .flashBegin, timeout: 30.0) // Erase can take time
        #if DEBUG
        print("[FlashingService] FLASH_BEGIN response: status=\(response.status), error=\(response.error)")
        #endif
        guard response.isSuccess else {
            throw FlashingError.flashBeginFailed(status: response.status)
        }
    }

    private func flashData(block: Data, sequenceNumber: Int) async throws {
        let command = ESP32Protocol.buildFlashDataCommand(
            data: block,
            sequenceNumber: UInt32(sequenceNumber)
        )
        let encoded = SLIPCodec.encode(command)

        #if DEBUG
        if sequenceNumber == 0 {
            print("[FlashingService] FLASH_DATA block 0: \(encoded.count) bytes encoded, block size: \(block.count)")
            print("[FlashingService] First 60 bytes: \(encoded.prefix(60).map { String(format: "%02X", $0) }.joined(separator: " "))")
        }
        #endif

        try await connection?.write(encoded)

        let response = try await waitForResponse(command: .flashData, timeout: responseTimeout)
        #if DEBUG
        print("[FlashingService] FLASH_DATA[\(sequenceNumber)] response: status=\(response.status), error=\(response.error)")
        #endif
        guard response.isSuccess else {
            throw FlashingError.flashDataFailed(
                blockNumber: sequenceNumber,
                status: response.status
            )
        }
    }

    private func flashEnd(reboot: Bool, isUSBJTAGSerial: Bool = true) async throws {
        let command = ESP32Protocol.buildFlashEndCommand(reboot: reboot)
        let encoded = SLIPCodec.encode(command)
        try await connection?.write(encoded)

        #if DEBUG
        print("[FlashingService] Sent FLASH_END command (reboot=\(reboot))")
        #endif

        // Flash end might not get a response if rebooting
        do {
            let response = try await waitForResponse(command: .flashEnd, timeout: 2.0)
            #if DEBUG
            print("[FlashingService] FLASH_END response: status=\(response.status), error=\(response.error)")
            #endif
            if !response.isSuccess && !reboot {
                throw FlashingError.flashEndFailed
            }
        } catch {
            // Expected if rebooting
            if !reboot {
                throw error
            }
        }

        // For USB-JTAG-Serial devices, the FLASH_END reboot flag often doesn't work
        // because the ROM bootloader's soft reset doesn't reset the USB peripheral.
        // We need to do a hard reset using DTR/RTS.
        if reboot && isUSBJTAGSerial {
            #if DEBUG
            print("[FlashingService] Performing hard reset for USB-JTAG-Serial device")
            #endif
            try await connection?.hardReset()
        }
    }

    private func waitForResponse(
        command: ESP32Command,
        timeout: TimeInterval
    ) async throws -> ESP32Response {
        let deadline = Date().addingTimeInterval(timeout)
        slipDecoder.reset()

        #if DEBUG
        var totalBytesReceived = 0
        #endif

        while Date() < deadline {
            guard !isCancelled else {
                throw FlashingError.cancelled
            }

            do {
                let data = try await connection?.read(timeout: 0.1) ?? Data()

                #if DEBUG
                if !data.isEmpty {
                    totalBytesReceived += data.count
                    print("[FlashingService] Received \(data.count) bytes: \(data.prefix(50).map { String(format: "%02X", $0) }.joined(separator: " "))\(data.count > 50 ? "..." : "")")
                }
                #endif

                let packets = slipDecoder.process(data)

                for packet in packets {
                    #if DEBUG
                    print("[FlashingService] Decoded SLIP packet: \(packet.count) bytes")
                    #endif

                    if let response = ESP32Response.parse(packet),
                       response.command == command.rawValue {
                        return response
                    }
                }
            } catch SerialError.timeout {
                continue
            }
        }

        #if DEBUG
        print("[FlashingService] Timeout waiting for \(command), total bytes received: \(totalBytesReceived)")
        #endif

        throw FlashingError.timeout(operation: "waiting for \(command) response")
    }
}
