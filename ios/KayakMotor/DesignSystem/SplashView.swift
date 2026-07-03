import SwiftUI

/// Ekran startowy/łączenia: marka + status połączenia z silnikiem. Pokazywany na
/// starcie i chowany po nawiązaniu łącza albo po krótkim czasie (nie blokuje mapy).
struct SplashView: View {
    let statusText: String
    @State private var appeared = false
    @State private var pulse = false

    var body: some View {
        ZStack {
            SunlightTheme.brandGradient.ignoresSafeArea()

            VStack(spacing: 22) {
                BrandLogo(size: 132)
                    .scaleEffect(appeared ? 1 : 0.82)
                    .opacity(appeared ? 1 : 0)
                    .overlay(
                        RoundedRectangle(cornerRadius: 132 * 0.22, style: .continuous)
                            .stroke(SunlightTheme.brand.opacity(0.5), lineWidth: 2)
                            .scaleEffect(pulse ? 1.14 : 1)
                            .opacity(pulse ? 0 : 0.8)
                            .frame(width: 132, height: 132)
                    )

                VStack(spacing: 4) {
                    Text("Kayak Motor")
                        .font(SunlightTheme.rounded(30, .heavy))
                        .foregroundStyle(.white)
                    Text("nawigacja do punktu")
                        .font(SunlightTheme.rounded(14, .medium))
                        .foregroundStyle(.white.opacity(0.6))
                }
                .opacity(appeared ? 1 : 0)

                HStack(spacing: 10) {
                    ProgressView().tint(SunlightTheme.brand)
                    Text(statusText)
                        .font(SunlightTheme.rounded(15, .semibold))
                        .foregroundStyle(.white.opacity(0.85))
                }
                .padding(.top, 8)
                .opacity(appeared ? 1 : 0)
            }
        }
        .onAppear {
            withAnimation(.easeOut(duration: 0.5)) { appeared = true }
            withAnimation(.easeOut(duration: 1.4).repeatForever(autoreverses: false)) { pulse = true }
        }
    }
}

#Preview {
    SplashView(statusText: "Łączę z silnikiem…")
}
