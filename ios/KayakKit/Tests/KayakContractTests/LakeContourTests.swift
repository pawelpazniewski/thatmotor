import Foundation
import Testing
@testable import KayakContract

@Suite("Parser konturu jeziora (GeoJSON)")
struct LakeContourTests {
    private let featureCollection = """
    {"type":"FeatureCollection","features":[
      {"type":"Feature","properties":{"natural":"water"},
       "geometry":{"type":"Polygon","coordinates":[
         [[21.00,52.00],[21.10,52.00],[21.10,52.10],[21.00,52.10],[21.00,52.00]]]}}]}
    """

    @Test("FeatureCollection z Polygon → wierzchołki [GeoPoint] (lon,lat)")
    func parsesFeatureCollection() throws {
        let ring = try LakeContour.polygon(fromGeoJSON: Data(featureCollection.utf8))
        #expect(ring.count == 5)
        #expect(ring.first?.lonDegrees == 21.00)
        #expect(ring.first?.latDegrees == 52.00)
    }

    @Test("parsowany kontur współpracuje z geofence")
    func integratesWithGeofence() throws {
        let ring = try LakeContour.polygon(fromGeoJSON: Data(featureCollection.utf8))
        #expect(WaterGeofence.contains(GeoPoint(latDegrees: 52.05, lonDegrees: 21.05), polygon: ring))
        #expect(!WaterGeofence.contains(GeoPoint(latDegrees: 52.05, lonDegrees: 21.50), polygon: ring))
    }

    @Test("niepoprawny JSON → invalidJSON")
    func rejectsBadJSON() {
        #expect(throws: LakeContourError.invalidJSON) {
            try LakeContour.polygon(fromGeoJSON: Data("nie-json".utf8))
        }
    }

    @Test("brak polygonu → noPolygon")
    func rejectsNoPolygon() {
        let pointOnly = #"{"type":"Feature","geometry":{"type":"Point","coordinates":[21,52]}}"#
        #expect(throws: LakeContourError.noPolygon) {
            try LakeContour.polygon(fromGeoJSON: Data(pointOnly.utf8))
        }
    }
}
