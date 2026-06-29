# Code Review — Faza 1 (Fundamenty wejść: Unit 1 + Unit 2)

**Zadanie:** spot-lock-position-hold
**Branch:** `feature/spot-lock-position-hold`
**Commity recenzowane:** `c06795f` (Unit 1 — świeżość GPS), `2983edd` (Unit 2 — switch_debounce + CH3)
**Data review:** 2026-06-29

---

## Werdykt severity gate

✅ **GOTOWE DO KONTYNUACJI** — 0 problemów P1 (blocking), 0 problemów P2 (important). Wyłącznie nity P3 do rozważenia.

## Liczniki

| Severity | KOD | TEST | E2E | Razem |
|----------|-----|------|-----|-------|
| 🔴 P1 (blocking)   | 0 | 0 | 0 | **0** |
| 🟠 P2 (important)  | 0 | 0 | 0 | **0** |
| 🟡 P3 (nit)        | 3 | 2 | 0 | **5** |

## Bramki jakości (zweryfikowane przez recenzenta)

- ✅ Host-tests: `test/host/run.sh` → **305 Tests, 0 Failures, 0 Ignored**. W tym 8 nowych testów `sensor_freshness` + 9 `switch_debounce` (8 przeniesionych z CH4 1:1 + 1 nowy CH3).
- ✅ `idf.py build` (esp32s3, N16R8): zielony, binarka 0xe9b70 B (39% partycji wolne).
- ✅ Czystość nagłówków: grep `esp_*`/`driver/*` w `sensor_freshness.h` i `switch_debounce.h` — brak include (jedyne trafienie to `esp_timer_get_time()` w komentarzu kontraktu).

## Zgodność z planem technicznym

Plan: `docs/plans/2026-06-29-001-feat-spot-lock-position-hold-plan.md` (Faza 1, Unit 1 linie 119–148, Unit 2 linie 150–177).

- **Unit 1** — zgodny 1:1: `sensor_freshness.{h,c}`, `gps_state.fresh`, `s_last_fix_ms`, `GPS_STALE_AFTER_MS=1500`, `update_freshness` w tasku czytnika; rejestracja w `PURE_SOURCES`/`TEST_SOURCES`/`test_main.c`. Wszystkie scenariusze testowe z planu pokryte.
- **Unit 2** — zgodny 1:1: czysty rename `ch4_switch → switch_debounce` (symbole + testy + CMake), druga instancja `s_ch3_switch` na `RC_CAP_CH3`, `spot_lock_switch_on` + `spot_lock_switch_edge_on` do `loop_inputs`. Wszystkie scenariusze testowe pokryte.
- **Odchylenia od planu:** brak. Wszystkie pliki testowe zdefiniowane w planie istnieją i zawierają asercje.

---

## Findingi (konsolidacja 3 agentów: architecture, test-coverage, security)

### 🟡 P3 — nity (do rozważenia, nieblokujące)

1. 🟡 **KOD** `components/rc_capture/include/rc_sample.h:13-16` — nieaktualne komentarze pinów (artefakt migracji ESP32-classic → S3). Nagłówek mówi `CH3 → GPIO27`, podczas gdy faktyczna mapa w `rc_capture.c:28-31` to `CH3 → GPIO8` (a `control_loop.c:71` poprawnie pisze GPIO8). CH3 staje się aktywny w tej fazie, więc błędny "GPIO27" jest teraz mylący i grozi błędem montażowym. *Już wytrackowane w sekcji „Zamknięcie" pliku zadań.* (zgłoszone przez security + architecture)

2. 🟡 **KOD** `components/gps/src/sensor_freshness.c` vs `components/imu/src/bno085.c:199` — granica świeżości różni się od kontraktu IMU, który commit deklaruje „mirror". IMU: stale przy `> threshold` (fresh dokładnie na progu); GPS: stale przy `>= threshold` (strict `<`). Wybór GPS jest świadomy i lepiej przetestowany, ale stwierdzenie „mirrors imu_state.ok contract" jest nieścisłe o 1 ms. Albo zrównać IMU później, albo zmiękczyć opis. (architecture)

3. 🟡 **KOD** `components/control_loop/src/control_loop.c` — 448 linii (> 300 z coding-rules §1). Przerost istniejący (Unit 2 dodał ~30 linii), nie regresja. Jeśli kolejne fazy będą rozbudowywać ten plik, rozważyć ekstrakcję adaptera HAL `aux_switch` (pary `make_*_cfg` + `apply_*_switch` dla CH3/CH4). (architecture)

4. 🟡 **TEST** `test/host/test_switch_debounce.c:220` — `test_ch3_enter_abort_and_hold` jest funkcjonalnie zbieżny z `test_rising_edge` + `test_falling_edge` + `test_holding_*` (ten sam generyczny moduł). Plan jawnie go wymaga (linia 175), reguła „duplication > complexity" go usprawiedliwia — zostawić. (test-coverage)

5. 🟡 **TEST** Scenariusz Unit 1 „parse bez fixu nie odświeża `last_ms`" testowany na czystej `sensor_freshness_stamp`, nie na okablowaniu w `gps_reader.c` (zgodne z Pure⊥HAL). Integracja `gps_get_state()` zwraca świeżość pozostaje w niezaznaczonych checkboxach „Weryfikacja" — osobny krok weryfikacyjny. (test-coverage)

### Nota do Unit 6 (nie finding Fazy 1)

- 🟡 **Obserwacja** `gps_reader.c:182` seeduje `s_last_fix_ms = now_ms()`, więc przez pierwsze ~1.5 s po starcie `sensor_is_fresh` zwraca true zanim dotrze realny fix. Bezpieczne TYLKO jeśli konsument spot-lock (Unit 6) bramkuje wejście również po `s_state.fix` (start false). Sam `fresh` nie wystarcza jako warunek wejścia w hold. Do pilnowania w review Unit 6. (security)

---

## E2E / Weryfikacja w przeglądarce

**N/A dla Fazy 1.** Brak scenariuszy `Weryfikacja:` typu browser/UI — wszystkie kryteria ukończenia Fazy 1 są host-testowe / grep / build (potwierdzone powyżej). E2E telemetrii panelu należy do Unit 7 (Faza 3, hardware — log w `known-issues`).

## Mocne strony

- Podręcznikowy `Pure ⊥ HAL`: `apply_ch3_switch` to cienki adapter delegujący całą logikę do czystego `switch_debounce_update`; decyzja świeżości w pełni wyekstrahowana i host-testowana.
- Wrap-safe `sensor_is_fresh` z testami o realnej mocy wyroczni (FAILują przy naiwnym signed `now-last` — zweryfikowane empirycznie przez agenta).
- Rename bez osłabienia asercji: wszystkie 8 scenariuszy CH4 przeniesione 1:1, zero usuniętych asercji.
- Kontrakt R12 (aux kanały poza `rc_valid`/failsafe) zachowany — `grep` w `rc_validity.c` potwierdza brak referencji CH3/CH4.
- Named constant `SPOT_LOCK_CH3_THRESHOLD_US` zamiast magic number; świadomy YAGNI (hardcode zamiast wymyślania pola settings).
