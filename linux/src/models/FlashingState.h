// FAME Smart Flasher - Linux Qt Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

#ifndef FLASHINGSTATE_H
#define FLASHINGSTATE_H

#include <QString>
#include <cstdint>

/**
 * Represents the current state of the flashing process
 */
enum class FlashingStateType {
    Idle,
    Connecting,
    Syncing,
    ChangingBaudRate,
    Erasing,
    Flashing,
    Verifying,
    Restarting,
    Complete,
    Error
};

/**
 * Errors that can occur during flashing
 */
enum class FlashingErrorType {
    None,
    PortNotFound,
    ConnectionFailed,
    SyncFailed,
    BaudChangeTimeout,
    FlashBeginFailed,
    FlashDataFailed,
    FlashEndFailed,
    ChecksumMismatch,
    Timeout,
    InvalidFirmware,
    PortDisconnected,
    Cancelled
};

/**
 * Flashing state with associated data
 */
struct FlashingState {
    FlashingStateType type = FlashingStateType::Idle;
    double progress = 0.0;
    FlashingErrorType errorType = FlashingErrorType::None;
    QString errorMessage;
    int errorData = 0;

    static FlashingState idle() {
        return FlashingState{FlashingStateType::Idle};
    }

    static FlashingState connecting() {
        return FlashingState{FlashingStateType::Connecting};
    }

    static FlashingState syncing() {
        return FlashingState{FlashingStateType::Syncing};
    }

    static FlashingState changingBaudRate() {
        return FlashingState{FlashingStateType::ChangingBaudRate};
    }

    static FlashingState erasing() {
        return FlashingState{FlashingStateType::Erasing};
    }

    static FlashingState flashing(double progress) {
        FlashingState state{FlashingStateType::Flashing};
        state.progress = progress;
        return state;
    }

    static FlashingState verifying() {
        return FlashingState{FlashingStateType::Verifying};
    }

    static FlashingState restarting() {
        return FlashingState{FlashingStateType::Restarting};
    }

    static FlashingState complete() {
        return FlashingState{FlashingStateType::Complete};
    }

    static FlashingState error(FlashingErrorType type, const QString& message = "", int data = 0) {
        FlashingState state{FlashingStateType::Error};
        state.errorType = type;
        state.errorMessage = message;
        state.errorData = data;
        return state;
    }

    bool isActive() const {
        switch (type) {
        case FlashingStateType::Idle:
        case FlashingStateType::Complete:
        case FlashingStateType::Error:
            return false;
        default:
            return true;
        }
    }

    QString statusMessage() const {
        switch (type) {
        case FlashingStateType::Idle:
            return "Ready";
        case FlashingStateType::Connecting:
            return "Connecting to device...";
        case FlashingStateType::Syncing:
            return "Syncing with bootloader...";
        case FlashingStateType::ChangingBaudRate:
            return "Changing baud rate...";
        case FlashingStateType::Erasing:
            return "Erasing flash...";
        case FlashingStateType::Flashing:
            return QString("Flashing... %1%").arg(static_cast<int>(progress * 100));
        case FlashingStateType::Verifying:
            return "Verifying...";
        case FlashingStateType::Restarting:
            return "Restarting device...";
        case FlashingStateType::Complete:
            return "Flash complete!";
        case FlashingStateType::Error:
            return errorDescription();
        }
        return "Unknown";
    }

    QString errorDescription() const {
        if (type != FlashingStateType::Error) {
            return "";
        }

        switch (errorType) {
        case FlashingErrorType::None:
            return "";
        case FlashingErrorType::PortNotFound:
            return "Serial port not found";
        case FlashingErrorType::ConnectionFailed:
            return QString("Connection failed: %1").arg(errorMessage);
        case FlashingErrorType::SyncFailed:
            return QString("Failed to sync after %1 attempts").arg(errorData);
        case FlashingErrorType::BaudChangeTimeout:
            return "Timeout changing baud rate";
        case FlashingErrorType::FlashBeginFailed:
            return QString("Flash begin failed (0x%1)").arg(errorData, 2, 16, QChar('0'));
        case FlashingErrorType::FlashDataFailed:
            return QString("Flash data failed at block %1").arg(errorData);
        case FlashingErrorType::FlashEndFailed:
            return "Flash end failed";
        case FlashingErrorType::ChecksumMismatch:
            return "Checksum mismatch";
        case FlashingErrorType::Timeout:
            return QString("Timeout: %1").arg(errorMessage);
        case FlashingErrorType::InvalidFirmware:
            return QString("Invalid firmware: %1").arg(errorMessage);
        case FlashingErrorType::PortDisconnected:
            return "Port disconnected";
        case FlashingErrorType::Cancelled:
            return "Operation cancelled";
        }
        return "Unknown error";
    }
};

#endif // FLASHINGSTATE_H
