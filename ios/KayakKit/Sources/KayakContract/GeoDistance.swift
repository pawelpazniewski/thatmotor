import Foundation

/// Czysta geometria dystansu do prezentacji (np. etykieta przy pinezce celu).
/// Bez CoreLocation/MapLibre — host-testowalna, stopnie na wejściu.
public enum GeoDistance {
    private static let earthRadiusMetres = 6_371_000.0

    /// Dystans po wielkim okręgu (haversine) w metrach między dwoma punktami
    /// podanymi w stopniach. Symetryczny; 0 dla identycznych punktów.
    public static func metres(fromLat lat1: Double, fromLon lon1: Double,
                              toLat lat2: Double, toLon lon2: Double) -> Double {
        let dLat = radians(lat2 - lat1)
        let dLon = radians(lon2 - lon1)
        let a = sin(dLat / 2) * sin(dLat / 2)
            + cos(radians(lat1)) * cos(radians(lat2)) * sin(dLon / 2) * sin(dLon / 2)
        let c = 2 * atan2(sqrt(a), sqrt(1 - a))
        return earthRadiusMetres * c
    }

    /// Krótka etykieta dla użytkownika: "340 m" poniżej 1 km, "1.2 km" powyżej.
    /// Ujemne (nie powinno wystąpić) -> myślnik.
    public static func shortLabel(metres: Double) -> String {
        if metres < 0 {
            return "—"
        }
        if metres < 1000 {
            return "\(Int(metres.rounded())) m"
        }
        return String(format: "%.1f km", metres / 1000)
    }

    private static func radians(_ degrees: Double) -> Double {
        degrees * .pi / 180
    }
}
