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

    @Test("Namiar 0° (północ) przesuwa TYLKO szerokość, w górę")
    func destinationBearingZeroMovesLatOnly() {
        let d = GeoDistance.destination(fromLat: 52.0, fromLon: 21.0,
                                        bearingDegrees: 0, distanceMetres: 111_195)
        // ~1 deg lat na tym promieniu — mylony znak/oś zawaliłby to, nie tylko
        // przesunięcie w niewłaściwą stronę.
        #expect(abs(d.lat - 53.0) < 0.01)
        #expect(abs(d.lon - 21.0) < 0.01)
    }

    @Test("Namiar 180° (południe) przesuwa szerokość w dół")
    func destinationBearingSouthDecreasesLat() {
        let d = GeoDistance.destination(fromLat: 52.0, fromLon: 21.0,
                                        bearingDegrees: 180, distanceMetres: 111_195)
        #expect(abs(d.lat - 51.0) < 0.01)
        #expect(abs(d.lon - 21.0) < 0.01)
    }

    @Test("Namiar 90° (wschód) na równiku przesuwa TYLKO długość, w prawo")
    func destinationBearingEastOnEquatorMovesLonOnly() {
        // Na równiku 1 deg lon ≈ ten sam dystans co 1 deg lat gdzie indziej
        // (cos(lat)=1) -- odróżnia formułę namiaru od zwykłego haversine offsetu.
        let d = GeoDistance.destination(fromLat: 0, fromLon: 0,
                                        bearingDegrees: 90, distanceMetres: 111_195)
        #expect(abs(d.lat - 0.0) < 0.01)
        #expect(abs(d.lon - 1.0) < 0.01)
    }

    @Test("Spójność: dystans do wyliczonego punktu docelowego ≈ zadany dystans")
    func destinationRoundTripsThroughMetres() {
        let start = (lat: 52.4, lon: 21.6)
        let bearing = 37.0
        let distance = 250.0
        let d = GeoDistance.destination(fromLat: start.lat, fromLon: start.lon,
                                        bearingDegrees: bearing, distanceMetres: distance)
        let back = GeoDistance.metres(fromLat: start.lat, fromLon: start.lon,
                                      toLat: d.lat, toLon: d.lon)
        #expect(abs(back - distance) < 0.5)
    }
}
