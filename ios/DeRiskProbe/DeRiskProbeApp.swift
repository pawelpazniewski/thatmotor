import SwiftUI

// Unit 0 — bramka de-risk. Throwaway app do potwierdzenia łączności
// iPhone ↔ 192.168.4.1 (AP kayak-motor) po Wi‑Fi przy aktywnym LTE.
// URUCHOMIĆ NA REALNYM URZĄDZENIU — Simulator nie dołączy do AP ani nie
// osiągnie lokalnego IP.
@main
struct DeRiskProbeApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}
