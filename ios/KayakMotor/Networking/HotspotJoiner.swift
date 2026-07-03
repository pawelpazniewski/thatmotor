import Foundation
import NetworkExtension

/// Dołączanie do AP silnika. `joinOnce=false`, by sieć została zapamiętana.
/// Programowy join bywa zawodny (bug DHCP iOS) → UI ma fallback „dołącz ręcznie".
enum HotspotJoiner {
    static let ssid = "kayak-motor"
    static let passphrase = "CHANGE-ME-kayak"

    static func join() async throws {
        let config = NEHotspotConfiguration(ssid: ssid, passphrase: passphrase, isWEP: false)
        config.joinOnce = false
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            NEHotspotConfigurationManager.shared.apply(config) { error in
                if let error,
                   (error as NSError).code != NEHotspotConfigurationError.alreadyAssociated.rawValue {
                    cont.resume(throwing: error)
                } else {
                    cont.resume()
                }
            }
        }
    }
}
