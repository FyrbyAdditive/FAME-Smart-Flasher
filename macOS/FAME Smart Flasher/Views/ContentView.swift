import SwiftUI

/// Main content view
struct ContentView: View {
    @Environment(FlasherViewModel.self) private var viewModel

    var body: some View {
        VStack(spacing: 0) {
            // Main content
            FlasherView()
                .padding()

            // Serial monitor (if enabled)
            if viewModel.isSerialMonitorEnabled {
                Divider()
                SerialMonitorView()
                    .frame(height: 180)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .frame(minWidth: 450, idealWidth: 500, maxWidth: 600)
        .frame(minHeight: viewModel.isSerialMonitorEnabled ? 600 : 450)
        .background(Color(nsColor: .windowBackgroundColor))
        .animation(.easeInOut(duration: 0.2), value: viewModel.isSerialMonitorEnabled)
        .navigationTitle("FAME Smart Flasher")
    }
}

#Preview {
    ContentView()
        .environment(FlasherViewModel())
}
