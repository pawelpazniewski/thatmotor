import SwiftUI

/// Czytelność na wodzie (R10): wysoki kontrast, duże cele dotykowe, obsługa jedną
/// ręką (sterowanie nisko, w zasięgu kciuka).
enum SunlightTheme {
    /// Minimalny bok celu dotykowego (Apple HIG: 44 pt; tu większe na słońce).
    static let minHitTarget: CGFloat = 56
    static let stopButtonSize: CGFloat = 96
    static let cornerRadius: CGFloat = 16

    static let stopColor = Color.red
    static let disarmColor = Color.orange
    static let gotoColor = Color.green
    static let panelBackground = Color.black.opacity(0.55)
}
