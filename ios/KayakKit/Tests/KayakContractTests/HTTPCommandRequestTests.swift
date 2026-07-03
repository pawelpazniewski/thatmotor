import Foundation
import Testing
@testable import KayakContract

@Suite("Builder żądania HTTP komendy")
struct HTTPCommandRequestTests {
    @Test("goto → POST /api/command, JSON, body z lat/lon")
    func gotoRequest() throws {
        let target = LatLonE7(latE7: 522_297_000, lonE7: 210_122_000)
        let req = try HTTPCommandRequest.make(.goto(target))
        #expect(req.httpMethod == "POST")
        #expect(req.url?.absoluteString == "http://192.168.4.1/api/command")
        #expect(req.value(forHTTPHeaderField: "Content-Type") == "application/json")
        let obj = try JSONSerialization.jsonObject(with: req.httpBody!) as! [String: Any]
        #expect(obj["cmd"] as? String == "goto")
        #expect(obj["lat_e7"] as? Int == 522_297_000)
    }

    @Test("baseURL można nadpisać (test/lab)")
    func customBaseURL() throws {
        let req = try HTTPCommandRequest.make(.gotoCancel, baseURL: URL(string: "http://127.0.0.1:8080")!)
        #expect(req.url?.absoluteString == "http://127.0.0.1:8080/api/command")
    }
}
