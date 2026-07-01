import Testing
@testable import KayakContract

@Suite("Miękka bariera geofence (point-in-polygon)")
struct WaterGeofenceTests {
    // Kwadrat 0..10 x 0..10 (lon x lat).
    private let square = [
        GeoPoint(latDegrees: 0, lonDegrees: 0),
        GeoPoint(latDegrees: 0, lonDegrees: 10),
        GeoPoint(latDegrees: 10, lonDegrees: 10),
        GeoPoint(latDegrees: 10, lonDegrees: 0),
    ]

    @Test("punkt wewnątrz → contains true")
    func insideIsContained() {
        #expect(WaterGeofence.contains(GeoPoint(latDegrees: 5, lonDegrees: 5), polygon: square))
    }

    // Moc wyroczni: punkt „lądowy" poza konturem MUSI dać false. Gdyby contains
    // zawsze zwracało true, ten test FAILuje.
    @Test("punkt poza → contains false")
    func outsideIsNotContained() {
        #expect(!WaterGeofence.contains(GeoPoint(latDegrees: 20, lonDegrees: 20), polygon: square))
        #expect(!WaterGeofence.contains(GeoPoint(latDegrees: 5, lonDegrees: -1), polygon: square))
    }

    @Test("zdegenerowany polygon (<3 wierzchołki) → false")
    func degeneratePolygon() {
        #expect(!WaterGeofence.contains(GeoPoint(latDegrees: 5, lonDegrees: 5),
                                        polygon: [GeoPoint(latDegrees: 0, lonDegrees: 0)]))
    }

    @Test("wklęsły polygon (L-kształt) rozróżnia zatokę od lądu")
    func concaveShape() {
        // L-kształt: pełny dolny pas + lewa kolumna; prawa-górna ćwiartka poza.
        let lShape = [
            GeoPoint(latDegrees: 0, lonDegrees: 0),
            GeoPoint(latDegrees: 0, lonDegrees: 10),
            GeoPoint(latDegrees: 4, lonDegrees: 10),
            GeoPoint(latDegrees: 4, lonDegrees: 4),
            GeoPoint(latDegrees: 10, lonDegrees: 4),
            GeoPoint(latDegrees: 10, lonDegrees: 0),
        ]
        #expect(WaterGeofence.contains(GeoPoint(latDegrees: 2, lonDegrees: 8), polygon: lShape))
        #expect(!WaterGeofence.contains(GeoPoint(latDegrees: 8, lonDegrees: 8), polygon: lShape))
    }
}
