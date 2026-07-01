import SwiftUI
import KayakContract

/// Panel nawigacji (R3): świadome „Płyń do punktu" + odległość/namiar z telemetrii.
struct GotoControlsView: View {
    let hasTarget: Bool
    let sendState: GotoTargetController.SendState
    let telemetry: Telemetry?
    let blockReason: GotoBlockReason?
    let onGoto: () -> Void

    var body: some View {
        VStack(spacing: 8) {
            if let t = telemetry, t.gotoState != .off {
                progressRow(t)
            }
            if let reason = blockReason {
                Text(reason.message)
                    .font(.footnote.bold())
                    .foregroundStyle(.orange)
            }
            Button(action: onGoto) {
                Label("Płyń do punktu", systemImage: "location.north.line.fill")
                    .font(.title3.bold())
                    .frame(maxWidth: .infinity, minHeight: SunlightTheme.minHitTarget)
                    .background(hasTarget ? SunlightTheme.gotoColor : Color.gray,
                               in: RoundedRectangle(cornerRadius: SunlightTheme.cornerRadius))
                    .foregroundStyle(.white)
            }
            .disabled(!hasTarget || sendState == .sending)

            if case let .failed(message) = sendState {
                Text(message).font(.caption).foregroundStyle(.red)
            }
        }
    }

    private func progressRow(_ t: Telemetry) -> some View {
        HStack {
            Label("\(t.gotoErrM) m", systemImage: "ruler")
            Spacer()
            Label(String(format: "%.0f°", t.gotoBearingDegrees), systemImage: "safari")
            Spacer()
            Text(t.gotoState == .paused ? "PAUZA" : "AKTYWNE")
                .bold()
                .foregroundStyle(t.gotoState == .paused ? .orange : .green)
        }
        .font(.subheadline.monospacedDigit())
        .foregroundStyle(.white)
    }
}
