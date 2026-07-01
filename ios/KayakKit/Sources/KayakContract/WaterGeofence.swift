import Foundation

/// Punkt w stopniach WGS84 (dla testu geofence w domenie stopni).
public struct GeoPoint: Equatable, Sendable {
    public let latDegrees: Double
    public let lonDegrees: Double

    public init(latDegrees: Double, lonDegrees: Double) {
        self.latDegrees = latDegrees
        self.lonDegrees = lonDegrees
    }
}

/// Miękka bariera „poza akwenem" (R7) — NIE failsafe, tylko barierka UX.
/// Ray-casting point-in-polygon względem konturu jednego jeziora. Poza polygonem
/// → aplikacja pokazuje ostrzeżenie z potwierdzeniem, nigdy nie blokuje twardo
/// (kontur OSM bywa niedokładny).
public enum WaterGeofence {
    /// `true`, gdy punkt leży wewnątrz polygonu (algorytm parzystości przecięć).
    /// Polygon jako lista wierzchołków (bez potrzeby domykania — ostatni łączy się
    /// z pierwszym). Zwraca `false` dla polygonu < 3 wierzchołków.
    public static func contains(_ point: GeoPoint, polygon: [GeoPoint]) -> Bool {
        guard polygon.count >= 3 else { return false }
        let x = point.lonDegrees
        let y = point.latDegrees
        var inside = false
        var j = polygon.count - 1
        for i in 0..<polygon.count {
            let xi = polygon[i].lonDegrees, yi = polygon[i].latDegrees
            let xj = polygon[j].lonDegrees, yj = polygon[j].latDegrees
            let intersects = ((yi > y) != (yj > y)) &&
                (x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            if intersects { inside.toggle() }
            j = i
        }
        return inside
    }
}
