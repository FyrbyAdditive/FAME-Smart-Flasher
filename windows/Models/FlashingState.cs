// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

namespace FAMESmartFlasher.Models;

/// <summary>
/// Represents the current state of the flashing process
/// </summary>
public abstract class FlashingState
{
    public static FlashingState Idle { get; } = new IdleState();
    public static FlashingState Connecting { get; } = new ConnectingState();
    public static FlashingState Syncing { get; } = new SyncingState();
    public static FlashingState ChangingBaudRate { get; } = new ChangingBaudRateState();
    public static FlashingState Erasing { get; } = new ErasingState();
    public static FlashingState Verifying { get; } = new VerifyingState();
    public static FlashingState Restarting { get; } = new RestartingState();
    public static FlashingState Complete { get; } = new CompleteState();

    public static FlashingState Flashing(double progress) => new FlashingProgressState(progress);
    public static FlashingState Error(FlashingException error) => new ErrorState(error);

    public abstract bool IsActive { get; }
    public abstract string StatusMessage { get; }

    private class IdleState : FlashingState
    {
        public override bool IsActive => false;
        public override string StatusMessage => "Ready";
    }

    private class ConnectingState : FlashingState
    {
        public override bool IsActive => true;
        public override string StatusMessage => "Connecting to device...";
    }

    private class SyncingState : FlashingState
    {
        public override bool IsActive => true;
        public override string StatusMessage => "Syncing with bootloader...";
    }

    private class ChangingBaudRateState : FlashingState
    {
        public override bool IsActive => true;
        public override string StatusMessage => "Changing baud rate...";
    }

    private class ErasingState : FlashingState
    {
        public override bool IsActive => true;
        public override string StatusMessage => "Erasing flash...";
    }

    private class FlashingProgressState : FlashingState
    {
        private readonly double _progress;

        public FlashingProgressState(double progress)
        {
            _progress = progress;
        }

        public override bool IsActive => true;
        public override string StatusMessage
        {
            get
            {
                var percent = (int)(_progress * 100);
                return $"Flashing... {percent}%";
            }
        }
    }

    private class VerifyingState : FlashingState
    {
        public override bool IsActive => true;
        public override string StatusMessage => "Verifying...";
    }

    private class RestartingState : FlashingState
    {
        public override bool IsActive => true;
        public override string StatusMessage => "Restarting device...";
    }

    private class CompleteState : FlashingState
    {
        public override bool IsActive => false;
        public override string StatusMessage => "Flash complete!";
    }

    private class ErrorState : FlashingState
    {
        private readonly FlashingException _error;

        public ErrorState(FlashingException error)
        {
            _error = error;
        }

        public override bool IsActive => false;
        public override string StatusMessage => $"Error: {_error.Message}";
    }
}

/// <summary>
/// Errors that can occur during flashing
/// </summary>
public class FlashingException : Exception
{
    public FlashingException(string message) : base(message) { }

    public static FlashingException PortNotFound() =>
        new("Serial port not found");

    public static FlashingException ConnectionFailed(string reason) =>
        new($"Connection failed: {reason}");

    public static FlashingException SyncFailed(int attempts) =>
        new($"Failed to sync after {attempts} attempts");

    public static FlashingException BaudChangeTimeout() =>
        new("Timeout changing baud rate");

    public static FlashingException FlashBeginFailed(byte status) =>
        new($"Flash begin failed (0x{status:X})");

    public static FlashingException FlashDataFailed(int blockNumber, byte status) =>
        new($"Flash data failed at block {blockNumber} (0x{status:X})");

    public static FlashingException FlashEndFailed() =>
        new("Flash end failed");

    public static FlashingException ChecksumMismatch() =>
        new("Checksum mismatch");

    public static FlashingException Timeout(string operation) =>
        new($"Timeout: {operation}");

    public static FlashingException InvalidFirmware(string reason) =>
        new($"Invalid firmware: {reason}");

    public static FlashingException PortDisconnected() =>
        new("Port disconnected");

    public static FlashingException Cancelled() =>
        new("Operation cancelled");
}
