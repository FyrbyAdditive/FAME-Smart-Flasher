import SwiftUI

@main
struct FAMESmartFlasherApp: App {
    @State private var flasherViewModel = FlasherViewModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environment(flasherViewModel)
                .toolbar {
                    ToolbarItem(placement: .automatic) {
                        AboutButton()
                    }
                }
        }
        .windowResizability(.contentSize)
        .defaultSize(width: 500, height: 650)

        Window("About FAME Smart Flasher", id: "about") {
            AboutView()
        }
        .windowStyle(.hiddenTitleBar)
        .windowResizability(.contentSize)
        .defaultSize(width: 400, height: 350)
    }
}

/// About button for the toolbar
struct AboutButton: View {
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Button {
            openWindow(id: "about")
        } label: {
            Image(systemName: "info.circle")
        }
        .help("About")
    }
}
