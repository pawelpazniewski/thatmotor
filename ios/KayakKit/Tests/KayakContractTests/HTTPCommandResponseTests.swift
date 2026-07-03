import Foundation
import Testing
@testable import KayakContract

@Suite("Walidacja odpowiedzi komendy")
struct HTTPCommandResponseTests {
    @Test("2xx → brak błędu")
    func successPasses() throws {
        try HTTPCommandResponse.validate(statusCode: 200, body: Data(#"{"data":null,"error":null}"#.utf8))
    }

    // Moc wyroczni: 400 z kopertą błędu → typed ApiError (nie zignorowane, nie generic).
    @Test("400 z kopertą → ApiError z code/message")
    func badRequestThrowsApiError() {
        let body = Data(#"{"data":null,"error":{"code":"VALIDATION_FAILED","message":"failed"}}"#.utf8)
        #expect(throws: ApiError(code: "VALIDATION_FAILED", message: "failed")) {
            try HTTPCommandResponse.validate(statusCode: 400, body: body)
        }
    }

    @Test("nie-2xx bez koperty → notSuccess(status)")
    func nonSuccessWithoutEnvelope() {
        #expect(throws: CommandResponseError.notSuccess(status: 500)) {
            try HTTPCommandResponse.validate(statusCode: 500, body: Data("boom".utf8))
        }
    }
}
