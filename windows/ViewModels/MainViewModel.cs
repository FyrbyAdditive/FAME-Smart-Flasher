// FAME Smart Flasher - C# Port
// Copyright 2025 Fyrby Additive Manufacturing & Engineering
// SPDX-License-Identifier: Proprietary

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using FAMESmartFlasher.Models;
using FAMESmartFlasher.Services;
using FAMESmartFlasher.Serial;
using Microsoft.Win32;

namespace FAMESmartFlasher.ViewModels;

/// <summary>
/// Main view model for the flasher application
/// Uses CommunityToolkit.MVVM for property change notifications and commands
/// </summary>
public partial class MainViewModel : ObservableObject
{
    // MARK: - Observable Properties

    [ObservableProperty]
    private SerialPortInfo? _selectedPort;

    [ObservableProperty]
    private BaudRate _selectedBaudRate = BaudRate.Baud115200;

    [ObservableProperty]
    private FirmwareFile? _firmwareFile;

    [ObservableProperty]
    private FlashingState _flashingState = FlashingState.Idle;

    [ObservableProperty]
    private double _progress;

    [ObservableProperty]
    private bool _isSerialMonitorEnabled;

    [ObservableProperty]
    private string _serialMonitorOutput = string.Empty;

    [ObservableProperty]
    private bool _isSerialMonitorConnected;

    public ObservableCollection<SerialPortInfo> AvailablePorts { get; } = new();
    public ObservableCollection<BaudRate> BaudRates { get; } = new()
    {
        BaudRate.Baud115200,
        BaudRate.Baud230400,
        BaudRate.Baud460800,
        BaudRate.Baud921600
    };

    // MARK: - Dependencies

    private readonly FlashingService _flashingService = new();
    private readonly System.Threading.Timer _portRefreshTimer;
    private readonly object _serialMonitorLock = new();
    private Task? _serialMonitorTask;
    private SerialConnection? _serialMonitorConnection;
    private CancellationTokenSource? _serialMonitorCts;
    private volatile bool _isConnecting;

    private const int PortRefreshIntervalMs = 1000;

    // MARK: - Computed Properties

    public bool CanFlash => SelectedPort != null && FirmwareFile != null && FirmwareFile.IsValid && !FlashingState.IsActive;
    public string StatusMessage => FlashingState.StatusMessage;

    // MARK: - Initialization

    public MainViewModel()
    {
        // Refresh ports immediately and start periodic refresh
        RefreshPorts();
        _portRefreshTimer = new System.Threading.Timer(
            _ => Application.Current?.Dispatcher.Invoke(RefreshPorts),
            null,
            PortRefreshIntervalMs,
            PortRefreshIntervalMs);

        // Watch for property changes
        PropertyChanged += OnPropertyChanged;
    }

    private void OnPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(SelectedPort))
        {
            OnPropertyChanged(nameof(CanFlash));

            // Reconnect serial monitor if enabled and not flashing
            if (IsSerialMonitorEnabled && !FlashingState.IsActive)
            {
                Task.Run(async () =>
                {
                    await DisconnectSerialMonitorAsync();
                    await ConnectSerialMonitorAsync();
                });
            }
        }
        else if (e.PropertyName == nameof(FirmwareFile))
        {
            OnPropertyChanged(nameof(CanFlash));
        }
        else if (e.PropertyName == nameof(FlashingState))
        {
            OnPropertyChanged(nameof(CanFlash));
            OnPropertyChanged(nameof(StatusMessage));

            // Update progress based on state
            if (!FlashingState.IsActive)
            {
                if (FlashingState.StatusMessage.Contains("complete", StringComparison.OrdinalIgnoreCase))
                    Progress = 1.0;
                else
                    Progress = 0.0;
            }
        }
        else if (e.PropertyName == nameof(IsSerialMonitorEnabled))
        {
            // Handle serial monitor toggle
            if (FlashingState.IsActive) return;

            if (IsSerialMonitorEnabled)
            {
                Task.Run(ConnectSerialMonitorAsync);
            }
            else
            {
                Task.Run(DisconnectSerialMonitorAsync);
            }
        }
    }

    // MARK: - Commands

    [RelayCommand]
    private void SelectFirmware()
    {
        var dialog = new OpenFileDialog
        {
            Title = "Select Firmware File or PlatformIO Build Directory",
            Filter = "Firmware Files (*.bin)|*.bin|All Files (*.*)|*.*",
            CheckFileExists = true
        };

        if (dialog.ShowDialog() == true)
        {
            try
            {
                var path = dialog.FileName;
                var isDirectory = Directory.Exists(path);

                if (isDirectory)
                {
                    // Directory selected - try to load as PlatformIO build
                    FirmwareFile = FirmwareFile.FromPlatformIOBuild(path);
                }
                else
                {
                    // Single file selected
                    var data = File.ReadAllBytes(path);
                    FirmwareFile = new FirmwareFile(path, data);
                }

                if (!FirmwareFile.IsValid)
                {
                    var debugMsg = $"Invalid firmware. Images: {FirmwareFile.Images.Count}";
                    foreach (var img in FirmwareFile.Images)
                    {
                        debugMsg += $", {img.FileName}@0x{img.Offset:X} first=0x{(img.Data.Length > 0 ? img.Data[0] : 0):X2}";
                    }
                    System.Diagnostics.Debug.WriteLine($"[SelectFirmware] {debugMsg}");
                    FlashingState = FlashingState.Error(
                        FlashingException.InvalidFirmware($"Invalid firmware: first byte is 0x{(FirmwareFile.Images.FirstOrDefault()?.Data.FirstOrDefault() ?? 0):X2}, expected 0xE9"));
                }
                else
                {
                    System.Diagnostics.Debug.WriteLine($"[SelectFirmware] Valid firmware loaded: {FirmwareFile.FileName}, {FirmwareFile.Images.Count} images");
                    FlashingState = FlashingState.Idle;
                }
            }
            catch (Exception ex)
            {
                FlashingState = FlashingState.Error(
                    FlashingException.InvalidFirmware(ex.Message));
            }
        }
    }

    [RelayCommand]
    private void SelectDirectory()
    {
        var dialog = new Microsoft.Win32.OpenFileDialog
        {
            Title = "Select PlatformIO Build Directory",
            CheckFileExists = false,
            FileName = "Select Folder"
        };

        if (dialog.ShowDialog() == true)
        {
            try
            {
                var directory = Path.GetDirectoryName(dialog.FileName);
                if (directory != null)
                {
                    FirmwareFile = FirmwareFile.FromPlatformIOBuild(directory);

                    if (!FirmwareFile.IsValid)
                    {
                        var debugMsg = $"Invalid firmware from dir. Images: {FirmwareFile.Images.Count}";
                        foreach (var img in FirmwareFile.Images)
                        {
                            debugMsg += $", {img.FileName}@0x{img.Offset:X} first=0x{(img.Data.Length > 0 ? img.Data[0] : 0):X2}";
                        }
                        System.Diagnostics.Debug.WriteLine($"[SelectDirectory] {debugMsg}");
                        FlashingState = FlashingState.Error(
                            FlashingException.InvalidFirmware($"Invalid firmware: check that firmware.bin starts with 0xE9"));
                    }
                    else
                    {
                        System.Diagnostics.Debug.WriteLine($"[SelectDirectory] Valid firmware loaded: {FirmwareFile.FileName}, {FirmwareFile.Images.Count} images");
                        FlashingState = FlashingState.Idle;
                    }
                }
            }
            catch (Exception ex)
            {
                FlashingState = FlashingState.Error(
                    FlashingException.InvalidFirmware(ex.Message));
            }
        }
    }

    [RelayCommand]
    private void StartFlashing()
    {
        if (SelectedPort == null || FirmwareFile == null || !FirmwareFile.IsValid)
        {
            FlashingState = FlashingState.Error(FlashingException.InvalidFirmware("Invalid port or firmware"));
            return;
        }

        // Remember if serial monitor was enabled so we can reconnect after
        var shouldReconnectSerialMonitor = IsSerialMonitorEnabled;

        // Force disconnect serial monitor synchronously (but keep UI state)
        if (_serialMonitorConnection != null)
        {
            SerialMonitorOutput += "[Disconnecting for flash...]\n";
            try
            {
                _serialMonitorCts?.Cancel();
                _serialMonitorConnection?.Close();
                _serialMonitorConnection?.Dispose();
            }
            catch { }
            _serialMonitorConnection = null;
            _serialMonitorTask = null;
            _serialMonitorCts = null;
            IsSerialMonitorConnected = false;
        }

        // Capture values for background thread
        var firmware = FirmwareFile;
        var port = SelectedPort;
        var baudRate = SelectedBaudRate;

        FlashingState = FlashingState.Connecting;
        Progress = 0;

        // Start flashing using Task.Run which provides proper async context
        _ = Task.Run(async () =>
        {
            // Wait for port to be released
            await Task.Delay(2000).ConfigureAwait(false);

            try
            {
                // Simple callback to update UI
                void UpdateUI(FlashingState state)
                {
                    Application.Current.Dispatcher.BeginInvoke(() =>
                    {
                        FlashingState = state;
                        // Extract progress percentage from status message
                        if (state.StatusMessage.Contains("%"))
                        {
                            var match = System.Text.RegularExpressions.Regex.Match(state.StatusMessage, @"(\d+)%");
                            if (match.Success && int.TryParse(match.Groups[1].Value, out var pct))
                            {
                                Progress = pct / 100.0;
                            }
                        }
                    });
                }

                // Run flash with ConfigureAwait(false) to prevent sync context issues
                await _flashingService.FlashAsync(firmware, port, baudRate, UpdateUI, CancellationToken.None).ConfigureAwait(false);

                Application.Current.Dispatcher.BeginInvoke(() =>
                {
                    FlashingState = FlashingState.Complete;
                    Progress = 1.0;

                    // Reconnect serial monitor if it was enabled before flashing
                    if (shouldReconnectSerialMonitor)
                    {
                        SerialMonitorOutput += "[Flash complete, reconnecting...]\n";
                        _ = Task.Run(ConnectSerialMonitorAsync);
                    }
                });
            }
            catch (Exception ex)
            {
                var msg = ex is AggregateException aex ? aex.InnerException?.Message ?? ex.Message : ex.Message;
                Application.Current.Dispatcher.BeginInvoke(() =>
                {
                    FlashingState = FlashingState.Error(FlashingException.ConnectionFailed(msg));
                    SerialMonitorOutput += $"[Flash failed: {msg}]\n";

                    // Still try to reconnect serial monitor on failure
                    if (shouldReconnectSerialMonitor)
                    {
                        SerialMonitorOutput += "[Attempting to reconnect...]\n";
                        _ = Task.Run(ConnectSerialMonitorAsync);
                    }
                });
            }
        });
    }

    [RelayCommand]
    private void CancelFlashing()
    {
        _flashingService.Cancel();
        FlashingState = FlashingState.Idle;
    }

    [RelayCommand]
    private void ClearSerialOutput()
    {
        SerialMonitorOutput = string.Empty;
    }

    [RelayCommand]
    private void RefreshPorts()
    {
        // Don't refresh ports while serial monitor is connected
        // WMI queries interfere with the open connection
        if (IsSerialMonitorConnected)
            return;

        var ports = SerialPortManager.GetAvailablePorts();
        var currentPath = SelectedPort?.Path;

        AvailablePorts.Clear();
        foreach (var port in ports)
        {
            AvailablePorts.Add(port);
        }

        // Restore selection if port still exists
        if (currentPath != null)
        {
            SelectedPort = ports.FirstOrDefault(p => p.Path == currentPath);
        }

        // Auto-select ESP32-C3 if available
        if (SelectedPort == null)
        {
            SelectedPort = ports.FirstOrDefault(p => p.IsESP32C3) ?? ports.FirstOrDefault();
        }
    }

    // MARK: - Private Methods

    private async Task ConnectSerialMonitorAsync()
    {
        // Prevent concurrent connection attempts
        lock (_serialMonitorLock)
        {
            if (_isConnecting)
            {
                System.Diagnostics.Debug.WriteLine("[MainViewModel] Already connecting, skipping");
                return;
            }
            _isConnecting = true;
        }

        try
        {
            if (SelectedPort == null)
            {
                Application.Current.Dispatcher.BeginInvoke(() =>
                {
                    SerialMonitorOutput += "[No port selected]\n";
                });
                return;
            }

            if (FlashingState.IsActive) return;

            // Disconnect any existing connection first
            await DisconnectSerialMonitorInternalAsync();

            // Small delay to ensure port is fully released
            await Task.Delay(200);

            var portPath = SelectedPort.Path;
            var portName = SelectedPort.Name;
            var connection = new SerialConnection();
            _serialMonitorConnection = connection;
            _serialMonitorCts = new CancellationTokenSource();
            var ct = _serialMonitorCts.Token;

            _serialMonitorTask = Task.Run(async () =>
            {
                try
                {
                    await connection.OpenAsync(portPath);
                    await connection.SetBaudRateAsync(BaudRate.Baud115200);

                    Application.Current.Dispatcher.BeginInvoke(() =>
                    {
                        SerialMonitorOutput += $"[Connected to {portName}]\n";
                        IsSerialMonitorConnected = true;
                    });

                    var pendingText = string.Empty;
                    var lastUpdateTime = DateTime.UtcNow;
                    var updateInterval = TimeSpan.FromMilliseconds(100);

                    while (!ct.IsCancellationRequested)
                    {
                        byte[] data;
                        try
                        {
                            data = await connection.ReadAsync(100); // 100ms timeout
                        }
                        catch (IOException)
                        {
                            // Port disconnected - exit loop silently
                            break;
                        }
                        catch (InvalidOperationException)
                        {
                            // Port closed - exit loop silently
                            break;
                        }
                        catch (SerialException)
                        {
                            // Serial error - exit loop silently
                            break;
                        }
                        catch
                        {
                            // Unknown error - exit loop silently
                            break;
                        }

                        // Process data
                        if (data.Length > 0)
                        {
                            var text = System.Text.Encoding.UTF8.GetString(data);
                            pendingText += text;
                        }

                        // Batch updates
                        var now = DateTime.UtcNow;
                        if (pendingText.Length > 0 && now - lastUpdateTime >= updateInterval)
                        {
                            var textToAdd = pendingText;
                            pendingText = string.Empty;
                            lastUpdateTime = now;

                            Application.Current.Dispatcher.BeginInvoke(() =>
                            {
                                SerialMonitorOutput += textToAdd;

                                // Limit buffer size
                                if (SerialMonitorOutput.Length > 50000)
                                {
                                    SerialMonitorOutput = SerialMonitorOutput[^40000..];
                                }
                            });
                        }
                    }

                    // Flush any remaining text
                    if (pendingText.Length > 0)
                    {
                        var textToAdd = pendingText;
                        Application.Current.Dispatcher.BeginInvoke(() =>
                        {
                            SerialMonitorOutput += textToAdd;
                        });
                    }
                }
                catch (OperationCanceledException)
                {
                    // Normal cancellation - don't show error
                }
                catch (Exception ex)
                {
                    // Check if cancellation was requested before processing the error
                    if (!ct.IsCancellationRequested)
                    {
                        Application.Current.Dispatcher.BeginInvoke(() =>
                        {
                            SerialMonitorOutput += $"[Connection error: {ex.Message}]\n";
                            // Disable serial monitor on error to prevent reconnection loops
                            IsSerialMonitorEnabled = false;
                        });
                    }
                }
                finally
                {
                    try { connection.Close(); } catch { }
                    Application.Current.Dispatcher.BeginInvoke(() =>
                    {
                        IsSerialMonitorConnected = false;
                    });
                }
            }, ct);
        }
        finally
        {
            _isConnecting = false;
        }
    }

    private async Task DisconnectSerialMonitorAsync()
    {
        await DisconnectSerialMonitorInternalAsync();
        // Give the system time to fully release the port
        await Task.Delay(500);
    }

    private async Task DisconnectSerialMonitorInternalAsync()
    {
        // Cancel the token first
        _serialMonitorCts?.Cancel();

        // Close and dispose the connection - this will cause ReadAsync to fail and unblock the task
        var connection = _serialMonitorConnection;
        _serialMonitorConnection = null;

        if (connection != null)
        {
            try
            {
                connection.Close();
                connection.Dispose();
            }
            catch { }
        }

        // Wait for the task to complete with a timeout
        var task = _serialMonitorTask;
        _serialMonitorTask = null;

        if (task != null)
        {
            try
            {
                // Wait max 1 second for task to complete
                await Task.WhenAny(task, Task.Delay(1000));
            }
            catch { }
        }

        _serialMonitorCts?.Dispose();
        _serialMonitorCts = null;

        Application.Current.Dispatcher.BeginInvoke(() =>
        {
            IsSerialMonitorConnected = false;
        });
    }

    public void Cleanup()
    {
        _portRefreshTimer?.Dispose();

        // Force disconnect with timeout
        var disconnectTask = DisconnectSerialMonitorAsync();
        if (!disconnectTask.Wait(TimeSpan.FromSeconds(2)))
        {
            // Force cleanup if disconnect takes too long
            _serialMonitorCts?.Cancel();
            _serialMonitorConnection?.Close();
        }
    }
}
