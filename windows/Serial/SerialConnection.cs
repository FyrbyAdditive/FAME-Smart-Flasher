// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

using System.IO.Ports;
using FAMESmartFlasher.Models;

namespace FAMESmartFlasher.Serial;

/// <summary>
/// Serial port errors
/// </summary>
public class SerialException : Exception
{
    public SerialException(string message) : base(message) { }
    public SerialException(string message, Exception inner) : base(message, inner) { }
}

/// <summary>
/// Windows serial port connection with precise ESP32 reset timing
/// </summary>
public class SerialConnection : IDisposable
{
    private SerialPort? _port;
    private BaudRate _currentBaudRate = BaudRate.Baud115200;
    private bool _disposed;

    public bool IsConnected => _port?.IsOpen == true;

    /// <summary>
    /// Open a serial port
    /// </summary>
    /// <param name="portName">Port name (e.g., "COM3")</param>
    public async Task OpenAsync(string portName)
    {
        try
        {
            _port = new SerialPort(portName)
            {
                BaudRate = (int)BaudRate.Baud115200,
                DataBits = 8,
                Parity = Parity.None,
                StopBits = StopBits.One,
                Handshake = Handshake.None,
                ReadTimeout = 1000,
                WriteTimeout = 1000,
                // Disable DTR/RTS auto-control - we manage these manually
                DtrEnable = false,
                RtsEnable = false
            };

            _port.Open();
            _currentBaudRate = BaudRate.Baud115200;

            // Flush any pending data
            await Task.Run(() =>
            {
                _port.DiscardInBuffer();
                _port.DiscardOutBuffer();
            });
        }
        catch (Exception ex)
        {
            throw new SerialException($"Cannot open port {portName}: {ex.Message}", ex);
        }
    }

    /// <summary>
    /// Close the serial port
    /// </summary>
    public void Close()
    {
        if (_port?.IsOpen == true)
        {
            _port.Close();
        }
        _port?.Dispose();
        _port = null;
    }

    /// <summary>
    /// Set the baud rate
    /// </summary>
    public async Task SetBaudRateAsync(BaudRate rate)
    {
        if (_port == null || !_port.IsOpen)
            throw new SerialException("Not connected");

        try
        {
            _port.BaudRate = (int)rate;
            _currentBaudRate = rate;

            // Flush buffers after baud rate change
            await Task.Run(() =>
            {
                _port.DiscardInBuffer();
                _port.DiscardOutBuffer();
            });
        }
        catch (Exception ex)
        {
            throw new SerialException($"Failed to set baud rate: {ex.Message}", ex);
        }
    }

    /// <summary>
    /// Write data to the serial port
    /// </summary>
    public async Task WriteAsync(byte[] data)
    {
        if (_port == null || !_port.IsOpen)
            throw new SerialException("Not connected");

        try
        {
            await _port.BaseStream.WriteAsync(data, 0, data.Length);
            await _port.BaseStream.FlushAsync();
        }
        catch (Exception ex)
        {
            throw new SerialException($"Write failed: {ex.Message}", ex);
        }
    }

    /// <summary>
    /// Read data from the serial port with timeout
    /// </summary>
    /// <param name="timeoutMs">Timeout in milliseconds</param>
    /// <returns>Data read from port</returns>
    public async Task<byte[]> ReadAsync(int timeoutMs = 1000)
    {
        if (_port == null || !_port.IsOpen)
            throw new SerialException("Not connected");

        try
        {
            var buffer = new byte[4096];
            using var cts = new CancellationTokenSource(timeoutMs);

            var bytesRead = await _port.BaseStream.ReadAsync(buffer, 0, buffer.Length, cts.Token);

            if (bytesRead == 0)
                return Array.Empty<byte>();

            var result = new byte[bytesRead];
            Array.Copy(buffer, result, bytesRead);
            return result;
        }
        catch (OperationCanceledException)
        {
            // Timeout - return empty array
            return Array.Empty<byte>();
        }
        catch (Exception ex)
        {
            throw new SerialException($"Read failed: {ex.Message}", ex);
        }
    }

    /// <summary>
    /// Read data until deadline
    /// </summary>
    public async Task<byte[]> ReadUntilAsync(DateTime deadline, int minBytes = 1)
    {
        var result = new List<byte>();

        while (DateTime.Now < deadline)
        {
            var remaining = (int)(deadline - DateTime.Now).TotalMilliseconds;
            if (remaining <= 0) break;

            var data = await ReadAsync(Math.Min(remaining, 100));
            result.AddRange(data);

            if (result.Count >= minBytes)
                break;
        }

        return result.ToArray();
    }

    /// <summary>
    /// Flush input and output buffers
    /// </summary>
    public void Flush()
    {
        if (_port?.IsOpen == true)
        {
            _port.DiscardInBuffer();
            _port.DiscardOutBuffer();
        }
    }

    /// <summary>
    /// Set DTR (Data Terminal Ready) line state
    /// </summary>
    public void SetDTR(bool value)
    {
        if (_port == null || !_port.IsOpen)
            throw new SerialException("Not connected");

        _port.DtrEnable = value;
    }

    /// <summary>
    /// Set RTS (Request To Send) line state
    /// </summary>
    public void SetRTS(bool value)
    {
        if (_port == null || !_port.IsOpen)
            throw new SerialException("Not connected");

        _port.RtsEnable = value;
    }

    /// <summary>
    /// Set both DTR and RTS simultaneously
    /// </summary>
    public void SetDTRRTS(bool dtr, bool rts)
    {
        if (_port == null || !_port.IsOpen)
            throw new SerialException("Not connected");

        _port.DtrEnable = dtr;
        _port.RtsEnable = rts;
    }

    /// <summary>
    /// Enter bootloader mode using DTR/RTS reset sequence
    /// </summary>
    /// <param name="isUSBJTAGSerial">If true, uses USB-JTAG-Serial reset (ESP32-C3/S3). If false, uses classic reset.</param>
    public async Task EnterBootloaderModeAsync(bool isUSBJTAGSerial = true)
    {
        if (isUSBJTAGSerial)
        {
            await USBJTAGSerialResetAsync();
        }
        else
        {
            await ClassicResetAsync();
        }

        Flush();
    }

    /// <summary>
    /// USB-JTAG-Serial reset sequence - exact match of esptool implementation
    /// For ESP32-C3/S3 with native USB-JTAG-Serial peripheral
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
    /// </summary>
    private async Task USBJTAGSerialResetAsync()
    {
        // Step 1: Idle state - both lines deasserted
        SetRTS(false);
        SetDTR(false);
        await Task.Delay(100); // 100ms

        // Step 2: Set IO0 (GPIO9 low for boot mode)
        SetDTR(true);
        SetRTS(false);
        await Task.Delay(100); // 100ms

        // Step 3: Reset sequence
        SetRTS(true);   // Assert reset
        SetDTR(false);  // Release IO0
        SetRTS(true);   // Set RTS again (Windows driver quirk)
        await Task.Delay(100); // 100ms

        // Step 4: Chip out of reset - both lines deasserted
        SetDTR(false);
        SetRTS(false);

        // Give the chip time to start the bootloader
        await Task.Delay(50); // 50ms
    }

    /// <summary>
    /// Classic reset sequence from esptool (ClassicReset)
    /// For ESP32 with USB-UART bridge (CP2102, CH340, etc.)
    /// The bridge circuit typically has:
    /// - DTR -> GPIO0 (inverted)
    /// - RTS -> EN (inverted)
    /// </summary>
    private async Task ClassicResetAsync()
    {
        // Step 1: Assert RTS (EN=LOW, chip in reset), deassert DTR (GPIO0=HIGH)
        SetDTRRTS(dtr: false, rts: true);
        await Task.Delay(100); // 100ms

        // Step 2: Assert DTR (GPIO0=LOW for boot mode), deassert RTS (EN=HIGH, run)
        // Chip comes out of reset with GPIO0 low -> bootloader mode
        SetDTRRTS(dtr: true, rts: false);
        await Task.Delay(50); // 50ms

        // Step 3: Deassert DTR (GPIO0=HIGH, release boot pin)
        SetDTR(false);
        await Task.Delay(50); // 50ms
    }

    /// <summary>
    /// Perform a hard reset to run the newly flashed firmware
    /// For USB-JTAG-Serial devices, this triggers a proper chip reset
    /// that will start the application (not bootloader mode)
    /// </summary>
    public async Task HardResetAsync()
    {
        // Ensure DTR is low (GPIO9 high = normal boot mode)
        SetDTR(false);
        await Task.Delay(50); // 50ms

        // Pulse RTS to trigger reset
        SetRTS(true);
        await Task.Delay(100); // 100ms

        // Release reset - chip starts running
        SetRTS(false);
        await Task.Delay(100); // 100ms
    }

    public void Dispose()
    {
        if (_disposed) return;

        Close();
        _disposed = true;
        GC.SuppressFinalize(this);
    }
}
