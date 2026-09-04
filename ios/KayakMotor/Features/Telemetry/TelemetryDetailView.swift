import SwiftUI
import KayakContract

/// Dolny arkusz szczegółów telemetrii (R4): kurowany zestaw pól pogrupowany w
/// sekcje (GPS / Kompas / Spot-lock / Goto / Łącze) plus regulacja neutrala serwa
/// (trim, R5). Widok cienki — stringi składa `TelemetryDisplay` i akcesory
/// `Telemetry`, a staleness (R6) bramkuje `displayed(_:isFresh:)`: gdy łącze nie
/// jest świeże LUB brak ramki, każdy wiersz pokazuje myślnik zamiast zamrożonej
/// liczby. Trim aktywny w DISARMED i ARMED — firmware honoruje regulację
/// neutrala w obu tych stanach (ARMED: korekta na bieżąco w trakcie pływania).
struct TelemetryDetailView: View {
    let store: TelemetryStore
    let model: AppModel

    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                gpsSection
                compassSection
                spotLockSection
                gotoSection
                linkSection
                trimSection
            }
            .navigationTitle("Telemetria")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Gotowe") { dismiss() }
                }
            }
        }
    }

    // MARK: - Sekcje (kurowany zestaw, R4)

    private var gpsSection: some View {
        Section("GPS") {
            valueRow("Fix") { TelemetryDisplay.boolText($0.gpsFix) }
            valueRow("Satelity") { "\($0.gpsSats)" }
            valueRow("Prędkość") { String(format: "%.1f m/s", $0.speedMetersPerSecond) }
            valueRow("Pozycja") { coordinateText($0.boatLatLon) }
        }
    }

    private var compassSection: some View {
        Section {
            valueRow("Kurs") {
                degreesText(TelemetryDisplay.calibratedHeadingDegrees(
                    $0.headingDegrees, offsetDegrees: store.compassOffsetDegrees))
            }
            valueRow("Kalibracja IMU") { "\($0.imuCalib)/3" }
            valueRow("Czujnik OK") { TelemetryDisplay.boolText($0.imuOk) }
            compassOffsetRow
        } header: {
            Text("Kompas")
        } footer: {
            Text("Offset koryguje TYLKO widok (oś dzioba na mapie, „Kurs” tutaj) — nie zmienia sterowania łodzi.")
        }
    }

    /// Dostrojenie kursu na oko względem osi dzioba na mapie (patrz `LakeMapView`)
    /// — krok 1°, zakres ±30° wystarcza na realne błędy montażu/kalibracji.
    private var compassOffsetRow: some View {
        Stepper(value: Binding(
            get: { store.compassOffsetDegrees },
            set: { store.compassOffsetDegrees = $0 }
        ), in: -30...30, step: 1) {
            HStack {
                Text("Offset kompasu").foregroundStyle(.secondary)
                Spacer()
                Text(String(format: "%+.0f°", store.compassOffsetDegrees)).monospacedDigit()
            }
        }
    }

    private var spotLockSection: some View {
        Section("Spot-lock") {
            valueRow("Stan") { TelemetryDisplay.holdStateLabel($0.spotLockState) }
            valueRow("Błąd") { "\($0.spotLockErrM) m" }
            valueRow("Namiar") { degreesText($0.spotLockBearingDegrees) }
        }
    }

    private var gotoSection: some View {
        Section("Goto") {
            valueRow("Stan") { TelemetryDisplay.holdStateLabel($0.gotoState) }
            valueRow("Błąd") { "\($0.gotoErrM) m" }
            valueRow("Namiar") { degreesText($0.gotoBearingDegrees) }
            valueRow("Cel") {
                coordinateText(LatLonE7(latE7: $0.gotoTargetLatE7, lonE7: $0.gotoTargetLonE7))
            }
            valueRow("Dotarto") { TelemetryDisplay.boolText($0.gotoArrived) }
        }
    }

    private var linkSection: some View {
        Section("Łącze / RC") {
            valueRow("RC poprawny") { TelemetryDisplay.boolText($0.rcValid) }
            valueRow("Link świeży") { TelemetryDisplay.boolText($0.appLinkFresh) }
        }
    }

    // MARK: - Trim serwa (R5) — aktywny w DISARMED i ARMED

    private var trimSection: some View {
        let isEnabled = TelemetryDisplay.isTrimEnabled(state: store.latest?.state ?? .unknown)
        return Section {
            valueRow("Neutral serwa") { TelemetryDisplay.trimText(servoTrimUs: $0.servoTrimUs) }
            HStack(spacing: 20) {
                trimButton("minus", label: "Trim w lewo", action: model.trimLeft)
                trimButton("plus", label: "Trim w prawo", action: model.trimRight)
                Spacer()
                Button("Zapisz", action: model.saveTrim)
                    .buttonStyle(.bordered)
                    .tint(SunlightTheme.brand)
            }
            if !isEnabled {
                Text("Rozbrój lub uzbrój, aby wyregulować neutral")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        } header: {
            Text("Trim serwa")
        }
        .disabled(!isEnabled)
    }

    private func trimButton(_ icon: String, label: String,
                            action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon)
                .font(.system(size: 20, weight: .bold))
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .buttonStyle(.bordered)
        .tint(SunlightTheme.brand)
        .accessibilityLabel(label)
    }

    // MARK: - Wiersze i format (widok cienki)

    /// Świeżość: myślniki gdy brak ramki LUB łącze nie `.connected` (nie zamrażaj).
    private var isFresh: Bool { TelemetryDisplay.isFresh(store.linkState) }

    /// Jeden wiersz „etykieta — wartość". Surowy string liczy `make(Telemetry)`
    /// (przez `TelemetryDisplay`/akcesory kontraktu), a bramka `displayed`
    /// zamienia go na myślnik przy nieświeżym łączu lub braku ramki (R6).
    private func valueRow(_ label: String, _ make: (Telemetry) -> String) -> some View {
        let value = store.latest.map { TelemetryDisplay.displayed(make($0), isFresh: isFresh) }
            ?? TelemetryDisplay.placeholder
        return HStack {
            Text(label).foregroundStyle(.secondary)
            Spacer()
            Text(value).monospacedDigit()
        }
    }

    private func degreesText(_ value: Double) -> String {
        String(format: "%.0f°", value)
    }

    /// Format współrzędnych spójny z `WaypointListView` (reużycie `LatLonE7`).
    private func coordinateText(_ coordinate: LatLonE7) -> String {
        String(format: "%.5f, %.5f", coordinate.latDegrees, coordinate.lonDegrees)
    }
}
