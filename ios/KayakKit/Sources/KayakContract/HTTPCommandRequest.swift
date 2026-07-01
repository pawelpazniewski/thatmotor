import Foundation

/// Czysty builder żądania HTTP do `POST /api/command`. Wyodrębniony z klienta,
/// by budowanie żądania (URL, metoda, nagłówki, body) było host-testowalne bez sieci.
public enum HTTPCommandRequest {
    /// Domyślny adres AP silnika.
    public static let defaultBaseURL = URL(string: "http://192.168.4.1")!
    public static let path = "/api/command"

    public static func make(_ command: Command, baseURL: URL = defaultBaseURL) throws -> URLRequest {
        var request = URLRequest(url: baseURL.appendingPathComponent(path))
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try command.httpBody()
        return request
    }
}
