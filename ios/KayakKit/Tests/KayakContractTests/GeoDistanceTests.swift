import Testing
import Foundation
@testable import KayakContract

@Suite("Dystans geograficzny — haversine i etykieta")
struct GeoDistanceTests {
    @Test("Identyczne punkty → 0 m")
    func identicalPointsZero() {
        let d = GeoDistance.metres(fromLat: 52, fromLon: 21, toLat: 52, toLon: 21)
        #expect(d == 0)
    }

    @Test("~0.001° szerokości ≈ 111 m (haversine liczy realny dystans)")
    func oneMilliDegreeLatIsAbout111m() {
        let d = GeoDistance.metres(fromLat: 52.0, fromLon: 21.0,
                                   toLat: 52.001, toLon: 21.0)
        // 1e-3 deg lat ≈ 111.19 m — wąska tolerancja, więc zły promień/wzór failuje.
        #expect(abs(d - 111.19) < 1.0)
    }

    @Test("Symetria: A→B == B→A")
    func symmetric() {
        let ab = GeoDistance.metres(fromLat: 52.0, fromLon: 21.0, toLat: 53.83, toLon: 21.65)
        let ba = GeoDistance.metres(fromLat: 53.83, fromLon: 21.65, toLat: 52.0, toLon: 21.0)
        #expect(abs(ab - ba) < 0.001)
    }

    @Test("Etykieta poniżej 1 km → metry")
    func labelUnderKilometre() {
        #expect(GeoDistance.shortLabel(metres: 340) == "340 m")
        #expect(GeoDistance.shortLabel(metres: 0) == "0 m")
    }

    @Test("Granica 1000 m → kilometry (oracle na progu)")
    func labelKilometreBoundary() {
        #expect(GeoDistance.shortLabel(metres: 999) == "999 m")
        #expect(GeoDistance.shortLabel(metres: 1000) == "1.0 km")
        #expect(GeoDistance.shortLabel(metres: 1250) == "1.2 km")
    }
}
