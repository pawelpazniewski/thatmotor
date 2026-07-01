import SwiftUI

// Produkcyjna aplikacja iOS — cienki klient firmware kajaka (mapa offline +
// tap-to-goto). Zależności: KayakContract (czysta logika) + MapLibre (mapa).
@main
struct KayakMotorApp: App {
    var body: some Scene {
        WindowGroup {
            RootView()
        }
    }
}
