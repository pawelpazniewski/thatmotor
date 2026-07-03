---
title: "Klient (iOS) odwzorowuje cichy state-gate firmware zamiast polegać na błędzie serwera; logika prezentacji w host-testowalnym module"
date: 2026-07-03
category: testing-issues
severity: medium
stack:
  - Swift
  - SwiftUI
  - SPM
  - ESP-IDF
tags:
  - spec-revision
  - failsafe
  - state-gate
  - pure-hal
  - host-test
  - oracle-power
  - single-source-of-truth
status: verified
last_verified: 2026-07-03
---

# Klient odwzorowuje cichy gate firmware + logika prezentacji poza widokiem

Feature `ios-telemetry-hud`: dwupoziomowa telemetria w aplikacji iOS (HUD w rogu +
dolny arkusz) plus regulacja neutrala serwa (trim). Trzy nietrywialne rozstrzygnięcia
warte utrwalenia — wszystkie na styku klient↔firmware.

## Symptomy

- Brainstorm/plan zakładał trim serwa „na żywo, niezależnie od stanu, ostrzeżenie
  bez blokady". Firmware stosuje trim **wyłącznie w stanie DISARMED**
  (`loop_should_apply_pending` / `apply_trim_events` w `control_loop.c`); przy ARMED
  komenda HTTP wraca **200 OK i jest cicho ignorowana** — brak błędu do wykrycia po stronie klienta.
- Optimistic-UI dla wartości sterowanej (lokalna kopia `servo_trim_us`) rozjeżdżałby się
  ze stanem urządzenia — nowa wartość wraca dopiero kolejną ramką telemetrii (~100 ms).
- Cała logika prezentacji/decyzji groziła osadzeniem w widokach SwiftUI (target aplikacji
  bez testów). Review złapał P2: wybór aktywnego źródła celu (goto>spot-lock) przeciekł do
  nietestowalnego `HudView.target`.

## Root Cause

1. **Spec↔firmware mismatch:** klient chciał zachowania „na żywo", a urządzenie ma celowy
   gate stanu (safety-critical). Kanał HTTP nie sygnalizuje odrzucenia (200 OK), więc klient
   nie ma sygnału błędu do reakcji.
2. **Brak jednego źródła prawdy:** wartość zapisana na urządzeniu (`servo_trim_us`) i lokalna
   kopia w UI to dwie prawdy, które desynchronizują się przez opóźnienie ramki.
3. **Logika w warstwie nietestowalnej:** target aplikacji iOS nie ma testów; decyzja
   umieszczona w widoku = zero pokrycia i moc wyroczni = 0.

## Rozwiązanie

### 1. UI odwzorowuje gate firmware wg stanu z TELEMETRII (nie wg błędu serwera)

Sekcja trimu aktywna tylko dla `.disarmed`; poza tym wyszarzona z notką. Nie ruszamy kodu
safety-critical — UI-guard jest kosmetyczny, realny gate egzekwuje firmware.

```swift
/// Firmware stosuje trim WYŁĄCZNIE po rozbrojeniu → UI aktywuje sekcję tylko dla .disarmed.
public static func isTrimEnabled(state: SystemState) -> Bool {
    state == .disarmed
}
```

### 2. Jedno źródło prawdy z telemetrii dla wartości sterowanej

Komendy trimu są **krokowe** (`trimLeft/trimRight/trimSave`, bez parametru — zgodnie z
modelem firmware). UI NIE trzyma lokalnej kopii `servo_trim_us`; wyświetla wartość z ramki
telemetrii. Zmiana przychodzi kolejną ramką (~100 ms). Eliminuje rozjazd optimistic-UI vs urządzenie.

### 3. Logika prezentacji/decyzji w czystym module SPM (Pure⊥HAL na iOS)

Format, staleness→`—`, gate trimu ORAZ wybór aktywnego źródła celu wyekstrahowane do
`KayakContract/TelemetryDisplay.swift` (bez importu SwiftUI, host-testowalne). Wspólny
predykat `isEngaged` zapewnia jedną regułę priorytetu goto>spot-lock dla `modeLabel` i
`activeTargetText` — nie rozjadą się.

```swift
private static func isEngaged(_ state: HoldState) -> Bool {
    state == .active || state == .paused
}
// modeLabel i activeTargetText używają TEGO SAMEGO isEngaged → jedna reguła priorytetu
public static func activeTargetText(gotoState: HoldState, /* ... */) -> String? {
    if isEngaged(gotoState) { return targetText(errM: gotoErrM, bearingDeg10: gotoBearingDeg10) }
    if isEngaged(spotLockState) { return targetText(errM: spotLockErrM, bearingDeg10: spotLockBearingDeg10) }
    return nil // tryb ręczny — brak celu
}
```

Host-testy z mocą wyroczni: stale MUSI dać myślnik; `isTrimEnabled` tylko `.disarmed`;
konwersje deg10/cm-s z wejściem POZA wyjściem — mutacja bramki failuje test. Wynik: 71/71
zielone, app target BUILD SUCCEEDED.

## Komendy diagnostyczne

```bash
# Weryfikacja kluczy JSON 1:1 z firmware (kontrakt klient↔urządzenie)
grep -n "servo_trim_us\|spot_lock\|imu_calib" components/web_panel/src/telemetry_json.c
# Gdzie firmware faktycznie stosuje trim (potwierdź gate stanu)
grep -rn "apply_trim\|should_apply_pending\|DISARMED" components/control_loop/src/
# Host-testy prezentacji
cd ios/KayakKit && swift test --filter TelemetryDisplayTests
```

## Zapobieganie

- Gdy klient chce „na żywo", a urządzenie ma celowy gate stanu — **dostosuj UI do gate'u**
  (odczyt stanu z telemetrii), nie osłabiaj failsafe i NIE polegaj na błędzie serwera, którego
  nie ma (200 OK ≠ wykonano).
- UI-guard odwzorowuje, nie zastępuje gate'u urządzenia — realny gate zostaje w firmware.
- Wartość zapisywana na urządzeniu = jedno źródło prawdy z telemetrii; nie duplikuj jej
  lokalnie w UI (opóźnienie ramki rozjeżdża kopie).
- Target UI bez testów → wypchnij WSZYSTKĄ logikę decyzji do czystego modułu (SPM, bez UI
  frameworka) i pokryj host-testami z mocą wyroczni; widoki składają gotowe stringi.
- Klucze JSON i znak pól (`%d` dla `servo_trim_us`) weryfikuj 1:1 ze źródłem firmware.

## Powiązane

- `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md` — Pure⊥HAL (tu rozszerzone na warstwę UI iOS)
- `docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md` — moc wyroczni w testach transformacji
- `docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md` — pierwszeństwo failsafe (tu: klient ustępuje gate'owi firmware)
- `docs/solutions/testing-issues/2026-07-03-safe-failsafe-inversion-oracle-rewrite-on-spec-change.md` — rewizja spec bez osłabiania wyroczni

## Kontekst

Branch `feature/ios-telemetry-hud`, zarchiwizowany w `docs/completed/ios-telemetry-hud/`.
5 Unitów, walidacja host 71/71 + iOS Simulator build. Rewizja R5 była decyzją użytkownika
na styku spec↔firmware (trim tylko DISARMED). Scenariusze [E2E] symulatorowe pozostawione
jako ręczny follow-up (brak interaktywnego symulatora/urządzenia w tym środowisku).
