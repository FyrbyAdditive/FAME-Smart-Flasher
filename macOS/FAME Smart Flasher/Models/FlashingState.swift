import Foundation

/// Represents the current state of the flashing process
enum FlashingState: Equatable {
    case idle
    case connecting
    case syncing
    case changingBaudRate
    case erasing
    case flashing(progress: Double)
    case verifying
    case restarting
    case complete
    case error(FlashingError)

    var isActive: Bool {
        switch self {
        case .idle, .complete, .error:
            return false
        default:
            return true
        }
    }

    var statusMessage: String {
        switch self {
        case .idle:
            return "Ready"
        case .connecting:
            return "Connecting to device..."
        case .syncing:
            return "Syncing with bootloader..."
        case .changingBaudRate:
            return "Changing baud rate..."
        case .erasing:
            return "Erasing flash..."
        case .flashing(let progress):
            let percent = Int(progress * 100)
            return "Flashing... \(percent)%"
        case .verifying:
            return "Verifying..."
        case .restarting:
            return "Restarting device..."
        case .complete:
            return "Flash complete!"
        case .error(let error):
            return "Error: \(error.localizedDescription)"
        }
    }
}

/// Errors that can occur during flashing
enum FlashingError: Error, LocalizedError, Equatable {
    case portNotFound
    case connectionFailed(String)
    case syncFailed(attempts: Int)
    case baudChangeTimeout
    case flashBeginFailed(status: UInt8)
    case flashDataFailed(blockNumber: Int, status: UInt8)
    case flashEndFailed
    case checksumMismatch
    case timeout(operation: String)
    case invalidFirmware(reason: String)
    case portDisconnected
    case cancelled

    var errorDescription: String? {
        switch self {
        case .portNotFound:
            return "Serial port not found"
        case .connectionFailed(let reason):
            return "Connection failed: \(reason)"
        case .syncFailed(let attempts):
            return "Failed to sync after \(attempts) attempts"
        case .baudChangeTimeout:
            return "Timeout changing baud rate"
        case .flashBeginFailed(let status):
            return "Flash begin failed (0x\(String(status, radix: 16)))"
        case .flashDataFailed(let block, let status):
            return "Flash data failed at block \(block) (0x\(String(status, radix: 16)))"
        case .flashEndFailed:
            return "Flash end failed"
        case .checksumMismatch:
            return "Checksum mismatch"
        case .timeout(let operation):
            return "Timeout: \(operation)"
        case .invalidFirmware(let reason):
            return "Invalid firmware: \(reason)"
        case .portDisconnected:
            return "Port disconnected"
        case .cancelled:
            return "Operation cancelled"
        }
    }

    static func == (lhs: FlashingError, rhs: FlashingError) -> Bool {
        lhs.localizedDescription == rhs.localizedDescription
    }
}
