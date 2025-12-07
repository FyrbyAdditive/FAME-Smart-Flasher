// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#include "FirmwareFile.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <algorithm>

FirmwareFile::FirmwareFile(const QString& filePath, const QByteArray& data)
{
    // Check if this looks like a bootloader (starts at 0x0)
    // Bootloader typically has different characteristics than app:
    // - Both start with 0xE9 magic
    // - Bootloader is typically smaller (< 64KB)
    // - Merged binary name often contains "merged" or "factory"
    QString fileName = QFileInfo(filePath).fileName().toLower();
    bool isMergedBinary = fileName.contains("merged") ||
                          fileName.contains("factory") ||
                          fileName.contains("combined") ||
                          fileName.contains("full");

    // If the filename suggests it's a merged binary, flash at 0x0
    // Otherwise assume it's an app-only binary at 0x10000
    uint32_t offset = isMergedBinary ? 0x0000 : 0x10000;

    FirmwareImage image;
    image.filePath = filePath;
    image.data = data;
    image.offset = offset;
    m_images.push_back(image);
}

FirmwareFile::FirmwareFile(const std::vector<FirmwareImage>& images)
    : m_images(images)
{
    // Sort by offset
    std::sort(m_images.begin(), m_images.end(),
              [](const FirmwareImage& a, const FirmwareImage& b) {
                  return a.offset < b.offset;
              });
}

FirmwareFile FirmwareFile::fromPlatformIOBuild(const QString& dirPath)
{
    std::vector<FirmwareImage> images;
    QDir dir(dirPath);

    // Standard ESP32 flash offsets
    struct FileOffset {
        QString name;
        uint32_t offset;
    };

    const FileOffset fileOffsets[] = {
        {"bootloader.bin", 0x0000},
        {"partitions.bin", 0x8000},
        {"firmware.bin", 0x10000}
    };

    for (const auto& fo : fileOffsets) {
        QString filePath = dir.filePath(fo.name);
        if (QFile::exists(filePath)) {
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly)) {
                FirmwareImage image;
                image.filePath = filePath;
                image.data = file.readAll();
                image.offset = fo.offset;
                images.push_back(image);
                file.close();
            }
        }
    }

    if (images.empty()) {
        throw FirmwareLoadError(FirmwareLoadError::NoFilesFound,
                                "No firmware files found in directory");
    }

    // At minimum we need firmware.bin
    bool hasFirmware = std::any_of(images.begin(), images.end(),
                                   [](const FirmwareImage& img) {
                                       return img.offset == 0x10000;
                                   });
    if (!hasFirmware) {
        throw FirmwareLoadError(FirmwareLoadError::MissingFirmware,
                                "Missing firmware.bin");
    }

    return FirmwareFile(images);
}

FirmwareFile FirmwareFile::loadFromFile(const QString& filePath)
{
    QFileInfo fileInfo(filePath);

    if (fileInfo.isDir()) {
        return fromPlatformIOBuild(filePath);
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        throw FirmwareLoadError(FirmwareLoadError::InvalidFile,
                                QString("Cannot open file: %1").arg(filePath));
    }

    QByteArray data = file.readAll();
    file.close();

    return FirmwareFile(filePath, data);
}

int FirmwareFile::totalSize() const
{
    int total = 0;
    for (const auto& image : m_images) {
        total += image.size();
    }
    return total;
}

QByteArray FirmwareFile::data() const
{
    // For backward compatibility, return the app firmware data
    for (const auto& image : m_images) {
        if (image.offset == 0x10000) {
            return image.data;
        }
    }
    return m_images.empty() ? QByteArray() : m_images[0].data;
}

QString FirmwareFile::fileName() const
{
    if (m_images.size() > 1) {
        return QString("%1 files").arg(m_images.size());
    }
    return m_images.empty() ? "No firmware" : m_images[0].fileName();
}

QString FirmwareFile::sizeDescription() const
{
    qint64 bytes = totalSize();
    QLocale locale;

    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(locale.toString(bytes / 1024.0, 'f', 1));
    } else {
        return QString("%1 MB").arg(locale.toString(bytes / (1024.0 * 1024.0), 'f', 2));
    }
}

bool FirmwareFile::isValid() const
{
    if (m_images.empty()) {
        return false;
    }
    return std::all_of(m_images.begin(), m_images.end(),
                       [](const FirmwareImage& img) { return img.isValid(); });
}

bool FirmwareFile::isComplete() const
{
    bool hasBootloader = false;
    bool hasPartitions = false;
    bool hasApp = false;

    for (const auto& image : m_images) {
        if (image.offset == 0x0000) hasBootloader = true;
        if (image.offset == 0x8000) hasPartitions = true;
        if (image.offset == 0x10000) hasApp = true;
    }

    return hasBootloader && hasPartitions && hasApp;
}

QString FirmwareFile::flashDescription() const
{
    QStringList parts;

    for (const auto& image : m_images) {
        QString name;
        switch (image.offset) {
        case 0x0000: name = "bootloader"; break;
        case 0x8000: name = "partitions"; break;
        case 0x10000: name = "app"; break;
        default: name = image.fileName(); break;
        }

        FirmwareFile tempFile({image});
        QString desc = QString("%1 @ 0x%2 (%3)")
                           .arg(name)
                           .arg(image.offset, 0, 16)
                           .arg(tempFile.sizeDescription());
        parts.append(desc);
    }

    return parts.join(", ");
}
