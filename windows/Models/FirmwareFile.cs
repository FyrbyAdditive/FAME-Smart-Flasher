// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

using System.IO;

namespace FAMESmartFlasher.Models;

/// <summary>
/// Represents a single firmware image with its flash offset
/// </summary>
public class FirmwareImage
{
    public required string FilePath { get; init; }
    public required byte[] Data { get; init; }
    public required uint Offset { get; init; }

    public int Size => Data.Length;
    public string FileName => Path.GetFileName(FilePath);

    /// <summary>
    /// Check if the file appears to be valid ESP32 firmware
    /// ESP32 firmware magic byte is 0xE9
    /// </summary>
    public bool IsValid => Data.Length >= 8 && Data[0] == 0xE9;
}

/// <summary>
/// Represents a complete firmware package (bootloader, partitions, app)
/// ESP32-C3 flash layout:
/// - 0x0000: bootloader.bin (second-stage bootloader)
/// - 0x8000: partitions.bin (partition table)
/// - 0x10000: firmware.bin (application)
/// </summary>
public class FirmwareFile
{
    public List<FirmwareImage> Images { get; init; } = new();

    /// <summary>
    /// Single-file constructor
    /// Detects merged firmware (starting at 0x0) vs app-only (at 0x10000)
    /// </summary>
    public FirmwareFile(string filePath, byte[] data)
    {
        var fileName = Path.GetFileName(filePath).ToLowerInvariant();
        var isMergedBinary = fileName.Contains("merged") ||
                            fileName.Contains("factory") ||
                            fileName.Contains("combined") ||
                            fileName.Contains("full");

        var offset = isMergedBinary ? 0x0000u : 0x10000u;
        Images = new List<FirmwareImage>
        {
            new FirmwareImage
            {
                FilePath = filePath,
                Data = data,
                Offset = offset
            }
        };
    }

    /// <summary>
    /// Multi-file constructor for complete firmware package
    /// </summary>
    public FirmwareFile(List<FirmwareImage> images)
    {
        Images = images.OrderBy(i => i.Offset).ToList();
    }

    /// <summary>
    /// Create from PlatformIO build directory
    /// </summary>
    public static FirmwareFile FromPlatformIOBuild(string directoryPath)
    {
        var images = new List<FirmwareImage>();

        var fileOffsets = new (string FileName, uint Offset)[]
        {
            ("bootloader.bin", 0x0000),
            ("partitions.bin", 0x8000),
            ("firmware.bin", 0x10000)
        };

        foreach (var (fileName, offset) in fileOffsets)
        {
            var filePath = Path.Combine(directoryPath, fileName);
            if (File.Exists(filePath))
            {
                var data = File.ReadAllBytes(filePath);
                images.Add(new FirmwareImage
                {
                    FilePath = filePath,
                    Data = data,
                    Offset = offset
                });
            }
        }

        if (images.Count == 0)
            throw new FirmwareLoadException("No firmware files found in directory");

        // At minimum we need firmware.bin
        if (!images.Any(i => i.Offset == 0x10000))
            throw new FirmwareLoadException("Missing firmware.bin");

        return new FirmwareFile(images);
    }

    public int TotalSize => Images.Sum(i => i.Size);
    public int Size => TotalSize;

    public byte[] Data =>
        // For backward compatibility, return the app firmware data
        Images.FirstOrDefault(i => i.Offset == 0x10000)?.Data ?? Array.Empty<byte>();

    public string FileName =>
        Images.Count > 1 ? $"{Images.Count} files" : Images.FirstOrDefault()?.FileName ?? "No firmware";

    public string SizeDescription
    {
        get
        {
            var kb = TotalSize / 1024.0;
            return kb >= 1024
                ? $"{kb / 1024:F1} MB"
                : $"{kb:F1} KB";
        }
    }

    /// <summary>
    /// Check if the firmware package is valid
    /// For app firmware at 0x10000 or merged firmware at 0x0, check ESP32 magic byte (0xE9)
    /// Bootloader and partition table have different formats
    /// </summary>
    public bool IsValid => Images.Any(i => (i.Offset == 0x10000 || i.Offset == 0x0) && i.IsValid);

    /// <summary>
    /// Check if this is a complete package (has bootloader, partitions, and app)
    /// </summary>
    public bool IsComplete
    {
        get
        {
            var offsets = Images.Select(i => i.Offset).ToHashSet();
            return offsets.Contains(0x0000) && offsets.Contains(0x8000) && offsets.Contains(0x10000);
        }
    }

    /// <summary>
    /// Description of what will be flashed
    /// </summary>
    public string FlashDescription
    {
        get
        {
            var parts = Images.Select(image =>
            {
                var name = image.Offset switch
                {
                    0x0000 => "bootloader",
                    0x8000 => "partitions",
                    0x10000 => "app",
                    _ => image.FileName
                };

                var kb = image.Size / 1024.0;
                var size = kb >= 1024 ? $"{kb / 1024:F1} MB" : $"{kb:F1} KB";
                return $"{name} @ 0x{image.Offset:X} ({size})";
            });

            return string.Join(", ", parts);
        }
    }
}

/// <summary>
/// Firmware load errors
/// </summary>
public class FirmwareLoadException : Exception
{
    public FirmwareLoadException(string message) : base(message) { }
}
