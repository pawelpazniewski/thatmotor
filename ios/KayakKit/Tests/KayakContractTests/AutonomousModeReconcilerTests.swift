import Testing
@testable import KayakContract

@Suite("Uzgodnienie trybu autonomicznego (intencja × telemetria)")
struct AutonomousModeReconcilerTests {
    private func reconcile(_ intent: AutonomousIntent,
                           goto: HoldState, spot: HoldState) -> AutonomousLabel {
        AutonomousModeReconciler.reconcile(intent: intent, gotoState: goto, spotLockState: spot)
    }

    @Test("intencja .hold + goto active → Kotwica")
    func holdActiveIsAnchor() {
        let label = reconcile(.hold, goto: .active, spot: .off)
        #expect(label == .anchor)
        #expect(label.text == "Kotwica")
    }

    @Test("intencja .goto + goto active → Jadę do celu")
    func gotoActiveIsNavigating() {
        #expect(reconcile(.goto, goto: .active, spot: .off) == .navigatingToTarget)
    }

    // Moc wyroczni: goto żyje, ale intencja zgubiona (force-quit) → neutralna
    // „Trzymam pozycję", NIE „Kotwica"/„Jadę". Gdyby reconciler ignorował intencję,
    // ten test by failował.
    @Test("intencja .none + goto active → Trzymam pozycję (po force-quit)")
    func noneActiveIsHolding() {
        let label = reconcile(.none, goto: .active, spot: .off)
        #expect(label == .holdingPosition)
        #expect(label.text == "Trzymam pozycję")
    }

    @Test("goto off + spot_lock active → Pilot przejął (CH3)")
    func gotoOffSpotActiveIsRemote() {
        let label = reconcile(.goto, goto: .off, spot: .active)
        #expect(label == .remoteOverride)
        #expect(label.text == "Pilot przejął (CH3)")
    }

    @Test("goto off + spot_lock off → stan wyczyszczony (bez etykiety)")
    func allOffIsCleared() {
        let label = reconcile(.hold, goto: .off, spot: .off)
        #expect(label == .cleared)
        #expect(label.text == nil)
    }
}
