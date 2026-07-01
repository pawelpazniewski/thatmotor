import SwiftUI
import KayakContract

/// Korzeń aplikacji (Unit 5): mapa offline z markerem łodzi + status łącza i
/// atrybucja. Sterowanie goto/waypointy dokładane w kolejnych Unitach.
struct RootView: View {
    @State private var store = TelemetryStore()

    var body: some View {
        ZStack(alignment: .top) {
            LakeMapView(boat: store.boat)
                .ignoresSafeArea()

            HStack {
                LinkBadge(state: store.linkState)
                Spacer()
                if let reason = store.gotoBlockReason {
                    Text(reason.message)
                        .font(.caption.bold())
                        .padding(.horizontal, 8).padding(.vertical, 4)
                        .background(.orange.opacity(0.85), in: Capsule())
                }
            }
            .padding()

            VStack {
                Spacer()
                HStack {
                    Spacer()
                    AttributionOverlay().padding(8)
                }
            }
        }
        .task {
            store.start()
        }
    }
}

private struct LinkBadge: View {
    let state: LinkState

    private var label: String {
        switch state {
        case .disconnected: return "Rozłączono"
        case .joining: return "Łączenie…"
        case .connected: return "Połączono"
        case .stale: return "Brak danych"
        }
    }

    private var color: Color {
        switch state {
        case .connected: return .green
        case .joining: return .yellow
        case .stale: return .orange
        case .disconnected: return .red
        }
    }

    var body: some View {
        Label(label, systemImage: "dot.radiowaves.left.and.right")
            .font(.caption.bold())
            .foregroundStyle(.white)
            .padding(.horizontal, 8).padding(.vertical, 4)
            .background(color.opacity(0.85), in: Capsule())
    }
}

#Preview {
    RootView()
}
