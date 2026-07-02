import SwiftUI
import KayakContract

/// Korzeń aplikacji: mapa offline + status łącza + tap-to-goto + STOP/Rozbrój +
/// waypointy + ostrzeżenie geofence. Cienki klient — cała logika ruchu w firmware.
struct RootView: View {
    @State private var model = AppModel()
    @State private var showWaypoints = false
    @State private var camera = MapCameraController()

    var body: some View {
        ZStack(alignment: .top) {
            LakeMapView(
                boat: model.telemetry.boat,
                target: model.target.stagedCoordinate,
                camera: camera,
                onTap: { model.handleMapTap($0) }
            )
            .ignoresSafeArea()

            topBar

            zoomControls

            bottomPanel
        }
        .confirmationDialog(
            "Ten punkt wygląda na ląd / poza jeziorem — na pewno?",
            isPresented: $model.showGeofenceWarning,
            titleVisibility: .visible
        ) {
            Button("Płyń mimo to", role: .destructive) { model.confirmGeofencedGoto() }
            Button("Anuluj", role: .cancel) {}
        }
        .sheet(isPresented: $showWaypoints) {
            WaypointListView(
                waypoints: model.waypointList,
                canSave: model.canSaveWaypoint,
                onSave: { model.saveCurrentPosition(name: $0) },
                onSelect: { model.selectWaypoint($0) },
                onDelete: { model.deleteWaypoint($0) }
            )
        }
        .task { model.start() }
    }

    private var topBar: some View {
        HStack {
            LinkBadge(state: model.telemetry.linkState)
            Spacer()
            Button { showWaypoints = true } label: {
                Image(systemName: "mappin.and.ellipse")
                    .font(.title3)
                    .frame(width: SunlightTheme.minHitTarget, height: SunlightTheme.minHitTarget)
                    .background(SunlightTheme.panelBackground, in: Circle())
                    .foregroundStyle(.white)
            }
        }
        .padding()
    }

    private var zoomControls: some View {
        VStack(spacing: 12) {
            zoomButton("plus") { camera.zoomIn() }
            zoomButton("minus") { camera.zoomOut() }
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .trailing)
    }

    private func zoomButton(_ systemName: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.title2.bold())
                .frame(width: SunlightTheme.minHitTarget, height: SunlightTheme.minHitTarget)
                .background(SunlightTheme.panelBackground, in: Circle())
                .foregroundStyle(.white)
        }
    }

    private var bottomPanel: some View {
        VStack {
            Spacer()
            if model.telemetry.latest?.gotoArrived == true {
                ArrivalHintView()
            }
            GotoControlsView(
                hasTarget: model.target.staged != nil,
                sendState: model.target.sendState,
                telemetry: model.telemetry.latest,
                blockReason: model.telemetry.gotoBlockReason,
                onGoto: { model.requestGoto() }
            )
            HStack {
                SafetyControlsView(
                    onStop: { model.stopGoto() },
                    onDisarm: { model.disarm() }
                )
                Spacer()
                AttributionOverlay()
            }
        }
        .padding()
        .background(
            LinearGradient(colors: [.clear, SunlightTheme.panelBackground],
                           startPoint: .top, endPoint: .bottom)
                .ignoresSafeArea()
                .allowsHitTesting(false)
        )
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
