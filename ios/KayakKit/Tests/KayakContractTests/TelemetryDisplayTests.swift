import Foundation
import Testing
@testable import KayakContract

@Suite("Prezentacja telemetrii — format, staleness, gate trimu")
struct TelemetryDisplayTests {
    @Test("stateLabel mapuje stan na etykietę")
    func stateLabelMapsState() {
        #expect(TelemetryDisplay.stateLabel(.armed) == "ARMED")
        #expect(TelemetryDisplay.stateLabel(.failsafe) == "FAILSAFE")
        #expect(TelemetryDisplay.stateLabel(.disarmed) == "DISARMED")
    }

    @Test("targetText składa dystans i namiar")
    func targetTextFormats() {
        // bearingDeg10 450 → 45.0° ; errM 123
        #expect(TelemetryDisplay.targetText(errM: 123, bearingDeg10: 450) == "123 m · 45°")
    }

    // Moc wyroczni: stale MUSI dać myślnik. Bez transformacji (return value)
    // ten test failuje, bo isFresh:false wciąż zwróciłoby "5.0 m/s".
    @Test("displayed zwraca myślnik przy stale, wartość przy fresh")
    func displayedGatesOnFreshness() {
        #expect(TelemetryDisplay.displayed("5.0 m/s", isFresh: false) == "—")
        #expect(TelemetryDisplay.displayed("5.0 m/s", isFresh: true) == "5.0 m/s")
    }

    @Test("modeLabel rozróżnia spot-lock, goto pauzę i off")
    func modeLabelDistinguishesModes() {
        let spotLock = TelemetryDisplay.modeLabel(spotLock: .active, goto: .off)
        let gotoPaused = TelemetryDisplay.modeLabel(spotLock: .off, goto: .paused)
        let off = TelemetryDisplay.modeLabel(spotLock: .off, goto: .off)
        // Trzy różne etykiety — żadna para nie może się pokrywać.
        #expect(spotLock != gotoPaused)
        #expect(gotoPaused != off)
        #expect(spotLock != off)
    }

    // Moc wyroczni: bez sprawdzenia fix funkcja zwróciłaby "0 sat" (mylące).
    @Test("gpsQualityText bez fixu → brak fix, nie 0 sat")
    func gpsQualityTextNoFix() {
        #expect(TelemetryDisplay.gpsQualityText(fix: false, sats: 0) == "brak fix")
        #expect(TelemetryDisplay.gpsQualityText(fix: true, sats: 12) == "12 sat")
    }

    // Moc wyroczni: mutacja isTrimEnabled na „zawsze true" MUSI failować
    // na którymś z pozostałych stanów.
    @Test("isTrimEnabled dla DISARMED i ARMED")
    func trimEnabledDisarmedAndArmed() {
        #expect(TelemetryDisplay.isTrimEnabled(state: .disarmed) == true)
        #expect(TelemetryDisplay.isTrimEnabled(state: .armed) == true)
        for state: SystemState in [.failsafe, .escCalibration, .deploy, .unknown] {
            #expect(TelemetryDisplay.isTrimEnabled(state: state) == false)
        }
    }

    // Moc wyroczni: fresh tylko dla .connected. Mutacja „zawsze true" failuje
    // na .stale/.disconnected/.joining.
    @Test("isFresh tylko dla connected")
    func isFreshOnlyConnected() {
        #expect(TelemetryDisplay.isFresh(.connected) == true)
        for link: LinkState in [.stale, .disconnected, .joining] {
            #expect(TelemetryDisplay.isFresh(link) == false)
        }
    }

    @Test("trimText zachowuje znak")
    func trimTextKeepsSign() {
        #expect(TelemetryDisplay.trimText(servoTrimUs: 120) == "+120 µs")
        #expect(TelemetryDisplay.trimText(servoTrimUs: -140) == "-140 µs")
    }

    @Test("speedHeadingText składa prędkość i kurs")
    func speedHeadingTextFormats() {
        // 150 cm/s → 1.5 m/s ; 1234 deg10 → 123°
        #expect(TelemetryDisplay.speedHeadingText(speedCms: 150, headingDeg10: 1234) == "1.5 m/s · 123°")
    }

    // Moc wyroczni: oba źródła aktywne, RÓŻNE pola → wynik MUSI pochodzić z goto.
    // Mutacja odwracająca priorytet dałaby "200 m · 180°" i test failuje.
    @Test("activeTargetText: oba źródła aktywne → wybiera goto (priorytet)")
    func activeTargetPrefersGoto() {
        let text = TelemetryDisplay.activeTargetText(
            gotoState: .active, gotoErrM: 100, gotoBearingDeg10: 900,
            spotLockState: .active, spotLockErrM: 200, spotLockBearingDeg10: 1800)
        #expect(text == "100 m · 90°")
    }

    // Goto zgaszony → spadamy na spot-lock, z pól spot-locka (nie goto).
    @Test("activeTargetText: tylko spot-lock aktywny → wybiera spot-lock")
    func activeTargetFallsBackToSpotLock() {
        let text = TelemetryDisplay.activeTargetText(
            gotoState: .off, gotoErrM: 100, gotoBearingDeg10: 900,
            spotLockState: .active, spotLockErrM: 200, spotLockBearingDeg10: 1800)
        #expect(text == "200 m · 180°")
    }

    // Żadne źródło nie zaangażowane (off/unknown) → brak celu (tryb ręczny).
    @Test("activeTargetText: żadne źródło aktywne → nil")
    func activeTargetNoneWhenManual() {
        #expect(TelemetryDisplay.activeTargetText(
            gotoState: .off, gotoErrM: 100, gotoBearingDeg10: 900,
            spotLockState: .unknown, spotLockErrM: 200, spotLockBearingDeg10: 1800) == nil)
    }

    @Test("boolText mapuje wartość logiczną na Tak/Nie")
    func boolTextMapsBool() {
        #expect(TelemetryDisplay.boolText(true) == "Tak")
        #expect(TelemetryDisplay.boolText(false) == "Nie")
    }

    // Moc wyroczni: każdy stan ma odrębną etykietę, unknown → myślnik.
    @Test("holdStateLabel mapuje stan hold, unknown → myślnik")
    func holdStateLabelMapsState() {
        #expect(TelemetryDisplay.holdStateLabel(.off) == "Wyłączony")
        #expect(TelemetryDisplay.holdStateLabel(.active) == "Aktywny")
        #expect(TelemetryDisplay.holdStateLabel(.paused) == "Pauza")
        #expect(TelemetryDisplay.holdStateLabel(.unknown) == "—")
    }

    // Goto w PAUZIE też ma pierwszeństwo i bierze WŁASNE pola — potwierdza, że
    // errM/bearing nie są mylone między źródłami (spot-lock miałby inne wartości).
    @Test("activeTargetText: goto w pauzie ma pierwszeństwo, z pól goto")
    func activeTargetGotoPausedUsesGotoFields() {
        let text = TelemetryDisplay.activeTargetText(
            gotoState: .paused, gotoErrM: 55, gotoBearingDeg10: 100,
            spotLockState: .active, spotLockErrM: 999, spotLockBearingDeg10: 3599)
        #expect(text == "55 m · 10°")
    }

    @Test("calibratedHeadingDegrees: zero offset to identyczność")
    func calibratedHeadingZeroOffsetIsIdentity() {
        #expect(TelemetryDisplay.calibratedHeadingDegrees(50.8, offsetDegrees: 0) == 50.8)
    }

    @Test("calibratedHeadingDegrees: dodaje offset wprost, bez zawijania")
    func calibratedHeadingAddsOffset() {
        #expect(TelemetryDisplay.calibratedHeadingDegrees(50.8, offsetDegrees: 10) == 60.8)
        #expect(TelemetryDisplay.calibratedHeadingDegrees(50.8, offsetDegrees: -10) == 40.8)
    }

    @Test("calibratedHeadingDegrees: zawija powyżej 360 (moc wyroczni: identyczność by nie złapała braku zawijania)")
    func calibratedHeadingWrapsAboveRange() {
        #expect(TelemetryDisplay.calibratedHeadingDegrees(350, offsetDegrees: 20) == 10)
    }

    @Test("calibratedHeadingDegrees: zawija poniżej 0 (moc wyroczni: dodatni offset by nie złapał brakującego +360)")
    func calibratedHeadingWrapsBelowRange() {
        #expect(TelemetryDisplay.calibratedHeadingDegrees(5, offsetDegrees: -10) == 355)
    }
}
