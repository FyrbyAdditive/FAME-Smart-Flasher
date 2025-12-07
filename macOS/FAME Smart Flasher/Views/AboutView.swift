import SwiftUI

/// About page view
struct AboutView: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 20) {
            // Company logo
            Image("CompanyLogo")
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(maxWidth: 280)

            // App name and version
            VStack(spacing: 8) {
                Text("FAME Smart Flasher")
                    .font(.title2)
                    .fontWeight(.semibold)

                Text("Version 1.0.0")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
            }

            // Description
            Text("ESP32-C3 Firmware Flasher")
                .font(.callout)
                .foregroundColor(.secondary)

            // Links
            VStack(spacing: 8) {
                Link("fyrbyadditive.com", destination: URL(string: "https://fyrbyadditive.com")!)
                Link("GitHub", destination: URL(string: "https://github.com/FyrbyAdditive/FAME-Smart-Flasher")!)
            }
            .font(.callout)

            // Copyright
            VStack(spacing: 4) {
                Text("Copyright 2025")
                    .font(.caption)
                    .foregroundColor(.secondary)

                Text("Fyrby Additive Manufacturing & Engineering")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
            }

            // Close button
            Button("OK") {
                dismiss()
            }
            .buttonStyle(.borderedProminent)
            .keyboardShortcut(.defaultAction)
        }
        .padding(32)
        .frame(width: 350, height: 420)
    }
}

#Preview {
    AboutView()
}
