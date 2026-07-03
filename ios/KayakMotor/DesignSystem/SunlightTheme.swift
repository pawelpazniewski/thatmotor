import SwiftUI

/// Czytelność na wodzie (R10): wysoki kontrast, duże cele dotykowe, obsługa jedną
/// ręką. Estetyka „morski instrument": granatowe solidne panele + akwamarynowy akcent.
enum SunlightTheme {
    /// Minimalny bok celu dotykowego (Apple HIG: 44 pt; tu większe na słońce).
    static let minHitTarget: CGFloat = 56
    static let stopButtonSize: CGFloat = 92
    static let cornerRadius: CGFloat = 16
    static let panelRadius: CGFloat = 26

    // Akcje (semantyka bezpieczeństwa — nie zmieniać dla estetyki).
    static let stopColor = Color.red
    static let disarmColor = Color.orange
    static let gotoColor = Color(red: 0.16, green: 0.72, blue: 0.52)

    // Marka / powierzchnie.
    static let brand = Color(red: 0.18, green: 0.80, blue: 0.86)       // akwamaryna
    static let brandDeep = Color(red: 0.03, green: 0.36, blue: 0.46)   // głęboki morski
    static let ink = Color(red: 0.03, green: 0.09, blue: 0.15)         // prawie-czarny granat
    static let surface = Color(red: 0.05, green: 0.13, blue: 0.21)     // panel
    static let hairline = Color.white.opacity(0.12)
    static let panelBackground = Color(red: 0.05, green: 0.13, blue: 0.21).opacity(0.82)

    /// Zaokrąglona, pogrubiona typografia instrumentu (SF Pro Rounded — bez bundlowania).
    static func rounded(_ size: CGFloat, _ weight: Font.Weight = .bold) -> Font {
        .system(size: size, weight: weight, design: .rounded)
    }

    static let brandGradient = LinearGradient(
        colors: [ink, brandDeep], startPoint: .topLeading, endPoint: .bottomTrailing)
}
