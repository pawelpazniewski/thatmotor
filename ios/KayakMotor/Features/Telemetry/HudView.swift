import SwiftUI
import KayakContract

/// Kompaktowy HUD „rzut oka" w lewym górnym rogu mapy: 4 wiersze żywej telemetrii
/// (jakość GPS / stan systemu / tryb + cel / prędkość + kurs). Cały panel jest
/// tapowalny — otwiera dolny arkusz szczegółów.
///
/// Widok cienki: wszystkie stringi składa `TelemetryDisplay`, a staleness (R6)
/// bramkuje `displayed(_:isFresh:)` — gdy łącze nie jest świeże LUB brak ramki,
/// każdy wiersz pokazuje myślnik zamiast zamrożonej liczby.
struct HudView: View {
    /// Górny limit szerokości panelu — trzyma HUD kompaktowym w rogu, żeby długi
    /// wiersz trybu (np. „Spot-lock (pauza) · 1234 m · 180°") truncował się
    /// (`lineLimit(1)`), a nie rozciągał panelu przez środek mapy.
    private static let maxPanelWidth: CGFloat = 240

    let store: TelemetryStore
    let onTap: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            row("dot.radiowaves.up.forward", gpsText)
            row("shield.lefthalf.filled", stateText)
            row("scope", modeText)
            row("speedometer", speedHeadingText)
        }
        .frame(maxWidth: Self.maxPanelWidth, alignment: .leading)
        .padding(.horizontal, 12)
        .padding(.vertical, 10)
        .background(SunlightTheme.panelBackground,
                    in: RoundedRectangle(cornerRadius: SunlightTheme.panelRadius, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: SunlightTheme.panelRadius, style: .continuous)
            .strokeBorder(SunlightTheme.hairline))
        .shadow(color: .black.opacity(0.25), radius: 6, y: 3)
        .contentShape(Rectangle())
        .onTapGesture(perform: onTap)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Telemetria — dotknij, aby otworzyć szczegóły")
        .accessibilityAddTraits(.isButton)
    }

    private func row(_ icon: String, _ text: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: icon)
                .font(.system(size: 12, weight: .semibold))
                .frame(width: 16)
                .foregroundStyle(SunlightTheme.brand)
            Text(text)
                .font(SunlightTheme.rounded(14, .semibold)).monospacedDigit()
                .foregroundStyle(.white)
                .lineLimit(1)
        }
    }

    // MARK: - Kompozycja wierszy (logika w TelemetryDisplay; widok tylko składa)

    /// Świeżość: myślniki gdy brak ramki LUB łącze nie `.connected` (nie zamrażaj).
    private var isFresh: Bool { TelemetryDisplay.isFresh(store.linkState) }

    private var gpsText: String {
        guard let t = store.latest else { return TelemetryDisplay.placeholder }
        return TelemetryDisplay.displayed(
            TelemetryDisplay.gpsQualityText(fix: t.gpsFix, sats: t.gpsSats),
            isFresh: isFresh)
    }

    private var stateText: String {
        guard let t = store.latest else { return TelemetryDisplay.placeholder }
        return TelemetryDisplay.displayed(
            TelemetryDisplay.stateLabel(t.state),
            isFresh: isFresh)
    }

    private var modeText: String {
        guard let t = store.latest else { return TelemetryDisplay.placeholder }
        let mode = TelemetryDisplay.modeLabel(spotLock: t.spotLockState, goto: t.gotoState)
        let combined = target(t).map { "\(mode) · \($0)" } ?? mode
        return TelemetryDisplay.displayed(combined, isFresh: isFresh)
    }

    private var speedHeadingText: String {
        guard let t = store.latest else { return TelemetryDisplay.placeholder }
        return TelemetryDisplay.displayed(
            TelemetryDisplay.speedHeadingText(speedCms: t.gpsSpeedCms, headingDeg10: t.imuHeadingDeg10),
            isFresh: isFresh)
    }

    /// Dystans/namiar z aktywnego źródła — wybór (goto > spot-lock) i format są
    /// w `TelemetryDisplay` (host-testowalne); widok tylko przekazuje pola.
    private func target(_ t: Telemetry) -> String? {
        TelemetryDisplay.activeTargetText(
            gotoState: t.gotoState, gotoErrM: t.gotoErrM, gotoBearingDeg10: t.gotoBearingDeg10,
            spotLockState: t.spotLockState, spotLockErrM: t.spotLockErrM,
            spotLockBearingDeg10: t.spotLockBearingDeg10)
    }
}
