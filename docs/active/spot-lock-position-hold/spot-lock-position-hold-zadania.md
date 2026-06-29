# Zadania: Spot-lock — automatyczne utrzymywanie pozycji GPS (CH3)

**Branch:** `feature/spot-lock-position-hold`
**Ostatnia aktualizacja:** 2026-06-29

## Źródła
- Requirements doc: `docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md`
- Plan techniczny: `docs/plans/2026-06-29-001-feat-spot-lock-position-hold-plan.md`

Legenda: `Test:` = scenariusz testowy (host/Unity), `Weryfikacja:` = kryterium ukończenia.

---

## Faza 1 — Fundamenty wejść

### Unit 1: Licznik świeżości GPS + czysty predykat wrap-safe (R5)

Implementacja:
- [x] Stwórz `components/gps/include/sensor_freshness.h` (`bool sensor_is_fresh(uint32_t now_ms, uint32_t last_ms, uint32_t threshold_ms)`, kontrakt epoki w nagłówku)
- [x] Stwórz `components/gps/src/sensor_freshness.c`
- [x] Modyfikuj `components/gps/include/nmea_parse.h` — pole świeżości w `gps_state`
- [x] Modyfikuj `components/gps/src/gps_reader.c` — `s_last_fix_ms`, `GPS_STALE_AFTER_MS 1500`, staleness w pętli taska, odświeżanie `s_last_fix_ms` tylko przy parsie z fixem
- [x] Stwórz `test/host/test_sensor_freshness.c`; zarejestruj w `test/host/CMakeLists.txt` (PURE_SOURCES + TEST_SOURCES) i `test/host/test_main.c`

Testy (test-first dla `sensor_is_fresh`):
- [x] Test: `now-last < threshold` → fresh=true; `> threshold` → false; `== threshold` zdefiniowane i przetestowane
- [x] Test: granica wrapu uint32 (`last` tuż przed `UINT32_MAX`, `now` po przewinięciu) → poprawne; test FAILuje przy naiwnym `now-last`
- [x] Test: parse bez fixu nie odświeża `last_ms`

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone (w tym wrap)
- [ ] Weryfikacja: `gps_get_state()` zwraca świeżość zgodną z progiem
- [ ] Weryfikacja: grep brak `esp_*`/`driver/*` w `sensor_freshness.h`

### Unit 2: Wspólny moduł switch-debounce + odczyt CH3 (R1, R4)

Implementacja:
- [x] Rename `components/rc_validity/src/ch4_switch.c` → `switch_debounce.c`, `include/ch4_switch.h` → `switch_debounce.h` (symbole `ch4_switch_*` → `switch_debounce_*`)
- [x] Aktualizuj konsumentów CH4: `control_loop.c::apply_ch4_switch`, `make_ch4_switch_cfg`; `components/rc_validity/CMakeLists.txt`; `test/host/CMakeLists.txt`
- [x] Modyfikuj `control_loop.c` — odczyt `RC_CAP_CH3`, instancja `switch_debounce_state s_ch3_switch` + cfg, wynik do `loop_inputs` (`spot_lock_switch_on` + zbocze)
- [x] Rename `test/host/test_ch4_switch.c` → `test_switch_debounce.c`; aktualizuj `test_main.c`

Testy:
- [x] Test: wszystkie istniejące scenariusze CH4 przechodzą pod nową nazwą (baseline bez eventu, debounce N ramek, hold przy out-of-band, edge-only)
- [x] Test: CH3 low→high po debounce → event TO_HIGH; high→low → TO_LOW; trzymanie nie powtarza eventu

Weryfikacja:
- [ ] Weryfikacja: pełny host-suite zielony (zero regresji CH4)
- [ ] Weryfikacja: `idf.py build` zielony
- [ ] Weryfikacja: CH3 ON/OFF widoczny w snapshocie i jako wejście do `loop_step`

---

## Do poprawy po review fazy 1

Severity gate: ✅ GOTOWE DO KONTYNUACJI (0× P1, 0× P2). Pełny raport: `review-faza-1.md`.
Bramki zielone: host-tests 305/305, `idf.py build` (esp32s3) OK. Brak odchyleń od planu.

Tylko nity P3 (opcjonalne):
- [ ] 🟡 [nit] **components/rc_capture/include/rc_sample.h:13-16** — nieaktualne komentarze pinów (`CH3 → GPIO27`, faktycznie GPIO8; też CH1/CH2/CH4). Już wytrackowane w „Zamknięcie".
- [ ] 🟡 [nit] **components/gps/src/sensor_freshness.c** — opis „mirrors imu_state.ok contract" nieścisły o 1 ms (GPS strict `<`, IMU `>`); zmiękczyć opis lub zrównać IMU.
- [ ] 🟡 [nit] **components/control_loop/src/control_loop.c** — 448 linii (>300); przerost istniejący. Jeśli rośnie dalej, rozważyć ekstrakcję adaptera `aux_switch` (CH3/CH4).
- [ ] 🟡 [nit] Nota do Unit 6: seed `s_last_fix_ms` daje ~1.5 s `fresh=true` bez realnego fixu — wejście w hold MUSI bramkować też `s_state.fix` (nie sam `fresh`).

---

## Faza 2 — Czysta logika spot-lock

### Unit 3: Czysty moduł geo_math (odległość + bearing) (R1, R2)

Implementacja:
- [x] Stwórz `components/control_loop/include/geo_math.h` (`geo_offset_m`, `geo_distance_m`, `geo_bearing_deg10`)
- [x] Stwórz `components/control_loop/src/geo_math.c` (equirectangular, named constants, tylko `math.h`)
- [x] Stwórz `test/host/test_geo_math.c`; zarejestruj w CMake + `test_main.c`

Testy (test-first, oracle o znanej geometrii):
- [x] Test: punkt na N → bearing ≈ 0; E ≈ 90; S ≈ 180; W ≈ 270 (z tolerancją)
- [x] Test: znany dystans (np. 0,001° lat ≈ 111 m) w tolerancji
- [x] Test: zerowy offset → dystans 0; bearing zdefiniowany i przetestowany
- [x] Test: korekcja cos(lat) na wyższej szerokości zmniejsza dE (FAILuje bez korekcji)

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone
- [ ] Weryfikacja: grep brak `esp_*`/`driver/*` w `geo_math.h`

### Unit 4: Czysty regulator spot_lock_step() (R2, R3, R4, R5, R6, R7)

Implementacja:
- [x] Stwórz `components/control_loop/include/spot_lock.h` (`spot_lock_inputs`, `spot_lock_state`, `spot_lock_outputs`, `spot_lock_step(...)`)
- [x] Stwórz `components/control_loop/src/spot_lock.c` (sub-stan OFF/ACTIVE/PAUSED, wejście/abort, deadband, bramka ±60°, P-control, cap gazu)
- [x] Modyfikuj `test/host/CMakeLists.txt` (dodaj `spot_lock.c`)
- [x] Stwórz `test/host/test_spot_lock.c`; zarejestruj w `test_main.c`

Testy (test-first; cap/bramka/deadband wejściem POZA zakresem — oracle power):
- [x] Test: wejście ARMED+fresh+neutral+CH3 zbocze ON → ACTIVE, cel = bieżąca pozycja
- [x] Test: blokada wejścia — CH3 ON ale DISARMED → OFF; brak fixu → OFF; drążek wychylony → OFF (każdy warunek osobno)
- [x] Test: deadband — `dist` w strefie → throttle neutral + servo center; poza strefą → throttle>neutral
- [x] Test: bramka ±60° — błąd 80° → throttle neutral, servo skręca; błąd 10° → throttle>neutral
- [x] Test: cap gazu — bardzo duży `dist` → throttle == max_throttle_pct (nie wyżej)
- [x] Test: pauza — ACTIVE + `gps_fresh=false` → PAUSED + neutral + center; powrót → ACTIVE z tym celem; to samo dla `imu_ok=false`
- [x] Test: abort — ACTIVE + CH3 OFF → OFF; ACTIVE + drążek poza deadbandem → OFF

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone
- [ ] Weryfikacja: grep brak `esp_*`/`driver/*` w `spot_lock.h`
- [ ] Weryfikacja: funkcja deterministyczna (te same wejścia → te same wyjścia)

---

## Do poprawy po review fazy 2

Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI (0× P1, 1× P2). Pełny raport: `review-faza-2.md`.
Bramki zielone: host-tests 327/327, `idf.py build` (esp32s3) OK, `geo_math.h` składniowo
poprawny (bloki komentarzy zbalansowane). Brak odchyleń od planu.

- [x] 🟠 [important] **components/control_loop/src/spot_lock.c:135** — `gps_has_fix` nie jest re-walidowany w trakcie ACTIVE hold (gate sprawdza tylko `!gps_fresh || !imu_ok`). Do rozstrzygnięcia w Unit 6: potwierdzić, że `gps_fresh` w trakcie hold prowoduje jakość fixu (fresh wygasa ≤1,5 s po utracie fixu); jeśli nie — dodać `|| !in->gps_has_fix` do warunku pauzy. ✅ Naprawione (cykl 1): warunek pauzy = `!gps_fresh || !imu_ok || !gps_has_fix`; host-test `test_pause_on_fix_loss_then_resume_keeps_target` z mocą wyroczni.

Nity P3 (opcjonalne):
- [ ] 🟡 [nit] **components/control_loop/src/spot_lock.c:73-77** — `throttle_command`: cast-before-clamp; dla `dist_m > ~32 km` iloczyn przekracza `INT32_MAX` (cast impl-defined). Cap i tak wymusza bezpieczny zakres; opcjonalnie clamp w `double` przed castem.
- [ ] 🟡 [nit] **components/control_loop/src/spot_lock.c:113** — brak NULL-guardów na `in`/`p`/`st` (kontrakt non-NULL; akceptowalne dla czystej funkcji wewnętrznej).
- [ ] 🟡 [nit] **components/control_loop/src/geo_math.c:31** — model equirectangular degeneruje przy biegunach (`cos(ref_lat)→0`); nagłówek nie dokumentuje tego limitu (bez znaczenia dla szerokości kajakowych).
- [ ] 🟡 [nit] **geo_math.c / spot_lock.c** — duplikacja stałych `DEG10_*`; `SPOT_LOCK_CMD_FULL_SCALE` zdublowany względem `SIGNAL_NORMALIZED_FULL_SCALE` — Unit 6 musi utrzymać synchronizację.
- [ ] 🟡 [nit] **components/control_loop/src/spot_lock.c:41-49** — `heading_error_deg10` przy błędzie 1800 zwraca +1800 (asymetria na granicy); nieszkodliwe (bramka ±600 odrzuca).
- [ ] 🟡 [nit] **test/host/test_spot_lock.c** — wejściowy gate `gps_fresh` nie testowany osobno (pokryte `gps_has_fix=false` i brak zbocza; logika dzielona z pauzą).
- [ ] 🟡 [nit] **test/host/test_spot_lock.c** — brak dedykowanego testu przejścia z sub-stanu PAUSED (PAUSED→OFF / PAUSED→PAUSED); wspólny pierwszy guard, ryzyko niskie.
- [ ] 🟡 [nit] **test/host/test_spot_lock.c:157** — asercja `throttle_cmd==0` redundantna względem wyroczni deadbandu (oracle niesie `servo_cmd==0`); zostawić.

---

## Faza 3 — Integracja, parametry, telemetria

### Unit 5: Parametry spot-lock w settings (SI-6) (R6, R7, R8)

Implementacja:
- [x] Modyfikuj `components/settings/include/settings_model.h` — pola (`spot_lock_deadband_m/dm`, `spot_lock_max_throttle_pct`, `spot_lock_gain_*`), bump `SETTINGS_SCHEMA_VERSION`
- [x] Modyfikuj `components/settings/src/settings_ranges.h` — MIN/MAX/DEFAULT (deadband ~3 m, max gaz ~35%)
- [x] Modyfikuj `components/settings/src/settings_defaults.c` — przypisz defaults
- [x] Modyfikuj `components/settings/src/settings_validate.c` — walidacja zakresów
- [x] Modyfikuj `components/web_panel/src/params_json.c` — dopisz do `U16_FIELDS`
- [x] Rozszerz `test/host/test_settings_validate.c` (+ `test_blob_codec.c` round-trip nowej wersji, jeśli dotyczy)

Testy:
- [x] Test: wartość poza zakresem (np. max gaz 200%) odrzucona/clampowana; w zakresie akceptowana
- [x] Test: defaults ładują się przy świeżej/skorrumpowanej NVS (UNCALIBRATED) z sensownymi wartościami spot-lock
- [x] Test: POST nowych pól w ARMED → 409 (SI-6 niezmienione)

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone
- [ ] Weryfikacja: nowe pola serializują się w `/params`
- [ ] Weryfikacja: `idf.py build` zielony

### Unit 6: Integracja spot_lock w loop_step (R1, R2, R3, R4, R5, R7)

Implementacja:
- [x] Modyfikuj `components/control_loop/include/loop_step.h` — `loop_inputs` (gps lat/lon+fresh, imu heading+ok, `spot_lock_switch_on`+zbocze), `loop_state` (`spot_lock_state`)
- [x] Modyfikuj `components/control_loop/src/loop_step.c` — po `sm_step`: gdy `state==ARMED` wywołaj `spot_lock_step`; ACTIVE/PAUSED → nadpisz tryby celu na `*_SPOT_LOCK`; predykat `stick_within_neutral`
- [x] Modyfikuj `components/signal_chain/include/signal_chain.h` + `src/throttle_chain.c` + `src/servo_chain.c` — tryby `THROTTLE_TARGET_SPOT_LOCK`/`SERVO_TARGET_SPOT_LOCK` z computed command (ramp/slew → `map_normalized_to_us` → hard clamp)
- [x] Modyfikuj `control_loop.c::read_inputs` — odczyt `gps_get_state`/`imu_get_state`/`RC_CAP_CH3` → `loop_inputs`; współdziel z `publish_snapshot`
- [x] Rozszerz `test/host/test_loop_step.c`, `test_throttle_chain.c`, `test_servo_chain.c` o tryb spot-lock

Testy (najpierw failing test integracyjny wejście→hold→abort):
- [x] Test: ARMED+fresh+neutral+CH3 ON → servo/ESC computed (≠ tor stickowy), flaga ACTIVE
- [x] Test: ACTIVE + utrata RC → `sm_step`=FAILSAFE → ESC neutral + servo center (override się NIE wykonuje); FAILuje gdyby override działał poza ARMED
- [x] Test: ACTIVE + CH3 OFF → tor manualny ≤ 1 cykl
- [x] Test: ACTIVE + gaz/ster poza deadband → natychmiast manual (override)
- [x] Test: ACTIVE + `!gps_fresh`/`!imu_ok` → ESC neutral + servo center, flaga PAUSED (nie OFF, nie failsafe)
- [x] Test: każde wyjście spot-lock przez hard clamp (out-of-window computed → clamp)

Weryfikacja:
- [ ] Weryfikacja: host-tests zielone; zero regresji `loop_step`/state_machine/chain
- [ ] Weryfikacja: `idf.py build` zielony
- [ ] Weryfikacja: grep brak nowych `esp_*`/`driver/*` w czystych nagłówkach

### Unit 7: Telemetria spot-lock + panel (R9)

Implementacja:
- [x] Modyfikuj `components/control_loop/include/control_loop.h` — `spot_lock_state` (u8), `spot_lock_err_m` (u16), `spot_lock_bearing_deg10` (u16)
- [x] Modyfikuj `control_loop.c::publish_snapshot` — populacja z `loop_outputs`/telemetry
- [x] Modyfikuj `components/web_panel/src/ws_telemetry.c::snapshot_to_json` — nowe pola (ints/bools only)
- [x] Modyfikuj front-end panelu — blok „Spot-lock: off/active/paused, błąd X m, kierunek Y° / dziób Z°"
- [x] Rozszerz host-test serializacji telemetrii (jeśli istnieje) lub zweryfikuj kontrakt JSON

Testy:
- [x] Test: snapshot z ACTIVE serializuje `spot_lock_state=1`, `err_m`, `bearing_deg10` jako int (host: `test_spot_lock_holds_with_computed_throttle` asercje `loop_telemetry.spot_lock_*`; ścieżka JSON `snapshot_to_json` to cienki HAL `%u`, zweryfikowany `idf.py build`)
- [ ] Test: [E2E] panel pokazuje off→active po CH3 ON, błąd maleje przy dopływaniu, paused przy utracie GPS (hardware — log w known-issues) — ODŁOŻONE do `docs/completed/kayak-motor-firmware-v1/known-issues.md` §4b

Weryfikacja:
- [ ] Weryfikacja: `idf.py build` zielony
- [ ] Weryfikacja: panel renderuje nowe pola; JSON zawiera `spot_lock_*`
- [ ] Weryfikacja: hardware/E2E odłożone do `docs/completed/kayak-motor-firmware-v1/known-issues.md`

---

## Zamknięcie

- [ ] Aktualizacja `README`/pinout: CH3=GPIO8 aktywny przełącznik spot-lock; ujednolić mylący komentarz w `rc_sample.h` (CH3=GPIO8, nie GPIO27)
- [ ] Dopisanie luk hardware/E2E do `docs/completed/kayak-motor-firmware-v1/known-issues.md`
- [ ] Finalny self-check: `test/host/run.sh` zielony + `idf.py build` zielony
- [ ] Rozważ `/dev-compound` dla nowych wzorców (świeżość GPS, sensor→control bez naruszenia failsafe)
