import Foundation
import KayakContract

enum CommandClientError: Error, Equatable {
    case notHTTP
    case httpStatus(Int)
}

/// Abstrakcja wysyłania komend — pozwala podmienić transport (URLSession →
/// NWConnection/.wifi) bez ruszania warstw wyżej, gdy bramka Unit 0 tego wymaga.
protocol CommandSending: Sendable {
    func send(_ command: Command) async throws
}

/// Klient HTTP komend na bazie `URLSession`. Buduje żądanie czystym
/// `HTTPCommandRequest`, mapuje 400/koperty błędu na `ApiError` (typed, nie string).
struct CommandClient: CommandSending {
    let baseURL: URL
    let session: URLSession

    init(baseURL: URL = HTTPCommandRequest.defaultBaseURL, session: URLSession = makeSession()) {
        self.baseURL = baseURL
        self.session = session
    }

    static func makeSession() -> URLSession {
        let config = URLSessionConfiguration.ephemeral
        config.waitsForConnectivity = true
        config.timeoutIntervalForRequest = 5
        config.timeoutIntervalForResource = 8
        return URLSession(configuration: config)
    }

    func send(_ command: Command) async throws {
        let request = try HTTPCommandRequest.make(command, baseURL: baseURL)
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse else {
            throw CommandClientError.notHTTP
        }
        guard (200...299).contains(http.statusCode) else {
            // Firmware zwraca kopertę błędu z code/message — preferuj typed ApiError.
            if let envelope = try? JSONDecoder().decode(CommandEnvelope.self, from: data),
               let apiError = envelope.error {
                throw apiError
            }
            throw CommandClientError.httpStatus(http.statusCode)
        }
    }
}
