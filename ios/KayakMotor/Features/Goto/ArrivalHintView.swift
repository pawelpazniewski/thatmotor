import SwiftUI

/// Po dojściu do celu (`goto_arrived`) — podpowiedź włączenia CH3 na RC do
/// bezobsługowego trzymania. „Aplikacja wozi, RC trzyma."
struct ArrivalHintView: View {
    var body: some View {
        Label("Dotarto — włącz CH3 na RC, by trzymać punkt", systemImage: "checkmark.circle.fill")
            .font(.subheadline.bold())
            .foregroundStyle(.white)
            .padding(.horizontal, 12).padding(.vertical, 8)
            .frame(maxWidth: .infinity)
            .background(.green.opacity(0.85), in: RoundedRectangle(cornerRadius: SunlightTheme.cornerRadius))
    }
}
