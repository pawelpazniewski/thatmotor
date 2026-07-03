import Foundation

/// Błąd odpowiedzi komendy bez parsowalnej koperty błędu firmware.
public enum CommandResponseError: Error, Equatable {
    case notSuccess(status: Int)
}

/// Czysta walidacja odpowiedzi `POST /api/command` — wyodrębniona z klienta, by
/// mapowanie status/koperta → wynik było host-testowalne bez sieci/Simulatora.
public enum HTTPCommandResponse {
    /// Nie rzuca dla 2xx. Dla nie-2xx: rzuca `ApiError` z koperty `{error}`
    /// (preferowane — typed code/message), a bez parsowalnej koperty
    /// `CommandResponseError.notSuccess`.
    public static func validate(statusCode: Int, body: Data) throws {
        if (200...299).contains(statusCode) { return }
        if let envelope = try? JSONDecoder().decode(CommandEnvelope.self, from: body),
           let apiError = envelope.error {
            throw apiError
        }
        throw CommandResponseError.notSuccess(status: statusCode)
    }
}
