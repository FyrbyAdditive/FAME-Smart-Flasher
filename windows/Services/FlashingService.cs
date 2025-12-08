// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

using System.Diagnostics;
using System.IO.Ports;
using FAMESmartFlasher.Models;
using FAMESmartFlasher.Protocol;

namespace FAMESmartFlasher.Services;

/// <summary>
/// Service that orchestrates the ESP32 flashing process
/// Uses synchronous I/O to avoid async/await deadlock issues in WPF
/// </summary>
public class FlashingService
{
    private SerialPort? _port;
    private readonly SLIPDecoder _slipDecoder = new();
    private volatile bool _isCancelled;

    private const int SyncRetries = 20;
    private const int ResponseTimeoutMs = 5000;

    /// <summary>
    /// Flash firmware to an ESP32 device (async wrapper for compatibility)
    /// </summary>
    public Task FlashAsync(
        FirmwareFile firmware,
        SerialPortInfo port,
        BaudRate baudRate,
        IProgress<FlashingState> progress,
        CancellationToken cancellationToken = default)
    {
        return Task.Run(() => FlashSync(firmware, port, baudRate, s => progress.Report(s), cancellationToken), cancellationToken);
    }

    /// <summary>
    /// Flash firmware with a simple Action callback (no sync context issues)
    /// </summary>
    public Task FlashAsync(
        FirmwareFile firmware,
        SerialPortInfo port,
        BaudRate baudRate,
        Action<FlashingState> onProgress,
        CancellationToken cancellationToken = default)
    {
        // Run synchronous flashing on a thread pool thread
        return Task.Run(() => FlashSync(firmware, port, baudRate, onProgress, cancellationToken), cancellationToken);
    }

    /// <summary>
    /// Synchronous flash implementation - runs on background thread
    /// </summary>
    private void FlashSync(
        FirmwareFile firmware,
        SerialPortInfo port,
        BaudRate baudRate,
        Action<FlashingState> reportProgress,
        CancellationToken cancellationToken)
    {
        _isCancelled = false;

        try
        {
            // 1. Connect
            Debug.WriteLine($"[FlashingService] Opening port {port.Path}...");
            reportProgress(FlashingState.Connecting);
            OpenPort(port.Path);
            Debug.WriteLine($"[FlashingService] Port opened successfully");

            // 2. Enter bootloader mode using DTR/RTS reset sequence
            var isUSBJTAGSerial = port.IsESP32C3;
            Debug.WriteLine($"[FlashingService] Triggering bootloader entry via DTR/RTS (USB-JTAG-Serial: {isUSBJTAGSerial})");
            EnterBootloaderMode(isUSBJTAGSerial);

            // Wait for chip to enter bootloader
            Thread.Sleep(500);

            // Read any boot log output
            try
            {
                var bootData = ReadWithTimeout(100);
                if (bootData.Length > 0)
                {
                    Debug.WriteLine($"[FlashingService] Boot output (hex): {BitConverter.ToString(bootData)}");
                }
            }
            catch
            {
                // Ignore timeout
            }

            // Flush any remaining boot messages
            FlushPort();

            // 3. Try syncing without closing the port first
            var syncSucceeded = false;
            Debug.WriteLine("[FlashingService] Attempting sync without port close/reopen");

            try
            {
                reportProgress(FlashingState.Syncing);
                SyncWithRetry(cancellationToken);
                syncSucceeded = true;
                Debug.WriteLine("[FlashingService] Sync succeeded without port reopen");

                // CRITICAL: Disable watchdogs IMMEDIATELY after first sync
                if (isUSBJTAGSerial)
                {
                    Debug.WriteLine("[FlashingService] Disabling watchdogs immediately after sync");
                    DisableWatchdogs(cancellationToken);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[FlashingService] First sync attempt failed: {ex.Message}, trying with port reopen");
            }

            // If sync failed, try closing and reopening the port
            if (!syncSucceeded)
            {
                ClosePort();

                // Wait for USB re-enumeration
                Thread.Sleep(2000);

                // Try to reopen the port multiple times
                var opened = false;
                for (var attempt = 1; attempt <= 5; attempt++)
                {
                    try
                    {
                        OpenPort(port.Path);
                        opened = true;
                        Debug.WriteLine($"[FlashingService] Port reopened on attempt {attempt}");
                        break;
                    }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"[FlashingService] Port open attempt {attempt} failed: {ex.Message}");
                        if (attempt < 5)
                        {
                            Thread.Sleep(500);
                        }
                    }
                }

                if (!opened)
                {
                    throw FlashingException.ConnectionFailed("Could not reopen port after reset");
                }

                // Flush any garbage data
                FlushPort();

                // Try sync again
                reportProgress(FlashingState.Syncing);
                SyncWithRetry(cancellationToken);

                // CRITICAL: Disable watchdogs IMMEDIATELY after sync
                if (isUSBJTAGSerial)
                {
                    Debug.WriteLine("[FlashingService] Disabling watchdogs immediately after sync (retry path)");
                    DisableWatchdogs(cancellationToken);
                }
            }

            // 4. Change baud rate if needed
            if (baudRate != BaudRate.Baud115200)
            {
                reportProgress(FlashingState.ChangingBaudRate);
                ChangeBaudRate(baudRate, cancellationToken);
            }

            // 5. Attach SPI flash
            Debug.WriteLine("[FlashingService] Sending SPI_ATTACH command");
            SpiAttach(cancellationToken);

            // 6. Flash all images in the firmware package
            var totalBytes = firmware.Images.Sum(i => i.Size);
            var bytesFlashed = 0;

            Debug.WriteLine($"[FlashingService] Flashing {firmware.Images.Count} image(s), total size: {totalBytes} bytes");

            foreach (var image in firmware.Images)
            {
                CheckCancelled(cancellationToken);

                const int blockSize = ESP32Protocol.FlashBlockSize;
                var numBlocks = (image.Size + blockSize - 1) / blockSize;

                Debug.WriteLine($"[FlashingService] Flashing {image.FileName}: {image.Size} bytes at offset 0x{image.Offset:X}");
                var header = image.Data.Take(16).ToArray();
                Debug.WriteLine($"[FlashingService] Header: {BitConverter.ToString(header)}");
                if (image.Data[0] == 0xE9)
                {
                    Debug.WriteLine("[FlashingService] Valid ESP32 image detected (magic: 0xE9)");
                }

                // Begin flash for this image
                reportProgress(FlashingState.Erasing);
                FlashBegin((uint)image.Size, (uint)numBlocks, blockSize, image.Offset, cancellationToken);

                // Send data blocks
                for (var blockNum = 0; blockNum < numBlocks; blockNum++)
                {
                    CheckCancelled(cancellationToken);

                    var start = blockNum * blockSize;
                    var end = Math.Min(start + blockSize, image.Size);
                    var blockData = new byte[blockSize];

                    // Copy block data and pad with 0xFF if needed
                    Array.Copy(image.Data, start, blockData, 0, end - start);
                    if (end - start < blockSize)
                    {
                        Array.Fill(blockData, (byte)0xFF, end - start, blockSize - (end - start));
                    }

                    // Calculate overall progress across all images
                    var imageProgress = (double)(blockNum + 1) / numBlocks;
                    var overallProgress = (bytesFlashed + (imageProgress * image.Size)) / totalBytes;
                    reportProgress(FlashingState.Flashing(overallProgress));

                    FlashData(blockData, blockNum, cancellationToken);

                    // Small delay after each block to prevent USB-JTAG-Serial buffer overflow
                    Thread.Sleep(5);
                }

                bytesFlashed += image.Size;
            }

            // 7. Verify (implicit - checksums validated per block)
            reportProgress(FlashingState.Verifying);
            Thread.Sleep(100);

            // 8. Complete flashing and reboot
            reportProgress(FlashingState.Restarting);
            FlashEnd(reboot: true, isUSBJTAGSerial, cancellationToken);

            Thread.Sleep(1000); // Let device restart

            reportProgress(FlashingState.Complete);
        }
        finally
        {
            ClosePort();
        }
    }

    /// <summary>
    /// Cancel the current flash operation
    /// </summary>
    public void Cancel()
    {
        _isCancelled = true;
    }

    // MARK: - Port Management

    private void OpenPort(string portName)
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
            DtrEnable = false,
            RtsEnable = false
        };

        _port.Open();
        FlushPort();
    }

    private void ClosePort()
    {
        if (_port != null)
        {
            try
            {
                if (_port.IsOpen)
                    _port.Close();
                _port.Dispose();
            }
            catch { }
            _port = null;
        }
    }

    private void FlushPort()
    {
        if (_port?.IsOpen == true)
        {
            _port.DiscardInBuffer();
            _port.DiscardOutBuffer();
        }
    }

    private void WriteData(byte[] data)
    {
        if (_port == null || !_port.IsOpen)
            throw FlashingException.PortDisconnected();

        _port.Write(data, 0, data.Length);
    }

    private byte[] ReadWithTimeout(int timeoutMs)
    {
        if (_port == null || !_port.IsOpen)
            throw FlashingException.PortDisconnected();

        var buffer = new byte[4096];
        var oldTimeout = _port.ReadTimeout;
        _port.ReadTimeout = timeoutMs;

        try
        {
            var bytesRead = _port.Read(buffer, 0, buffer.Length);
            if (bytesRead == 0)
                return Array.Empty<byte>();

            var result = new byte[bytesRead];
            Array.Copy(buffer, result, bytesRead);
            return result;
        }
        catch (TimeoutException)
        {
            return Array.Empty<byte>();
        }
        finally
        {
            _port.ReadTimeout = oldTimeout;
        }
    }

    // MARK: - DTR/RTS Reset

    private void EnterBootloaderMode(bool isUSBJTAGSerial)
    {
        if (_port == null || !_port.IsOpen)
            throw FlashingException.PortDisconnected();

        if (isUSBJTAGSerial)
        {
            USBJTAGSerialReset();
        }
        else
        {
            ClassicReset();
        }

        FlushPort();
    }

    private void USBJTAGSerialReset()
    {
        // Step 1: Idle state - both lines deasserted
        _port!.RtsEnable = false;
        _port.DtrEnable = false;
        Thread.Sleep(100);

        // Step 2: Set IO0 (GPIO9 low for boot mode)
        _port.DtrEnable = true;
        _port.RtsEnable = false;
        Thread.Sleep(100);

        // Step 3: Reset sequence
        _port.RtsEnable = true;   // Assert reset
        _port.DtrEnable = false;  // Release IO0
        _port.RtsEnable = true;   // Set RTS again (Windows driver quirk)
        Thread.Sleep(100);

        // Step 4: Chip out of reset - both lines deasserted
        _port.DtrEnable = false;
        _port.RtsEnable = false;

        // Give the chip time to start the bootloader
        Thread.Sleep(50);
    }

    private void ClassicReset()
    {
        // Step 1: Assert RTS (EN=LOW, chip in reset), deassert DTR (GPIO0=HIGH)
        _port!.DtrEnable = false;
        _port.RtsEnable = true;
        Thread.Sleep(100);

        // Step 2: Assert DTR (GPIO0=LOW for boot mode), deassert RTS (EN=HIGH, run)
        _port.DtrEnable = true;
        _port.RtsEnable = false;
        Thread.Sleep(50);

        // Step 3: Deassert DTR (GPIO0=HIGH, release boot pin)
        _port.DtrEnable = false;
        Thread.Sleep(50);
    }

    private void HardReset()
    {
        if (_port == null || !_port.IsOpen) return;

        // Ensure DTR is low (GPIO9 high = normal boot mode)
        _port.DtrEnable = false;
        Thread.Sleep(50);

        // Pulse RTS to trigger reset
        _port.RtsEnable = true;
        Thread.Sleep(100);

        // Release reset - chip starts running
        _port.RtsEnable = false;
        Thread.Sleep(100);
    }

    // MARK: - Protocol Commands

    private void CheckCancelled(CancellationToken cancellationToken)
    {
        if (_isCancelled || cancellationToken.IsCancellationRequested)
            throw FlashingException.Cancelled();
    }

    private void SyncWithRetry(CancellationToken cancellationToken)
    {
        for (var attempt = 1; attempt <= SyncRetries; attempt++)
        {
            try
            {
                PerformSync(cancellationToken);
                return; // Success
            }
            catch
            {
                if (attempt == SyncRetries)
                {
                    throw FlashingException.SyncFailed(SyncRetries);
                }
                Thread.Sleep(50);
            }
        }
    }

    private void PerformSync(CancellationToken cancellationToken)
    {
        var syncCommand = ESP32Protocol.BuildSyncCommand();
        var slipEncoded = SLIPCodec.Encode(syncCommand);

        Debug.WriteLine($"[FlashingService] Sending SYNC command, {slipEncoded.Length} bytes");

        // Send ONE sync packet
        WriteData(slipEncoded);
        Debug.WriteLine("[FlashingService] Sent sync packet");

        // Wait for first response
        var response = WaitForResponse(ESP32Command.Sync, ResponseTimeoutMs, cancellationToken);

        Debug.WriteLine($"[FlashingService] Got response: command={response.Command}, status={response.Status}, error={response.Error}");

        if (!response.IsSuccess)
        {
            throw FlashingException.SyncFailed(1);
        }

        // Read 7 more responses to drain extra sync responses
        Debug.WriteLine("[FlashingService] Draining extra sync responses...");
        for (var i = 0; i < 7; i++)
        {
            try
            {
                WaitForResponse(ESP32Command.Sync, 100, cancellationToken);
            }
            catch
            {
                // Ignore
            }
        }

        // Flush any remaining data
        FlushPort();
    }

    private void ChangeBaudRate(BaudRate rate, CancellationToken cancellationToken)
    {
        var command = ESP32Protocol.BuildChangeBaudCommand((uint)rate, 115200);
        var encoded = SLIPCodec.Encode(command);
        WriteData(encoded);

        // Brief delay then change host baud rate
        Thread.Sleep(50);
        _port!.BaudRate = (int)rate;
        Thread.Sleep(50);

        // Sync again at new baud rate
        PerformSync(cancellationToken);
    }

    private void SpiAttach(CancellationToken cancellationToken)
    {
        var command = ESP32Protocol.BuildSpiAttachCommand();
        var encoded = SLIPCodec.Encode(command);
        WriteData(encoded);

        var response = WaitForResponse(ESP32Command.SpiAttach, ResponseTimeoutMs, cancellationToken);
        Debug.WriteLine($"[FlashingService] SPI_ATTACH response: status={response.Status}, error={response.Error}");

        if (!response.IsSuccess)
        {
            throw FlashingException.ConnectionFailed($"SPI attach failed: status={response.Status}, error={response.Error}");
        }
    }

    private void DisableWatchdogs(CancellationToken cancellationToken)
    {
        // 1. Disable RTC Watchdog
        WriteReg(ESP32C3Registers.RtcWdtWprotect, ESP32C3Registers.RtcWdtWkey, cancellationToken);
        var wdtConfig = ReadReg(ESP32C3Registers.RtcWdtConfig0, cancellationToken);
        var newWdtConfig = wdtConfig & ~ESP32C3Registers.WdtEnBit;
        WriteReg(ESP32C3Registers.RtcWdtConfig0, newWdtConfig, cancellationToken);
        WriteReg(ESP32C3Registers.RtcWdtWprotect, 0, cancellationToken);

        Debug.WriteLine($"[FlashingService] RTC WDT disabled (was 0x{wdtConfig:X}, now 0x{newWdtConfig:X})");

        // 2. Enable Super Watchdog auto-feed (effectively disables it)
        WriteReg(ESP32C3Registers.SwdWprotect, ESP32C3Registers.SwdWkey, cancellationToken);
        var swdConfig = ReadReg(ESP32C3Registers.SwdConf, cancellationToken);
        var newSwdConfig = swdConfig | ESP32C3Registers.SwdAutoFeedEnBit;
        WriteReg(ESP32C3Registers.SwdConf, newSwdConfig, cancellationToken);
        WriteReg(ESP32C3Registers.SwdWprotect, 0, cancellationToken);

        Debug.WriteLine($"[FlashingService] SWD auto-feed enabled (was 0x{swdConfig:X}, now 0x{newSwdConfig:X})");
    }

    private uint ReadReg(uint address, CancellationToken cancellationToken)
    {
        var command = ESP32Protocol.BuildReadRegCommand(address);
        var encoded = SLIPCodec.Encode(command);
        WriteData(encoded);

        var response = WaitForResponse(ESP32Command.ReadReg, ResponseTimeoutMs, cancellationToken);
        if (!response.IsSuccess)
        {
            throw FlashingException.ConnectionFailed($"READ_REG failed at 0x{address:X}");
        }
        return response.Value;
    }

    private void WriteReg(uint address, uint value, CancellationToken cancellationToken)
    {
        var command = ESP32Protocol.BuildWriteRegCommand(address, value);
        var encoded = SLIPCodec.Encode(command);
        WriteData(encoded);

        var response = WaitForResponse(ESP32Command.WriteReg, ResponseTimeoutMs, cancellationToken);
        if (!response.IsSuccess)
        {
            throw FlashingException.ConnectionFailed($"WRITE_REG failed at 0x{address:X}");
        }
    }

    private void FlashBegin(uint size, uint numBlocks, int blockSize, uint offset, CancellationToken cancellationToken)
    {
        var command = ESP32Protocol.BuildFlashBeginCommand(size, numBlocks, (uint)blockSize, offset);
        var encoded = SLIPCodec.Encode(command);
        WriteData(encoded);

        var response = WaitForResponse(ESP32Command.FlashBegin, 30000, cancellationToken); // Erase can take time
        Debug.WriteLine($"[FlashingService] FLASH_BEGIN response: status={response.Status}, error={response.Error}");

        if (!response.IsSuccess)
        {
            throw FlashingException.FlashBeginFailed(response.Status);
        }
    }

    private void FlashData(byte[] block, int sequenceNumber, CancellationToken cancellationToken)
    {
        var command = ESP32Protocol.BuildFlashDataCommand(block, (uint)sequenceNumber);
        var encoded = SLIPCodec.Encode(command);

        if (sequenceNumber == 0)
        {
            Debug.WriteLine($"[FlashingService] FLASH_DATA block 0: {encoded.Length} bytes encoded, block size: {block.Length}");
            Debug.WriteLine($"[FlashingService] First 60 bytes: {BitConverter.ToString(encoded.Take(60).ToArray())}");
        }

        WriteData(encoded);

        var response = WaitForResponse(ESP32Command.FlashData, ResponseTimeoutMs, cancellationToken);
        Debug.WriteLine($"[FlashingService] FLASH_DATA[{sequenceNumber}] response: status={response.Status}, error={response.Error}");

        if (!response.IsSuccess)
        {
            throw FlashingException.FlashDataFailed(sequenceNumber, response.Status);
        }
    }

    private void FlashEnd(bool reboot, bool isUSBJTAGSerial, CancellationToken cancellationToken)
    {
        var command = ESP32Protocol.BuildFlashEndCommand(reboot);
        var encoded = SLIPCodec.Encode(command);
        WriteData(encoded);

        Debug.WriteLine($"[FlashingService] Sent FLASH_END command (reboot={reboot})");

        // Flash end might not get a response if rebooting
        try
        {
            var response = WaitForResponse(ESP32Command.FlashEnd, 1000, cancellationToken);
            Debug.WriteLine($"[FlashingService] FLASH_END response: status={response.Status}, error={response.Error}");

            if (!response.IsSuccess && !reboot)
            {
                throw FlashingException.FlashEndFailed();
            }
        }
        catch
        {
            // Expected if rebooting
            if (!reboot)
            {
                throw;
            }
        }

        // For USB-JTAG-Serial devices, perform hard reset
        if (reboot && isUSBJTAGSerial)
        {
            Debug.WriteLine("[FlashingService] Performing hard reset for USB-JTAG-Serial device");
            HardReset();
        }
    }

    private ESP32Response WaitForResponse(ESP32Command command, int timeoutMs, CancellationToken cancellationToken)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        _slipDecoder.Reset();

        var totalBytesReceived = 0;

        while (DateTime.UtcNow < deadline)
        {
            CheckCancelled(cancellationToken);

            try
            {
                var data = ReadWithTimeout(100);

                if (data.Length > 0)
                {
                    totalBytesReceived += data.Length;
                    var preview = data.Take(50).ToArray();
                    Debug.WriteLine($"[FlashingService] Received {data.Length} bytes: {BitConverter.ToString(preview)}{(data.Length > 50 ? "..." : "")}");
                }

                var packets = _slipDecoder.Process(data);

                foreach (var packet in packets)
                {
                    Debug.WriteLine($"[FlashingService] Decoded SLIP packet: {packet.Length} bytes");

                    var response = ESP32Response.Parse(packet);
                    if (response != null && response.Command == (byte)command)
                    {
                        return response;
                    }
                }
            }
            catch (TimeoutException)
            {
                // Continue waiting
            }
        }

        Debug.WriteLine($"[FlashingService] Timeout waiting for {command}, total bytes received: {totalBytesReceived}");
        throw FlashingException.Timeout($"waiting for {command} response");
    }
}
