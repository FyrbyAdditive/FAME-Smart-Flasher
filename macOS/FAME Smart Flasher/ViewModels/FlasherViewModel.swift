import SwiftUI
import UniformTypeIdentifiers

/// Main view model for the flasher application
@MainActor
@Observable
final class FlasherViewModel {
    // MARK: - State

    var selectedPort: SerialPort? {
        didSet {
            if selectedPort != oldValue && isSerialMonitorEnabled && !flashingState.isActive {
                Task {
                    await reconnectSerialMonitor()
                }
            }
        }
    }
    var selectedBaudRate: BaudRate = .baud115200
    var firmwareFile: FirmwareFile?
    var flashingState: FlashingState = .idle
    var progress: Double = 0.0

    // Serial monitor
    var isSerialMonitorEnabled = false
    var serialMonitorOutput = ""
    var isSerialMonitorConnected = false

    // MARK: - Dependencies

    let portManager = SerialPortManager()
    private let flashingService = FlashingService()
    private var serialMonitorTask: Task<Void, Never>?
    private var serialMonitorConnection: SerialConnection?
    private var autoReconnectTask: Task<Void, Never>?
    private static let reconnectInterval: UInt64 = 2_000_000_000 // 2 seconds

    // MARK: - Computed Properties

    var canFlash: Bool {
        selectedPort != nil &&
        firmwareFile != nil &&
        !flashingState.isActive
    }

    var statusMessage: String {
        flashingState.statusMessage
    }

    // MARK: - Initialization

    init() {
        portManager.startObserving()
    }

    // MARK: - Actions

    /// Select a firmware file or PlatformIO build directory
    func selectFirmware() async {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [
            UTType(filenameExtension: "bin") ?? .data,
            .folder
        ]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = true
        panel.message = "Select firmware file (.bin) or PlatformIO build folder"
        panel.prompt = "Select"

        let response = await panel.begin()
        guard response == .OK, let url = panel.url else { return }

        do {
            var isDirectory: ObjCBool = false
            FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory)

            if isDirectory.boolValue {
                // Directory selected - try to load as PlatformIO build
                firmwareFile = try FirmwareFile.fromPlatformIOBuild(at: url)
            } else {
                // Single file selected
                let data = try Data(contentsOf: url)
                firmwareFile = FirmwareFile(url: url, data: data)
            }

            if !firmwareFile!.isValid {
                flashingState = .error(.invalidFirmware(reason: "Missing ESP32 magic byte"))
            } else {
                flashingState = .idle
            }
        } catch {
            flashingState = .error(.invalidFirmware(reason: error.localizedDescription))
        }
    }

    /// Start the flashing process
    func startFlashing() async {
        guard let port = selectedPort,
              let firmware = firmwareFile else { return }

        // Save serial monitor state and fully disconnect before flashing
        let wasSerialMonitorEnabled = isSerialMonitorEnabled
        if wasSerialMonitorEnabled || serialMonitorConnection != nil {
            serialMonitorOutput += "[Disconnecting for flash...]\n"
            stopAutoReconnect()

            // Cancel the monitor task and wait for it to complete
            let taskToWait = serialMonitorTask
            serialMonitorTask?.cancel()
            serialMonitorTask = nil

            // Close the connection first to unblock any pending reads
            await serialMonitorConnection?.close()
            serialMonitorConnection = nil
            isSerialMonitorConnected = false

            // Wait for the task to actually finish
            if let task = taskToWait {
                _ = await task.result
            }

            // Longer delay to ensure port is fully released by the OS
            try? await Task.sleep(nanoseconds: 1_000_000_000) // 1 second
        }

        do {
            try await flashingService.flash(
                firmware: firmware,
                port: port,
                baudRate: selectedBaudRate
            ) { [weak self] state in
                Task { @MainActor in
                    self?.handleFlashingStateChange(state)
                }
            }

            // Reconnect serial monitor if it was enabled
            if wasSerialMonitorEnabled {
                try await Task.sleep(nanoseconds: 1_000_000_000) // 1 second delay
                await connectSerialMonitor()
            }
        } catch {
            if let flashError = error as? FlashingError {
                flashingState = .error(flashError)
            } else {
                flashingState = .error(.connectionFailed(error.localizedDescription))
            }
            // Try to reconnect serial monitor even on error
            if wasSerialMonitorEnabled {
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                await connectSerialMonitor()
            }
        }
    }

    /// Cancel the current flash operation
    func cancelFlashing() {
        Task {
            await flashingService.cancel()
        }
        flashingState = .idle
    }

    /// Toggle the serial monitor
    func toggleSerialMonitor() async {
        if isSerialMonitorEnabled {
            await disconnectSerialMonitor()
            isSerialMonitorEnabled = false
        } else {
            isSerialMonitorEnabled = true
            await connectSerialMonitor()
        }
    }

    /// Clear the serial monitor output
    func clearSerialOutput() {
        serialMonitorOutput = ""
    }

    // MARK: - Private Methods

    private func handleFlashingStateChange(_ state: FlashingState) {
        flashingState = state

        if case .flashing(let p) = state {
            progress = p
        } else if case .complete = state {
            progress = 1.0
        } else if case .idle = state {
            progress = 0.0
        }
    }

    private func connectSerialMonitor() async {
        guard let port = selectedPort else {
            serialMonitorOutput += "[No port selected]\n"
            startAutoReconnect()
            return
        }
        guard !flashingState.isActive else { return }

        let connection = SerialConnection()
        serialMonitorConnection = connection

        serialMonitorTask = Task.detached { [weak self] in
            do {
                try await connection.open(path: port.path)
                try await connection.setBaudRate(.baud115200)

                await MainActor.run {
                    self?.serialMonitorOutput += "[Connected to \(port.name)]\n"
                    self?.isSerialMonitorConnected = true
                    self?.stopAutoReconnect()
                }

                // Buffer for batching updates to reduce UI churn
                var pendingText = ""
                var lastUpdateTime = Date()
                let updateInterval: TimeInterval = 0.1 // Batch updates every 100ms

                while !Task.isCancelled {
                    do {
                        let data = try await connection.read(timeout: 0.1) // Shorter timeout for responsiveness
                        if !data.isEmpty {
                            // Try UTF-8 first, fall back to ASCII
                            let text = String(data: data, encoding: .utf8)
                                ?? String(data: data, encoding: .ascii)
                                ?? data.map { String(format: "%02X ", $0) }.joined()

                            pendingText += text
                        }

                        // Batch updates to reduce UI thread load
                        let now = Date()
                        if !pendingText.isEmpty && now.timeIntervalSince(lastUpdateTime) >= updateInterval {
                            let textToAdd = pendingText
                            pendingText = ""
                            lastUpdateTime = now

                            await MainActor.run {
                                guard let self = self else { return }
                                self.serialMonitorOutput += textToAdd
                                // Limit buffer size
                                if self.serialMonitorOutput.count > 50000 {
                                    self.serialMonitorOutput = String(self.serialMonitorOutput.suffix(40000))
                                }
                            }
                        }
                    } catch SerialError.timeout {
                        // Timeout - flush any pending text
                        if !pendingText.isEmpty {
                            let textToAdd = pendingText
                            pendingText = ""
                            lastUpdateTime = Date()

                            await MainActor.run {
                                guard let self = self else { return }
                                self.serialMonitorOutput += textToAdd
                                if self.serialMonitorOutput.count > 50000 {
                                    self.serialMonitorOutput = String(self.serialMonitorOutput.suffix(40000))
                                }
                            }
                        }
                        continue
                    } catch SerialError.notConnected {
                        // Connection was closed, exit cleanly
                        break
                    } catch {
                        // Other error, exit loop and report
                        throw error
                    }
                }
            } catch {
                await MainActor.run {
                    self?.serialMonitorOutput += "[Disconnected: \(error.localizedDescription)]\n"
                    self?.isSerialMonitorConnected = false
                    // Start auto-reconnect if still enabled
                    if self?.isSerialMonitorEnabled == true && self?.flashingState.isActive == false {
                        self?.startAutoReconnect()
                    }
                }
            }

            await connection.close()
            await MainActor.run {
                self?.isSerialMonitorConnected = false
            }
        }
    }

    private func disconnectSerialMonitor() async {
        stopAutoReconnect()
        serialMonitorTask?.cancel()
        serialMonitorTask = nil
        await serialMonitorConnection?.close()
        serialMonitorConnection = nil
        isSerialMonitorConnected = false
    }

    private func reconnectSerialMonitor() async {
        await disconnectSerialMonitor()
        if selectedPort != nil {
            await connectSerialMonitor()
        }
    }

    private func startAutoReconnect() {
        guard autoReconnectTask == nil else { return }
        guard isSerialMonitorEnabled else { return }

        autoReconnectTask = Task.detached { [weak self] in
            while !Task.isCancelled {
                do {
                    try await Task.sleep(nanoseconds: Self.reconnectInterval)
                } catch {
                    break
                }

                guard let self = self else { break }

                let shouldContinue = await MainActor.run {
                    guard self.isSerialMonitorEnabled else { return false }
                    guard !self.isSerialMonitorConnected else { return false }
                    guard !self.flashingState.isActive else { return true } // Continue waiting
                    self.serialMonitorOutput += "[Attempting to reconnect...]\n"
                    return true
                }

                guard shouldContinue else { break }

                // Only attempt reconnect if not already connected and not flashing
                let canReconnect = await MainActor.run {
                    !self.isSerialMonitorConnected && !self.flashingState.isActive && self.isSerialMonitorEnabled
                }

                if canReconnect {
                    await self.connectSerialMonitor()
                }
            }
        }
    }

    private func stopAutoReconnect() {
        autoReconnectTask?.cancel()
        autoReconnectTask = nil
    }
}
