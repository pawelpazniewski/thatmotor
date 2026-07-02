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
- [x] `components/control_loop/include/control_loop.h` — dodać `bool hold_request;` do `control_loop_ui_events`.
- [x] `components/web_panel/src/http_server.c` — `to_ui_events` przenosi `hold_request`; gałąź `hold` NIE przez `extract_goto_target` (brak coords → nie zwracać 400).
- [x] Pure helper decyzji grabu: `(fresh, fix, lat_e7, lon_e7) → {engage, lat, lon}` = `fresh && fix && in-range`. Umieszczony w `components/control_loop/{src,include}/goto_grab.c/h` (NIE w web_panel — control_loop nie może zależeć od web_panel: cykl). Range check mirroruje `goto_target_valid`.
- [x] `components/control_loop/src/control_loop.c` — `apply_goto_events`: na `hold_request` jeden `gps_get_state()` → helper → przy engage `s_goto_engage=true`, `s_goto_lat/lon_e7` z fixu, stamp `s_last_goto_ms`.

**Testy:**
- [x] Test: helper `fresh=true, fix=true, lat/lon w zakresie` → `engage=true`, cel = wejście.
- [x] Test: helper `fix=false` (seed-fresh) → `engage=false`; mutacja „pomiń bramkę fix" failuje.
- [x] Test: helper lat/lon POZA zakresem geo → `engage=false` (mirror `goto_target_valid`).
- [x] Test: `command_parse` regresja — `hold` nie ustawia `goto_lat/lon`.

**Weryfikacja:**
- [ ] Weryfikacja: host-tests zielone; przegląd: `hold` bez body nie zwraca 400; przy fixie `s_goto_engage` latchuje `SRC_GOTO` z własną pozycją.

---

## Do poprawy po review fazy 1

Gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI (P1=0, P2=1, P3=4). Pełny raport: `review-faza-1.md`.
Krytyczne inwarianty failsafe (flip chirurgiczny, bramka re-latchu, predykat sensoryczny, ścieżka RC, moc wyroczni) — WSZYSTKIE zweryfikowane OK. 442 host-testy zielone.

- [x] 🟠 [important] **components/control_loop/src/spot_lock.c:157,160-162,189,224** — Martwy kod po flipie: `comms_gated` == `false` w obu call-site'ach → gałąź `if (comms_gated && !in->comms_fresh)` nieosiągalna, parametr martwy. Usunąć parametr i gałąź (proza już w komentarzu :148-153). Powód surowy: moduł failsafe — martwa ścieżka może po cichu re-odwrócić failsafe zmianą jednego boola; usunięcie czyni inwariant strukturalnym (§6, §5#10, §11).
- [ ] 🟡 [nit] **components/control_loop/include/control_loop.h:95, command_parse.h:40** — Kolizja nazewnicza: `hold_request` engażuje SRC_GOTO, a SRC_HOLD to hold pilota RC. Rozważ `anchor_request` lub notkę przy polu.
- [ ] 🟡 [nit] **test/host/test_goto_target.c:122** — Testy `goto_grab_decide` w `test_goto_target.c` zamiast dedykowanego `test_goto_grab.c` (kolokacja suite).
- [ ] 🟡 [nit] **test/host/test_goto_target.c (suite goto_grab)** — Inkluzywna granica geo (±90/±180) niepinowana; mutacja `>=`→`>` w `grab_in_range` przeszłaby (duplikacja stałych łamie tranzytywne pokrycie). Dodać 1 test at-boundary.
- [ ] 🟡 [nit] **goto_grab.h:28-31 vs goto_target.h:22-26** — Brak compile-time linku pinującego mirror stałych; opcjonalny `TEST_ASSERT_EQUAL` drift-guard.

---

## Faza 2 — iOS

### Unit 4: `.hold` w kontrakcie + mapowanie przycisku + lokalna intencja — **S**

Zależności: Unit 2 (firmware rozumie `hold`). Realizuje: R1, R5, R7.

**Implementacja:**
- [x] `ios/KayakKit/Sources/KayakContract/Command.swift` — `case hold`; `cmdName`→`"hold"`; `httpBody()` bez współrzędnych (jak `gotoCancel`).
- [x] `ios/KayakMotor/App/AppModel.swift` — `requestSpotLock()` → `commands.send(.hold)` + `lastActionMessage` (feedback jak disarm/STOP — spot-lock nie ma pinezki, więc NIE `target.markSending`); dodać `enum AutonomousIntent { none, goto, hold }` ustawiane przy tapnięciu. **ODSTĘPSTWO**: `AutonomousIntent` w KayakContract (pure), NIE w AppModel — pure reconciler (Unit 7) musi go typować, a nie może zależeć od app-targetu.

**Testy:**
- [x] Test: `Command.hold.httpBody()` → `{"cmd":"hold"}` (bez lat/lon).
- [x] Test: `Command.hold.cmdName == "hold"` (pokryty przez `holdBody` — pole `cmd` w httpBody; `cmdName` jest private).
- [ ] Test: tap Spot-lock → `AutonomousIntent=.hold`; tap Goto → `.goto` — **app-target (AppModel używa UIKit, poza pakietem SPM)**; niedostępne w `swift test`, odroczone do compile-verify (xcodebuild) + device.

**Weryfikacja:**
- [ ] Weryfikacja: testy KayakContract zielone; tap Spot-lock POST-uje `hold`.

---

### Unit 5: Wyszarzanie Spot-lock bez fixu (pure readiness) — **S**

Zależności: Unit 4. Realizuje: R6.

**Implementacja:**
- [x] Stwórz `ios/KayakKit/Sources/KayakContract/SpotLockReadiness.swift` — `canEngage(telemetry)` + `blockReason` (deleguje do `GotoReadiness` dla non-nil; nil → `.noGpsFix`).
- [x] `ios/KayakMotor/Features/Goto/ControlBarView.swift` — przycisk Spot-lock `enabled: SpotLockReadiness.canEngage(telemetry)` (wzór Goto).
- [x] Stwórz `ios/KayakKit/Tests/KayakContractTests/SpotLockReadinessTests.swift`.

**Testy:**
- [x] Test: `gpsFix=false` → `canEngage=false` (+ blockReason `.noGpsFix` „Brak fixu GPS").
- [x] Test: `gpsFix=true` + ARMED → `canEngage=true`.
- [x] Test: `telemetry=nil` → `canEngage=false`.

**Weryfikacja:**
- [ ] Weryfikacja: testy zielone; w UI przycisk 0.5 opacity + `.disabled` bez fixu.

---

### Unit 6: Usunięcie keepalive / idle-timer — **S**

Zależności: Unit 1 (firmware nie pauzuje). Realizuje: R3.

**Implementacja:**
- [x] `ios/KayakMotor/App/AppModel.swift` — usunięto `keepaliveTick`, `KeepaliveController`, `UIApplication.isIdleTimerDisabled` (i nieużywany `import UIKit`). `start()`/`stop()` dotykają tylko telemetrii.
- [x] `ios/KayakMotor/Features/Goto/KeepaliveController.swift` — usunięty (martwy po usunięciu keepalive).
- [x] `ios/KayakKit/Sources/KayakContract/Keepalive.swift` — usunięto `KeepaliveDecision` + `keepaliveIntervalSeconds`; plik zmieniono nazwę na `GotoTiming.swift` (zostaje tylko `commsTimeoutSeconds` — próg świeżości telemetrii dla `TelemetryStore`, prze-dokumentowany).
- [x] `ios/KayakKit/Tests/KayakContractTests/KeepaliveTests.swift` — usunięty (usuwana funkcjonalność — zgodne z regułą: usuwamy testy WRAZ z testowaną funkcją).

**Testy:**
- [x] Test: brak referencji do `KeepaliveDecision`/`isIdleTimerDisabled`/`keepaliveTick` (grep pusty; `swift test` + xcodebuild kompilują — brak martwego kodu).
- [ ] Test (urządzenie): goto → wygaś ekran 30 s → łódź kontynuuje; po odblokowaniu tryb aktywny (agent-browser N/D — manualnie). **DEVICE E2E — dla review.**

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
