import Foundation
import KayakContract

/// Podtrzymanie goto: cyklicznie (~2 Hz) wywołuje przekazany `tick`, dopóki nie
/// zatrzymane. Sama decyzja o ponawianiu należy do wołającego (`KeepaliveDecision`).
/// Jawny cleanup przy `stop`/`deinit` — brak wycieku timera.
@MainActor
final class KeepaliveController {
    private var task: Task<Void, Never>?

    func start(interval: TimeInterval = GotoTiming.keepaliveIntervalSeconds,
               tick: @escaping @MainActor () async -> Void) {
        stop()
        task = Task { @MainActor in
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(interval))
                if Task.isCancelled { break }
                await tick()
            }
        }
    }

    func stop() {
        task?.cancel()
        task = nil
    }

    deinit {
        task?.cancel()
    }
}
