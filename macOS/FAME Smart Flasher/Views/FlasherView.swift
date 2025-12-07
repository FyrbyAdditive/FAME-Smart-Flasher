import SwiftUI

/// Main flashing interface
struct FlasherView: View {
    @Environment(FlasherViewModel.self) private var viewModel
    @State private var showAdvanced = false

    var body: some View {
        @Bindable var vm = viewModel

        VStack(spacing: 16) {
            // Port Selection
            PortSelectionView()

            // Firmware Selection
            FirmwareSelectionView()

            // Advanced Settings (collapsible)
            DisclosureGroup("Advanced Settings", isExpanded: $showAdvanced) {
                BaudRateSelectionView()
                    .padding(.top, 8)
            }
            .disabled(viewModel.flashingState.isActive)

            Spacer()
                .frame(height: 8)

            // Progress Section
            ProgressSection()

            // Status Message
            StatusMessageView()

            Spacer()
                .frame(height: 8)

            // Flash Button
            FlashButton()

            // Serial Monitor Toggle
            Toggle("Show Serial Monitor", isOn: Binding(
                get: { viewModel.isSerialMonitorEnabled },
                set: { _ in
                    Task {
                        await viewModel.toggleSerialMonitor()
                    }
                }
            ))
            .toggleStyle(.checkbox)
            .disabled(viewModel.flashingState.isActive)
        }
    }
}

/// Port selection dropdown with refresh button
struct PortSelectionView: View {
    @Environment(FlasherViewModel.self) private var viewModel

    var body: some View {
        @Bindable var vm = viewModel

        HStack {
            Text("USB Port")
                .frame(width: 80, alignment: .leading)

            Picker("", selection: $vm.selectedPort) {
                Text("Select port...").tag(nil as SerialPort?)
                ForEach(viewModel.portManager.availablePorts) { port in
                    HStack {
                        Text(port.displayName)
                        if port.isESP32C3 {
                            Text("(ESP32-C3)")
                                .foregroundColor(.secondary)
                                .font(.caption)
                        }
                    }
                    .tag(port as SerialPort?)
                }
            }
            .labelsHidden()
            .disabled(viewModel.flashingState.isActive)

            Button {
                viewModel.portManager.refreshPorts()
            } label: {
                Image(systemName: "arrow.clockwise")
            }
            .buttonStyle(.borderless)
            .disabled(viewModel.flashingState.isActive)
            .help("Refresh ports")
        }
    }
}

/// Baud rate selection
struct BaudRateSelectionView: View {
    @Environment(FlasherViewModel.self) private var viewModel

    var body: some View {
        @Bindable var vm = viewModel

        HStack {
            Text("Baud Rate")
                .frame(width: 80, alignment: .leading)

            Picker("", selection: $vm.selectedBaudRate) {
                ForEach(BaudRate.allCases) { rate in
                    Text(rate.displayName).tag(rate)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .disabled(viewModel.flashingState.isActive)
        }
    }
}

/// Firmware file selection
struct FirmwareSelectionView: View {
    @Environment(FlasherViewModel.self) private var viewModel

    var body: some View {
        HStack {
            Text("Firmware")
                .frame(width: 80, alignment: .leading)

            VStack(alignment: .leading, spacing: 4) {
                Button {
                    Task {
                        await viewModel.selectFirmware()
                    }
                } label: {
                    HStack {
                        Image(systemName: "doc.badge.plus")
                        Text(viewModel.firmwareFile?.fileName ?? "Select File...")
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(Color(nsColor: .controlBackgroundColor))
                    .cornerRadius(6)
                }
                .buttonStyle(.plain)
                .disabled(viewModel.flashingState.isActive)

                if let firmware = viewModel.firmwareFile {
                    Text(firmware.sizeDescription)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
        }
    }
}

/// Progress indicator section
struct ProgressSection: View {
    @Environment(FlasherViewModel.self) private var viewModel

    var body: some View {
        VStack(spacing: 8) {
            ProgressView(value: viewModel.progress)
                .progressViewStyle(.linear)

            if viewModel.flashingState.isActive {
                Text("\(Int(viewModel.progress * 100))%")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .opacity(viewModel.flashingState.isActive || viewModel.flashingState == .complete ? 1 : 0.3)
    }
}

/// Status message display
struct StatusMessageView: View {
    @Environment(FlasherViewModel.self) private var viewModel

    var body: some View {
        HStack {
            statusIcon
                .foregroundColor(statusColor)

            Text(viewModel.statusMessage)
                .font(.callout)
                .foregroundColor(statusColor)

            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(statusBackgroundColor)
        .cornerRadius(8)
    }

    private var statusIcon: Image {
        switch viewModel.flashingState {
        case .idle:
            return Image(systemName: "circle")
        case .connecting, .syncing, .changingBaudRate:
            return Image(systemName: "antenna.radiowaves.left.and.right")
        case .erasing:
            return Image(systemName: "trash")
        case .flashing:
            return Image(systemName: "bolt.fill")
        case .verifying:
            return Image(systemName: "checkmark.circle")
        case .restarting:
            return Image(systemName: "arrow.clockwise")
        case .complete:
            return Image(systemName: "checkmark.circle.fill")
        case .error:
            return Image(systemName: "exclamationmark.triangle.fill")
        }
    }

    private var statusColor: Color {
        switch viewModel.flashingState {
        case .complete:
            return .green
        case .error:
            return .red
        default:
            return .primary
        }
    }

    private var statusBackgroundColor: Color {
        switch viewModel.flashingState {
        case .complete:
            return .green.opacity(0.1)
        case .error:
            return .red.opacity(0.1)
        default:
            return Color(nsColor: .controlBackgroundColor)
        }
    }
}

/// Flash button
struct FlashButton: View {
    @Environment(FlasherViewModel.self) private var viewModel

    var body: some View {
        Button {
            if viewModel.flashingState.isActive {
                viewModel.cancelFlashing()
            } else {
                Task {
                    await viewModel.startFlashing()
                }
            }
        } label: {
            HStack {
                if viewModel.flashingState.isActive {
                    ProgressView()
                        .scaleEffect(0.7)
                        .frame(width: 20, height: 20)
                    Text("Cancel")
                } else {
                    Image(systemName: "bolt.fill")
                    Text("Flash Firmware")
                }
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, 12)
        }
        .buttonStyle(.borderedProminent)
        .tint(viewModel.flashingState.isActive ? .red : .accentColor)
        .disabled(!viewModel.canFlash && !viewModel.flashingState.isActive)
    }
}

#Preview {
    FlasherView()
        .padding()
        .frame(width: 500)
        .environment(FlasherViewModel())
}
