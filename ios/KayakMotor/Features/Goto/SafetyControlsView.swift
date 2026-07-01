import SwiftUI

/// Sterowanie bezpieczeństwa (R6): wielki, zawsze widoczny STOP (goto_cancel bez
/// rozbrajania) + osobny, mniejszy Rozbrój (disarm). App nigdy nie uzbraja.
struct SafetyControlsView: View {
    let onStop: () -> Void
    let onDisarm: () -> Void

    var body: some View {
        HStack(spacing: 16) {
            Button(role: .destructive, action: onStop) {
                Text("STOP")
                    .font(.system(size: 28, weight: .heavy))
                    .frame(width: SunlightTheme.stopButtonSize, height: SunlightTheme.stopButtonSize)
                    .background(SunlightTheme.stopColor, in: Circle())
                    .foregroundStyle(.white)
            }
            .accessibilityLabel("STOP — przerwij nawigację")

            Button(action: onDisarm) {
                Text("Rozbrój")
                    .font(.headline)
                    .frame(minWidth: SunlightTheme.minHitTarget, minHeight: SunlightTheme.minHitTarget)
                    .padding(.horizontal, 12)
                    .background(SunlightTheme.disarmColor, in: RoundedRectangle(cornerRadius: SunlightTheme.cornerRadius))
                    .foregroundStyle(.white)
            }
        }
    }
}
