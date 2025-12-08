// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

namespace FAMESmartFlasher.Models;

/// <summary>
/// Represents an available serial port
/// </summary>
public class SerialPortInfo
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public required string Path { get; init; }
    public int? VendorId { get; init; }
    public int? ProductId { get; init; }

    public string DisplayName => string.IsNullOrEmpty(Name) ? Path : Name;

    /// <summary>
    /// Check if this is an ESP32-C3 USB CDC device
    /// ESP32-C3 USB CDC VID/PID: 0x303A:0x1001
    /// </summary>
    public bool IsESP32C3 => VendorId == 0x303A && ProductId == 0x1001;

    public override bool Equals(object? obj) =>
        obj is SerialPortInfo other && Path == other.Path;

    public override int GetHashCode() => Path.GetHashCode();
}

/// <summary>
/// Supported baud rates for flashing
/// </summary>
public enum BaudRate
{
    Baud115200 = 115200,
    Baud230400 = 230400,
    Baud460800 = 460800,
    Baud921600 = 921600
}
