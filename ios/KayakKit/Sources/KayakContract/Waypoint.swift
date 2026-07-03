import Foundation

/// Zapisany cel — realna pozycja łódki utrwalona jako nazwany punkt.
public struct Waypoint: Codable, Identifiable, Equatable, Sendable {
    public let id: UUID
    public var name: String
    public let latE7: Int32
    public let lonE7: Int32
    public let createdAt: Date

    public init(id: UUID = UUID(), name: String, latE7: Int32, lonE7: Int32, createdAt: Date = Date()) {
        self.id = id
        self.name = name
        self.latE7 = latE7
        self.lonE7 = lonE7
        self.createdAt = createdAt
    }

    /// Cel goto z zapisanej pozycji.
    public var target: LatLonE7 { LatLonE7(latE7: latE7, lonE7: lonE7) }

    /// Buduje waypoint z bieżącej telemetrii — zwraca `nil` bez świeżego fixu
    /// (nie zapisujemy „null-island" 0,0).
    public static func fromBoat(_ t: Telemetry, name: String, now: Date = Date()) -> Waypoint? {
        guard t.gpsFix else { return nil }
        return Waypoint(name: name, latE7: t.gpsLatE7, lonE7: t.gpsLonE7, createdAt: now)
    }
}

/// Trwały magazyn waypointów (Codable → plik JSON, zapis atomowy). Logika
/// persystencji oddzielona od UI — host-testowalna z wstrzykniętą ścieżką.
public final class WaypointStore {
    private let fileURL: URL
    public private(set) var waypoints: [Waypoint]

    public init(fileURL: URL) {
        self.fileURL = fileURL
        self.waypoints = Self.read(from: fileURL)
    }

    private static func read(from url: URL) -> [Waypoint] {
        guard let data = try? Data(contentsOf: url) else { return [] }
        return (try? JSONDecoder().decode([Waypoint].self, from: data)) ?? []
    }

    private func persist() throws {
        let data = try JSONEncoder().encode(waypoints)
        try data.write(to: fileURL, options: .atomic)
    }

    @discardableResult
    public func add(_ waypoint: Waypoint) throws -> Waypoint {
        waypoints.append(waypoint)
        try persist()
        return waypoint
    }

    public func rename(id: Waypoint.ID, to name: String) throws {
        guard let idx = waypoints.firstIndex(where: { $0.id == id }) else { return }
        waypoints[idx].name = name
        try persist()
    }

    public func delete(id: Waypoint.ID) throws {
        waypoints.removeAll { $0.id == id }
        try persist()
    }
}
