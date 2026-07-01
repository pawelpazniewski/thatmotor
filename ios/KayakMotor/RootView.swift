import SwiftUI
import MapLibre
import KayakContract

/// Placeholder korzenia aplikacji (Unit 1). Mapa, HUD, sterowanie goto i waypointy
/// dokładane w kolejnych Unitach. Referuje MapLibre i KayakContract, by potwierdzić
/// linkowanie obu zależności.
struct RootView: View {
    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "location.north.line.fill")
                .font(.system(size: 48))
            Text("KayakMotor")
                .font(.largeTitle.bold())
            Text("Cienki klient firmware — mapa i tap-to-goto w budowie.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            Text(verbatim: String(
                format: "Keepalive: %.1f s (< watchdog %.1f s)",
                GotoTiming.keepaliveIntervalSeconds, GotoTiming.commsTimeoutSeconds))
                .font(.caption.monospaced())
        }
        .padding()
        .onAppear {
            // Dotknięcie typu MapLibre potwierdza linkowanie frameworka.
            _ = MLNMapView.self
        }
    }
}

#Preview {
    RootView()
}
