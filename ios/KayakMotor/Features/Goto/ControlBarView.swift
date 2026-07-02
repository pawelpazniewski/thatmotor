import SwiftUI
import KayakContract

/// Dolny pasek sterowania: 4 przyciski ikonowe inline — Goto, STOP, Rozbrój,
/// Spot-lock. Nad nimi kompaktowy status (dystans/namiar albo powód blokady),
/// pokazywany tylko gdy istotny. Spot-lock to na razie placeholder (logika później).
struct ControlBarView: View {
    let hasTarget: Bool
    let sendState: GotoTargetController.SendState
    let telemetry: Telemetry?
    let blockReason: GotoBlockReason?
    let onGoto: () -> Void
    let onStop: () -> Void
    let onDisarm: () -> Void
    let onSpotLock: () -> Void

    private let buttonSize: CGFloat = 64

    var body: some View {
        VStack(spacing: 10) {
            statusLine
            HStack(spacing: 18) {
                iconButton("location.north.line.fill",
                           tint: hasTarget ? SunlightTheme.gotoColor : Color.gray,
                           label: "Płyń do punktu",
                           enabled: hasTarget && sendState != .sending,
                           action: onGoto)
                iconButton("stop.fill", tint: SunlightTheme.stopColor, label: "STOP", action: onStop)
                iconButton("bolt.slash.fill", tint: SunlightTheme.disarmColor, label: "Rozbrój", action: onDisarm)
                iconButton("anchor", tint: SunlightTheme.brand, label: "Spot-lock (wkrótce)", action: onSpotLock)
            }
        }
    }

    @ViewBuilder private var statusLine: some View {
        if let reason = blockReason {
            statusPill(reason.message, color: .orange, icon: "exclamationmark.triangle.fill")
        } else if let t = telemetry, t.gotoState != .off {
            statusPill(String(format: "%d m · %.0f°", t.gotoErrM, t.gotoBearingDegrees),
                       color: t.gotoState == .paused ? .orange : SunlightTheme.gotoColor,
                       icon: t.gotoState == .paused ? "pause.fill" : "location.north.line.fill")
        } else if case let .failed(message) = sendState {
            statusPill(message, color: .red, icon: "xmark.circle.fill")
        }
    }

    private func statusPill(_ text: String, color: Color, icon: String) -> some View {
        Label(text, systemImage: icon)
            .font(SunlightTheme.rounded(13, .semibold)).monospacedDigit()
            .foregroundStyle(.white)
            .padding(.horizontal, 12).padding(.vertical, 6)
            .background(color.opacity(0.92), in: Capsule())
            .shadow(color: .black.opacity(0.25), radius: 5, y: 2)
    }

    private func iconButton(_ symbol: String, tint: Color, label: String,
                            enabled: Bool = true, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 26, weight: .bold))
                .frame(width: buttonSize, height: buttonSize)
                .background(tint, in: Circle())
                .overlay(Circle().strokeBorder(.white.opacity(0.18)))
                .foregroundStyle(.white)
                .shadow(color: tint.opacity(0.5), radius: 8, y: 3)
        }
        .disabled(!enabled)
        .opacity(enabled ? 1 : 0.5)
        .accessibilityLabel(label)
    }
}
