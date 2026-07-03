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
    let store: TelemetryStore
    let onTap: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            row("dot.radiowaves.up.forward", gpsText)
            row("shield.lefthalf.filled", stateText)
            row("scope", modeText)
            row("speedometer", speedHeadingText)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 10)
        .background(SunlightTheme.panelBackground,
                    in: RoundedRectangle(cornerRadius: SunlightTheme.cornerRadius, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: SunlightTheme.cornerRadius, style: .continuous)
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

    /// Dystans/namiar z aktywnego źródła: goto gdy goto aktywne/pauza, inaczej
    /// spot-lock; `nil` w trybie ręcznym (brak celu).
    private func target(_ t: Telemetry) -> String? {
        if t.gotoState != .off && t.gotoState != .unknown {
            return TelemetryDisplay.targetText(errM: t.gotoErrM, bearingDeg10: t.gotoBearingDeg10)
        }
        if t.spotLockState != .off && t.spotLockState != .unknown {
            return TelemetryDisplay.targetText(errM: t.spotLockErrM, bearingDeg10: t.spotLockBearingDeg10)
        }
        return nil
    }
}
