# Kontekst: Spot-lock — automatyczne utrzymywanie pozycji GPS (CH3)

**Branch:** `feature/spot-lock-position-hold`
**Ostatnia aktualizacja:** 2026-06-29 (Faza 3 ukończona — implementacja)

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

## Postęp implementacji

- **Faza 1 — Fundamenty wejść — UKOŃCZONA (2026-06-29).**
  - Unit 1 (commit `c06795f`): czysty moduł `sensor_freshness` (`sensor_is_fresh`
    wrap-safe modular subtraction; `sensor_freshness_stamp` odświeża znacznik tylko
    przy fixie). Pole `gps_state.fresh` ustawiane przez task czytnika
    (`GPS_STALE_AFTER_MS=1500`), poza failsafe. Boundary `==threshold` → stale (strict <).
    Decyzja: wydzielono `sensor_freshness_stamp` zza HAL, by reguła "odśwież tylko
    przy fixie" była host-testowalna (Pure ⊥ HAL).
  - Unit 2 (commit `2983edd`): rename `ch4_switch` → `switch_debounce` (symbole
    `switch_debounce_*` / `SWITCH_DEBOUNCE_*`), zero osłabienia asercji. Druga
    instancja `s_ch3_switch` w `control_loop.c` (`SPOT_LOCK_CH3_THRESHOLD_US=1500`,
    poza RC_valid). Nowe pola `loop_inputs.spot_lock_switch_on` /
    `spot_lock_switch_edge_on` populowane w `read_inputs`; `loop_step` skonsumuje je
    dopiero w Unit 6.
  - Walidacja: host-tests 305/305 zielone; `idf.py build` (esp32s3) zielony.
  - **Review Fazy 1 (2026-06-29):** ✅ severity gate CZYSTE — 0× P1, 0× P2, 5× P3
    (raport `review-faza-1.md`). Multi-agent: architecture / test-coverage / security.
    Potwierdzono: Pure⊥HAL (cienki `apply_ch3_switch`), single-writer `s_last_fix_ms`,
    wrap-safe `sensor_is_fresh` z mocą wyroczni (empirycznie FAILuje przy naiwnym
    `now-last`), kontrakt R12 (CH3/CH4 poza `rc_valid` — grep w `rc_validity.c`),
    rename CH4→switch_debounce bez osłabienia asercji (8 testów 1:1 + 1 CH3),
    czyste nagłówki (brak `esp_*`/`driver/*`). Zero odchyleń od planu technicznego.
    Nity P3: stale komentarze pinów `rc_sample.h` (GPIO27 vs faktyczne GPIO8 — w
    „Zamknięcie"), opis „mirror IMU" nieścisły o 1 ms granicy (GPS strict `<`),
    `control_loop.c` 448 linii (>300, przerost istniejący).
    **Do Unit 6:** seed `s_last_fix_ms=now_ms()` daje ~1.5 s `fresh=true` bez fixu —
    wejście w hold MUSI bramkować również `s_state.fix`, nie sam `fresh`.

- **Faza 2 — Czysta logika spot-lock — UKOŃCZONA (2026-06-29).**
  - Unit 3 (geo_math): czysty moduł `components/control_loop/{include,src}/geo_math`
    (equirectangular). `geo_offset_m` → ENU (north/east w metrach) względem ref,
    `geo_distance_m` (hypot), `geo_bearing_deg10` (atan2(east,north), [0,3599],
    zero-offset → 0). Korekcja `cos(ref_lat)` na długości geograficznej; named
    constants (EARTH_RADIUS_M, METERS_PER_DEG_LAT). Tylko `math.h`. 7 host-testów:
    N/E/S/W bearing, znany dystans 0,001°≈111 m, zerowy offset, oracle cos(lat)
    (wschód na 60°N ~ połowa wartości równikowej — FAILuje bez korekcji).
  - Unit 4 (spot_lock): czysty regulator `components/control_loop/{include,src}/spot_lock`.
    Sub-stan `SPOT_LOCK_OFF/ACTIVE/PAUSED` (osobny od `sm_state`). Priorytet przejść:
    (1) ANY→OFF gdy `!armed||!ch3_on||!sticks_neutral` (abort R4); (2) OFF→ACTIVE
    tylko na `ch3_edge_on && gps_fresh && gps_has_fix` ze snapshotem celu (R1/R3);
    (3) ACTIVE/PAUSED→PAUSED gdy `!gps_fresh||!imu_ok` (neutral+center, cel zachowany,
    R5); (4) ACTIVE: deadband→luz (R6), poza nim P-servo (∝ błąd kierunku) + P-throttle
    (∝ dist, cap R7) tylko w bramce ±60° (R2). Komendy znormalizowane
    (`SPOT_LOCK_CMD_FULL_SCALE=1000`), telemetria na intach (`err_m`, `bearing_deg10`).
    **Decyzja:** `spot_lock_params` jako samodzielna struktura w nagłówku (odsprzężona
    od `settings_model.h`) — Unit 6 zmapuje settings→params; utrzymuje Pure⊥HAL.
    **Uwzględniono notę z review Fazy 1:** wejście bramkuje `gps_has_fix` (realny fix),
    nie sam `gps_fresh` — osobny test `test_entry_blocked_without_real_fix`.
    15 host-testów (wejście, 4× blokada wejścia w tym brak fixu + brak zbocza, deadband
    in/out, bramka 80°/10°, cap gazu, pauza GPS+IMU z powrotem, 2× abort, determinizm).
    Cap/bramkę/deadband testowano wejściem POZA zakresem (oracle power).
  - Pułapka: komentarz `esp_*/driver/*` w nagłówku zamykał blok `/* */` (sekwencja `*/`
    w `esp_*/`) — przeredagowano na „esp_ or driver".
  - Walidacja: host-tests 327/327 zielone (+22: 7 geo_math + 15 spot_lock);
    `idf.py build` (esp32s3) zielony — `geo_math.c`/`spot_lock.c` dodane do
    `components/control_loop/CMakeLists.txt` (kompilują się na toolchainie target,
    jeszcze nieużywane przez `loop_step` — integracja w Unit 6). Grep czysty: brak
    `esp_*`/`driver/*` include w `geo_math.h`/`spot_lock.h`.
  - **Review Fazy 2 (2026-06-29):** ⚠️ severity gate ZASTRZEŻENIA — 0× P1, **1× P2**,
    8× P3 (raport `review-faza-2.md`). Multi-agent: security / test-coverage +
    architecture self-review (agent architecture zaciął się na szumie clangd xtensa —
    analiza ręczna). Bramki: host-tests 327/327, `idf.py build` (esp32s3) zielony,
    `geo_math.h`/`spot_lock.h` składniowo poprawne (bloki komentarzy zbalansowane —
    pułapka `*/` naprawiona). Moc wyroczni testów (cap/bramka/deadband/cos) potwierdzona
    EMPIRYCZNIE: każda transformacja FAILuje po usunięciu. Kierunek sterowania
    (`geo_offset_m(ref,current)` = wektor ku celowi) zweryfikowany. Numerycznie bezpieczne
    (różnice w `double`, `atan2(0,0)` short-circuit, brak NaN/overflow w realnym zakresie).
    **P2 do Unit 6:** `gps_has_fix` NIE jest re-walidowany w gate hold (`spot_lock.c:135`
    sprawdza tylko `!gps_fresh||!imu_ok`) — potwierdzić, że `fresh` prowoduje fix w trakcie
    hold (fresh wygasa ≤1,5 s po utracie fixu) lub dodać `|| !gps_has_fix` do pauzy.
  - **Re-review Fazy 2 (cykl 1, commit `f612827`):** ✅ CZYSTE — 0× P1, 0× P2. P2
    NAPRAWIONY: gate pauzy = `!gps_fresh || !imu_ok || !gps_has_fix` (re-walidacja fixu
    w trakcie hold; spójny z gate'em wejścia). Semantyka PAUSED→ACTIVE z tym samym celem
    zachowana (`ref_*` ustawiane tylko na OFF→ACTIVE). Nowy `test_pause_on_fix_loss_then_
    resume_keeps_target` z udowodnioną EMPIRYCZNIE mocą wyroczni (po usunięciu
    `!gps_has_fix` test FAILuje: Expected 2/PAUSED Was 1/ACTIVE). Brak osłabienia asercji,
    zero regresji. Bramki: host-tests **328/328**, `idf.py build` (esp32s3) zielony.
    Pozostają tylko nity P3 (carry-over, opcjonalne). Pełny raport: `review-faza-2.md`.

- **Faza 3 — Integracja, parametry, telemetria — UKOŃCZONA (implementacja, 2026-06-29).**
  - Unit 5 (commit `22b934d`): 4 parametry spot-lock w `settings_params`
    (`spot_lock_deadband_m`, `spot_lock_max_throttle_pct`, `spot_lock_throttle_gain`,
    `spot_lock_servo_gain`) z łagodnymi defaultami (3 m / 35% / gain 30 per m /
    gain 20 per deg). Bump `SETTINGS_SCHEMA_VERSION` 5→6 + migracja `blob_codec`
    (`BLOB_CODEC_FIELD_BYTES` 53→61, SIZE 57→65; prior-schema v5 odrzucany →
    reload defaults). Walidacja zakresów (deadband 0..100 m, pct reuse 0..100,
    gain 0..1000). Serializacja w `/params` (U16_FIELDS). **SI-6 NIEZMIENIONE:**
    `params_decide_write` bramkuje na stanie, nie na polach — POST nowych pól w
    ARMED → 409. +5 host-testów (range/defaults/SI-6/blob round-trip v6).
  - Unit 6 (commit `c16c924`): integracja w `loop_step`. Po `sm_step`, TYLKO w
    gałęzi `sm.state==ARMED`, `resolve_spot_lock` woła czysty `spot_lock_step`
    (mapuje `loop_inputs`→`spot_lock_inputs`, `settings`→`spot_lock_params`;
    pct→`max_throttle_norm`). ACTIVE/PAUSED → `THROTTLE_TARGET_SPOT_LOCK` /
    `SERVO_TARGET_SPOT_LOCK` (computed command threaded jako nowy param do
    `throttle_chain_step`/`servo_chain_step`; ten sam tor ramp/slew →
    `map_normalized_to_us` → hard clamp SI-3). **Failsafe ZAWSZE wygrywa:** poza
    ARMED spot-lock forsowany OFF (resolve_spot_lock), override się nie wykonuje
    → FAILSAFE daje neutral+center. Nowy predykat `steer_is_neutral` +
    `sticks_within_neutral` (wspólna domena neutralności gazu i steru, R3/R4).
    GPS/IMU do `loop_inputs` przez `apply_sensor_inputs` w `read_inputs` — tylko
    wejścia spot-lock, NIGDY rc_valid/sm_inputs/failsafe. `loop_telemetry` niesie
    substate/err_m/bearing. +10 host-testów (6 integracyjnych: hold z computed
    throttle, failsafe-beats-spot-lock z mocą wyroczni, CH3 off→manual ≤1 cykl,
    abort stickiem, pauza GPS, hard clamp; +2 throttle SPOT_LOCK; +2 servo
    SPOT_LOCK). **Decyzja:** `spot_lock_cmd` jako dodatkowy param chainu
    (po `mode`), ignorowany poza trybem SPOT_LOCK — uniknięto duplikacji toru
    ramp/map/clamp; zaktualizowano 24 istniejące call-sites bez osłabienia asercji.
  - Unit 7 (commit poniżej): telemetria + panel. `control_loop_snapshot` +
    `spot_lock_state`/`err_m`/`bearing_deg10` (ints), `publish_snapshot` populuje
    z `out->telemetry`. `snapshot_to_json` dodaje 3 pola (`%u`, ints only).
    Panel: karta „Spot-lock (CH3)" (state off/active/paused, błąd[m], bearing do
    punktu, dziób) + PARAM_LABELS dla 4 nowych parametrów. JSON serializacja to
    cienki HAL — kontrakt int zweryfikowany `idf.py build`; wartości
    `loop_telemetry.spot_lock_*` host-testowane w `test_spot_lock_holds_*`.
  - Walidacja: host-tests **343/343** zielone (+15: 5 Unit 5 + 10 Unit 6);
    `idf.py build` (esp32s3) zielony po każdym Unicie. Luki hardware/E2E spot-lock
    odłożone do `known-issues.md` §4b.
  - **control_loop.c** urósł ~448→~480 linii (>300, przerost istniejący/HAL).
    Dodano spójny helper `apply_sensor_inputs`; ekstrakcja adaptera aux odłożona
    (P3, jak w nocie review Fazy 1) — do rozważenia w przyszłym refaktorze HAL.

## Reguły projektu (bramki jakości)

- `.claude/rules/coding-rules.md`: pliki <300 linii, funkcje <50, nesting ≤2, NIGDY nie
  osłabiaj/usuwaj testów, każda nowa funkcja = happy + error case, fix kodu nie testu.
- `.claude/rules/learned-patterns.md`: wrap-safe recency w jednej domenie + host-test
  granicy; oracle power testu clampu/limitu (wejście poza zakresem); Pure ⊥ HAL
  (czyste funkcje bez `esp_*`/`driver/*`, grep-checkable).
- Self-check przed „gotowe": host-tests (`test/host/run.sh`) + `idf.py build`.
- Luki tylko-hardware (akwizycja fixu, realna reakcja silnika, zawrót za rufą) → log w
  `docs/completed/kayak-motor-firmware-v1/known-issues.md`.
