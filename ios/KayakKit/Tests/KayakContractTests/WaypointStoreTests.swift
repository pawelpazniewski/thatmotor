import Foundation
import Testing
@testable import KayakContract

@Suite("Waypointy — trwałość i zapis z pozycji łódki")
struct WaypointStoreTests {
    private func tempURL() -> URL {
        FileManager.default.temporaryDirectory
            .appendingPathComponent("wp-\(UUID().uuidString).json")
    }

    private func armedFrameWithFix(_ fix: Bool, lat: Int32, lon: Int32) -> Telemetry {
        let json = """
        {"state":1,"arm_reason":0,"rc_valid":true,"gps_fix":\(fix),"gps_sats":9,
        "gps_lat_e7":\(lat),"gps_lon_e7":\(lon),"gps_speed_cms":0,"imu_ok":true,
        "imu_heading_deg10":0,"goto_state":0,"goto_target_lat_e7":0,
        "goto_target_lon_e7":0,"goto_err_m":0,"goto_bearing_deg10":0,
        "goto_arrived":false,"app_link_fresh":false,"spot_lock_state":0}
        """
        return try! JSONDecoder().decode(Telemetry.self, from: Data(json.utf8))
    }

    // Moc wyroczni trwałości: nowy store czyta te same dane z pliku po „restarcie".
    // Bez realnego zapisu do pliku reload zwróciłby pustą listę → FAIL.
    @Test("add → nowy store (reload) widzi ten sam waypoint")
    func persistsAcrossReload() throws {
        let url = tempURL()
        defer { try? FileManager.default.removeItem(at: url) }

        let store = WaypointStore(fileURL: url)
        try store.add(Waypoint(name: "Przystań", latE7: 522_297_000, lonE7: 210_122_000))

        let reloaded = WaypointStore(fileURL: url)
        #expect(reloaded.waypoints.count == 1)
        #expect(reloaded.waypoints.first?.name == "Przystań")
        #expect(reloaded.waypoints.first?.latE7 == 522_297_000)
    }

    @Test("rename i delete utrwalają się")
    func renameAndDelete() throws {
        let url = tempURL()
        defer { try? FileManager.default.removeItem(at: url) }

        let store = WaypointStore(fileURL: url)
        let wp = try store.add(Waypoint(name: "A", latE7: 1, lonE7: 2))
        try store.rename(id: wp.id, to: "B")
        #expect(WaypointStore(fileURL: url).waypoints.first?.name == "B")

        try store.delete(id: wp.id)
        #expect(WaypointStore(fileURL: url).waypoints.isEmpty)
    }

    @Test("fromBoat zapisuje realną pozycję z fixem")
    func fromBoatWithFix() {
        let wp = Waypoint.fromBoat(armedFrameWithFix(true, lat: 522_297_000, lon: 210_122_000), name: "Tu")
        #expect(wp?.latE7 == 522_297_000)
        #expect(wp?.lonE7 == 210_122_000)
    }

    // Moc wyroczni: bez fixu NIE wolno zapisać (uniknięcie null-island 0,0).
    @Test("fromBoat bez fixu → nil (nie zapisuje 0,0)")
    func fromBoatNoFixRejected() {
        #expect(Waypoint.fromBoat(armedFrameWithFix(false, lat: 0, lon: 0), name: "Tu") == nil)
    }
}
