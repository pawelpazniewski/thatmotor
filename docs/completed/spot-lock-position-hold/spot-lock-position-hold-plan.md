# Plan: Spot-lock — automatyczne utrzymywanie pozycji GPS (CH3)

**Branch:** `feature/spot-lock-position-hold`
**Ostatnia aktualizacja:** 2026-06-29

## Źródła
- Requirements doc: `docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md`
- Plan techniczny: `docs/plans/2026-06-29-001-feat-spot-lock-position-hold-plan.md`

---

## Podsumowanie wykonawcze

Dodajemy autonomiczny tryb **spot-lock**: po przełączeniu CH3 (GPIO8) w ON system
zapamiętuje bieżącą pozycję GPS i utrzymuje kajak w jej okolicy, sterując servo
kierunku i ESC. Tryb jest **sub-trybem w obrębie ARMED** (flaga, nie nowy `sm_state`),
działa **tylko przodem** w schemacie „celuj dziobem w punkt → jedź przodem", **nie
trzyma kursu** dzioba i bezwarunkowo ustępuje istniejącemu failsafe RC. Cała logika
decyzyjna jest czystą funkcją host-testowaną (Pure ⊥ HAL), bez ingerencji w maszynę
stanów ani w tor RC_valid.

**Wartość biznesowa:** operator może zatrzymać się w jednym miejscu (wędkowanie,
zdjęcia, odpoczynek) bez kotwicy i bez ciągłego korygowania drążkami.

## Analiza obecnego stanu

- GPS NEO-M9N i kompas BNO085 działają jako **dane diagnostyczne** (czytane w
  `publish_snapshot` po `loop_step`, poza failsafe). `gps_state` **nie ma** licznika
  świeżości; IMU ma (`IMU_STALE_AFTER_MS`, `imu_ok`).
- CH3 przechwytywany na **GPIO8** (osobna grupa MCPWM), dziś tylko diagnostyczny
  (`ch3_us` w snapshocie), poza torem RC_valid.
- Pętla 50 Hz: `control_loop.c` (HAL) → czysta `loop_step()`; maszyna stanów
  `sm_step()` (DISARMED/ARMED/FAILSAFE/ESC_CAL/DEPLOY); signal_chain
  (`throttle_chain_step`/`servo_chain_step`) z mapperem `map_normalized_to_us` i
  hard clampem SI-3 (`clamp_pwm_us`).
- Debounce przełącznika istnieje jako generyczny `ch4_switch` (cfg+state+event).
- Settings end-to-end z regułą SI-6 (apply pending tylko w DISARMED, TOCTOU, single
  writer); telemetria WS serializowana **ints/bools only**.
- Host-test harness (`test/host/`, Unity, `run.sh`) — czyste moduły bez `esp_*`.

## Proponowany stan docelowy

- GPS z licznikiem świeżości (~1,5 s) i czystym, wrap-safe predykatem.
- Wspólny moduł `switch_debounce` (z `ch4_switch`) obsługujący CH4 i CH3.
- Czyste moduły `geo_math` (odległość+bearing) i `spot_lock` (regulator + sub-stan
  OFF/ACTIVE/PAUSED) — w pełni host-testowane.
- Nowe parametry settings (deadband, max gaz, gainy) z SI-6.
- Integracja w `loop_step`: GPS/IMU/CH3 jako wejścia, override spot-lock tylko w
  ARMED przez tryby celu signal_chain (ramp/clamp zachowane), ustępowanie failsafe.
- Telemetria spot-lock (stan, błąd[m], bearing[°] vs dziób) + panel.

## Fazy wdrożenia

### Faza 1 — Fundamenty wejść (sensory i przełącznik)

**Unit 1: Licznik świeżości GPS + czysty predykat wrap-safe** (R5) — nakład: **M**
- Cel: GPS dostaje świeżość (utrata = brak świeżego fixu ≤ ~1,5 s) wydzieloną do
  czystej, host-testowanej decyzji.
- Zależności: brak.
- Kryteria akceptacji: `sensor_is_fresh` poprawny w tym wrap uint32; `gps_get_state`
  zwraca świeżość zgodną z progiem; brak `esp_*`/`driver/*` w czystym nagłówku.

**Unit 2: Wspólny moduł switch-debounce + odczyt CH3** (R1, R4) — nakład: **M**
- Cel: generalizacja `ch4_switch`→`switch_debounce`; debounce'owany CH3 (ON/OFF +
  zbocze) jako wejście do pętli.
- Zależności: brak.
- Kryteria akceptacji: zero regresji testów CH4 pod nową nazwą; CH3 ON/OFF dostępny
  w `loop_step`; `idf.py build` zielony.

### Faza 2 — Czysta logika spot-lock (host-testowana, jeszcze nie steruje)

**Unit 3: Czysty moduł geo_math (odległość + bearing)** (R1, R2) — nakład: **M**
- Cel: lat/lon (e7) → błąd pozycji [m] i bearing do celu.
- Zależności: brak.
- Kryteria akceptacji: bearing N/E/S/W i znany dystans w tolerancji; korekcja cos(lat);
  brak `esp_*`/`driver/*` w nagłówku.

**Unit 4: Czysty regulator spot_lock_step()** (R2, R3, R4, R5, R6, R7) — nakład: **L**
- Cel: pełna decyzja spot-lock: sub-stan OFF/ACTIVE/PAUSED, wejście/abort, deadband,
  bramka ±60°, P-control, cap gazu — czysta funkcja.
- Zależności: Unit 3.
- Kryteria akceptacji: wszystkie scenariusze przejść/granic zielone (oracle power dla
  capu, bramki, deadbandu); funkcja deterministyczna; bez `esp_*`/`driver/*`.

### Faza 3 — Integracja, parametry, telemetria

**Unit 5: Parametry spot-lock w settings (SI-6)** (R6, R7, R8) — nakład: **M**
- Cel: deadband, max gaz, gainy jako konfigurowalne parametry z SI-6.
- Zależności: brak (potrzebne przez Unit 6).
- Kryteria akceptacji: walidacja zakresów; defaults łagodne; bump schematu z migracją
  do defaults; pola w `/params`; POST w ARMED → 409 (SI-6 niezmienione).

**Unit 6: Integracja spot_lock w loop_step (sterowanie)** (R1–R5, R7) — nakład: **L**
- Cel: GPS/IMU/CH3 do `loop_step`; override spot-lock tylko w ARMED przez tryby celu
  signal_chain (ramp/clamp), ustępowanie failsafe.
- Zależności: Unit 1, 2, 4, 5.
- Kryteria akceptacji: wejście→hold→pauza→abort pokryte w `test_loop_step`; failsafe
  wygrywa nad spot-lock; każde wyjście przez hard clamp; zero regresji; `idf.py build`.

**Unit 7: Telemetria spot-lock + panel** (R9) — nakład: **M**
- Cel: stan/błąd/bearing vs dziób w panelu.
- Zależności: Unit 6.
- Kryteria akceptacji: pola `spot_lock_*` w snapshocie i JSON (ints only); panel
  renderuje; E2E/hardware zalogowane w `known-issues`.

## Ocena ryzyka i mitygacje

- **Crossing GPS/IMU → tor sterowania** narusza kontrakt „diagnostyka poza failsafe".
  Mitygacja: wpływ tylko w gałęzi ARMED; `rc_valid`/`sm_inputs` nietknięte; testy
  potwierdzają prymat failsafe.
- **Rename `ch4_switch`→`switch_debounce`** — regresja przetestowanego modułu.
  Mitygacja: czysty rename + przeniesienie wszystkich przypadków bez osłabiania asercji.
- **Bump `SETTINGS_SCHEMA_VERSION`** — migracja NVS. Mitygacja: wzorzec wersjonowany
  (`blob_codec`), round-trip test.
- **`steer_deadband_us` default 0** → ścisły gate/abort steru. Mitygacja: świadoma
  decyzja bez osobnego progu; niezerowy deadband steru = strojenie w panelu.
- **Nastawy regulatora i zachowanie za rufą** nieweryfikowalne na hoście. Mitygacja:
  łagodne defaults, strojenie w terenie, log w `known-issues`.

## Mierniki sukcesu

- Po CH3 ON przy ARMED + świeży fix + drążki na zerze kajak utrzymuje się w okolicy
  punktu (rząd kilku metrów) bez interwencji.
- Punkt za rufą → jeden łagodny zawrót, brak donutów.
- CH3 OFF / ruch drążkiem → manual ≤ jeden cykl pętli.
- Utrata fixu/heading → stop (neutral) bez szarpania; powrót danych wznawia tryb.
- Spot-lock nigdy nie przekracza limitu gazu i nie obchodzi failsafe.
- Logika decyzyjna pokryta host-testami (Unity) bez sprzętu.

## Wymagane zasoby i zależności

- Reużycie: `gps`, `imu`, `rc_capture` (CH3/GPIO8), `rc_validity`/`ch4_switch`,
  `state_machine`, `signal_chain`, `pwm_out`/`safety_clamp`, `control_loop`,
  `settings`, `web_panel` (params + telemetria).
- Toolchain ESP-IDF (build) + host-test harness (cmake+ninja, `test/host/run.sh`).
- Założenia zweryfikowane: silnik nie zakłóca kompasu (heading wiarygodny);
  dokładność/częstotliwość NEO-M9N wystarcza dla deadbandu ~2–3 m.

## Szacunki czasowe (orientacyjne, sekwencja zależności)

- Faza 1 (Unit 1–2): równolegle możliwe; ~M+M.
- Faza 2 (Unit 3–4): Unit 4 zależy od 3; ~M+L.
- Faza 3 (Unit 5–7): Unit 5 równolegle; Unit 6 po 1/2/4/5; Unit 7 po 6; ~M+L+M.
- Rekomendacja de-risking: najpierw stabilna pętla na łagodnych nastawach, potem
  zacieśnianie w terenie.
