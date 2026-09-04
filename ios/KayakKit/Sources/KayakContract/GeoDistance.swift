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

    /// Punkt docelowy po wyjściu z (lat, lon) danym namiarem (stopnie, CW od
    /// północy) na zadany dystans w metrach (formuła sferyczna namiaru
    /// wprost — movable-type.co.uk/scripts/latlong.html). Do rysowania krótkiej
    /// linii osi łódki na mapie (weryfikacja kompasu względem realnego punktu).
    public static func destination(fromLat lat1: Double, fromLon lon1: Double,
                                   bearingDegrees: Double,
                                   distanceMetres: Double) -> (lat: Double, lon: Double) {
        let delta = distanceMetres / earthRadiusMetres
        let theta = radians(bearingDegrees)
        let phi1 = radians(lat1)
        let lambda1 = radians(lon1)

        let phi2 = asin(sin(phi1) * cos(delta) + cos(phi1) * sin(delta) * cos(theta))
        let lambda2 = lambda1 + atan2(sin(theta) * sin(delta) * cos(phi1),
                                      cos(delta) - sin(phi1) * sin(phi2))

        return (lat: degrees(phi2), lon: degrees(lambda2))
    }

    private static func radians(_ degrees: Double) -> Double {
        degrees * .pi / 180
    }

    private static func degrees(_ radians: Double) -> Double {
        radians * 180 / .pi
    }
}
