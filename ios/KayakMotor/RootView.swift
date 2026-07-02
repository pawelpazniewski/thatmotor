import SwiftUI
import KayakContract

/// Korzeń aplikacji: mapa offline + status łącza + tap-to-goto + STOP/Rozbrój +
/// waypointy + ostrzeżenie geofence. Cienki klient — cała logika ruchu w firmware.
struct RootView: View {
    @State private var model = AppModel()
    @State private var showWaypoints = false
    @State private var camera = MapCameraController()
    @State private var showSplash = true
    @Environment(\.scenePhase) private var scenePhase

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
            console

            if showSplash {
                SplashView(statusText: splashStatus)
                    .transition(.opacity)
                    .zIndex(1)
            }
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
        .task {
            model.start()
            try? await Task.sleep(for: .seconds(2.2))
            hideSplash()
        }
        .onChange(of: model.telemetry.latest != nil) { _, hasData in
            if hasData { hideSplash() }
        }
        .onChange(of: scenePhase) { _, phase in
            // Powrót na pierwszy plan: reconnect WS + resync (iOS zawiesza socket w tle).
            if phase == .active { model.resync() }
        }
        .onChange(of: model.lastActionMessage) { _, msg in
            guard let msg else { return }
            Task {
                try? await Task.sleep(for: .seconds(2))
                if model.lastActionMessage == msg { model.lastActionMessage = nil }
            }
        }
    }

    private func hideSplash() {
        guard showSplash else { return }
        withAnimation(.easeInOut(duration: 0.45)) { showSplash = false }
    }

    // MARK: - Górny pasek

    private var topBar: some View {
        HStack {
            statusChip
            Spacer()
            circleButton("mappin.and.ellipse", label: "Waypointy") { showWaypoints = true }
        }
        .padding(.horizontal, 14)
    }

    private var statusChip: some View {
        HStack(spacing: 8) {
            BrandLogo(size: 30)
            Circle().fill(linkColor).frame(width: 9, height: 9)
            Text(linkLabel)
                .font(SunlightTheme.rounded(14, .semibold))
                .foregroundStyle(.white)
        }
        .padding(.leading, 6).padding(.trailing, 14).padding(.vertical, 6)
        .background(SunlightTheme.panelBackground, in: Capsule())
        .overlay(Capsule().strokeBorder(SunlightTheme.hairline))
        .shadow(color: .black.opacity(0.25), radius: 6, y: 3)
    }

    // MARK: - Kontrolki mapy (prawa krawędź)

    private var zoomControls: some View {
        VStack(spacing: 12) {
            circleButton("location.fill", label: "Wróć do mojej pozycji") {
                camera.recenter(boat: model.telemetry.boat.coordinate)
            }
            circleButton("plus", label: "Przybliż") { camera.zoomIn() }
            circleButton("minus", label: "Oddal") { camera.zoomOut() }
        }
        .padding(.horizontal, 14)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .trailing)
    }

    private func circleButton(_ systemName: String, label: String,
                              action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: systemName)
                .font(.system(size: 20, weight: .semibold))
                .frame(width: SunlightTheme.minHitTarget, height: SunlightTheme.minHitTarget)
                .background(SunlightTheme.panelBackground, in: Circle())
                .overlay(Circle().strokeBorder(SunlightTheme.hairline))
                .foregroundStyle(.white)
                .shadow(color: .black.opacity(0.25), radius: 6, y: 3)
        }
        .accessibilityLabel(label)
    }

    // MARK: - Dolna konsola

    private var console: some View {
        VStack {
            Spacer()
            VStack(spacing: 10) {
                if model.telemetry.latest?.gotoArrived == true {
                    ArrivalHintView().padding(.horizontal, 16)
                }
                if let msg = model.lastActionMessage {
                    toastPill(msg)
                }
                if model.target.staged != nil {
                    removeTargetButton
                }
                ControlBarView(
                    hasTarget: model.target.staged != nil,
                    sendState: model.target.sendState,
                    telemetry: model.telemetry.latest,
                    blockReason: model.telemetry.gotoBlockReason,
                    onGoto: { model.requestGoto() },
                    onStop: { model.stopGoto() },
                    onDisarm: { model.disarm() },
                    onSpotLock: { model.requestSpotLock() }
                )
                AttributionOverlay()
            }
            .padding(.bottom, 8)
            .animation(.easeInOut(duration: 0.25), value: model.lastActionMessage)
            .animation(.easeInOut(duration: 0.2), value: model.target.staged != nil)
        }
    }

    private var removeTargetButton: some View {
        Button { model.clearTarget() } label: {
            Label("Usuń punkt", systemImage: "xmark.circle.fill")
                .font(SunlightTheme.rounded(14, .semibold))
                .foregroundStyle(.white)
                .padding(.horizontal, 14).padding(.vertical, 8)
                .background(SunlightTheme.panelBackground, in: Capsule())
                .overlay(Capsule().strokeBorder(SunlightTheme.hairline))
        }
        .accessibilityLabel("Usuń punkt nawigacji")
        .transition(.opacity)
    }

    private func toastPill(_ text: String) -> some View {
        Text(text)
            .font(SunlightTheme.rounded(14, .semibold))
            .foregroundStyle(.white)
            .padding(.horizontal, 14).padding(.vertical, 8)
            .background(SunlightTheme.panelBackground, in: Capsule())
            .overlay(Capsule().strokeBorder(SunlightTheme.hairline))
            .transition(.opacity)
    }

    // MARK: - Stan łącza

    private var linkLabel: String {
        switch model.telemetry.linkState {
        case .disconnected: return "Rozłączono"
        case .joining: return "Łączenie…"
        case .connected: return "Połączono"
        case .stale: return "Brak danych"
        }
    }

    private var linkColor: Color {
        switch model.telemetry.linkState {
        case .connected: return .green
        case .joining: return .yellow
        case .stale: return .orange
        case .disconnected: return .red
        }
    }

    private var splashStatus: String {
        switch model.telemetry.linkState {
        case .connected: return "Połączono"
        case .stale: return "Brak danych z silnika"
        default: return "Łączę z silnikiem…"
        }
    }
}

#Preview {
    RootView()
}
