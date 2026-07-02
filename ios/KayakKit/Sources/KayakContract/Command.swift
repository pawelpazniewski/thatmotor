import Foundation

/// Komenda wysyłana do `POST /api/command`. App używa goto/goto_cancel/disarm/hold.
/// `hold` = kotwica w bieżącej pozycji łodzi (firmware łapie własny fix); nie niesie
/// współrzędnych — jak `gotoCancel`. App NIGDY nie uzbraja — brak `arm` w tym enumie celowo.
public enum Command: Equatable, Sendable {
    case goto(LatLonE7)
    case gotoCancel
    case disarm
    case hold

    private var cmdName: String {
        switch self {
        case .goto: return "goto"
        case .gotoCancel: return "goto_cancel"
        case .disarm: return "disarm"
        case .hold: return "hold"
        }
    }

    /// Serializuje body żądania (≤256 B po stronie firmware). Klucze i skalowanie
    /// dokładnie wg kontraktu: goto niesie już-zaokrąglone `lat_e7`/`lon_e7` (int32).
    public func httpBody() throws -> Data {
        var object: [String: Any] = ["cmd": cmdName]
        if case let .goto(target) = self {
            object["lat_e7"] = Int(target.latE7)
            object["lon_e7"] = Int(target.lonE7)
        }
        return try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
    }
}

/// Ustandaryzowany błąd z koperty `{data,error}` firmware.
public struct ApiError: Decodable, Equatable, Error, Sendable {
    public let code: String
    public let message: String

    public init(code: String, message: String) {
        self.code = code
        self.message = message
    }
}

/// Koperta odpowiedzi API: sukces `{"data":null,"error":null}` (200),
/// błąd `{"data":null,"error":{"code","message"}}` (400).
public struct CommandEnvelope: Decodable, Sendable {
    public let error: ApiError?

    private enum CodingKeys: String, CodingKey { case error }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        error = try c.decodeIfPresent(ApiError.self, forKey: .error)
    }

    /// `true`, gdy koperta nie niesie błędu (odpowiedź sukcesu).
    public var isSuccess: Bool { error == nil }
}
