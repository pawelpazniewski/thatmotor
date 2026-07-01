import Testing
@testable import KayakContract

@Suite("Maszyna stanu łącza")
struct LinkStateTests {
    @Test("join → joining, otwarcie/ramka → connected")
    func connects() {
        var m = LinkStateMachine(stalenessThreshold: 1.0)
        m.apply(.joinStarted)
        #expect(m.state == .joining)
        m.apply(.socketOpened)
        #expect(m.state == .connected)
    }

    // Moc wyroczni: po przekroczeniu progu bez ramki → stale. Usunięcie warunku
    // progu (albo zawsze connected) → ten test FAILuje.
    @Test("brak ramki > próg → stale; ramka wraca → connected")
    func goesStaleAndRecovers() {
        var m = LinkStateMachine(stalenessThreshold: 1.0, state: .connected)
        m.apply(.tick(sinceLastFrame: 1.5))
        #expect(m.state == .stale)
        m.apply(.frameReceived)
        #expect(m.state == .connected)
    }

    @Test("tik w oknie świeżości nie zmienia connected")
    func staysFreshWithinWindow() {
        var m = LinkStateMachine(stalenessThreshold: 1.0, state: .connected)
        m.apply(.tick(sinceLastFrame: 0.5))
        #expect(m.state == .connected)
    }

    @Test("zamknięcie socketu → disconnected (z każdego stanu)")
    func closes() {
        var m = LinkStateMachine(state: .stale)
        m.apply(.socketClosed)
        #expect(m.state == .disconnected)
    }

    @Test("tik na rozłączonym łączu nie robi stale")
    func tickOnDisconnectedIsNoop() {
        var m = LinkStateMachine(state: .disconnected)
        m.apply(.tick(sinceLastFrame: 99))
        #expect(m.state == .disconnected)
    }
}
