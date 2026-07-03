import Foundation
import Testing
@testable import KayakContract

@Suite("Komendy i koperta odpowiedzi")
struct CommandEnvelopeTests {
    @Test("goto serializuje cmd + lat_e7/lon_e7 (int32)")
    func gotoBody() throws {
        let target = LatLonE7(latE7: 522_297_000, lonE7: 210_122_000)
        let data = try Command.goto(target).httpBody()
        let obj = try JSONSerialization.jsonObject(with: data) as! [String: Any]
        #expect(obj["cmd"] as? String == "goto")
        #expect(obj["lat_e7"] as? Int == 522_297_000)
        #expect(obj["lon_e7"] as? Int == 210_122_000)
    }

    @Test("goto_cancel/disarm niosą tylko cmd (bez współrzędnych)")
    func simpleCommands() throws {
        let cancel = try JSONSerialization.jsonObject(
            with: Command.gotoCancel.httpBody()) as! [String: Any]
        #expect(cancel["cmd"] as? String == "goto_cancel")
        #expect(cancel["lat_e7"] == nil)

        let disarm = try JSONSerialization.jsonObject(
            with: Command.disarm.httpBody()) as! [String: Any]
        #expect(disarm["cmd"] as? String == "disarm")
    }

    @Test("hold serializuje tylko cmd=hold (bez lat/lon — kotwica na własnym fixie)")
    func holdBody() throws {
        let obj = try JSONSerialization.jsonObject(with: Command.hold.httpBody()) as! [String: Any]
        #expect(obj["cmd"] as? String == "hold")
        #expect(obj["lat_e7"] == nil)
        #expect(obj["lon_e7"] == nil)
        #expect(obj.count == 1)
    }

    @Test("koperta sukcesu {data:null,error:null} → isSuccess")
    func successEnvelope() throws {
        let env = try JSONDecoder().decode(
            CommandEnvelope.self, from: Data(#"{"data":null,"error":null}"#.utf8))
        #expect(env.isSuccess)
        #expect(env.error == nil)
    }

    @Test("koperta błędu 400 → ApiError z code/message")
    func errorEnvelope() throws {
        let json = #"{"data":null,"error":{"code":"VALIDATION_FAILED","message":"One or more parameters failed validation"}}"#
        let env = try JSONDecoder().decode(CommandEnvelope.self, from: Data(json.utf8))
        #expect(!env.isSuccess)
        #expect(env.error?.code == "VALIDATION_FAILED")
        #expect(env.error?.message.contains("validation") == true)
    }
}
