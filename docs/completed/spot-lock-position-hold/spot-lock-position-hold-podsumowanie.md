# Podsumowanie ukończenia: Spot-lock — automatyczne utrzymywanie pozycji GPS (CH3)

**Zadanie:** spot-lock-position-hold
**Branch:** `feature/spot-lock-position-hold`
**Data ukończenia:** 2026-06-29

## Status końcowy

- **343/343 testów hosta (Unity) PASS** — standalone CMake+Unity (`test/host/run.sh`).
- **`idf.py build` (target esp32s3) PASS** — po każdym Unicie.
- Wszystkie 3 fazy (Unit 1–7) zaimplementowane i zreviewowane (multi-agent review każdej fazy).
- **Review:** Faza 1 ✅ CZYSTE (0× P1/P2); Faza 2 ⚠️ 1× P2 naprawiony w 1 cyklu + ✅ CZYSTE
  re-review; Faza 3 ✅ CZYSTE (0× P1/P2). Pozostają tylko nity P3 (opcjonalne) opisane w
  `review-faza-2.md` / `review-faza-3.md`.
- Czynności `[HW]`/`[E2E]` (akwizycja fixu, realne utrzymanie pozycji, zawrót za rufą, brak
  zakłócenia kompasu, panel/JSON na żywo) **świadomie odłożone** do
  `docs/completed/kayak-motor-firmware-v1/known-issues.md` §4b — luki weryfikacji zależne od
  środowiska, NIE braki implementacji.

## Co zostało dostarczone

Autonomiczny tryb **spot-lock**: po przełączeniu CH3 (GPIO8) w ON system zapamiętuje bieżącą
pozycję GPS i utrzymuje kajak w jej okolicy (servo skrętu + ESC, tylko przodem, schemat „celuj
dziobem w punkt → jedź przodem"). Tryb jest **sub-trybem w obrębie ARMED** (flaga, nie nowy
`sm_state`), nie trzyma kursu dzioba i **bezwarunkowo ustępuje failsafe RC**. Cała logika
decyzyjna jest czystą funkcją host-testowaną (Pure ⊥ HAL).

- **Faza 1 — Fundamenty wejść (Unit 1–2):**
  - Unit 1: czysty wrap-safe predykat świeżości GPS (`sensor_freshness`); `gps_state.fresh`
    z `GPS_STALE_AFTER_MS=1500`, znacznik odświeżany tylko przy parsie z fixem.
  - Unit 2: generalizacja `ch4_switch` → wspólny `switch_debounce`; druga instancja dla CH3
    (debounce, ON/OFF + zbocze) jako wejście pętli, poza torem RC_valid (R12).
- **Faza 2 — Czysta logika (Unit 3–4):**
  - Unit 3: czysty `geo_math` (equirectangular: offset ENU, dystans hypot, bearing atan2 +
    korekcja `cos(lat)`), tylko `math.h`.
  - Unit 4: czysty regulator `spot_lock_step` — sub-stan OFF/ACTIVE/PAUSED, wejście/abort,
    deadband, bramka ±60°, P-control, cap gazu (R7). Komendy znormalizowane, telemetria na intach.
- **Faza 3 — Integracja, parametry, telemetria (Unit 5–7):**
  - Unit 5: 4 parametry settings (deadband_m, max_throttle_pct, throttle_gain, servo_gain),
    bump `SETTINGS_SCHEMA_VERSION` 5→6 + migracja `blob_codec` (reload defaults), SI-6 niezmienione.
  - Unit 6: integracja w `loop_step` — override spot-lock TYLKO w gałęzi `sm.state==ARMED`,
    przez nowe tryby celu signal_chain (`THROTTLE/SERVO_TARGET_SPOT_LOCK`, ten sam tor
    ramp/slew → `map_normalized_to_us` → hard clamp SI-3). GPS/IMU/CH3 jako wejścia, nigdy
    do rc_valid/sm_inputs/failsafe.
  - Unit 7: telemetria `spot_lock_state`/`err_m`/`bearing_deg10` (ints only) w snapshocie +
    JSON; karta „Spot-lock (CH3)" w panelu + etykiety nowych parametrów.

## Podjęte kluczowe decyzje

| Decyzja | Wybór |
|---|---|
| Spot-lock = flaga w ARMED, nie `sm_state` | override w `loop_step` tylko gdy `sm.state==ARMED`; FAILSAFE → override się nie wykonuje → spot-lock ustępuje bezwarunkowo; zero zmian w maszynie stanów |
| GPS/IMU w torze sterowania | wyłącznie jako wejścia spot-lock; NIGDY do failsafe/rc_valid; utrata → **pauza** (neutral+center), nie failsafe |
| Reużycie mappera + ramp + clamp | spot-lock produkuje komendy znormalizowane; integracja przez nowe tryby celu chainu (SI-3 niezagrożone) |
| Jedna domena neutralności drążka | wspólny `sticks_within_neutral` dla wejścia (R3) i abortu (R4); `steer_deadband_us` default 0 |
| Świeżość GPS jak IMU | czysty wrap-safe predykat (modular subtraction, kontrakt epoki w nagłówku); wejście w hold bramkuje **realny fix** (`gps_has_fix`), nie sam `fresh` |
| Wspólny `switch_debounce` | rename generycznego `ch4_switch`; druga instancja dla CH3 bez osłabiania asercji |
| geo_math equirectangular | float wewnątrz (FPU), telemetria na intach; korekcja `cos(ref_lat)` |
| Regulator: czysty P + bramka ±60° | gain dist→throttle, gain błąd→servo; punkt za rufą (|błąd|>60°) → brak gazu, servo skręca → naturalny łagodny zawrót; I/D odroczone |
| Settings: bump schematu | `blob_codec` migruje do defaults (reload), reużycie toru pending-apply SI-6 |
| P2 z review Fazy 2 | gate pauzy = `!gps_fresh \|\| !imu_ok \|\| !gps_has_fix` (re-walidacja fixu w trakcie hold) |

## Główne utworzone/zmodyfikowane pliki

Utworzone:
- `components/gps/{include,src}/sensor_freshness.{h,c}` — wrap-safe predykat świeżości (pure)
- `components/control_loop/{include,src}/geo_math.{h,c}` — offset/dystans/bearing (pure)
- `components/control_loop/{include,src}/spot_lock.{h,c}` — regulator + sub-stan (pure)
- `test/host/test_sensor_freshness.c`, `test_geo_math.c`, `test_spot_lock.c`

Rename:
- `components/rc_validity/.../ch4_switch.{c,h}` → `switch_debounce.{c,h}` (+ `test_switch_debounce.c`)

Modyfikacje:
- `components/gps/include/nmea_parse.h`, `src/gps_reader.c` — pole/odświeżanie świeżości
- `components/control_loop/include/loop_step.h`, `src/loop_step.c` — wejścia GPS/IMU/CH3, override ARMED
- `components/control_loop/include/control_loop.h`, `src/control_loop.c` — `read_inputs`/`apply_sensor_inputs`, snapshot
- `components/signal_chain/include/signal_chain.h`, `src/throttle_chain.c`, `src/servo_chain.c` — tryby SPOT_LOCK
- `components/settings/include/settings_model.h`, `src/settings_{ranges.h,defaults.c,validate.c}` — parametry + bump schematu
- `components/web_panel/src/params_json.c`, `src/ws_telemetry.c` + front-end panelu
- `test/host/test_{loop_step,throttle_chain,servo_chain,settings_validate,blob_codec}.c`

## Wyciągnięte wnioski

1. **Sub-tryb w obrębie ARMED zamiast nowego `sm_state`** — pozwala wprowadzić autonomię bez
   ingerencji w maszynę bezpieczeństwa i jej testy; prymat failsafe wynika z umieszczenia
   override TYLKO w gałęzi ARMED (poza ARMED spot-lock wymuszony OFF). Udowodnione testem
   `test_failsafe_beats_spot_lock` o mocy wyroczni.
2. **Sensory diagnostyczne → tor sterowania bez naruszenia failsafe** — GPS/IMU wchodzą tylko
   jako wejścia spot-lock przez `read_inputs`/`loop_inputs`; `rc_valid`/`sm_inputs` nietknięte.
   Utrata danych = pauza (neutral+center), nie failsafe.
3. **Wrap-safe świeżość jak istniejący wzorzec recency** — modular unsigned subtraction w jednej
   domenie zegara, kontrakt epoki w nagłówku, host-test granicy wrapu uint32. Wejście w hold musi
   bramkować realny fix, nie sam licznik świeżości (seed znacznika daje ~1,5 s `fresh=true` bez fixu).
4. **Re-walidacja warunku jakości w trakcie hold, nie tylko na wejściu** (finding P2 Fazy 2) —
   gate pauzy musi zawierać `!gps_has_fix`, by hold ustąpił przy utracie fixu mimo świeżego znacznika.
5. **Oracle power w testach transformacji ograniczających** — cap/bramka/deadband/korekcja cos(lat)
   testowane wejściem POZA zakresem; każda transformacja FAILuje po jej usunięciu (zweryfikowane empirycznie).
6. **Pułapka składniowa `*/` w komentarzu nagłówka** — sekwencja `esp_*/driver/*` zamykała blok
   `/* */`; przeredagowano na „esp_ or driver". Drobna, ale blokująca kompilację.

## Powiązane dokumenty

- Plan: `spot-lock-position-hold-plan.md`
- Kontekst: `spot-lock-position-hold-kontekst.md`
- Zadania (pełna lista z findingami review): `spot-lock-position-hold-zadania.md`
- Raporty review faz 1–3: `review-faza-{1,2,3}.md`
- Known issues (odroczone `[HW]`/`[E2E]`): `docs/completed/kayak-motor-firmware-v1/known-issues.md` §4b
- Requirements: `docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md`
- Plan techniczny: `docs/plans/2026-06-29-001-feat-spot-lock-position-hold-plan.md`

## Commity feature

`dda0b4c`, `35c716b`, `cb684bd` (WIP S3), `c06795f` (Unit 1), `2983edd` (Unit 2),
`f5bfe4f` (Faza 1), `ccbe986` (Unit 3/4), `f612827` (re-review Fazy 2),
`22b934d` (Unit 5), `c16c924` (Unit 6), `6af50c2` (Unit 7).
