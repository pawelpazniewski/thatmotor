# Podsumowanie: Spot-lock z aplikacji + latchowany model komend

**Data ukończenia:** 2026-07-03
**Branch:** `feature/app-spot-lock`
**Status:** ✅ Ukończone (3 fazy) — walidacja końcowa zielona

## Co zostało dostarczone

Czwarty przycisk iOS (Spot-lock, dotąd placeholder) zmapowany na firmware jako komenda
`hold` = kotwica w bieżącej pozycji łodzi (`goto(własny fix)`, jedno źródło `SRC_GOTO`).
Wraz z tym zmieniony model bezpieczeństwa: komendy z app (goto + spot-lock) stały się
**latchowanymi intencjami trwałymi wobec utraty linku** — firmware przestał pauzować silnik
przy comms-timeout. **Pilot RC pozostał jedynym failsafe.**

Kluczowy zrealizowany use case: ustaw punkt goto / kotwicę, wygaś ekran telefonu, odejdź —
łódź płynie / trzyma pozycję dalej.

### Faza 1 — Firmware (Unit 1-3)
- **Unit 1** (eccc76d): chirurgiczny flip `comms_gated=false` dla `SRC_GOTO` w `hold_or_pause` —
  utrata linku nie pauzuje goto. Bramka re-latchu (`is_entering_goto || comms_fresh`) i predykat
  sensoryczny (GPS/IMU co cykl) nietknięte. Oracle odwrócenia przepisany test-first.
- **Unit 2** (4f4d3ef): keyword `hold` w `command_parse` (pure) → `hold_request`.
- **Unit 3** (bae631e): okablowanie `hold` + atomowy grab własnego fixu (jeden `gps_get_state()`
  → `goto_grab_decide` → latch przy `fresh && fix && in-range`). `hold` bez body nie idzie przez
  `extract_goto_target` (brak 400).
- Post-review (6307321): usunięto martwy parametr/gałąź `comms_gated` po flipie (P2 z review fazy 1).

### Faza 2 — iOS (Unit 4-7)
- **Unit 4** (dc0c42e): `Command.hold` (`{"cmd":"hold"}` bez współrzędnych); `requestSpotLock()`
  → `send(.hold)`; `AutonomousIntent {none,goto,hold}`.
- **Unit 5** (d63d8c8): `SpotLockReadiness` (pure) — przycisk wyszarzony bez fixu.
- **Unit 6** (fc4b5f5): usunięto keepalive / idle-timer (`KeepaliveController`, `KeepaliveDecision`,
  `keepaliveTick`, `isIdleTimerDisabled`). `Keepalive.swift` → `GotoTiming.swift` (zostaje
  `commsTimeoutSeconds` jako próg świeżości telemetrii).
- **Unit 7** (d8fe38e): `AutonomousModeReconciler` (pure) → etykieta trybu; `resync()` =
  `reconnect()` + `reconcileAutonomousState()` na `scenePhase == .active`.

### Faza 3 — Dokumentacja
- `docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md` — baner
  o odwróceniu P1 (PAUSE→CONTINUE); P2/P3 dalej obowiązują.
- `docs/completed/kayak-motor-firmware-v1/known-issues.md` — §4d oznaczone jako odwrócone, nowa §4e.

## Walidacja końcowa

- **Firmware host-tests:** 442/0 PASS (baseline 437; +1 hold parse, +4 grab, 2 przepisane oracle).
- **iOS `swift test` (KayakContract):** 50/50 PASS (baseline 45; +1 hold, +4 SpotLockReadiness,
  +5 reconciler, −5 KeepaliveTests usunięte wraz z funkcją).
- **iOS `xcodebuild` (app-target, Xcode 26.6):** BUILD SUCCEEDED (po `xcodegen generate`).
- **Review Fazy 1:** gate ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI (P1=0, P2=1 naprawiony, P3=4 odroczone).
- **Review Fazy 2:** gate ✅ CZYSTE (P1=0, P2=0, P3=2 nieszkodliwe, bramkowane telemetrią).

## Podjęte kluczowe decyzje

- **Jedno źródło `SRC_GOTO`**: `hold` = `goto(własny fix)`; bez nowego trybu/telemetrii firmware.
- **Chirurgiczne odwrócenie pauzy**: flip TYLKO `comms_gated`; `comms_fresh` zostaje w bramce
  re-latchu (anty-null-island) i realizuje retarget-w-locie.
- **Rozdzielone domeny degradacji**: comms-loss przestaje pauzować; GPS/IMU-loss dalej pauzuje co cykl.
- **Grab fixu atomowo w pętli**: jeden `gps_get_state()`, bramka `fresh && fix && in-range`.
- **Etykieta trybu lokalnie w app** (reconciler): app-hold i app-goto telemetrycznie identyczne
  (`goto_state=active`); `spot_lock_state=active` przy `goto_state=off` = pilot przejął CH3.
- **Usunięcie keepalive**: persist nie potrzebuje resendu ani „trzymaj ekran".

## Odstępstwa od planu (ocenione jako POPRAWNE w review)

1. **`goto_grab_decide` w komponencie `control_loop`** (nie `web_panel`), bo `web_panel` REQUIRES
   `control_loop` — odwrotna zależność byłaby circular dependency. Helper ma własny range-check
   mirrorujący `goto_target_valid` (świadoma drobna duplikacja stałych zamiast couplingu).
2. **`AutonomousIntent` w KayakContract** (nie AppModel) — pure reconciler musi go typować,
   a KayakContract nie może zależeć od app-targetu.
3. **`requestSpotLock` → `lastActionMessage`** zamiast `target.markSending/Sent` — kotwica nie ma
   pinezki mapy (własna pozycja łodzi); wzór jak `disarm`/`stopGoto`.
4. **`Keepalive.swift` → `GotoTiming.swift`** — po usunięciu `KeepaliveDecision` plik trzyma tylko
   `commsTimeoutSeconds`; nazwa „Keepalive" byłaby myląca.

## Główne pliki (utworzone / zmodyfikowane)

### Firmware
- `components/control_loop/src/spot_lock.c` — flip `comms_gated=false`, usunięcie martwej gałęzi.
- `components/control_loop/src/goto_grab.{c,h}` — **nowy** pure helper `goto_grab_decide`.
- `components/control_loop/src/control_loop.c` — `apply_goto_events` na `hold_request` (atomowy grab).
- `components/web_panel/src/command_parse.c` (+ `.h`) — keyword `hold` → `hold_request`.
- `components/web_panel/src/http_server.c` — `to_ui_events` przenosi `hold_request` (bez `extract_goto_target`).
- `test/host/{test_spot_lock.c, test_command_parse.c, test_goto_target.c, test_loop_step.c}` — testy.

### iOS
- `ios/KayakKit/Sources/KayakContract/Command.swift` — `case hold`; `AutonomousIntent`.
- `ios/KayakKit/Sources/KayakContract/SpotLockReadiness.swift` — **nowy** (pure readiness).
- `ios/KayakKit/Sources/KayakContract/AutonomousModeReconciler.swift` — **nowy** (pure reconciler).
- `ios/KayakKit/Sources/KayakContract/GotoTiming.swift` — zmiana nazwy z `Keepalive.swift`.
- `ios/KayakMotor/App/AppModel.swift` — `requestSpotLock`, `resync`, usunięcie keepalive/idle-timer.
- `ios/KayakMotor/RootView.swift` — `scenePhase` → `resync()`.
- `ios/KayakMotor/Features/{Goto/ControlBarView.swift, Telemetry/TelemetryStore.swift}` — enable/reconnect.
- `ios/KayakKit/Tests/KayakContractTests/{SpotLockReadinessTests, AutonomousModeReconcilerTests}.swift` — **nowe**.
- Usunięte: `KeepaliveController.swift`, `KeepaliveTests.swift` (wraz z funkcjonalnością).

## Wyciągnięte wnioski

- **Odwracanie zweryfikowanego zachowania failsafe = chirurgiczny flip + moc wyroczni**: zmiana
  jednej flagi (`comms_gated`), a przywrócenie `true` musi czynić test odwrócenia CZERWONYM
  (empirycznie potwierdzone — 2 testy). Krytyczne inwarianty (bramka re-latchu, predykat sensoryczny,
  ścieżka RC) niezależnie zweryfikowane grepem i pokryciem testów.
- **Martwa ścieżka w module failsafe jest re-armowalnym re-odwróceniem** — usunięcie parametru po
  flipie czyni inwariant strukturalnym, nie konwencją call-site (P2 z review fazy 1, naprawiony).
- **Circular dependency wygrywa nad kolokacją**: pure helper trafia do modułu, który nie tworzy
  cyklu, nawet kosztem drobnej duplikacji stałych geo (§11 reguł: duplication > complexity).
- **Pure typy współdzielone przez app + testy idą do warstwy kontraktu**, nie do app-targetu
  (`AutonomousIntent` w KayakContract) — inaczej pure reconciler nie może ich typować.
- **Usuwanie funkcjonalności = usuwanie jej testów** (KeepaliveTests) — to NIE osłabianie asercji.

## Świadomie odroczone (znane ograniczenia, nie niedokończona praca)

- **4 P3-nity Fazy 1** (kolizja nazwy `hold_request`↔`SRC_HOLD`; testy grab poza dedykowanym
  plikiem; niepinowana inkluzywna granica geo; brak drift-guardu stałych) — pominięte per pipeline,
  udokumentowane w `review-faza-1.md`.
- **Device-E2E** (wygaszenie ekranu 30 s podczas goto; cykl background→foreground z odtworzeniem
  pinezki; CH3-preempt kotwicy) — natywny iOS, agent-browser N/D, weryfikacja manualna na urządzeniu.
- **1 test app-target** (tap Spot-lock → `AutonomousIntent=.hold` w AppModel — UIKit, poza pakietem
  SPM) — niedostępny w `swift test`, pokryty compile-verify przez `xcodebuild`.

## Źródła

- Requirements doc: `docs/dev-brainstorms/2026-07-02-app-spot-lock-requirements.md`
- Plan techniczny: `docs/plans/2026-07-02-001-feat-app-spot-lock-latched-commands-plan.md`
