// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef FIRMWAREFILE_H
#define FIRMWAREFILE_H

#include <QString>
#include <QByteArray>
#include <QUrl>
#include <vector>
#include <cstdint>
#include <stdexcept>

/**
 * Represents a single firmware image with its flash offset
 */
struct FirmwareImage {
    QString filePath;
    QByteArray data;
    uint32_t offset;

    int size() const { return data.size(); }

    QString fileName() const {
        int lastSlash = filePath.lastIndexOf('/');
        return lastSlash >= 0 ? filePath.mid(lastSlash + 1) : filePath;
    }

    /**
     * Check if the file appears to be valid ESP32 firmware
     * ESP32 firmware magic byte is 0xE9
     */
    bool isValid() const {
        if (data.size() < 8) return false;
        return static_cast<uint8_t>(data[0]) == 0xE9;
    }
};

/**
 * Errors that can occur when loading firmware
 */
class FirmwareLoadError : public std::runtime_error {
public:
    enum Type {
        NoFilesFound,
        MissingFirmware,
        InvalidFile
    };

    FirmwareLoadError(Type type, const QString& message = "")
        : std::runtime_error(message.toStdString())
        , m_type(type)
        , m_message(message)
    {}

    Type type() const { return m_type; }
    QString message() const { return m_message; }

private:
    Type m_type;
    QString m_message;
};

/**
 * Represents a complete firmware package (bootloader, partitions, app)
 * ESP32-C3 flash layout:
 * - 0x0000: bootloader.bin (second-stage bootloader)
 * - 0x8000: partitions.bin (partition table)
 * - 0x10000: firmware.bin (application)
 */
class FirmwareFile {
public:
    FirmwareFile() = default;

    /**
     * Single-file constructor
     * Detects merged firmware (starting at 0x0) vs app-only (at 0x10000)
     */
    FirmwareFile(const QString& filePath, const QByteArray& data);

    /**
     * Multi-file constructor for complete firmware package
     */
    explicit FirmwareFile(const std::vector<FirmwareImage>& images);

    /**
     * Create from PlatformIO build directory
     */
    static FirmwareFile fromPlatformIOBuild(const QString& dirPath);

    /**
     * Load firmware from a file path
     */
    static FirmwareFile loadFromFile(const QString& filePath);

    const std::vector<FirmwareImage>& images() const { return m_images; }

    int totalSize() const;
    int size() const { return totalSize(); }

    /**
     * For backward compatibility, return the app firmware data
     */
    QByteArray data() const;

    QString fileName() const;
    QString sizeDescription() const;

    /**
     * Check if the firmware package is valid
     * All images must be valid
     */
    bool isValid() const;

    /**
     * Check if this is a complete package (has bootloader, partitions, and app)
     */
    bool isComplete() const;

    /**
     * Description of what will be flashed
     */
    QString flashDescription() const;

    bool isEmpty() const { return m_images.empty(); }

private:
    std::vector<FirmwareImage> m_images;
};

#endif // FIRMWAREFILE_H
