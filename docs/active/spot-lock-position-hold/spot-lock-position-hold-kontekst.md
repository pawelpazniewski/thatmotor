# Kontekst: Spot-lock — automatyczne utrzymywanie pozycji GPS (CH3)

**Branch:** `feature/spot-lock-position-hold`
**Ostatnia aktualizacja:** 2026-06-29

## Źródła
- Requirements doc: `docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md`
- Plan techniczny: `docs/plans/2026-06-29-001-feat-spot-lock-position-hold-plan.md`

---

## Powiązane pliki

### Do stworzenia
- `components/gps/include/sensor_freshness.h` + `components/gps/src/sensor_freshness.c` — czysty wrap-safe predykat świeżości
- `components/control_loop/include/geo_math.h` + `components/control_loop/src/geo_math.c` — odległość + bearing (equirectangular)
- `components/control_loop/include/spot_lock.h` + `components/control_loop/src/spot_lock.c` — regulator + sub-stan OFF/ACTIVE/PAUSED
- `test/host/test_sensor_freshness.c`, `test/host/test_geo_math.c`, `test/host/test_spot_lock.c`

### Do zmiany nazwy (rename)
- `components/rc_validity/src/ch4_switch.c` → `switch_debounce.c`; `include/ch4_switch.h` → `switch_debounce.h` (symbole `ch4_switch_*` → `switch_debounce_*`)
- `test/host/test_ch4_switch.c` → `test_switch_debounce.c` (zachowane asercje)

### Do modyfikacji
- `components/gps/include/nmea_parse.h` — pole świeżości w `gps_state`
- `components/gps/src/gps_reader.c` — `s_last_fix_ms`, `GPS_STALE_AFTER_MS 1500`, staleness w pętli taska
- `components/control_loop/src/control_loop.c` — `read_inputs` (odczyt GPS/IMU/CH3 → `loop_inputs`), `apply_ch4_switch`/`make_ch4_switch_cfg` (rename), instancja CH3 debounce, `publish_snapshot` (pola spot-lock)
- `components/control_loop/include/loop_step.h` + `src/loop_step.c` — `loop_inputs` (gps/imu/ch3), `loop_state` (spot_lock_state), override w gałęzi ARMED, predykat `stick_within_neutral`
- `components/control_loop/include/control_loop.h` — pola telemetrii `spot_lock_*`
- `components/signal_chain/include/signal_chain.h` + `src/throttle_chain.c` + `src/servo_chain.c` — tryby `THROTTLE_TARGET_SPOT_LOCK`/`SERVO_TARGET_SPOT_LOCK` z computed command (ramp/slew → `map_normalized_to_us` → hard clamp)
- `components/settings/include/settings_model.h` — nowe pola + bump `SETTINGS_SCHEMA_VERSION`
- `components/settings/src/settings_ranges.h`, `src/settings_defaults.c`, `src/settings_validate.c` — zakresy/defaults/walidacja
- `components/web_panel/src/params_json.c` — dopisanie do `U16_FIELDS`
- `components/web_panel/src/ws_telemetry.c` — `snapshot_to_json` (nowe pola, ints only)
- front-end panelu (web_panel) — wyświetlenie spot-lock
- `components/rc_validity/CMakeLists.txt`, `test/host/CMakeLists.txt`, `test/host/test_main.c` — rejestracja nowych/przemianowanych źródeł i suit

### Wzorce do naśladowania (referencje)
- `components/imu/src/bno085.c` — wzorzec świeżości sensora (`IMU_STALE_AFTER_MS`, `s_last_report_ms`, `store_state(false,...)`)
- `components/imu/src/quat_to_yaw.c` + `test/host/test_quat_to_yaw.c` — czysta matematyka + wrap kąta
- `components/control_loop/src/loop_step.c` + `test/host/test_loop_step.c` — kompozycja czystych kroków
- `components/state_machine/src/state_machine.c` — przejścia jako czyste funkcje, mapowanie trybów celu
- `components/signal_chain/src/chain_math.c` (`map_normalized_to_us`, `shape_deadband`, `normalize_us`)
- `components/safety_clamp/src/safety_clamp.c` + `test/host/test_safety_clamp.c` — hard clamp SI-3 + oracle power
- `components/rc_capture/src/cap_math.c` + `test/host/test_cap_math.c` — wrap-safe elapsed
- settings: istniejące pola `max_throttle_fwd_pct`, `throttle_deadband_us` (model→ranges→defaults→validate→params_json)

## Decyzje techniczne

1. **Spot-lock = flaga w ARMED, nie `sm_state`.** Override w `loop_step` tylko gdy
   `sm.state == SM_STATE_ARMED`. Gdy `sm_step` zwróci FAILSAFE → override się nie
   wykonuje → spot-lock ustępuje bezwarunkowo. Zero zmian w maszynie stanów i jej testach.

2. **GPS/IMU wchodzą do toru sterowania wyłącznie spot-lock, NIGDY do failsafe/RC_valid.**
   Odczyt przeniesiony do `read_inputs` → `loop_inputs`. Utrata GPS/heading → **pauza**
   (neutral + center), nie failsafe. `rc_valid`/`channel_valid`/`sm_inputs` nietknięte.

3. **Reużycie mappera + ramp + clamp.** Spot-lock produkuje komendy znormalizowane
   (servo signed; throttle tylko przód 0..cap). Integracja przez nowe tryby celu
   chainu; ramp/slew → `map_normalized_to_us` → hard clamp (SI-3 niezagrożone).

4. **Jedna domena neutralności drążka dla wejścia (R3) i abortu (R4).** Wspólny
   `stick_within_neutral(raw_us, deadband_us, params)` na bazie `normalize_us` + deadband,
   bez osobnego progu override. `steer_deadband_us` default = 0 (ścisłe) — ewentualny
   niezerowy deadband steru = strojenie w panelu.

5. **Świeżość GPS jak IMU + czysty predykat wrap-safe.** Domena `now_ms()`
   (`esp_timer_get_time()/1000`, uint32), modular subtraction, kontrakt epoki w nagłówku,
   host-test granicy wrapu. `GPS_STALE_AFTER_MS ~1500`. `s_last_fix_ms` odświeżany tylko
   przy parsie z fixem.

6. **Wspólny moduł switch-debounce.** `ch4_switch` jest już generyczny — rename do
   `switch_debounce`, druga instancja dla CH3 (poza RC_valid, jak CH4 / R12).

7. **geo_math: equirectangular.** `dN=(lat-ref)*k_lat`, `dE=(lon-ref)*k_lon*cos(ref_lat)`,
   `dist=hypot`, `bearing=atan2(dE,dN)`. Float wewnątrz (FPU), telemetria na intach.

8. **Regulator: start od czystego P** (gain odległości → throttle, gain błędu kierunku →
   servo) + bramka ±60° + cap gazu (R7) liczony w `spot_lock_step`. I/D odroczone do
   strojenia. Punkt za rufą (|błąd|>60°) → brak gazu, servo skręca → naturalny jeden
   łagodny zawrót (efekt bramki, nie osobna logika).

9. **Telemetria na intach:** `spot_lock_state` (0/1/2), `spot_lock_err_m` (uint16),
   `spot_lock_bearing_deg10` (uint16, spójne z `imu_heading_deg10`). Dziób już dostępny.

10. **Settings: bump `SETTINGS_SCHEMA_VERSION`** → `blob_codec` migruje brakujące pola do
    defaults. Reużycie istniejącego toru pending-apply (SI-6), bez nowej logiki apply.

## Zależności i kolejność

- Faza 1 (Unit 1, 2) — niezależne, mogą iść równolegle.
- Faza 2 (Unit 3 → Unit 4) — Unit 4 zależy od geo_math.
- Faza 3: Unit 5 niezależny; **Unit 6 zależy od 1, 2, 4, 5**; Unit 7 zależy od 6.

## Reguły projektu (bramki jakości)

- `.claude/rules/coding-rules.md`: pliki <300 linii, funkcje <50, nesting ≤2, NIGDY nie
  osłabiaj/usuwaj testów, każda nowa funkcja = happy + error case, fix kodu nie testu.
- `.claude/rules/learned-patterns.md`: wrap-safe recency w jednej domenie + host-test
  granicy; oracle power testu clampu/limitu (wejście poza zakresem); Pure ⊥ HAL
  (czyste funkcje bez `esp_*`/`driver/*`, grep-checkable).
- Self-check przed „gotowe": host-tests (`test/host/run.sh`) + `idf.py build`.
- Luki tylko-hardware (akwizycja fixu, realna reakcja silnika, zawrót za rufą) → log w
  `docs/completed/kayak-motor-firmware-v1/known-issues.md`.
