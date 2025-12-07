import Foundation

/// Represents an available serial port
struct SerialPort: Identifiable, Hashable {
    let id: String
    let name: String
    let path: String
    let vendorId: Int?
    let productId: Int?

    var displayName: String {
        name.isEmpty ? path : name
    }

    var isESP32C3: Bool {
        // ESP32-C3 USB CDC VID/PID
        vendorId == 0x303A && productId == 0x1001
    }
}

/// Supported baud rates for flashing
enum BaudRate: Int, CaseIterable, Identifiable {
    case baud115200 = 115200
    case baud230400 = 230400
    case baud460800 = 460800
    case baud921600 = 921600

    var id: Int { rawValue }

    var displayName: String {
        switch self {
        case .baud115200: return "115200"
        case .baud230400: return "230400"
        case .baud460800: return "460800"
        case .baud921600: return "921600"
        }
    }

    var speedConstant: speed_t {
        switch self {
        case .baud115200: return speed_t(B115200)
        case .baud230400: return speed_t(B230400)
        case .baud460800: return 460800
        case .baud921600: return 921600
        }
    }
}

/// Represents a single firmware image with its flash offset
struct FirmwareImage {
    let url: URL
    let data: Data
    let offset: UInt32

    var size: Int { data.count }
    var fileName: String { url.lastPathComponent }

    /// Check if the file appears to be valid ESP32 firmware
    var isValid: Bool {
        guard data.count >= 8 else { return false }
        // ESP32 firmware magic byte is 0xE9
        return data[0] == 0xE9
    }
}

/// Represents a complete firmware package (bootloader, partitions, app)
/// ESP32-C3 flash layout:
/// - 0x0000: bootloader.bin (second-stage bootloader)
/// - 0x8000: partitions.bin (partition table)
/// - 0x10000: firmware.bin (application)
struct FirmwareFile {
    let images: [FirmwareImage]

    /// Single-file constructor
    /// - Detects merged firmware (starting at 0x0) vs app-only (at 0x10000)
    init(url: URL, data: Data) {
        // Check if this looks like a bootloader (starts at 0x0)
        // Bootloader typically has different characteristics than app:
        // - Both start with 0xE9 magic
        // - Bootloader is typically smaller (< 64KB)
        // - Merged binary name often contains "merged" or "factory"
        let fileName = url.lastPathComponent.lowercased()
        let isMergedBinary = fileName.contains("merged") ||
                            fileName.contains("factory") ||
                            fileName.contains("combined") ||
                            fileName.contains("full")

        // If the filename suggests it's a merged binary, flash at 0x0
        // Otherwise assume it's an app-only binary at 0x10000
        let offset: UInt32 = isMergedBinary ? 0x0000 : 0x10000
        self.images = [FirmwareImage(url: url, data: data, offset: offset)]
    }

    /// Multi-file constructor for complete firmware package
    init(images: [FirmwareImage]) {
        self.images = images.sorted { $0.offset < $1.offset }
    }

    /// Create from PlatformIO build directory
    static func fromPlatformIOBuild(at url: URL) throws -> FirmwareFile {
        var images: [FirmwareImage] = []
        let fileManager = FileManager.default

        // Standard ESP32 flash offsets
        let fileOffsets: [(String, UInt32)] = [
            ("bootloader.bin", 0x0000),
            ("partitions.bin", 0x8000),
            ("firmware.bin", 0x10000)
        ]

        for (filename, offset) in fileOffsets {
            let fileURL = url.appendingPathComponent(filename)
            if fileManager.fileExists(atPath: fileURL.path) {
                let data = try Data(contentsOf: fileURL)
                images.append(FirmwareImage(url: fileURL, data: data, offset: offset))
            }
        }

        guard !images.isEmpty else {
            throw FirmwareLoadError.noFilesFound
        }

        // At minimum we need firmware.bin
        guard images.contains(where: { $0.offset == 0x10000 }) else {
            throw FirmwareLoadError.missingFirmware
        }

        return FirmwareFile(images: images)
    }

    var totalSize: Int {
        images.reduce(0) { $0 + $1.size }
    }

    var size: Int { totalSize }

    var data: Data {
        // For backward compatibility, return the app firmware data
        images.first(where: { $0.offset == 0x10000 })?.data ?? Data()
    }

    var fileName: String {
        if images.count > 1 {
            return "\(images.count) files"
        }
        return images.first?.fileName ?? "No firmware"
    }

    var sizeDescription: String {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        return formatter.string(fromByteCount: Int64(totalSize))
    }

    /// Check if the firmware package is valid
    var isValid: Bool {
        // All images must be valid
        images.allSatisfy { $0.isValid }
    }

    /// Check if this is a complete package (has bootloader, partitions, and app)
    var isComplete: Bool {
        let offsets = Set(images.map { $0.offset })
        return offsets.contains(0x0000) && offsets.contains(0x8000) && offsets.contains(0x10000)
    }

    /// Description of what will be flashed
    var flashDescription: String {
        let parts = images.map { image -> String in
            let name: String
            switch image.offset {
            case 0x0000: name = "bootloader"
            case 0x8000: name = "partitions"
            case 0x10000: name = "app"
            default: name = image.fileName
            }
            let formatter = ByteCountFormatter()
            formatter.countStyle = .file
            return "\(name) @ 0x\(String(image.offset, radix: 16)) (\(formatter.string(fromByteCount: Int64(image.size))))"
        }
        return parts.joined(separator: ", ")
    }
}

enum FirmwareLoadError: Error, LocalizedError {
    case noFilesFound
    case missingFirmware
    case invalidFile(String)

    var errorDescription: String? {
        switch self {
        case .noFilesFound:
            return "No firmware files found in directory"
        case .missingFirmware:
            return "Missing firmware.bin"
        case .invalidFile(let name):
            return "Invalid firmware file: \(name)"
        }
    }
}
