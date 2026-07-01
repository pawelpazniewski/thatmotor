import Testing
@testable import KayakContract

@Suite("Decyzja keepalive i idle-timer")
struct KeepaliveTests {
    @Test("keepalive interwał < timeout watchdogu firmware")
    func intervalBelowWatchdog() {
        #expect(GotoTiming.keepaliveIntervalSeconds < GotoTiming.commsTimeoutSeconds)
    }

    @Test("ponawia gdy goto aktywne/pauza i jest cel")
    func resendsWhenActiveOrPaused() {
        #expect(KeepaliveDecision.shouldResend(gotoState: .active, hasTarget: true))
        #expect(KeepaliveDecision.shouldResend(gotoState: .paused, hasTarget: true))
    }

    // Moc wyroczni: po STOP goto=off → NIE ponawiać. Usunięcie warunku „tylko gdy
    // active/paused" (np. zawsze true) → ten test FAILuje = keepalive nie zatrzymuje się.
    @Test("nie ponawia gdy goto off (po STOP)")
    func stopsWhenOff() {
        #expect(!KeepaliveDecision.shouldResend(gotoState: .off, hasTarget: true))
        #expect(!KeepaliveDecision.shouldResend(gotoState: .unknown, hasTarget: true))
    }

    @Test("nie ponawia bez celu")
    func noResendWithoutTarget() {
        #expect(!KeepaliveDecision.shouldResend(gotoState: .active, hasTarget: false))
    }

    @Test("idle-timer blokowany tylko podczas aktywnego przejazdu")
    func idleTimerOnlyDuringActive() {
        #expect(KeepaliveDecision.shouldDisableIdleTimer(gotoState: .active))
        #expect(!KeepaliveDecision.shouldDisableIdleTimer(gotoState: .paused))
        #expect(!KeepaliveDecision.shouldDisableIdleTimer(gotoState: .off))
    }
}
