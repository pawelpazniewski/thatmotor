# Zadania: Goto — autonomiczna nawigacja do punktu

Branch: `feature/goto-waypoint-navigation`
Ostatnia aktualizacja: 2026-07-01

Legenda: `Test:` = scenariusz testowy (host/E2E), `Weryfikacja:` = kryterium ukończenia Unitu.

---

## Faza 1 — Kanał celu z aplikacji

### Unit 1: Walidacja celu + rozszerzenie `command_parse` (R1, R6) — M
Zależności: brak

Implementacja:
- [x] Stwórz `components/web_panel/include/goto_target.h` + `src/goto_target.c` — czyste `bool goto_target_valid(int32_t lat_e7, int32_t lon_e7)` (zakres ±90°/±180° w e7, named constants)
- [x] Rozszerz `components/web_panel/include/command_parse.h` — `command_parse_result`: `goto_request`, `goto_cancel_request`, `int32_t goto_lat_e7`, `int32_t goto_lon_e7`
- [x] Rozszerz `components/web_panel/src/command_parse.c` — keyword `goto`/`goto_cancel` (payload wstrzykuje HTTP w Unit 2)
- [x] Zarejestruj w `test/host/CMakeLists.txt` (`goto_target.c` → PURE_SOURCES, `test_goto_target.c`) i `test/host/test_main.c`

Testy (test-first dla `goto_target_valid` — moc wyroczni, wartości poza zakresem):
- [x] Test: `goto_target_valid` cel w zakresie → true; `lat_e7=900000001` → false; `lon_e7=-1800000001` → false; dokładne granice ±90/±180 zdefiniowane i przetestowane
- [x] Test: `command_parse("goto")` → `goto_request=true`; `command_parse("goto_cancel")` → `goto_cancel_request=true`; nieznany keyword → `ok=false` (zero regresji istniejących komend)

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone; grep braku `esp_*`/`driver/*` w `goto_target.h`; zero regresji `test_command_parse`

### Unit 2: HTTP handler goto + transport przez mailbox (R1, R6) — M
Zależności: Unit 1

Implementacja:
- [x] Modyfikuj `components/web_panel/src/http_server.c` — dla `cmd=="goto"` odczytaj `lat_e7`/`lon_e7` (cJSON), `goto_target_valid`; błąd → `400 {data:null,error:{code,message}}`; `to_ui_events` kopiuje flagi + lat/lon
- [x] Modyfikuj `components/control_loop/include/control_loop.h` — `control_loop_ui_events`: `goto_request`, `goto_cancel_request`, `goto_lat_e7`, `goto_lon_e7`
- [x] Modyfikuj `components/control_loop/src/control_loop.c` — `apply_ui_events`: staged goto target + `goto_engage` latch; `goto_cancel` → wyczyść; stempel `s_last_goto_ms` (`sensor_freshness_stamp`)

Testy (test-first: kontrakt request/response dla błędnego lat/lon → 400):
- [ ] Test: poprawny `goto` z lat/lon w zakresie → UI event z `goto_request` i skopiowanym celem (ODROCZONE do Unit 4 — `to_ui_events`/`extract_goto_target` static+cJSON, brak cJSON w host-harness; brak asercji na skopiowany cel)
- [x] Test: `goto` z lat/lon poza zakresem → `400 {data:null,error:{code}}` (bez postu do mailbox)
- [x] Test: `goto_cancel` → UI event `goto_cancel_request` (uwaga: pokryte tylko na poziomie flagi parsera, nie mapowania `to_ui_events` — odroczone do Unit 4)

Weryfikacja:
- [ ] Weryfikacja: `idf.py build` zielony; host-tests zielone; `POST /api/command` zwraca poprawną kopertę dla obu ścieżek; UI event dociera do `apply_ui_events`

## Do poprawy po review fazy 1

Severity gate: ⛔ BLOKUJE (1× P1). Raport: `review-faza-1.md`. Host-tests 400/400 PASS, `idf.py build` PASS.

- [x] 🔴 [blocking] **http_server.c:168-169** (`extract_goto_target`) — cast `(int32_t)valuedouble` PRZED walidacją zakresu = UB (INF/NaN) + wrap-into-range bypass (np. `lat_e7=4394967296` → `1e8` przechodzi walidację). NAPRAWIONE: wyekstrahowano czystą, host-testowalną `goto_target_from_double(lat_d, lon_d, *lat_e7, *lon_e7)` w `goto_target.{h,c}` (`#include <math.h>`, `isfinite` + porównanie z `GOTO_*_E7_MIN/MAX` w domenie double PRZED castem); `extract_goto_target` to cienki adapter cJSON delegujący decyzję.
- [x] 🟠 [important] **goto-waypoint-navigation-zadania.md:37** — checkbox „poprawny goto → UI event ze skopiowanym celem" był `[x]` bez asercji na skopiowany cel (anty-pattern #7). Poprawiono na `[ ]` + „odroczone do Unit 4". (rozwiązane w tym review; zweryfikowane — stan checkboxów zgodny z rzeczywistością, nie cofnięto)
- [x] 🟡 [nit] **test_goto_target.c** — dodano host-testy warstwy walidacji `double` na `INFINITY`/`-INFINITY`/`NAN` oraz `4.39e9`/`-4.39e9` (wrap bypass) → każdy odrzucony (moc wyroczni: naive cast przed walidacją FAILuje te testy).
- [ ] 🟡 [nit] **control_loop.c:350** — `sensor_freshness_stamp(..., true)` z literałem `true` jest tożsamościowe z `= now_ms()` (martwy predykat). Rozważ bezpośrednie przypisanie z komentarzem o domenie zegara.
- [ ] 🟡 [nit] **http_server.c:211,221** — podwójny `cJSON_Parse` tego samego `reqbuf`. Akceptowalny trade-off (prostota > DRY); nie wymaga zmiany.

---

## Faza 2 — Rdzeń decyzyjny

### Unit 3: Rozszerzenie `spot_lock_step` o źródło celu + arbitraż CH3/goto + bramka linku (R2–R6) — L
Zależności: brak (równolegle do Fazy 1)

Implementacja:
- [x] Modyfikuj `components/control_loop/include/spot_lock.h` — `spot_lock_inputs`: `goto_engage`, `goto_lat_e7/lon_e7`, `comms_fresh`; `spot_lock_state`: `target_source` {SRC_NONE/SRC_HOLD/SRC_GOTO}; `spot_lock_outputs`: `bool arrived`
- [x] Modyfikuj `components/control_loop/src/spot_lock.c` — arbitraż źródła; dla SRC_GOTO `ref_*=goto_*`; bramka `comms_fresh` w pauzie tylko dla SRC_GOTO; CH3 preemptuje goto; `arrived=(err_m<=deadband_m)`
- [x] Rozszerz `test/host/test_spot_lock.c`

Testy (test-first, moc wyroczni — wchodź w stan, który bez bramki przecieka):
- [x] Test: SRC_GOTO — goto_engage+ARMED+neutral+fresh, CH3 OFF → ACTIVE, `ref_*==goto_*`; throttle>neutral gdy poza deadbandem i w ±60° (`test_goto_engages_active_with_external_target`)
- [x] Test: priorytet CH3 — goto ACTIVE, `ch3_on`(edge) → SRC_HOLD, `ref_*`=bieżąca pozycja (FAILuje bez preempcji) (`test_ch3_preempts_active_goto_and_snapshots_here_and_now` — mutacja preempt→OFF-only FAILuje ten test)
- [x] Test: bramka linku — SRC_GOTO ACTIVE, `comms_fresh=false` → PAUSED (neutral+center, cel zachowany); powrót → ACTIVE ten sam cel (`test_goto_pauses_on_comms_loss_then_resumes_same_target`)
- [x] Test: bramka linku NIE dotyczy SRC_HOLD — CH3-hold z `comms_fresh=false` nie pauzuje (FAILuje, gdy bramka obejmie SRC_HOLD) (`test_comms_gate_does_not_pause_ch3_hold` — mutacja comms-gate→hold FAILuje ten test)
- [x] Test: override — SRC_GOTO ACTIVE + `!sticks_neutral` → OFF (`test_goto_override_on_stick_deflection`)
- [x] Test: regresja CH3 — wszystkie istniejące scenariusze spot-lock przechodzą bez zmian (16 istniejących asercji `test_spot_lock` bez zmian; `arrived` pokryte `test_goto_arrived_flag_tracks_deadband`)

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone (nowe + wszystkie istniejące `test_spot_lock`); grep braku `esp_*`/`driver/*` w `spot_lock.h`; funkcja deterministyczna

---

## Faza 3 — Integracja + comms-watchdog

### Unit 4: Integracja goto w `loop_step`/`control_loop` + watchdog + cykl życia latcha (R2–R6) — L
Zależności: Unit 2, Unit 3

Implementacja:
- [ ] Modyfikuj `components/control_loop/include/loop_step.h` — `loop_inputs`: `goto_engage`, `goto_lat_e7/lon_e7`, `comms_fresh`
- [ ] Modyfikuj `components/control_loop/src/loop_step.c` — `resolve_spot_lock`: przekaż nowe wejścia; sygnalizuj wyczyszczenie latcha przy override/cancel/CH3-preempt (bramka `state==ARMED` bez zmian)
- [ ] Modyfikuj `components/control_loop/src/control_loop.c` — `s_last_goto_ms`, `s_goto_engage`, `s_goto_lat/lon_e7`; `read_inputs`: `comms_fresh = sensor_is_fresh(now_ms(), s_last_goto_ms, s_params.goto_comms_timeout_ms)`; kasowanie latcha po override/cancel/preempt
- [ ] Rozszerz `test/host/test_loop_step.c`

Testy (test-first: failing test integracyjny pełnej ścieżki, potem implementacja):
- [ ] Test: komenda goto w ARMED+neutral+fresh, CH3 OFF → servo/ESC computed (≠ tor stickowy), SRC_GOTO ACTIVE
- [ ] Test: w trakcie goto utrata RC → `sm_step`=FAILSAFE → ESC neutral+servo center (override się NIE wykonuje; FAILuje gdyby goto działało poza ARMED)
- [ ] Test: w trakcie goto `comms_fresh=false` → PAUSED (neutral+center), latch zachowany; powrót → ACTIVE ten sam cel
- [ ] Test: w trakcie goto `!sticks_neutral` → OFF **i** latch skasowany (powrót drążka NIE wznawia; FAILuje gdy latch przetrwa)
- [ ] Test: w trakcie goto CH3 ON → SRC_HOLD (hold „tu i teraz"), latch goto skasowany
- [ ] Test: `goto_cancel` → OFF, latch skasowany
- [ ] Test: każde wyjście goto przechodzi przez hard clamp SI-3

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone; `idf.py build` zielony; zero regresji `loop_step`/state_machine/spot_lock/chain; grep braku nowych `esp_*`/`driver/*` w czystych nagłówkach

---

## Faza 4 — Parametry + telemetria + panel

### Unit 5: Parametr `goto_comms_timeout_ms` (SI-6) (R7) — S
Zależności: brak (potrzebny przez Unit 4)

Implementacja:
- [ ] Modyfikuj `components/settings/include/settings_model.h` — `uint16_t goto_comms_timeout_ms`; **bump `SETTINGS_SCHEMA_VERSION`**
- [ ] Modyfikuj `settings_ranges.h` (MIN/MAX/DEFAULT ~1500 ms), `settings_defaults.c`, `settings_validate.c`, `components/web_panel/src/params_json.c` (`U16_FIELDS`)
- [ ] Rozszerz `test/host/test_settings_validate.c`; `test/host/test_blob_codec.c` (round-trip nowej wersji)

Testy:
- [ ] Test: wartość poza zakresem odrzucona/clampowana; w zakresie akceptowana
- [ ] Test: defaults ładują się przy świeżej/skorrumpowanej NVS z sensownym timeoutem
- [ ] Test: POST `goto_comms_timeout_ms` w ARMED → 409 (SI-6 niezmienione)

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone; pole serializuje się w `/api/params`; `idf.py build` zielony

### Unit 6: Telemetria goto + panel (R8) — M
Zależności: Unit 4

Implementacja:
- [ ] Modyfikuj `components/control_loop/include/control_loop.h` — `control_loop_snapshot`: `goto_state`, `goto_target_lat_e7/lon_e7`, `goto_err_m`, `goto_bearing_deg10`, `goto_arrived`, `app_link_fresh`
- [ ] Modyfikuj `components/control_loop/src/control_loop.c::publish_snapshot` — populacja z `loop_outputs`/watchdog
- [ ] Modyfikuj `components/web_panel/src/ws_telemetry.c::snapshot_to_json` — nowe pola (ints/bools only)
- [ ] Modyfikuj front-end panelu ESP (HTML/JS `web_panel`) — blok „Goto: off/active/paused, cel, błąd, bearing/dziób, arrived, link"

Testy:
- [ ] Test: (jeśli host-testowalne) snapshot z goto ACTIVE serializuje `goto_state=1`, cel, `err_m`, `bearing_deg10`, `app_link_fresh` jako int/bool
- [ ] Test: [E2E] (known-issues/na wodzie) panel/app pokazuje off→active po komendzie, błąd maleje przy dopływaniu, paused przy utracie linku, hold po dojściu

Weryfikacja:
- [ ] Weryfikacja: `idf.py build` zielony; panel renderuje nowe pola; JSON zawiera `goto_*`; hardware/E2E odłożone do `known-issues`

---

## Domknięcie
- [ ] Dopisz luki hardware/na wodzie do `docs/completed/kayak-motor-firmware-v1/known-issues.md`
- [ ] Udokumentuj kontrakt API dla aplikacji iOS (goto/goto_cancel, keepalive, pola `goto_*`)
- [ ] Rozważ `/dev-compound` dla wzorca „app-driven override z comms-watchdogiem bez naruszenia failsafe RC"
