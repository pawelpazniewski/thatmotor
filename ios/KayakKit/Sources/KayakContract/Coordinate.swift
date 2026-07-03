import Foundation

/// Oś geograficzna — determinuje dopuszczalny zakres stopni.
public enum GeoAxis {
    case latitude
    case longitude
}

/// Konwersja WGS84 (stopnie) ↔ deg×1e7 (int32), spójna z kontraktem firmware
/// (`gps_lat_e7`, `goto lat_e7`).
///
/// Walidacja odbywa się w domenie `Double` PRZED castem na `Int32` — cast
/// `INF`/`NaN` na int to UB, a walidacja po castcie widzi już zawiniętą wartość.
/// Wzorzec z docs/solutions (`goto_target_from_double`).
public enum Coordinate {
    /// Zakresy w stopniach (spójne z firmware ±90 / ±180).
    public static let latDegreesRange: ClosedRange<Double> = -90.0...90.0
    public static let lonDegreesRange: ClosedRange<Double> = -180.0...180.0

    /// Zamienia stopnie na deg×1e7. Zwraca `nil` gdy wejście jest nie-skończone
    /// lub poza zakresem osi (walidacja w domenie double, przed castem).
    public static func toE7(_ degrees: Double, axis: GeoAxis) -> Int32? {
        guard degrees.isFinite else { return nil }
        let range = axis == .latitude ? latDegreesRange : lonDegreesRange
        guard range.contains(degrees) else { return nil }
        return Int32((degrees * 1e7).rounded())
    }

    /// Zamienia deg×1e7 z powrotem na stopnie.
    public static func fromE7(_ e7: Int32) -> Double {
        Double(e7) / 1e7
    }
}

/// Para współrzędnych w domenie e7 — gotowa do wysłania jako cel `goto`.
/// Inicjalizator zawodzi (`nil`), gdy którakolwiek oś jest poza zakresem.
public struct LatLonE7: Equatable, Sendable {
    public let latE7: Int32
    public let lonE7: Int32

    public init(latE7: Int32, lonE7: Int32) {
        self.latE7 = latE7
        self.lonE7 = lonE7
    }

    /// Buduje cel z par stopni, walidując oba wymiary przed castem.
    public init?(latDegrees: Double, lonDegrees: Double) {
        guard let lat = Coordinate.toE7(latDegrees, axis: .latitude),
              let lon = Coordinate.toE7(lonDegrees, axis: .longitude) else {
            return nil
        }
        self.latE7 = lat
        self.lonE7 = lon
    }

    public var latDegrees: Double { Coordinate.fromE7(latE7) }
    public var lonDegrees: Double { Coordinate.fromE7(lonE7) }
}
