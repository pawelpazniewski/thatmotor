# Kontekst: Goto — autonomiczna nawigacja do punktu

Branch: `feature/goto-waypoint-navigation`
Ostatnia aktualizacja: 2026-07-01

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

## Powiązana pamięć
- [[ios-app-goto-direction]] — kierunek aplikacji iOS (SwiftUI + MapLibre) korzystającej z tego kontraktu API.
