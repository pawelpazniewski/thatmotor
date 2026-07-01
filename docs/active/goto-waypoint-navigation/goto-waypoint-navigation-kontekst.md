# Kontekst: Goto — autonomiczna nawigacja do punktu

Branch: `feature/goto-waypoint-navigation`
Ostatnia aktualizacja: 2026-07-01 (Faza 2 / Unit 3 ukończona — rdzeń decyzyjny)

## Faza 2 — Rdzeń decyzyjny (Unit 3) — ukończona 2026-07-01
Rozszerzenie czystej funkcji `spot_lock_step` o arbitraż źródła celu, bez regresji ścieżki CH3.
- **`spot_lock.h`:** nowy enum `spot_lock_target_source` {`SPOT_LOCK_SRC_NONE/HOLD/GOTO`}; `spot_lock_inputs` +`goto_engage`,`goto_lat_e7`,`goto_lon_e7`,`comms_fresh`; `spot_lock_state` +`target_source`; `spot_lock_outputs` +`bool arrived`. Nagłówek nadal grep-clean (brak `esp_*`/`driver/*`).
- **`spot_lock.c`:** `spot_lock_step` przepisane na arbitraż w ARMED: (1) `!armed||!sticks_neutral`→OFF (`make_off`, kasuje source); (2) `ch3_on`→`run_ch3_hold` (SRC_HOLD, snapshot "tu i teraz" na wejściu/preempcji goto — wymaga edge+fresh fix; CH3 fizycznie preemptuje goto); (3) `goto_engage&&!ch3_on`→SRC_GOTO (`ref_*=goto_*`, bramka `comms_fresh`); (4) inaczej OFF. Wspólny `hold_or_pause(comms_gated)` — bramka linku TYLKO dla SRC_GOTO (SRC_HOLD nigdy nie pauzuje na utratę linku, jest RC-owy). `arrived=(err_m<=deadband_m)` w ACTIVE. Matematyka ruchu (geo, ±60°, deadband, cap) reużyta bez zmian.
- **Kluczowa decyzja regresji:** `make_active_state()` w teście reprezentuje aktywny CH3-hold → ustawiono `target_source=SPOT_LOCK_SRC_HOLD` (fixture odzwierciedla rozszerzony model stanu; ZERO asercji osłabionych/usuniętych). Detekcja wejścia/preempcji keyed na `target_source != SRC_HOLD` (nie na `substate==OFF`), co umożliwia preempcję aktywnego goto przez CH3.
- **Testy:** 6 nowych w `test_spot_lock.c` (goto-active, CH3-preempt, comms-pause/resume, comms-nie-dotyczy-hold, override, arrived). Wszystkie 16 istniejących asercji spot-lock bez zmian. Host: 411/411 PASS (405→411). `idf.py build` (esp32s3): PASS (37% wolne).
- **Moc wyroczni zweryfikowana mutacją:** comms-gate→hold FAILuje `test_comms_gate_does_not_pause_ch3_hold`; preempt→`substate==OFF`-only FAILuje `test_ch3_preempts_active_goto`.
- **Nie podłączone do pętli** — `spot_lock_step` woła się z `loop_step`, ale przekazanie `goto_engage/goto_*/comms_fresh` z `control_loop` + watchdog `comms_fresh` = Unit 4 (Faza 3). Do tego czasu nowe pola wejść pozostają domyślnie 0/false w istniejącym wywołaniu (zachowanie CH3 niezmienione).

## Re-review Faza 1 (fix cykl 1, commit `ffbc43a`)
Severity gate: ✅ CZYSTE (P1=0, P2=0, P3=2 przeniesione/zaakceptowane). Host-tests 405/405 PASS, `idf.py build` PASS.
- P1 (cast przed walidacją) ROZWIĄZANY: czysta `goto_target_from_double` waliduje `isfinite`+zakres w domenie double PRZED castem na int32; `extract_goto_target` to cienki adapter cJSON. 5 nowych oracle-testów (INF/NaN/4.39e9 wrap odrzucane — mutacja naive-cast failuje).
- P2 (overstated `[x]`) ROZWIĄZANY: checkbox zadania.md:37 → `[ ]` „ODROCZONE do Unit 4".
- Brak nowych findingów, brak regresji. Faza 1 gotowa do kontynuacji (Faza 2).

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md
- Plan techniczny: docs/plans/2026-07-01-001-feat-goto-waypoint-navigation-plan.md

## Kluczowa obserwacja architektoniczna

Silnik ruchu **już istnieje** — `spot_lock_step` robi point-and-shoot (bearing do celu, stożek ±60°, forward-only, deadband, cap). `goto` NIE jest nowym regulatorem: to **inne źródło celu** (`ref_lat/lon` z zewnątrz zamiast snapshotu bieżącej pozycji) + warstwa bezpieczeństwa sieciowego. Reużywamy, nie duplikujemy.

## Powiązane pliki

### Rdzeń silnika (reuse + rozszerzenie)
- `components/control_loop/include/spot_lock.h` — `spot_lock_inputs/state/outputs`, `spot_lock_step()`; stan trzyma `ref_lat_e7/ref_lon_e7` (:63-68), sub-stany OFF/ACTIVE/PAUSED (:34-38). **Rozszerzyć** o `goto_engage`, `goto_lat/lon_e7`, `comms_fresh`, `target_source`, `arrived`.
- `components/control_loop/src/spot_lock.c` — przejścia + matematyka ruchu (reuse bez zmian dla CH3).
- `components/control_loop/include/geo_math.h` — dystans+bearing z lat/lon (e7). Reuse bez zmian.

### Bramka pierwszeństwa (krytyczna dla R6)
- `components/control_loop/src/loop_step.c:237-250` — `resolve_spot_lock()`: `resolved_state != SM_STATE_ARMED` → OFF bezwarunkowo. `spot_lock_step` wołane tylko w ARMED (:269). **goto rzuca się na tę samą bramkę — zero nowego toru failsafe.**

### Comms-watchdog (rdzeń gotowy)
- `components/gps/include/sensor_freshness.h` — `sensor_is_fresh(now_ms, last_ms, threshold_ms)` (wrap-safe, kontrakt epoki), `sensor_freshness_stamp(prev, now, valid)`. **Reuse wprost** dla linku app.
- `components/control_loop/src/control_loop.c:61` — `now_ms()` = `esp_timer_get_time()/1000`.

### Ścieżka komendy HTTP → pętla
- `components/web_panel/src/http_server.c` (~:177) — `post_command`/`extract_command` (cJSON). **Rozszerzyć** o `goto` (payload lat/lon, 400 na błąd) + `goto_cancel`.
- `components/web_panel/include/command_parse.h`, `src/command_parse.c` — keyword→flagi (czyste). **Dodać** goto/goto_cancel.
- `components/control_loop/include/control_loop.h:70-81` — `control_loop_ui_events` (mailbox, edge-semantics). **Dodać** pola goto.
- `components/control_loop/src/control_loop.c:331` — `apply_ui_events` (konsumpcja). **Dodać** staged target + latch + stempel `s_last_goto_ms`.

### Parametry (SI-6)
- `components/settings/include/settings_model.h` — `spot_lock_*` istnieją (:87-93). **Dodać** `goto_comms_timeout_ms` + bump `SETTINGS_SCHEMA_VERSION`.
- `settings_ranges.h`, `settings_defaults.c`, `settings_validate.c`, `components/web_panel/src/params_json.c` (`U16_FIELDS`).

### Telemetria
- `components/control_loop/include/control_loop.h:26-63` — `control_loop_snapshot` (ma `spot_lock_*`, `gps_*`). **Dodać** `goto_*`.
- `components/web_panel/src/ws_telemetry.c` — `snapshot_to_json` (ints/bools only).
- Front-end panelu ESP (HTML/JS `web_panel`).

### Host-test harness
- `test/host/CMakeLists.txt` (`PURE_SOURCES`+`TEST_SOURCES`), `run.sh`, `test_main.c`.
- Wzorce testów: `test_spot_lock.c`, `test_loop_step.c`, `test_sensor_freshness.c`, `test_command_parse.c`, `test_settings_validate.c`.

## Decyzje techniczne

1. **Reuse silnika spot-lock, oś rozszerzenia = źródło celu.** Rename modułu odrzucony (churn w przetestowanym kodzie).
2. **CH3 priorytet fizyczny nad goto.** Kolejność w ARMED: override→OFF; CH3→SRC_HOLD (snapshot, preempt goto); goto&&!CH3→SRC_GOTO; else OFF.
3. **Utrata linku → PAUSED** (nie abort), bramka świeżości tylko dla SRC_GOTO.
4. **goto na istniejącej jednej bramce failsafe** (tylko ARMED).
5. **Override kasuje latch goto** (brak auto-resume).
6. **Współrzędne `int32 e7` na łączu** (bez floatów); walidacja jako czysty predykat; błąd → 400.
7. **Keepalive przez ponowny `goto` ~2 Hz** (format wire odroczony); stempel przez `sensor_freshness_stamp`, `comms_fresh` co cykl.
8. **Telemetria na intach/boolach.**

## Zależności
- Reuse: `spot_lock`, `geo_math`, `sensor_freshness`, `loop_step`/`resolve_spot_lock`, command→mailbox, settings SI-6, telemetria WS.
- Założenia (dziedziczone ze spot-locka): BNO085 heading wiarygodny (silnik nie zakłóca), GPS NEO-M9N dokładność wystarcza dla deadbandu.
- Sekwencja Unitów: 1→2 (Faza 1), 3 (równolegle), 4 wymaga 2+3, 5 równolegle (przed 4-integracją watchdoga), 6 wymaga 4.

## Wiedza instytucjonalna (must-follow)
- Failsafe-precedence: override PO maszynie stanów, tylko w ARMED; nigdy do `rc_valid`/`sm_inputs`. (`docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md`)
- fresh ≠ valid: bramkuj link co cykl.
- Wrap-safe recency w jednej domenie zegara (`now_ms()`).
- Pure ⊥ HAL: nowa logika czysta, HAL cienki.
- Oracle power: bramki/limity/priorytet testuj wejściem poza zakresem / w stanie który bez bramki przecieka.

## Stan realizacji

### Faza 1 — Kanał celu z aplikacji (Unit 1 + Unit 2) — ukończona 2026-07-01
- **Unit 1:** nowy czysty moduł `components/web_panel/{include/goto_target.h,src/goto_target.c}` — `goto_target_valid(lat_e7, lon_e7)` z named constants `GOTO_LAT/LON_E7_MIN/MAX` (±90°/±180°, inclusive). Brak include `esp_*`/`driver/*` (grep-clean). `command_parse` rozpoznaje keywordy `goto`/`goto_cancel` (keyword-only: flagi ustawione, lat/lon = 0, wstrzykiwane przez HTTP). Zarejestrowane w host-harness (`test_goto_target.c`, rozszerzony `test_command_parse.c`).
- **Unit 2:** `http_server.c::post_command` dla `goto` wyjmuje `lat_e7`/`lon_e7` (cJSON, `extract_goto_target`), waliduje `goto_target_valid`; malformed/out-of-range → `400 {data:null,error:{code:"VALIDATION_FAILED"}}` bez postu do mailbox. `to_ui_events` kopiuje flagi + cel. `control_loop_ui_events` rozszerzone o pola goto. `control_loop.c::apply_goto_events`: latch `s_goto_engage` + staged `s_goto_lat/lon_e7` + stempel `s_last_goto_ms` przez `sensor_freshness_stamp` (baza watchdoga Unit 4); `goto_cancel` czyści latch.
- **Decyzja:** kontrakt 400 host-testowany przez czyste bloki (`goto_target_valid` + `api_build_error`), bo `http_server` linkuje `esp_http_server.h` i nie jest host-linkowalny. Mapowanie `to_ui_events`→struct i konsumpcja `apply_goto_events` to cienki HAL, weryfikowany na poziomie pętli w Unit 4 (zgodnie z planem).
- **Staged goto state** (`s_goto_engage/lat/lon`, `s_last_goto_ms`) jest zapisywany, ale jeszcze niekonsumowany — podłączenie do `loop_step` przez `loop_inputs` + watchdog `comms_fresh` = Unit 4 (Faza 3).
- Host-tests: 400/400 PASS. `idf.py build` (esp32s3): PASS (37% partycji app wolne).

### Review Fazy 1 (2026-07-01) — ⛔ BLOKUJE
Raport: `review-faza-1.md`. 3 agenty (security, architecture, test-coverage). Walidacja: host 400/400 PASS, `idf.py build` PASS.
- **P1 blocking (1):** `http_server.c:168-169` `extract_goto_target` — cast `(int32_t)valuedouble` PRZED walidacją zakresu → UB (INF/NaN) + wrap-into-range bypass (`lat_e7=4394967296` → `1e8` przechodzi). Konsensus 3 agentów co do defektu; security-sentinel klasyfikuje P1 (UB + trywialny bypass na jedynym deliverable Unitu 2). Fix: walidacja w domenie `double` + `isfinite` PRZED castem. **Wniosek: walidacja MUSI działać na oryginalnej wartości z łącza, nie na już-zawężonej przez cast** — JSON nie zna int32.
- **P2 important (1):** checkbox zadania.md:37 overstated `[x]` bez asercji na skopiowany cel → poprawiony na `[ ]`/odroczone (Unit 4). Potwierdzono: cJSON nieobecny w host-harness, więc mapowanie `to_ui_events`/`extract_goto_target` genuinnie nie-host-linkowalne (zgodne z klauzulą planu 290-291).
- **P3 nit (5):** brak host-testu spoza-int32/INF (po fix P1); `sensor_freshness_stamp(...,true)` martwy predykat (tożsame z `=now_ms()`); podwójny `cJSON_Parse` (akceptowalny); goto_cancel/malformed/empty-body pokryte pośrednio (uzasadnione harnessem).
- **Czyste:** Pure⊥HAL (grep-clean), named constants, wzorce spójne, moc wyroczni Unit 1 STRONG (mutacje FAILują), brak wycieków cJSON. E2E = N/A (firmware).

## Powiązana pamięć
- [[ios-app-goto-direction]] — kierunek aplikacji iOS (SwiftUI + MapLibre) korzystającej z tego kontraktu API.
