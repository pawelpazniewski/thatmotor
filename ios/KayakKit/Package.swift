// swift-tools-version: 6.0
import PackageDescription

// KayakKit — czysta logika kontraktu firmware (bez UIKit/SwiftUI/MapLibre).
// Host-testowalna przez `swift test` bez Simulatora. Reużywalna niezależnie od
// transportu (WiFi/BLE). Wzorzec "Pure ⊥ HAL" z docs/solutions.
let package = Package(
    name: "KayakKit",
    platforms: [.iOS(.v17), .macOS(.v13)],
    products: [
        .library(name: "KayakContract", targets: ["KayakContract"]),
    ],
    targets: [
        .target(name: "KayakContract"),
        .testTarget(name: "KayakContractTests", dependencies: ["KayakContract"]),
    ]
)
