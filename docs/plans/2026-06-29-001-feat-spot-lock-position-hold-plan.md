---
title: "feat: Spot-lock — automatyczne utrzymywanie pozycji GPS (CH3)"
type: feat
status: active
date: 2026-06-29
origin: docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md
---

# feat: Spot-lock — automatyczne utrzymywanie pozycji GPS (CH3)

## Przegląd

Dodajemy autonomiczny tryb **spot-lock**: po przełączeniu CH3 (GPIO8) w ON system
zapamiętuje bieżącą pozycję GPS i utrzymuje kajak w jej okolicy, sterując servo
kierunku i ESC. Tryb jest **sub-trybem w obrębie ARMED** (flaga, nie nowy stan
maszyny), działa **tylko przodem** w schemacie „celuj dziobem w punkt → jedź
przodem", nie trzyma kursu dzioba i bezwarunkowo ustępuje istniejącemu failsafe RC.
Cała logika decyzyjna jest czystą funkcją host-testowaną (Pure ⊥ HAL), bez
ingerencji w maszynę stanów ani w tor RC_valid.

## Ujęcie problemu

Operator kajaka chce zatrzymać się w jednym miejscu na wodzie (wędkowanie, zdjęcia,
odpoczynek) bez kotwicy i bez ciągłego korygowania drążkami. Wiatr i prąd spychają
kajak. Sprzęt jest gotowy: GPS NEO-M9N i kompas BNO085 działają jako źródła danych
(dziś wyłącznie diagnostyka, poza failsafe), CH3 jest zarezerwowany na GPIO8.
Brakuje warstwy logiki utrzymania pozycji oraz licznika świeżości GPS.

Fundament fizyczny (z dokumentu źródłowego): kajak jest **nieholonomiczny** — porusza
się tylko tam, gdzie patrzy dziób, a ster ma władzę **wyłącznie gdy łódka płynie**.
Stąd: (1) korekta pozycji = wycelowanie dzioba + dopłynięcie (jedna pętla);
(2) niezależne trzymanie kursu jest niewykonalne (w spoczynku silnik stoi → ster bez
władzy) — spot-lock **nie trzyma kursu**, kierunek dzioba jest efektem ubocznym.
(zob. źródło: docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md — „Kluczowe ograniczenie")

## Śledzenie wymagań

- **R1.** Aktywacja przez CH3 ON z zapisem celu „tu i teraz" (snapshot lat/lon; kursu nie zapamiętujemy).
- **R2.** Utrzymanie pozycji jedną pętlą: w martwej strefie ESC neutral (luz); poza nią — obróć dziób do punktu i przy dziobie w stożku ±60° dodaj ciąg do przodu proporcjonalny do odległości.
- **R3.** Warunki wejścia (wszystkie): ARMED, świeży fix GPS (≤ ~1,5 s), **oba drążki CH1/CH2 w neutralu** (w martwej strefie drążka).
- **R4.** Dwa niezależne aborty: CH3 OFF → natychmiast manual; ruch gazu lub steru **poza martwą strefę drążka** (tę samą, której używa tor manualny, **bez osobnego progu**) → natychmiast manual.
- **R5.** Świeżość sensorów: licznik świeżości GPS (~1,5 s, jak `IMU_STALE_AFTER_MS`). Utrata GPS **lub** świeżego heading (`imu_ok=false`) w trakcie → ESC neutral + servo center, tryb przechodzi w **„paused"** (pozostaje aktywny), wznawia po powrocie danych. Brak timeoutu wyjścia — granicą czasową jest failsafe RC.
- **R6.** Konfigurowalna martwa strefa pozycji (deadband), default ~3 m; ESC neutral w jej obrębie.
- **R7.** Konfigurowalny limit maksymalnego gazu (authority cap), default ~35%; spot-lock nigdy go nie przekracza.
- **R8.** Parametry (deadband, max gaz, gainy) edytowalne w panelu z regułą SI-6 (apply tylko w DISARMED); sensowne wartości domyślne.
- **R9.** Telemetria spot-lock: stan (off/aktywny/paused), błąd pozycji [m], kierunek do punktu [°] vs bieżący dziób [°].

## Granice scope'u (non-goals)

- Brak nawigacji do waypointów / cruise / podążania trasą — tylko jeden zapamiętany punkt.
- Brak ustawiania celu z panelu — cel zawsze „tu i teraz" przy CH3 ON (nie ręczne lat/lon w v1).
- Brak jog/nudge (przesuwania punktu) w v1.
- Brak trzymania kursu dzioba — fizycznie niewykonalne na tym sprzęcie w spoczynku.
- Brak ciągu wstecznego — spot-lock działa tylko przodem.
- Brak zmiany istniejącego failsafe i toru RC_valid — spot-lock jest flagą w ARMED i ustępuje failsafe.
- Nie ruszamy maszyny stanów (`sm_state`) ani jej testów — spot-lock nie jest nowym stanem.

## Kontekst i research

### Relevantny kod i wzorce

- **Pętla 50 Hz i czysta logika:** `components/control_loop/src/control_loop.c` (`run_one_cycle`, `read_inputs`, `publish_snapshot`, `maybe_apply_pending`) oraz czysta `loop_step()` w `components/control_loop/src/loop_step.c` (sygn. w `include/loop_step.h`). To wzorzec dla nowej czystej logiki i punkt integracji.
- **Maszyna stanów (NIE modyfikujemy semantyki):** `components/state_machine/src/state_machine.c` — `sm_step()`, `throttle_target_for`, `servo_target_for`. FAILSAFE wymusza `THROTTLE_TARGET_NEUTRAL` + `SERVO_TARGET_CENTER`. Spot-lock nakłada override **tylko gdy `state == SM_STATE_ARMED`**, więc automatycznie ustępuje failsafe.
- **Signal chain (cele + mapowanie + clamp):** `components/signal_chain/include/signal_chain.h` (`throttle_target_mode`, `servo_target_mode`, `throttle_chain_step`, `servo_chain_step`), reuse `map_normalized_to_us()` z `components/signal_chain/src/chain_math.c` jako wspólny mapper „komenda znormalizowana → us". Kolejność: normalize → deadband → reverse → (limit|endpoints) → TARGET → ramp/slew → map → **HARD CLAMP (SI-3)**.
- **Hard clamp SI-3:** `components/safety_clamp/src/safety_clamp.c` (`clamp_pwm_us`), bezwarunkowy przed wyjściem (`components/pwm_out/src/pwm_out_logic.c`).
- **Deadband drążka:** `components/signal_chain/src/chain_math.c` (`shape_deadband`, `apply_deadband`, `normalize_us`), `throttle_deadband_us` (default 80 µs), `steer_deadband_us` (default 0). To samo źródło neutralności dla wejścia i abortu spot-lock (R3/R4).
- **Wzorzec świeżości sensora (do replikacji dla GPS):** `components/imu/src/bno085.c` — `IMU_STALE_AFTER_MS 1000`, `s_last_report_ms`, kontrola staleness w pętli taska, `store_state(false,...)`; getter `imu_get_state()` (non-blocking, mutex). Stan: `components/imu/include/imu.h` (`imu_state{ ok, heading_deg10, calib }`).
- **GPS:** `components/gps/include/nmea_parse.h` (`gps_state{ fix, sats, lat_e7, lon_e7, speed_cms, course_deg }` — **brak timestampu/świeżości, do dorobienia**), `components/gps/src/gps_reader.c` (task UART, `gps_get_state()` non-blocking, mutex), `components/gps/src/nmea_parse.c` (`nmea_parse_line`).
- **CH3 + debounce przełącznika:** CH3 już przechwytywany na **GPIO8** (`components/rc_capture/src/rc_capture.c`, `RC_CAP_CH3`, osobna grupa MCPWM). Wzorzec debounce: `components/rc_validity/src/ch4_switch.c` + `include/ch4_switch.h` (generyczny: `cfg{threshold,sanity,debounce}`, `state`, `event`). Konsumpcja CH4: `apply_ch4_switch()` w `control_loop.c`.
- **Settings end-to-end (SI-6):** `components/settings/include/settings_model.h` (`settings_params`, `SETTINGS_SCHEMA_VERSION`), `src/settings_defaults.c`, `src/settings_ranges.h`, `src/settings_validate.c`, `components/web_panel/src/params_json.c` (tabele `U16_FIELDS`/`BOOL_FIELDS`, makro-driven), gate `params_decide.c` (`PARAMS_WRITE_*`), apply w `control_loop.c::maybe_apply_pending` (TOCTOU, single writer). `blob_codec` jest wersjonowany — bump `schema_version` migruje brakujące pola do defaults.
- **Telemetria:** `control_loop_snapshot` w `components/control_loop/include/control_loop.h` (ma już `ch3_us`, `gps_*`, `imu_*`), populacja w `publish_snapshot`, serializacja **ints/bools only** w `components/web_panel/src/ws_telemetry.c` (`snapshot_to_json`, push ~10 Hz).
- **Host-test harness:** `test/host/CMakeLists.txt` (`PURE_SOURCES` + `PURE_INCLUDE_DIRS`), `test/host/run.sh` (`cmake -GNinja -B build && ninja -C build && ./build/host_tests`), `test/host/test_main.c` rejestruje suity (`run_*_tests`). Wzorce: `test_loop_step.c`, `test_state_machine.c`, `test_ch4_switch.c`, `test_safety_clamp.c`, `test_nmea_parse.c`, `test_quat_to_yaw.c`.

### Wiedza instytucjonalna

- **Wrap-safe recency w jednej domenie zegara** — `docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md`. Świeżość GPS licz jako unsigned modular subtraction (`now - last`) w **jednej** domenie (jak IMU: `now_ms()` = `esp_timer_get_time()/1000`), raw tick → jednostki dopiero przy porównaniu; **udokumentuj kontrakt epoki w nagłówku** i dodaj host-test wokół granicy wrapu.
- **Pure ⊥ HAL** — `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md`. Decyzja spot-lock i geo-matematyka = czyste moduły bez `esp_*.h`/`driver/*.h` (grep-checkable na include w `include/`), HAL (gps_reader) cienki. Logikę „tylko na sprzęcie" (akwizycja fixu, realna reakcja silnika) zaloguj jako jawną lukę pokrycia.
- **Oracle power testu clampu/limitu** — `docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md`. Limit max-gazu (R7), bramkę ±60° (R2) i deadband (R6) testuj wejściem **poza** zakresem (nie tożsamością na granicy). Reguła: „czy test FAILuje, gdy usunę testowaną transformację?".
- **Placeholdery progów (`docs/completed/kayak-motor-firmware-v1/known-issues.md`):** progi czasowe/ramowe to placeholdery do strojenia po pomiarach — ten sam sposób myślenia stosuj do progu świeżości GPS i nastaw spot-lock; veryfikacje hardware/E2E odkładaj do `known-issues`.

### Referencje zewnętrzne

- Pominięte. Codebase ma silne lokalne wzorce dla każdego elementu (świeżość sensora, czysta logika host-testowana, mapowanie+clamp, settings/telemetria). Geo-matematyka to standardowa równoprostokątna (equirectangular) aproksymacja — implementowana lokalnie jako czysty moduł.

## Kluczowe decyzje techniczne

- **Spot-lock = flaga w ARMED + nowe tryby celu w signal_chain.** Nie dodajemy `sm_state`. `sm_step()` dalej zwraca ARMED; nowa logika override aplikuje się w `loop_step` **tylko gdy `state == SM_STATE_ARMED`**. Gdy `sm_step` zwróci FAILSAFE (utrata RC) — override się nie wykonuje, więc spot-lock ustępuje bezwarunkowo. Uzasadnienie: zero zmian w maszynie stanów i jej testach (zob. źródło: Kluczowe decyzje).
- **GPS i heading wchodzą do toru sterowania spot-lock, ale NIGDY do failsafe/RC_valid.** Dziś GPS/IMU są wyłącznie diagnostyczne (czytane w `publish_snapshot` po `loop_step`). Spot-lock wymaga ich w `loop_step` — przenosimy odczyt do `read_inputs` i podajemy do `loop_inputs`. Kontrakt: GPS/IMU mogą tylko sterować spot-lockiem; utrata GPS/heading → **pauza**, nie failsafe; `rc_valid`/`channel_valid`/`sm_inputs` pozostają nietknięte.
- **Czysta `spot_lock_step()` (Pure ⊥ HAL).** Sygnatura koncepcyjna: `(inputs: cel lat/lon, bieżąca lat/lon, gps_fresh, heading_deg10, imu_ok, ch3_on, sticks_neutral, params) + state(in/out) → outputs(throttle_cmd, servo_cmd, sub-state, błąd[m], bearing[°])`. Float dozwolony wewnątrz (ESP32-S3 ma FPU); telemetria zostaje na intach. Wewnętrzny sub-stan: `OFF / ACTIVE / PAUSED` (osobny od `sm_state`).
- **Reużycie mappera komendy → us + ramp + clamp.** Spot-lock produkuje komendy znormalizowane (servo: signed wokół centrum; throttle: tylko przód, 0..cap). Integracja przez nowe tryby `THROTTLE_TARGET_SPOT_LOCK` / `SERVO_TARGET_SPOT_LOCK` niosące komendę computed; chain robi ramp/slew → `map_normalized_to_us` → **hard clamp**. Limit max-gazu (R7) liczony w `spot_lock_step` (host-testowalny), a forward power-limit chainu zostaje jako backstop.
- **Jedna domena neutralności drążka dla wejścia (R3) i abortu (R4).** Wspólny predykat `stick_within_neutral(raw_us, deadband_us, params)` oparty na `normalize_us` + próg deadbandu — bez osobnego progu override. Reużyty przez gate wejścia i detekcję override.
- **Świeżość GPS jak IMU + czysty predykat wrap-safe.** Dorabiamy `s_last_fix_ms`/kontrolę staleness w `gps_reader.c` (`GPS_STALE_AFTER_MS ~1500`) i flagę świeżości w `gps_state`. Decyzję „świeże?" wydzielamy do czystego `sensor_is_fresh(now_ms, last_ms, threshold_ms)` (wrap-safe) z host-testem granicy wrapu.
- **Wspólny moduł switch-debounce.** Generalizujemy istniejący `ch4_switch` do `switch_debounce` (zmiana nazwy modułu i symboli, zachowane wszystkie testy) używany przez CH4 i CH3. Reużycie zamiast duplikacji (coding-rules §3).
- **Telemetria na intach.** `spot_lock_state` (0/1/2), `spot_lock_err_m` (uint16, metry), `spot_lock_bearing_deg10` (uint16, °×10, spójne z `imu_heading_deg10`). Bieżący dziób już dostępny jako `imu_heading_deg10`.

## Otwarte pytania

### Rozwiązane podczas planowania

- **Czy spot-lock to nowy `sm_state`?** Nie — flaga w ARMED + tryby celu w signal_chain (zachowanie maszyny stanów i testów nietknięte).
- **Jak wstrzyknąć computed throttle/servo zachowując SI-3?** Przez nowe tryby celu w chainie, które przepuszczają komendę przez ramp/slew → `map_normalized_to_us` → hard clamp. Żaden tor spot-lock nie omija clampu.
- **Domena czasu świeżości GPS?** `now_ms()` (`esp_timer_get_time()/1000`, uint32) — ta sama co IMU; modular subtraction; kontrakt epoki w nagłówku; host-test wrapu.
- **Predykat neutralności drążka dla R3/R4?** Wspólny `stick_within_neutral` oparty na deadbandzie kanału (`throttle_deadband_us`, `steer_deadband_us`), bez osobnego progu. Uwaga: `steer_deadband_us` default = 0 → przy zerowym deadbandzie steru wejście/abort są ścisłe; ewentualne ustawienie niezerowego deadbandu steru to kwestia strojenia w panelu (nie zmiana logiki).
- **Reprezentacja błędu pozycji i bearingu?** Czysty `geo_math`: equirectangular z lat/lon (e7) → metry (dN, dE), `dist = hypot`, `bearing = atan2(dE, dN)`; float wewnątrz, wynik do telemetrii rzutowany na int.
- **Czy `blob_codec`/NVS wymaga zmian na nowe parametry?** Nie — bump `SETTINGS_SCHEMA_VERSION` migruje brakujące pola do defaults (wzorzec istniejący).

### Odroczone do implementacji

- **Dokładne sygnatury** `spot_lock_step()`, `geo_math`, rozszerzeń `throttle_chain_step`/`servo_chain_step` o tryb spot-lock — ustalić przy dotknięciu kodu, zachowując kontrakt ramp/clamp.
- **Struktura regulatora:** start od czystego P (gain odległości → throttle, gain błędu kierunku → servo) z bramką ±60° i deadbandem; I/D odroczone do strojenia w terenie, jeśli P nie wystarczy.
- **Startowe wartości gainów** (R6/R7/R8) — placeholdery do strojenia na wodzie (deadband ~3 m, max gaz ~35%); zachowanie przy punkcie za rufą (jeden łagodny zawrót) weryfikowane na hardware, zalogowane w `known-issues`.
- **Czy `steer_deadband_us` potrzebuje niezerowego defaultu** dla komfortu spot-lock — decyzja po pierwszych próbach w terenie (czysto konfiguracyjna).
- **Mikro-jitter heading/GPS** powodujący szarpanie servo w pobliżu punktu — filtrowanie/hystereza odroczone do obserwacji runtime.

## Implementation Units

Pogrupowane w 3 fazy. Fazy 1–2 nie zmieniają zachowania aktuatorów (fundamenty + czysta logika); Faza 3 włącza tryb i wystawia go w panelu.

### Faza 1 — Fundamenty wejść (sensory i przełącznik)

- [ ] **Unit 1: Licznik świeżości GPS + czysty predykat wrap-safe**

**Cel:** GPS dostaje świeżość („utrata GPS" = brak świeżego fixu ≤ ~1,5 s), wydzieloną do czystej, host-testowanej decyzji.

**Wymagania:** R5

**Zależności:** Brak

**Pliki:**
- Stwórz: `components/gps/include/sensor_freshness.h`, `components/gps/src/sensor_freshness.c` (czysty `bool sensor_is_fresh(uint32_t now_ms, uint32_t last_ms, uint32_t threshold_ms)`, wrap-safe; kontrakt epoki w nagłówku)
- Modyfikuj: `components/gps/include/nmea_parse.h` (pole świeżości w `gps_state`, np. `bool fresh`) — lub osobne pole w getterze; `components/gps/src/gps_reader.c` (`s_last_fix_ms`, `GPS_STALE_AFTER_MS 1500`, ustaw świeżość na podstawie `sensor_is_fresh` w pętli taska, aktualizuj `s_last_fix_ms` przy udanym parsie z fixem)
- Test (unit): `test/host/test_sensor_freshness.c`; rejestracja w `test/host/CMakeLists.txt` (`PURE_SOURCES` + `TEST_SOURCES`) i `test/host/test_main.c`

**Podejście:**
- Replikuj wzorzec IMU (`s_last_report_ms` + kontrola staleness w pętli), ale decyzję świeżości deleguj do czystego `sensor_is_fresh` (Pure ⊥ HAL).
- Aktualizuj `s_last_fix_ms` tylko gdy parse dał użyteczny fix (`fix == true`), żeby „zamarły" sygnał bez fixu liczył się jako utrata.
- Domena `now_ms()` jak IMU; modular subtraction; nie mieszać z domeną capture-tick.

**Notatka wykonawcza:** Test-first dla `sensor_is_fresh` — w tym host-test wokół granicy wrapu uint32 (learning: wrap-safe recency).

**Wzorce do naśladowania:** `components/imu/src/bno085.c` (staleness), `components/rc_capture/src/cap_math.c` (wrap-safe elapsed), `test/host/test_cap_math.c`.

**Scenariusze testowe:**
- [Unit] `now-last < threshold` → fresh=true; `> threshold` → false; dokładnie `== threshold` → zdefiniowane i przetestowane.
- [Unit] Granica wrapu: `last` tuż przed `UINT32_MAX`, `now` po przewinięciu → poprawne (test FAILuje przy naiwnym `now-last` ze znakiem / bez modular).
- [Unit] (HAL-adjacent, jeśli wydzielone) parse bez fixu nie odświeża `last_ms`.

**Weryfikacja:** Host-tests zielone (w tym wrap). `gps_get_state()` zwraca świeżość zgodną z progiem; pole widoczne dalej w snapshocie (Unit 7). Grep: brak `esp_*`/`driver/*` w `components/gps/include/sensor_freshness.h`.

- [ ] **Unit 2: Wspólny moduł switch-debounce + odczyt CH3**

**Cel:** Generalizacja `ch4_switch` do współdzielonego `switch_debounce` i podłączenie debounce'owanego stanu CH3 (ON/OFF + zbocze) do pętli.

**Wymagania:** R1, R4

**Zależności:** Brak

**Pliki:**
- Modyfikuj/zmień nazwę: `components/rc_validity/src/ch4_switch.c` → `switch_debounce.c`, `include/ch4_switch.h` → `switch_debounce.h` (symbole `ch4_switch_*` → `switch_debounce_*`, zachowana semantyka: threshold+sanity+debounce, baseline bez eventu, hold przy braku pulsu)
- Modyfikuj: wszyscy konsumenci CH4 (`components/control_loop/src/control_loop.c::apply_ch4_switch`, `make_ch4_switch_cfg`); `components/rc_validity/CMakeLists.txt`; `test/host/CMakeLists.txt` (`ch4_switch.c`→`switch_debounce.c`)
- Modyfikuj: `components/control_loop/src/control_loop.c` — odczyt `RC_CAP_CH3`, instancja `switch_debounce_state s_ch3_switch` + cfg, wynik do `loop_inputs` (`spot_lock_switch_on` + zbocze)
- Test (unit): zmień nazwę `test/host/test_ch4_switch.c` → `test_switch_debounce.c` (zachowane asercje), aktualizuj `test_main.c`

**Podejście:**
- Moduł jest już generyczny — to głównie rename + dodanie drugiej instancji dla CH3. CH3 NIE wchodzi do `rc_valid` (jak CH4, R12).
- CH3 cfg: `threshold_us` (próg ON), sanity = pasmo RC; debounce analogiczny do CH4.
- Do `loop_inputs` podaj zarówno bieżący poziom (`spot_lock_switch_on`) jak i fakt zbocza (do detekcji aktywacji vs trzymania).

**Notatka wykonawcza:** Refaktor istniejącego, w pełni przetestowanego modułu — przenieś WSZYSTKIE istniejące przypadki testowe bez osłabiania asercji (coding-rules §2).

**Wzorce do naśladowania:** `components/rc_validity/src/ch4_switch.c`, `apply_ch4_switch` w `control_loop.c`.

**Scenariusze testowe:**
- [Unit] Wszystkie istniejące scenariusze CH4 przechodzą pod nową nazwą (baseline bez eventu, debounce N ramek, hold przy out-of-band, edge-only).
- [Unit] CH3: przejście low→high po debounce zwraca event TO_HIGH; high→low zwraca TO_LOW; trzymanie nie powtarza eventu.

**Weryfikacja:** Pełny host-suite zielony (zero regresji CH4). `idf.py build` zielony. CH3 ON/OFF widoczny w snapshocie (`ch3_us` już istnieje) i jako wejście do `loop_step`.

### Faza 2 — Czysta logika spot-lock (host-testowana, jeszcze nie steruje)

- [ ] **Unit 3: Czysty moduł geo_math (odległość + bearing)**

**Cel:** Konwersja lat/lon (e7) na błąd pozycji w metrach i kierunek (bearing) do celu.

**Wymagania:** R1, R2

**Zależności:** Brak

**Pliki:**
- Stwórz: `components/control_loop/include/geo_math.h`, `components/control_loop/src/geo_math.c` (czyste: `geo_offset_m(lat_e7, lon_e7, ref_lat_e7, ref_lon_e7) → (dN_m, dE_m)`; `geo_distance_m`; `geo_bearing_deg10`)
- Test (unit): `test/host/test_geo_math.c`; rejestracja w CMake + `test_main.c`

**Podejście:**
- Equirectangular (płaska aproksymacja, poprawna na dystansach rzędu metrów): `dN = (lat-ref_lat)*k_lat`, `dE = (lon-ref_lon)*k_lon*cos(ref_lat)`; `dist = hypot(dN,dE)`; `bearing = atan2(dE, dN)` → [0,360)×10.
- Float wewnątrz (FPU). Stałe metry/stopień jako named constants (coding-rules §6 magic numbers).
- Tylko `math.h` — bez `esp_*`/`driver/*`.

**Notatka wykonawcza:** Test-first; użyj oracle'i o znanej geometrii (czysty N/E/S/W, znana odległość).

**Wzorce do naśladowania:** `components/imu/src/quat_to_yaw.c` (czysta matematyka + wrap kąta), `test/host/test_quat_to_yaw.c`.

**Scenariusze testowe:**
- [Unit] Punkt na północ od ref → bearing ≈ 0; wschód ≈ 90; południe ≈ 180; zachód ≈ 270 (z tolerancją).
- [Unit] Znany dystans (np. 0,001° lat ≈ 111 m) w granicach tolerancji.
- [Unit] Zerowy offset → dystans 0; bearing zdefiniowany (np. 0) i przetestowany.
- [Unit] Skalowanie cos(lat) na wyższej szerokości zmniejsza dE (test FAILuje bez korekcji cos).

**Weryfikacja:** Host-tests zielone; grep braku `esp_*`/`driver/*` w `geo_math.h`.

- [ ] **Unit 4: Czysty regulator spot_lock_step()**

**Cel:** Pełna decyzja spot-lock: sub-stan (OFF/ACTIVE/PAUSED), wejście/abort, deadband pozycji, bramka ±60°, P-control, limit max-gazu — jako czysta funkcja.

**Wymagania:** R2, R3, R4, R5, R6, R7

**Zależności:** Unit 3 (geo_math)

**Pliki:**
- Stwórz: `components/control_loop/include/spot_lock.h`, `components/control_loop/src/spot_lock.c` (struktury `spot_lock_inputs`, `spot_lock_state`, `spot_lock_outputs`; `spot_lock_outputs spot_lock_step(const spot_lock_inputs*, const settings_params*, spot_lock_state*)`)
- Modyfikuj: `test/host/CMakeLists.txt` (dodaj `spot_lock.c`, `geo_math.c` już dodane)
- Test (unit): `test/host/test_spot_lock.c`; rejestracja w `test_main.c`

**Podejście:**
- Sub-stan wewnętrzny (osobny od `sm_state`):
  - **OFF → ACTIVE:** tylko gdy `ch3_on` (świeże zbocze włączenia), `armed`, `gps_fresh`, `sticks_neutral` (R3). W momencie wejścia: snapshot celu (`ref_lat/lon` = bieżąca pozycja).
  - **ACTIVE:** licz `dist`/`bearing` (geo_math). Jeśli `dist ≤ deadband` (R6) → throttle=neutral, servo=center (luz, nie trzymamy kursu). Inaczej: servo ∝ błąd kierunku (bearing−heading, wrap do [−180,180]); throttle podawany **tylko** gdy |błąd kierunku| ≤ 60° (R2), ∝ `dist`, **clamp do max_throttle_pct** (R7).
  - **ACTIVE → PAUSED:** `!gps_fresh` lub `!imu_ok` (R5) → throttle=neutral, servo=center, pozostań aktywny. **PAUSED → ACTIVE:** dane wróciły (ten sam cel).
  - **dowolny → OFF:** `!ch3_on` (CH3 OFF) lub `!sticks_neutral` (override, R4) lub `!armed`.
- Wyjścia: `throttle_cmd` (znormalizowany, tylko przód, 0..cap), `servo_cmd` (znormalizowany signed), `sub_state`, `err_m`, `bearing_deg10` (do telemetrii).
- Punkt za rufą (|błąd|>60°): brak gazu → łódka pełznie obracając się minimalnym servo → naturalny jeden łagodny zawrót (efekt bramki, nie osobna logika).

**Notatka wykonawcza:** Test-first. Limit gazu (R7), bramkę ±60° (R2) i deadband (R6) testuj wejściem **poza** zakresem (oracle power; learning hard-clamp).

**Wzorce do naśladowania:** `components/control_loop/src/loop_step.c` (kompozycja czystych kroków), `components/state_machine/src/state_machine.c` (przejścia jako czyste funkcje), `test/host/test_loop_step.c`.

**Scenariusze testowe:**
- [Unit] Wejście: ARMED+fresh+neutral+CH3 zbocze ON → ACTIVE, cel = bieżąca pozycja.
- [Unit] Blokada wejścia: CH3 ON ale DISARMED → OFF; brak fixu → OFF; drążek wychylony → OFF (każdy warunek osobno; test FAILuje, gdy usunę dany warunek).
- [Unit] Deadband: `dist` w strefie → throttle neutral, servo center; `dist` poza strefą → throttle>neutral (wejście poza zakresem, nie tożsamość granicy).
- [Unit] Bramka ±60°: błąd kierunku 80° → throttle=neutral, servo skręca; błąd 10° → throttle>neutral.
- [Unit] Cap gazu: bardzo duży `dist` → throttle == max_throttle_pct (nie wyżej).
- [Unit] Pauza: ACTIVE, `gps_fresh=false` → PAUSED + throttle neutral + servo center; powrót fresh → ACTIVE z tym samym celem. To samo dla `imu_ok=false`.
- [Unit] Abort: ACTIVE + CH3 OFF → OFF; ACTIVE + drążek poza deadbandem → OFF (override bez osobnego progu).

**Weryfikacja:** Host-tests zielone; grep braku `esp_*`/`driver/*` w `spot_lock.h`. Czysta funkcja deterministyczna (te same wejścia → te same wyjścia).

### Faza 3 — Integracja, parametry, telemetria

- [ ] **Unit 5: Parametry spot-lock w settings (SI-6)**

**Cel:** Deadband, max gaz i gainy regulatora jako konfigurowalne parametry z regułą SI-6.

**Wymagania:** R6, R7, R8

**Zależności:** Brak (równolegle do Unit 3/4; potrzebne przez Unit 6)

**Pliki:**
- Modyfikuj: `components/settings/include/settings_model.h` (pola: `spot_lock_deadband_m` lub `_dm` dla pod-metrowej precyzji, `spot_lock_max_throttle_pct`, `spot_lock_gain_*`; **bump `SETTINGS_SCHEMA_VERSION`**), `components/settings/src/settings_ranges.h` (MIN/MAX/DEFAULT: deadband ~3 m, max gaz ~35%), `components/settings/src/settings_defaults.c` (przypisz defaults), `components/settings/src/settings_validate.c` (walidacja zakresów), `components/web_panel/src/params_json.c` (dopisz do `U16_FIELDS`)
- Test (unit): rozszerz `test/host/test_settings_validate.c` (zakresy nowych pól); `test/host/test_blob_codec.c` (round-trip nowej wersji schematu, jeśli dotyczy)

**Podejście:**
- Reużyj w pełni istniejący tor pending-apply (`params_decide` → `maybe_apply_pending`, TOCTOU, single writer) — **bez nowej logiki apply** (SI-6 już zapewnione).
- Jednostki całkowite (np. metry lub decymetry, %×1) — bez floatów w settings/telemetrii.
- Defaults łagodne (de-risking): duża martwa strefa, niski gaz na start.

**Notatka wykonawcza:** Bez modyfikacji istniejących testów settings poza dodaniem nowych przypadków; przy bumpie schematu zweryfikuj round-trip/migrację do defaults.

**Wzorce do naśladowania:** istniejące pola `max_throttle_fwd_pct`, `throttle_deadband_us` (model→ranges→defaults→validate→params_json), `test/host/test_settings_validate.c`.

**Scenariusze testowe:**
- [Unit] Wartość poza zakresem (np. max gaz 200%) odrzucona/clampowana zgodnie z konwencją modułu; wartość w zakresie akceptowana.
- [Unit] Defaults ładują się przy świeżej/skorrumpowanej NVS (UNCALIBRATED) z sensownymi wartościami spot-lock.
- [Unit] POST nowych pól w ARMED → 409 (reguła SI-6 niezmieniona).

**Weryfikacja:** Host-tests zielone; nowe pola serializują się w `/params` (snapshot JSON); `idf.py build` zielony.

- [ ] **Unit 6: Integracja spot_lock w loop_step (sterowanie)**

**Cel:** Podłączenie GPS/IMU/CH3 do `loop_step`, aplikacja override spot-lock w obrębie ARMED przez tryby celu signal_chain, z zachowaniem ramp/clamp i ustępowaniem failsafe.

**Wymagania:** R1, R2, R3, R4, R5, R7

**Zależności:** Unit 1, 2, 4, 5

**Pliki:**
- Modyfikuj: `components/control_loop/include/loop_step.h` (rozszerz `loop_inputs` o `gps` lat/lon+fresh, `imu` heading+ok, `spot_lock_switch_on`+zbocze; `loop_state` o `spot_lock_state`), `components/control_loop/src/loop_step.c` (po `sm_step`: jeśli `state==ARMED` → `spot_lock_step`; gdy ACTIVE/PAUSED nadpisz tryby celu na `*_SPOT_LOCK` z computed command; predykat `stick_within_neutral`)
- Modyfikuj: `components/signal_chain/include/signal_chain.h` + `src/throttle_chain.c` + `src/servo_chain.c` (tryby `THROTTLE_TARGET_SPOT_LOCK`/`SERVO_TARGET_SPOT_LOCK` niosące computed command przez ramp/slew → `map_normalized_to_us` → hard clamp)
- Modyfikuj: `components/control_loop/src/control_loop.c::read_inputs` (odczyt `gps_get_state`, `imu_get_state`, `RC_CAP_CH3` → `loop_inputs`; współdziel z `publish_snapshot`, by nie czytać dwa razy)
- Test (unit): rozszerz `test/host/test_loop_step.c`; rozszerz `test/host/test_throttle_chain.c` i `test/host/test_servo_chain.c` o tryb spot-lock

**Podejście:**
- Override aplikowany **wyłącznie** gdy `sm.state == SM_STATE_ARMED`. FAILSAFE/DISARMED → ścieżka bez zmian (spot-lock yields). To utrzymuje kontrakt failsafe bez dotykania maszyny stanów.
- `stick_within_neutral` współdzielony przez gate wejścia (R3) i override (R4) — ta sama domena deadbandu, bez osobnego progu.
- Computed throttle spot-lock dalej przez ESC ramp (łagodny rozruch). Forward power-limit chainu = backstop dla R7.
- GPS/IMU czytane raz w `read_inputs`; `publish_snapshot` reużywa wartości (spójny obraz w tym cyklu).

**Notatka wykonawcza:** Najpierw failing test integracyjny w `test_loop_step.c` dla pełnej ścieżki wejścia→hold→abort, potem implementacja.

**Wzorce do naśladowania:** `components/control_loop/src/loop_step.c` (`resolve_esc`, kolejność kroków), `components/state_machine/src/state_machine.c::servo_target_for` (mapowanie trybów celu).

**Scenariusze testowe:**
- [Unit] ARMED+fresh+neutral+CH3 ON → loop_step daje servo/ESC computed (≠ tor stickowy) i flaga ACTIVE w `loop_state`.
- [Unit] W trakcie ACTIVE: utrata RC → `sm_step`=FAILSAFE → ESC neutral + servo center (override się NIE wykonuje; failsafe wygrywa). Test FAILuje, gdyby override działał poza ARMED.
- [Unit] CH3 OFF podczas ACTIVE → natychmiast tor manualny (≤ 1 cykl).
- [Unit] Wychylenie gazu/steru poza deadband podczas ACTIVE → natychmiast manual (override).
- [Unit] `!gps_fresh`/`!imu_ok` podczas ACTIVE → ESC neutral + servo center, flaga PAUSED (nie OFF, nie failsafe).
- [Unit] Każde wyjście spot-lock przechodzi przez hard clamp (SI-3) — out-of-window computed command jest clampowany.

**Weryfikacja:** Host-tests zielone; `idf.py build` zielony. Zero regresji istniejących testów `loop_step`/state_machine/chain. Grep: brak nowych include `esp_*`/`driver/*` w czystych nagłówkach.

- [ ] **Unit 7: Telemetria spot-lock + panel**

**Cel:** Stan spot-lock, błąd pozycji, kierunek do punktu vs dziób widoczne w panelu (diagnostyka/strojenie).

**Wymagania:** R9

**Zależności:** Unit 6 (źródło wartości w `loop_outputs`/`loop_state`)

**Pliki:**
- Modyfikuj: `components/control_loop/include/control_loop.h` (`control_loop_snapshot`: `uint8_t spot_lock_state`, `uint16_t spot_lock_err_m`, `uint16_t spot_lock_bearing_deg10`), `components/control_loop/src/control_loop.c::publish_snapshot` (populacja z `loop_outputs`/telemetry), `components/web_panel/src/ws_telemetry.c::snapshot_to_json` (nowe pola, **ints/bools only**)
- Modyfikuj: front-end panelu (HTML/JS web_panel) — wyświetlenie stanu/błędu/bearingu vs `imu_heading_deg10`
- Test (unit): jeśli istnieje host-test serializacji telemetrii — rozszerz; w przeciwnym razie zweryfikuj przez kontrakt JSON (pola obecne)

**Podejście:**
- Bieżący dziób już dostępny (`imu_heading_deg10`); dodaj tylko bearing-do-punktu i błąd.
- Serializacja hand-rolled `snprintf` — dopisz pola spójnie ze stylem (bez floatów).
- Panel: czytelny blok „Spot-lock: off/active/paused, błąd X m, kierunek Y° / dziób Z°".

**Wzorce do naśladowania:** istniejące pola `gps_*`/`imu_*` w `snapshot_to_json` i ich populacja w `publish_snapshot`.

**Scenariusze testowe:**
- [Unit] (jeśli host-testowalne) Snapshot z ACTIVE serializuje `spot_lock_state=1`, `err_m`, `bearing_deg10` jako int.
- [E2E] Scenariusz (po /agent-browser lub ręcznie na wodzie, zalogowany w known-issues jako gap hardware): panel pokazuje przejście off→active po CH3 ON, błąd maleje przy dopływaniu, paused przy utracie GPS.

**Weryfikacja:** `idf.py build` zielony; panel renderuje nowe pola; JSON zawiera `spot_lock_*`. Hardware/E2E odłożone do `known-issues` zgodnie z konwencją.

## Wpływ systemowy

- **Graf interakcji:** Nowe wejścia sterujące (GPS lat/lon+fresh, IMU heading+ok, CH3) wpływają do `loop_step` przez `read_inputs`. Override aplikuje się tylko w gałęzi `state==ARMED`. `sm_step` i `rc_validity` nietknięte.
- **Propagacja błędów:** Utrata GPS/heading → pauza (neutral) wewnątrz spot-lock; utrata RC → FAILSAFE (istniejący tor) wygrywa nad spot-lock; każde wyjście kończy hard clamp SI-3.
- **Ryzyka cyklu życia stanu:** Sub-stan spot-lock żyje w `loop_state` (single writer = pętla). Cel `ref_lat/lon` snapshotowany przy wejściu; PAUSED zachowuje cel; OFF czyści flagę. Settings pending-apply tylko w DISARMED (SI-6) — zmiana parametrów spot-lock nie zaburza aktywnego trybu (bo ARMED blokuje apply).
- **Parytet surface API:** `/params` (nowe pola), telemetria WS (nowe pola). Kontrakt `{data, error}` i 409-w-ARMED bez zmian.
- **Pokrycie integracyjne:** Pełna ścieżka wejście→hold→pauza→abort pokryta w `test_loop_step.c`; zachowanie na wodzie (akwizycja fixu, realna reakcja silnika, „jeden łagodny zawrót") = jawna luka hardware w `known-issues`.

## Ryzyka i zależności

- **Crossing GPS/IMU → tor sterowania.** Ryzyko naruszenia kontraktu „diagnostyka poza failsafe". Mitygacja: GPS/IMU wpływają wyłącznie na spot-lock w gałęzi ARMED; `rc_valid`/`sm_inputs` formalnie nietknięte; testy potwierdzają, że failsafe wygrywa.
- **Rename `ch4_switch`→`switch_debounce`.** Ryzyko regresji przetestowanego modułu. Mitygacja: czysty rename + przeniesienie wszystkich przypadków testowych bez osłabiania asercji; pełny host-suite jako bramka.
- **Bump `SETTINGS_SCHEMA_VERSION`.** Ryzyko migracji NVS. Mitygacja: wzorzec wersjonowany (`blob_codec`) → brakujące pola = defaults; round-trip test.
- **`steer_deadband_us` default 0** → ścisły gate/abort steru. Mitygacja: świadoma decyzja (bez osobnego progu); ewentualny niezerowy deadband steru jako strojenie w panelu.
- **Nastawy regulatora i zachowanie za rufą** = nieweryfikowalne na hoście. Mitygacja: łagodne defaults (de-risking), strojenie w terenie, log w `known-issues`.

## Dokumentacja / Notatki operacyjne

- **Rollout/de-risking (zob. źródło: Rekomendacja realizacji):** start na łagodnych nastawach (duża martwa strefa, niski max gaz), najpierw weryfikacja braku donutów i zachowania przy punkcie za rufą, potem zacieśnianie nastaw w panelu.
- **`docs/completed/kayak-motor-firmware-v1/known-issues.md`:** dopisz luki hardware/E2E: akwizycja/utrata fixu na wodzie, realna reakcja silnika, „jeden łagodny zawrót", strojenie deadband/max-gaz/gainów, ewentualny niezerowy `steer_deadband_us`.
- **README/pinout:** zaznacz CH3=GPIO8 jako aktywny przełącznik spot-lock (dziś opisany jako „future/diagnostic"); ujednolić mylący komentarz w `rc_sample.h` (CH3 = GPIO8, nie GPIO27).
- Po wdrożeniu rozważ `/dev-compound` dla nowych wzorców (świeżość GPS, integracja sensor→control bez naruszenia failsafe).

## Źródła i referencje

- **Dokument źródłowy:** [docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md](../dev-brainstorms/2026-06-29-spot-lock-requirements.md)
- Wiedza instytucjonalna: `docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md`, `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md`, `docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md`
- Powiązany kod: `components/control_loop/src/loop_step.c`, `components/state_machine/src/state_machine.c`, `components/signal_chain/src/{throttle_chain,servo_chain,chain_math}.c`, `components/imu/src/bno085.c`, `components/gps/src/{gps_reader,nmea_parse}.c`, `components/rc_validity/src/ch4_switch.c`, `components/settings/*`, `components/web_panel/src/{params_json,ws_telemetry,params_decide}.c`
- Reguły projektu: `.claude/rules/coding-rules.md`, `.claude/rules/learned-patterns.md`
