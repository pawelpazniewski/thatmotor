# Review — Faza 2 (iOS, Unit 4-7) — app-spot-lock

Data: 2026-07-03
Reviewer: dev-autopilot (orkiestrator — bezpośrednio; agent-wrapper utknął na watchdogu 600s, review wykonany w pętli głównej wielosoczewkowo: architektura / poprawność / async / reguły / testy)

## Severity gate: ✅ CZYSTE (tylko P3, brak P1/P2)

### Liczniki
- 🔴 P1 (blocking): **0**
- 🟠 P2 (important): **0**
- 🟡 P3 (nit): **2**

### Typy findingów
- P3 (2): 2 KOD (oba nieszkodliwe dzięki bramkowaniu telemetrią)
- E2E: N/D (natywny iOS — brak warstwy przeglądarki; walidacja: `swift test` 50/50 + `xcodebuild` BUILD SUCCEEDED)

## Walidacja
- **`swift test` (KayakContract):** 50/50 PASS (12 suit). +1 hold, +4 SpotLockReadiness, +5 reconciler, −5 KeepaliveTests (usunięte z funkcją).
- **`xcodebuild` (app-target, Xcode 26.6):** BUILD SUCCEEDED (po `xcodegen generate`).
- **Brak martwych referencji** keepalive/idleTimer/KeepaliveController/KeepaliveDecision (grep czysty; 2 trafienia to komentarze historyczne w AppModel).

## Weryfikacja kluczowych punktów
- **Unit 4** — `Command.hold` → `{"cmd":"hold"}` bez lat/lon (case ≠ `.goto`). `requestSpotLock` ustawia `intent=.hold` + `send(.hold)`. ✓
- **Unit 5** — `SpotLockReadiness` pure, deleguje do `GotoReadiness`, `nil` telemetria → zablokowane. Przycisk `enabled: canEngage(telemetry)`. ✓
- **Unit 6** — czyste usunięcie funkcjonalności; `commsTimeoutSeconds` przeniesiony do `GotoTiming.swift`, nadal używany przez `TelemetryStore`/`LinkStateMachine` jako próg świeżości telemetrii (to NIE usunięty goto-comms-watchdog — ma prawo zostać). Usunięcie `KeepaliveTests` zgodne z regułą (usuwana funkcja). ✓
- **Unit 7** — `AutonomousModeReconciler` pure, reguły poprawne (goto active + intent → etykieta; goto off + spotLock active → override CH3; intent none + goto active → „Trzymam pozycję" po force-quit). `resync()` = `reconnect()` (stop→start, świeży NWConnection) + `reconcileAutonomousState()` (odtworzenie pinezki z telemetrii). `onChange(scenePhase==.active)`. Async cleanup (Task cancel) poprawny. ✓

## Odstępstwa od planu — ocenione jako POPRAWNE
1. `AutonomousIntent` w KayakContract (nie AppModel) — pure reconciler go typuje; KayakContract nie może zależeć od app-targetu (odwrotność łamałaby warstwy). Konieczność.
2. `requestSpotLock` → `lastActionMessage` zamiast `target.markSending/Sent` — kotwica nie ma pinezki mapy (własna pozycja łodzi), sendState pinezki byłby mylący. Wzór jak `disarm`/`stopGoto`.
3. `Keepalive.swift` → `GotoTiming.swift` — po usunięciu `KeepaliveDecision` plik trzyma tylko `commsTimeoutSeconds` (nadal używane); nazwa „Keepalive" byłaby myląca.

## Findingi P3 (nit — nieblokujące, nie naprawiane w tym cyklu)
- 🟡 [P3][KOD] `requestSpotLock`/`requestGoto` ustawiają `autonomousIntent` PRZED potwierdzeniem wysłania. Nieszkodliwe: `autonomousLabel` jest bramkowany telemetrią — przy `gotoState=off` reconcile zwraca `.cleared` niezależnie od intencji, więc nieudane wysłanie nie daje fałszywej „Kotwicy".
- 🟡 [P3][KOD] STOP (`stopGoto`) nie resetuje jawnie `autonomousIntent`. Nieszkodliwe z tego samego powodu (telemetria `gotoState=off` → etykieta `.cleared`). `clearTarget` i `reconcileAutonomousState` resetują intencję jawnie.

## Checkboxy Weryfikacja: — status
- Kod/test-weryfikowalne (swift test zielone, xcodebuild zielony, przycisk wyszarza się, brak resendu): ✓ potwierdzone.
- Device-only (wygaszenie ekranu 30 s, cykl background→foreground na urządzeniu): pozostają MANUALNE (agent-browser N/D dla natywnego iOS) — do weryfikacji na urządzeniu z AP silnika.

## Decyzja
Severity gate CZYSTE (tylko 2× P3 nieszkodliwe) → przejście do Fazy 3 bez cyklu fix.
