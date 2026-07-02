# Zadania: Spot-lock z aplikacji + latchowany model komend

Branch: `feature/app-spot-lock`
Ostatnia aktualizacja: 2026-07-02

Nakład: S ≤0.5d · M ~1d · L ~2d · XL >2d

---

## Faza 1 — Firmware

### Unit 1: Odwrócenie PAUSE→CONTINUE dla `SRC_GOTO` (chirurgiczny flip) — **M** ⚠️ najwyższe ryzyko

Zależności: brak. Realizuje: R3, R4.

**Implementacja:**
- [x] Przepisać test wyroczni PRZED zmianą kodu (test-first): `test/host/test_spot_lock.c` — scenariusz odwrócenia.
- [x] `components/control_loop/src/spot_lock.c` — flip `comms_gated` na `false` w wywołaniu `hold_or_pause` dla gałęzi `SRC_GOTO` (~linia 217).
- [x] NIE ruszać bramki re-latchu `if (is_entering_goto || in->comms_fresh)` ani predykatu sensorycznego `!gps_fresh||!imu_ok||!gps_has_fix`.
- [x] `components/control_loop/include/spot_lock.h` + `include/loop_step.h` — prze-dokumentować `comms_fresh` („link failsafe" → „świeżość retargetu / bramka re-latchu").

**Testy:**
- [x] Test: ARMED + `SRC_GOTO` ACTIVE + `comms_fresh=false` + fix ważny → substate ACTIVE, `throttle_cmd>0`, `ref_*` zachowany (NOWY oracle odwrócenia).
- [x] Test: `comms_fresh=false` + upstream zeruje `goto_lat/lon` → `ref_*` == ostatni dobry (nie 0,0); mutacja „bezwarunkowy re-latch" failuje (ZACHOWANY).
- [x] Test: `SRC_GOTO` ACTIVE + `gps_has_fix=false` → PAUSED co cykl; mutacja „usuń re-walidację sensoryczną" failuje (ZACHOWANY).
- [x] Test: `!armed` → OFF; `SRC_HOLD` + `comms_fresh=false` → nadal ACTIVE (precedence, ZACHOWANY).

**Weryfikacja:**
- [ ] Weryfikacja: `test/host/run.sh` zielone; test odwrócenia przechodzi, a przywrócenie `comms_gated=true` czyni go czerwonym (moc wyroczni potwierdzona).

---

### Unit 2: Komenda `hold` w `command_parse` (pure) — **S**

Zależności: brak. Realizuje: R1, R2.

**Implementacja:**
- [x] `components/web_panel/include/command_parse.h` — dodać `bool hold_request;` + zaktualizować docstring listy keywordów.
- [x] `components/web_panel/src/command_parse.c` — dodać `{"hold", {.ok=true, .hold_request=true}}` do `COMMAND_TABLE`.
- [x] `test/host/test_command_parse.c` — dodać `test_hold_maps_to_hold_request`.

**Testy:**
- [x] Test: `command_parse("hold")` → `ok=true`, `hold_request=true`, `goto_request=false`, `goto_cancel_request=false`.
- [x] Test: nieznany keyword nadal `ok=false` (regresja — istniejący test bez zmian).

**Weryfikacja:**
- [ ] Weryfikacja: host-tests zielone; `test_command_parse` +1 test.

---

### Unit 3: Okablowanie `hold` + atomowy grab własnego fixu — **M**

Zależności: Unit 2 (flaga), Unit 1 (persist). Realizuje: R1, R2, R6 (bramka fixu).

**Implementacja:**
- [ ] `components/control_loop/include/control_loop.h` — dodać `bool hold_request;` do `control_loop_ui_events`.
- [ ] `components/web_panel/src/http_server.c` — `to_ui_events` przenosi `hold_request`; gałąź `hold` NIE przez `extract_goto_target` (brak coords → nie zwracać 400).
- [ ] Pure helper decyzji grabu: `(fresh, fix, lat_e7, lon_e7) → {engage, lat, lon}` = `fresh && fix && goto_target_valid(...)` (w `components/web_panel/src/goto_target.c` lub sąsiedztwie).
- [ ] `components/control_loop/src/control_loop.c` — `apply_goto_events`: na `hold_request` jeden `gps_get_state()` → helper → przy engage `s_goto_engage=true`, `s_goto_lat/lon_e7` z fixu, stamp `s_last_goto_ms`.

**Testy:**
- [ ] Test: helper `fresh=true, fix=true, lat/lon w zakresie` → `engage=true`, cel = wejście.
- [ ] Test: helper `fix=false` (seed-fresh) → `engage=false`; mutacja „pomiń bramkę fix" failuje.
- [ ] Test: helper lat/lon POZA int32 / INF → `engage=false` (reużycie `goto_target_valid`).
- [ ] Test: `command_parse` regresja — `hold` nie ustawia `goto_lat/lon`.

**Weryfikacja:**
- [ ] Weryfikacja: host-tests zielone; przegląd: `hold` bez body nie zwraca 400; przy fixie `s_goto_engage` latchuje `SRC_GOTO` z własną pozycją.

---

## Faza 2 — iOS

### Unit 4: `.hold` w kontrakcie + mapowanie przycisku + lokalna intencja — **S**

Zależności: Unit 2 (firmware rozumie `hold`). Realizuje: R1, R5, R7.

**Implementacja:**
- [ ] `ios/KayakKit/Sources/KayakContract/Command.swift` — `case hold`; `cmdName`→`"hold"`; `httpBody()` bez współrzędnych (jak `gotoCancel`).
- [ ] `ios/KayakMotor/App/AppModel.swift` — `requestSpotLock()` → `commands.send(.hold)` + `target.markSending/Sent`; dodać `enum AutonomousIntent { none, goto, hold }` ustawiane przy tapnięciu.

**Testy:**
- [ ] Test: `Command.hold.httpBody()` → `{"cmd":"hold"}` (bez lat/lon).
- [ ] Test: `Command.hold.cmdName == "hold"`.
- [ ] Test: tap Spot-lock → `AutonomousIntent=.hold`; tap Goto → `.goto`.

**Weryfikacja:**
- [ ] Weryfikacja: testy KayakContract zielone; tap Spot-lock POST-uje `hold`.

---

### Unit 5: Wyszarzanie Spot-lock bez fixu (pure readiness) — **S**

Zależności: Unit 4. Realizuje: R6.

**Implementacja:**
- [ ] Stwórz `ios/KayakKit/Sources/KayakContract/SpotLockReadiness.swift` — `canEngage(telemetry)` (+ opcjonalny `blockReason`).
- [ ] `ios/KayakMotor/Features/Goto/ControlBarView.swift` — przycisk Spot-lock `enabled:` z readiness (wzór Goto linie 23-27).
- [ ] Stwórz `ios/KayakKit/Tests/KayakContractTests/SpotLockReadinessTests.swift`.

**Testy:**
- [ ] Test: `gpsFix=false` → `canEngage=false` (+ blockReason „brak GPS").
- [ ] Test: `gpsFix=true` (+ ARMED jeśli w telemetrii) → `canEngage=true`.
- [ ] Test: `telemetry=nil` → `canEngage=false`.

**Weryfikacja:**
- [ ] Weryfikacja: testy zielone; w UI przycisk 0.5 opacity + `.disabled` bez fixu.

---

### Unit 6: Usunięcie keepalive / idle-timer — **S**

Zależności: Unit 1 (firmware nie pauzuje). Realizuje: R3.

**Implementacja:**
- [ ] `ios/KayakMotor/App/AppModel.swift` — usunąć `keepaliveTick`, `KeepaliveController`, ustawianie `UIApplication.isIdleTimerDisabled`.
- [ ] `ios/KayakKit/Sources/KayakContract/Keepalive.swift` — usunąć `KeepaliveDecision` (decyzja o pełnym usunięciu pliku w implementacji).
- [ ] `ios/KayakKit/Tests/KayakContractTests/KeepaliveTests.swift` — usunąć (usuwana funkcjonalność).

**Testy:**
- [ ] Test: brak referencji do `KeepaliveDecision`/`isIdleTimerDisabled` (kompilacja + brak martwego kodu).
- [ ] Test (urządzenie): goto → wygaś ekran 30 s → łódź kontynuuje; po odblokowaniu tryb aktywny (agent-browser N/D — manualnie).

**Weryfikacja:**
- [ ] Weryfikacja: build iOS zielony; ekran gaśnie normalnie; brak resendu w logach sieci.

---

### Unit 7: Resync-on-resume + reconciler intencji — **M**

Zależności: Unit 4 (intencja), Unit 6 (brak keepalive). Realizuje: R7, R8.

**Implementacja:**
- [ ] Stwórz `ios/KayakKit/Sources/KayakContract/AutonomousModeReconciler.swift` — pure `(intent, gotoState, spotLockState) → etykieta/stan`.
- [ ] `ios/KayakMotor/RootView.swift` — `@Environment(\.scenePhase)` + `.onChange(of:)` → na `.active` reconnect WS + `resync()`.
- [ ] `ios/KayakMotor/App/AppModel.swift` — `resync()`: reconnect telemetrii; uzgodnienie `AutonomousIntent` z telemetrią; odtworzenie pinezki celu.
- [ ] `ios/KayakMotor/Networking/TelemetrySocket.swift`/`TelemetryStore` — jawny `reconnect()` jeśli potrzebny.
- [ ] Stwórz `ios/KayakKit/Tests/KayakContractTests/AutonomousModeReconcilerTests.swift`.

**Testy:**
- [ ] Test: intencja `.hold` + `gotoState=active` → „Kotwica".
- [ ] Test: intencja `.none` + `gotoState=active` → „Trzymam pozycję" (po force-quit).
- [ ] Test: `gotoState=off` + `spotLockState=active` → „Pilot przejął".
- [ ] Test: `gotoState=off` + `spotLockState=off` → stan wyczyszczony.
- [ ] Test (urządzenie): goto aktywne → background 20 s → foreground → UI wciąż-aktywny + pinezka; pilot override → „zakończono".

**Weryfikacja:**
- [ ] Weryfikacja: testy reconcilera zielone; cykl background→foreground odtwarza stan; brak fałszywego „anulowano".

---

## Faza 3 — Dokumentacja (po wdrożeniu kodu)

- [ ] Zaktualizować `docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md` — P1 odwrócone (link do tego planu); P2/P3 obowiązują.
- [ ] Zaktualizować `docs/completed/kayak-motor-firmware-v1/known-issues.md §4d` — persist + device-E2E (persist po utracie linku, CH3-preempt kotwicy, grab własnego fixu).

## Źródła

- Requirements doc: docs/dev-brainstorms/2026-07-02-app-spot-lock-requirements.md
- Plan techniczny: docs/plans/2026-07-02-001-feat-app-spot-lock-latched-commands-plan.md
