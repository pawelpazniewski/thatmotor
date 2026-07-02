# Kontekst: Spot-lock z aplikacji + latchowany model komend

Branch: `feature/app-spot-lock`
Ostatnia aktualizacja: 2026-07-02

## Postęp — Faza 1 (Firmware) UKOŃCZONA

- **Unit 1** (commit eccc76d): chirurgiczny flip `comms_gated=false` dla `SRC_GOTO` w wywołaniu `hold_or_pause` (spot_lock.c:~218). Utrata linku nie pauzuje goto (R3/R4). Bramka re-latchu i predykat sensoryczny nietknięte. `comms_fresh` prze-dokumentowane (spot_lock.h, loop_step.h). Oracle odwrócenia przepisany test-first (potwierdzona moc wyroczni: 2 testy czerwone przed flipem). Dodatkowo przepisany integracyjny bliźniak w `test_loop_step.c` (`test_goto_persists_on_comms_loss_latch_retained`), który też asertował usuniętą pauzę.
- **Unit 2** (commit 4f4d3ef): `hold_request` w `command_parse_result` + wiersz `{"hold", ...}` w `COMMAND_TABLE`.
- **Unit 3** (commit bae631e): `hold_request` w `control_loop_ui_events` + `to_ui_events`; `hold` nie idzie przez `extract_goto_target` (brak 400). Atomowy grab: `apply_goto_events` na `hold_request` robi jeden `gps_get_state()` → `goto_grab_decide` → engage przy fresh+fix+range.
  - **ODSTĘPSTWO od planu**: pure helper `goto_grab_decide` umieszczony w komponencie **control_loop** (`goto_grab.c/h`), NIE w `web_panel/src/goto_target.c` jak sugerował plan. Powód: `web_panel` REQUIRES `control_loop`, więc odwrotna zależność (control_loop → web_panel, po `goto_target_valid`) byłaby circular dependency (zakazane regułą architektury). Helper ma własny range-check mirrorujący `goto_target_valid` (świadoma drobna duplikacja stałych zamiast couplingu).

Host-testy: 442 przechodzą (było 437; +1 hold parse, +4 grab, 2 przepisane). Weryfikacja: pozostają checkboxy `Weryfikacja:` dla /dev-docs-review + firmware ESP-IDF build (host-only tu nie weryfikuje device buildu).

## Powiązane pliki

### Firmware — do modyfikacji
- `components/web_panel/include/command_parse.h` — dodać `bool hold_request;` do `command_parse_result` + docstring keywordów.
- `components/web_panel/src/command_parse.c` — `{"hold", {.ok=true, .hold_request=true}}` w `COMMAND_TABLE`.
- `components/web_panel/src/http_server.c` — `to_ui_events()` przenosi `hold_request`; gałąź `hold` NIE przez `extract_goto_target` (brak coords → nie 400).
- `components/control_loop/include/control_loop.h` — dodać `bool hold_request;` do `control_loop_ui_events` (struct linie 83-98).
- `components/control_loop/src/control_loop.c` — `apply_goto_events()`: na `hold_request` jeden `gps_get_state()` → pure helper → latch `SRC_GOTO` z własną pozycją.
- `components/control_loop/src/spot_lock.c` — flip `comms_gated` na `false` dla `SRC_GOTO` w `hold_or_pause` (~linia 217); NIE ruszać bramki re-latchu (`is_entering_goto || comms_fresh`) ani gałęzi sensorycznej.
- `components/control_loop/include/spot_lock.h`, `include/loop_step.h` — prze-dokumentować `comms_fresh`.
- `components/web_panel/src/goto_target.c` (+ `.h`) — dołożyć pure helper decyzji grabu (reużywa `goto_target_valid`).

### Firmware — referencje (nie ruszać logiki)
- `components/gps/include/nmea_parse.h` — `gps_state {bool fix; bool fresh; int32_t lat_e7; int32_t lon_e7}`.
- `components/web_panel/include/goto_target.h` — `goto_target_valid()`, `goto_target_from_double()` (pure, host-tested).

### Firmware — testy
- `test/host/test_command_parse.c` — dodać `test_hold_maps_to_hold_request`.
- `test/host/test_spot_lock.c` — przepisać `test_goto_pauses_on_comms_loss_then_resumes_same_target` (:463-491) na test odwrócenia; zachować testy retencji/fresh≠fix/precedence.
- `test/host/test_goto_target.c` — testy pure helpera grabu (jeśli tam trafi).
- Uruchamianie: `test/host/run.sh` (cmake+ninja → `./build/host_tests`).

### iOS — do modyfikacji
- `ios/KayakKit/Sources/KayakContract/Command.swift` — `case hold` (bez współrzędnych, jak `gotoCancel`).
- `ios/KayakMotor/App/AppModel.swift` — `requestSpotLock()` → `.hold`; dodać `AutonomousIntent`; usunąć `keepaliveTick`/idle-timer; dodać `resync()`.
- `ios/KayakKit/Sources/KayakContract/Keepalive.swift` — usunąć `KeepaliveDecision`.
- `ios/KayakMotor/Features/Goto/ControlBarView.swift` — przycisk Spot-lock `enabled:` z readiness (linia 30; wzór Goto 23-27).
- `ios/KayakMotor/RootView.swift` — `@Environment(\.scenePhase)` + `.onChange` → reconnect/resync (okablowanie `onSpotLock` linia 147).
- `ios/KayakMotor/Networking/TelemetrySocket.swift` / `TelemetryStore` — ewentualny jawny `reconnect()`.

### iOS — do stworzenia
- `ios/KayakKit/Sources/KayakContract/SpotLockReadiness.swift` (+ test).
- `ios/KayakKit/Sources/KayakContract/AutonomousModeReconciler.swift` (+ test).

### iOS — testy (Swift Testing: `@Suite`/`@Test`/`#expect`)
- `ios/KayakKit/Tests/KayakContractTests/CommandEnvelopeTests.swift` — `.hold` serializacja.
- `.../SpotLockReadinessTests.swift`, `.../AutonomousModeReconcilerTests.swift` — nowe.
- `.../KeepaliveTests.swift` — usunąć (usuwana funkcjonalność).

## Decyzje techniczne

- **Jedno źródło `SRC_GOTO`**, `hold` = `goto(własny fix)`; bez nowego źródła/telemetrii.
- **Chirurgiczne odwrócenie pauzy** — flip tylko `comms_gated`; `comms_fresh` zostaje w bramce re-latchu (anty-null-island) i realizuje retarget-w-locie.
- **Rozdzielone domeny degradacji** — comms-loss przestaje pauzować; GPS/IMU-loss dalej pauzuje co cykl.
- **Grab fixu atomowo w pętli** — jeden `gps_get_state()`, bramka `fresh && fix && goto_target_valid`.
- **Etykieta lokalna w app** — app-hold i app-goto telemetrycznie identyczne (`goto_state=active`); `spot_lock_state=active` przy `goto_state=off` = pilot CH3.
- **Usunięcie keepalive** — persist nie potrzebuje resendu ani „trzymaj ekran".
- **Resync-on-resume** — iOS zawiesza WS w tle; na `.active` reconnect + uzgodnienie intencji.

## Wiedza instytucjonalna (czego NIE zepsuć)

- `2026-07-01-goto-app-override-validation-retention.md` — P1 (PAUSE) odwracamy; **P3 (null-island retencja) i P2 (walidacja double przed castem) ZACHOWUJEMY**.
- `2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md` — override w gałęzi ARMED; fresh ≠ valid fix (pauza sensoryczna co cykl); `sensor_freshness_stamp` przy ważnym odczycie.
- `2026-06-17-wrap-safe-recency-counter-domain.md` — `comms_fresh` w domenie `now_ms` (`sensor_is_fresh`, wrap-safe).
- `2026-06-17-hard-clamp-test-oracle-power.md` — test odwrócenia różnicuje starą/nową impl. (utrata TYLKO linku, fix trzymany).
- `2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md` — decyzje jako pure `(inputs)→(outputs)`, HAL cienki.

## Zależności między unitami

- Unit 1 (niezależny) → fundament persist.
- Unit 2 (niezależny) → Unit 3 (flaga) + Unit 4 (parytet kontraktu).
- Unit 3 → wymaga Unit 1 (persist) + Unit 2.
- Unit 6 → wymaga Unit 1.
- Unit 7 → wymaga Unit 4 + Unit 6.

## Źródła

- Requirements doc: docs/dev-brainstorms/2026-07-02-app-spot-lock-requirements.md
- Plan techniczny: docs/plans/2026-07-02-001-feat-app-spot-lock-latched-commands-plan.md
