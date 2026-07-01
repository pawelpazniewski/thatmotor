import Testing
@testable import KayakContract

@Suite("Konwersja współrzędnych WGS84 ↔ e7")
struct CoordinateConversionTests {
    @Test("toE7 skaluje stopnie na deg×1e7 z zaokrągleniem")
    func toE7Scales() {
        #expect(Coordinate.toE7(52.2297, axis: .latitude) == 522_297_000)
        #expect(Coordinate.toE7(0.0, axis: .longitude) == 0)
        #expect(Coordinate.toE7(-33.8688, axis: .latitude) == -338_688_000)
    }

    @Test("round-trip toE7→fromE7 jest stabilny")
    func roundTripStable() {
        let e7 = Coordinate.toE7(21.0122, axis: .longitude)!
        #expect(abs(Coordinate.fromE7(e7) - 21.0122) < 1e-6)
    }

    // Moc wyroczni: wejście POZA zakresem, nie tożsamość. Usunięcie walidacji
    // zakresu w domenie double sprawia, że ten test FAILuje (cast 91° przechodzi).
    @Test("toE7 odrzuca szerokość poza ±90")
    func rejectsLatitudeOutOfRange() {
        #expect(Coordinate.toE7(91.0, axis: .latitude) == nil)
        #expect(Coordinate.toE7(-90.0001, axis: .latitude) == nil)
    }

    @Test("toE7 odrzuca długość poza ±180")
    func rejectsLongitudeOutOfRange() {
        #expect(Coordinate.toE7(180.5, axis: .longitude) == nil)
    }

    @Test("toE7 odrzuca wartości nie-skończone (cast NaN/INF = UB)")
    func rejectsNonFinite() {
        #expect(Coordinate.toE7(.nan, axis: .latitude) == nil)
        #expect(Coordinate.toE7(.infinity, axis: .longitude) == nil)
    }

    @Test("LatLonE7 zawodzi gdy którakolwiek oś poza zakresem")
    func latLonInitFailsOutOfRange() {
        #expect(LatLonE7(latDegrees: 52.0, lonDegrees: 200.0) == nil)
        #expect(LatLonE7(latDegrees: 95.0, lonDegrees: 10.0) == nil)
        #expect(LatLonE7(latDegrees: 52.0, lonDegrees: 21.0) != nil)
    }
}
