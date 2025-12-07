import SwiftUI

/// Serial monitor panel
struct SerialMonitorView: View {
    @Environment(FlasherViewModel.self) private var viewModel

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("Serial Monitor")
                    .font(.caption)
                    .fontWeight(.medium)

                Spacer()

                // Connection status indicator
                Circle()
                    .fill(viewModel.isSerialMonitorEnabled && viewModel.selectedPort != nil ? .green : .gray)
                    .frame(width: 8, height: 8)

                Button {
                    viewModel.clearSerialOutput()
                } label: {
                    Image(systemName: "trash")
                        .font(.caption)
                }
                .buttonStyle(.borderless)
                .help("Clear output")
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(Color(nsColor: .controlBackgroundColor))

            Divider()

            // Output text
            ScrollViewReader { proxy in
                ScrollView {
                    Text(viewModel.serialMonitorOutput.isEmpty ? "No output yet..." : viewModel.serialMonitorOutput)
                        .font(.system(.caption, design: .monospaced))
                        .foregroundColor(viewModel.serialMonitorOutput.isEmpty ? .secondary : .primary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(8)
                        .textSelection(.enabled)
                        .id("bottom")
                }
                .background(Color(nsColor: .textBackgroundColor))
                .onChange(of: viewModel.serialMonitorOutput) { _, _ in
                    // Scroll without animation to avoid performance issues with fast updates
                    proxy.scrollTo("bottom", anchor: .bottom)
                }
            }
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }
}

#Preview {
    SerialMonitorView()
        .frame(width: 500, height: 200)
        .environment(FlasherViewModel())
}
