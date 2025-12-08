// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

using System.Management;
using System.Text.RegularExpressions;
using FAMESmartFlasher.Models;

namespace FAMESmartFlasher.Services;

/// <summary>
/// Manages serial port enumeration and device detection
/// Uses WMI to get USB VID/PID information for ESP32-C3 detection
/// </summary>
public class SerialPortManager
{
    /// <summary>
    /// Get all available serial ports with USB device information
    /// </summary>
    public static List<SerialPortInfo> GetAvailablePorts()
    {
        var ports = new List<SerialPortInfo>();
        var portNames = System.IO.Ports.SerialPort.GetPortNames();

        foreach (var portName in portNames)
        {
            try
            {
                var info = GetPortInfo(portName);
                if (info != null)
                {
                    ports.Add(info);
                }
            }
            catch
            {
                // If we can't get device info, add basic port info
                ports.Add(new SerialPortInfo
                {
                    Id = portName,
                    Name = portName,
                    Path = portName,
                    VendorId = null,
                    ProductId = null
                });
            }
        }

        return ports.OrderBy(p => p.Path).ToList();
    }

    /// <summary>
    /// Get detailed information for a specific port using WMI
    /// </summary>
    private static SerialPortInfo? GetPortInfo(string portName)
    {
        try
        {
            // Query WMI for USB device information
            using var searcher = new ManagementObjectSearcher(
                "SELECT * FROM Win32_PnPEntity WHERE Name LIKE '%(COM%'");

            foreach (ManagementObject obj in searcher.Get())
            {
                var name = obj["Name"]?.ToString() ?? "";
                var deviceId = obj["DeviceID"]?.ToString() ?? "";

                // Check if this is our port
                if (!name.Contains(portName))
                    continue;

                // Extract VID and PID from DeviceID
                // Format: USB\VID_303A&PID_1001\...
                var vidMatch = Regex.Match(deviceId, @"VID_([0-9A-F]{4})", RegexOptions.IgnoreCase);
                var pidMatch = Regex.Match(deviceId, @"PID_([0-9A-F]{4})", RegexOptions.IgnoreCase);

                int? vendorId = null;
                int? productId = null;

                if (vidMatch.Success)
                    vendorId = Convert.ToInt32(vidMatch.Groups[1].Value, 16);

                if (pidMatch.Success)
                    productId = Convert.ToInt32(pidMatch.Groups[1].Value, 16);

                // Clean up the name (remove COM port from description)
                var cleanName = Regex.Replace(name, @"\s*\(COM\d+\)", "").Trim();

                return new SerialPortInfo
                {
                    Id = portName,
                    Name = cleanName,
                    Path = portName,
                    VendorId = vendorId,
                    ProductId = productId
                };
            }
        }
        catch
        {
            // Fall through to basic info
        }

        // Return basic info if WMI query fails
        return new SerialPortInfo
        {
            Id = portName,
            Name = portName,
            Path = portName,
            VendorId = null,
            ProductId = null
        };
    }

    /// <summary>
    /// Find the first available ESP32-C3 device
    /// </summary>
    public static SerialPortInfo? FindESP32C3()
    {
        var ports = GetAvailablePorts();
        return ports.FirstOrDefault(p => p.IsESP32C3);
    }

    /// <summary>
    /// Check if a port exists and is available
    /// </summary>
    public static bool IsPortAvailable(string portName)
    {
        return System.IO.Ports.SerialPort.GetPortNames().Contains(portName);
    }
}
