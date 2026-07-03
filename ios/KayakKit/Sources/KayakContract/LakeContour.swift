import Foundation

/// Błąd parsowania konturu jeziora z GeoJSON.
public enum LakeContourError: Error, Equatable {
    case invalidJSON
    case noPolygon
}

/// Parser bundlowego konturu akwenu (GeoJSON) na listę wierzchołków `[GeoPoint]`.
/// Ten sam kontur zasila render mapy (MapLibre `MLNShapeSource`) i miękką barierę
/// geofence (`WaterGeofence`), więc mają wspólne źródło prawdy.
///
/// Obsługuje `FeatureCollection` / `Feature` / gołą `Geometry` typu `Polygon`
/// (bierze pierścień zewnętrzny pierwszego polygonu). Współrzędne GeoJSON to
/// `[lon, lat]`.
public enum LakeContour {
    public static func polygon(fromGeoJSON data: Data) throws -> [GeoPoint] {
        guard let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw LakeContourError.invalidJSON
        }
        guard let geometry = firstPolygonGeometry(in: root),
              let ring = outerRing(of: geometry) else {
            throw LakeContourError.noPolygon
        }
        let points = ring.compactMap { pair -> GeoPoint? in
            guard pair.count >= 2 else { return nil }
            return GeoPoint(latDegrees: pair[1], lonDegrees: pair[0])
        }
        guard points.count >= 3 else { throw LakeContourError.noPolygon }
        return points
    }

    private static func firstPolygonGeometry(in object: [String: Any]) -> [String: Any]? {
        switch object["type"] as? String {
        case "FeatureCollection":
            let features = object["features"] as? [[String: Any]] ?? []
            for feature in features {
                if let geometry = feature["geometry"] as? [String: Any],
                   geometry["type"] as? String == "Polygon" {
                    return geometry
                }
            }
            return nil
        case "Feature":
            let geometry = object["geometry"] as? [String: Any]
            return geometry?["type"] as? String == "Polygon" ? geometry : nil
        case "Polygon":
            return object
        default:
            return nil
        }
    }

    private static func outerRing(of geometry: [String: Any]) -> [[Double]]? {
        (geometry["coordinates"] as? [[[Double]]])?.first
    }
}
