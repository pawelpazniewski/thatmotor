# Review Fazy 3 — Integracja goto w pętli + comms-watchdog (Unit 4)

Commit: `f019b33` · Data: 2026-07-01 · Branch: `feature/kayak-motor-firmware-v1`
Plan techniczny: `docs/plans/2026-07-01-001-feat-goto-waypoint-navigation-plan.md` (Unit 4)

## Severity gate: ✅ CZYSTE

- **P1 (blocking): 0**
- **P2 (important): 0**
- **P3 (nit): 1** (KOD — dopuszczalna duplikacja predykatu priorytetu)

Typy findingów per severity: P3 = KOD. Brak findingów TEST. E2E = N/A (firmware ESP-IDF, brak przeglądarki — zgodnie z kontekstem).

## Walidacja

- **Host-tests:** `./test/host/run.sh` → **425/425 PASS, 0 Failures, 0 Ignored** (+12 nowych vs 413 z re-review Fazy 2, zero regresji).
- **Build:** `. $HOME/esp/idf-env.sh && idf.py build` (esp32s3) → **PASS** (exit 0, „Project build complete").
- **Grep-clean czystych nagłówków:** `loop_step.h` CLEAN, `spot_lock.h` CLEAN (zero `esp_*`/`driver/*`/`hal/`).
- **Static-assert parytetu:** `params_json.c:67` — `_Static_assert(U16_FIELD_COUNT + BOOL_FIELD_COUNT + 1 == PARAMS_JSON_FIELD_COUNT)` broni bufora, count 31→32 spójny z tabelą.

## Werdykt inwariantów 1–9

### 1. Failsafe-precedence — ✅ SZCZELNE (moc wyroczni potwierdzona)
goto/GPS/IMU/comms podpięte WYŁĄCZNIE do `spot_lock_step` przez `resolve_spot_lock` (bramka `resolved_state != SM_STATE_ARMED → OFF`, `loop_step.c:247-255`). `build_sm_inputs` (`loop_step.c:64-81`) i `resolve_rc_valid` (`:55-62`) nie referują żadnego pola goto/gps/comms — `rc_valid`/`sm_inputs`/`sm_step` nietknięte. `apply_goto_inputs`/`apply_sensor_inputs` explicite oznaczone jako „spot-lock control inputs ONLY, NEVER rc_valid/failsafe". Test `test_goto_rc_loss_failsafe_wins` wchodzi w stan przeciekający (goto drifted + aligned = WOULD thrust), asertuje ESC=1500 + servo=center + substate=OFF; usunięcie bramki ARMED daje forward thrust → test FAILuje. Realna wyrocznia.

### 2. Cykl życia latcha — ✅ SZCZELNE
`goto_latch_should_clear` (`loop_step.c:271-279`) czyści latch TYLKO gdy `ARMED && goto_engage && (!sticks_neutral || ch3_on)`. Comms-loss / GPS-loss / IMU-loss przechodzą przez `hold_or_pause` → PAUSED bez sygnału clear. `run_one_cycle` (`control_loop.c:498-500`) kasuje `s_goto_engage` wyłącznie na `out.goto_latch_clear`; `goto_cancel` kasuje osobno w `apply_goto_events` (`:354-356`), nigdy nie zeruje `s_goto_lat/lon_e7`. Pauza (comms/GPS) i trwały abort (override/preempt/cancel) rozróżnione z mocą wyroczni: `test_goto_pauses_on_comms_loss_latch_retained_then_resumes` + `_gps_loss_latch_retained` asertują `goto_latch_clear==false` i wznowienie tego samego celu; `test_goto_stick_override_clears_latch` + `_ch3_preempt_holds_here_and_clears_latch` asertują `goto_latch_clear==true`. Brak ścieżki, w której comms-loss kasuje latch.

### 3. Domena zegara watchdoga — ✅ SZCZELNE
`comms_fresh = sensor_is_fresh(now_ms(), s_last_goto_ms, s_params.goto_comms_timeout_ms)` (`control_loop.c:407-408`); stempel `s_last_goto_ms = sensor_freshness_stamp(s_last_goto_ms, now_ms(), true)` (`:352`). Obie strony w domenie `now_ms()` (`esp_timer_get_time()/1000`, uint32) — brak mieszania z capture-tick. Wrap-safety zapewniona kontraktem `sensor_freshness` (modular subtraction), pokryta istniejącym `test_sensor_freshness.c`.

### 4. comms_fresh liczone KAŻDY cykl — ✅ SZCZELNE
`apply_goto_inputs` wołane bezwarunkowo w `read_inputs` (`control_loop.c:426`) co cykl; `comms_fresh` przeliczane każdorazowo (fresh≠valid re-walidacja co cykl, zgodnie z learningiem). Nie jest cache'owane przy wejściu.

### 5. Hard clamp SI-3 — ✅ SZCZELNE
`test_goto_output_passes_hard_clamp`: `esc_forward_max_us=3000` (poza oknem [1000,2000]) + cap 100% → nasycona komenda mapuje na 3000 us; asercja `esc_us==2000` (sufit okna). Wyjście goto przechodzi przez ten sam `throttle_chain_step`/`map_normalized_to_us`/clamp co każda ścieżka (`resolve_esc:180-182`). Out-of-window computed → clamp potwierdzony.

### 6. Bump SCHEMA_VERSION 6→7 — ✅ SZCZELNE (legalny anchor update)
`SETTINGS_SCHEMA_VERSION 6U→7U` z komentarzem migracji; `blob_codec.h` FIELD_BYTES 61→63 / SIZE 65→67 (+2 dla u16, spójne z layoutem); serialize/deserialize dodają `goto_comms_timeout_ms` w kolejności deklaracji; defaults/ranges/validate spójne (MIN200/MAX5000/DEF1500). Round-trip `test_round_trip_preserves_every_field` rozszerzony o pole z wartością non-default (1200) — realny round-trip. `test_blob_size_matches_v7_layout` to legalna aktualizacja kotwicy dla realnej zmiany schematu (nowe wartości 63/67), nie osłabienie. Migracja skorumpowanej/świeżej NVS → defaults pokryta (`test_goto_comms_timeout_default_is_sane`, `_out_of_range_recovers`).

### 7. Zero test-weakeningu — ✅ SZCZELNE
Diff testów wyłącznie addytywny: +8 testów `test_loop_step.c`, +1 `test_spot_lock.c`, +3 `test_settings_validate.c`, +1 asercja round-trip. Jedyna modyfikacja istniejącego testu to rename `test_blob_size_matches_v6→v7_layout` z podniesionymi oczekiwaniami (61→63/65→67) — wzmocnienie kotwicy, nie osłabienie asercji. Żadna istniejąca asercja `loop_step`/`state_machine`/`spot_lock`/`chain`/`blob_codec` nietknięta.

### 8. Pure ⊥ HAL — ✅ SZCZELNE
`loop_step.h`/`loop_inputs`/`loop_outputs` grep-clean (zero `esp_*`/`driver/*`). `apply_goto_inputs` cienki (4 przypisania + 1 wywołanie `sensor_is_fresh`); cała decyzja arbitrażu/latcha w czystym `loop_step.c`/`spot_lock.c` host-testowanym. HAL (control_loop) tylko stempluje czas i kopiuje staged state.

### 9. Single-writer — ✅ SZCZELNE
`s_goto_engage`/`s_goto_lat/lon_e7`/`s_last_goto_ms` zapisywane wyłącznie w tasku pętli: `apply_goto_events` (przez `apply_ui_events` w `read_inputs`) ustawia, `run_one_cycle` kasuje przez `goto_latch_clear`. Oba w tym samym cyklu tego samego tasku (CPU1), sekwencyjnie: drain queue → `apply_goto_inputs` czyta spójny obraz → `loop_step` → clear. Brak race; mailbox `xQueueOverwrite` (length-1) daje „najnowszy goto wygrywa". W obrębie cyklu override deterministycznie wygrywa nad świeżo odebraną komendą.

## Odchylenia od planu

- **Przeniesienie zakresu Unit 5 → Faza 3:** pole `goto_comms_timeout_ms` end-to-end (model/ranges/defaults/validate/params_json/blob_codec + testy) dostarczone w tym commicie, bo Unit 4 potrzebuje realnego timeoutu zamiast magic number. Uzasadnione (unika hardcode'u w watchdogu); zgodne z planem funkcjonalnie. Pozostały ślad Unit 5 do Fazy 4: dedykowany test 409-w-ARMED (obecnie pokryty generycznym gate'em SI-6 `params_decide`).
- **Zgodne z planem (NIE braki):** telemetria goto + panel + test 409-w-ARMED = Faza 4 (Unit 5/6).
- Wszystkie pliki z sekcji „Pliki:" Unit 4 zmodyfikowane; wszystkie scenariusze testowe planu zaimplementowane z odpowiadającymi testami (7/7 checkboxów Testy odhaczonych).

## Findingi

### 🟡 P3 [nit] — KOD — `loop_step.c:271-279` (`goto_latch_should_clear`)
Predykat czyszczenia latcha (`!sticks_neutral || ch3_on`) re-derywuje warunki priorytetu, które `spot_lock_step` już egzekwuje wewnętrznie (`make_off` na `!sticks_neutral`, `run_ch3_hold` na `ch3_on`). Dwa miejsca muszą pozostać w synchronizacji przy ewentualnej zmianie kolejności źródeł. Per coding-rules §11 („Duplication > Complexity") to świadoma, czytelna duplikacja sygnału (nie współdzielony ukryty stan) — akceptowalna, ale warta jednolinijkowego komentarza „utrzymuj spójne z priorytetem w spot_lock_step". Bez akcji blokującej.

## Podsumowanie

Jądro bezpieczeństwa całego feature'u (integracja sterowania + watchdog) jest **strukturalnie szczelne**: failsafe-precedence, izolacja bramki linku, cykl życia latcha (pauza vs abort) i domena zegara watchdoga potwierdzone testami z realną mocą wyroczni. Zero test-weakeningu, zero regresji (425/425), build zielony. Faza 3 gotowa do kontynuacji (Faza 4 — telemetria/panel).
