# Code Review — Faza 3 (Integracja, parametry, telemetria)

**Zadanie:** spot-lock-position-hold
**Faza:** 3 (Unit 5 — SI-6 settings, Unit 6 — integracja loop_step, Unit 7 — telemetria + panel)
**Commity:** `22b934d` (Unit5), `c16c924` (Unit6), `6af50c2` (Unit7)
**Data:** 2026-06-29

---

## Severity gate: ✅ CZYSTE (GOTOWE DO KONTYNUACJI)

| Severity | Liczba | Typy |
|----------|--------|------|
| 🔴 P1 (blocking) | 0 | — |
| 🟠 P2 (important) | 0 | — |
| 🟡 P3 (nit) | 3 | KOD x3 |

To jest faza integracji sterująca silnikiem — review przeprowadzone z najwyższą
staranością na osi bezpieczeństwa. Wszystkie inwarianty bezpieczeństwa potwierdzone.

---

## Bramki jakości

- **Host-tests:** 343/343 zielone (0 failures, 0 ignored) — `test/host/run.sh`.
- **Firmware build:** `idf.py build` (esp32s3) zielony; binarka 0xec250 B, 39% partycji wolne.
- **Pure ⊥ HAL:** brak `esp_*`/`driver/*` includes w czystych nagłówkach
  (`spot_lock.h`, `geo_math.h`, `loop_step.h`, `signal_chain.h`, `sensor_freshness.h`) —
  trafienia grepa to wyłącznie tekst w komentarzach dokumentujący kontrakt.
- **Pułapka komentarza `*/`:** brak osieroconych `*/` w nowym czystym kodzie; build potwierdza.

---

## Potwierdzenie bezpieczeństwa

### 1. FAILSAFE PRECEDENCE — ✅ OK
- Override spot-lock liczony jest **po** `sm_step` i wykonywany **wyłącznie** w gałęzi
  `sm.state == SM_STATE_ARMED` (`resolve_spot_lock`, `loop_step.c`). Poza ARMED
  (DISARMED/FAILSAFE/ESC_CAL/DEPLOY) sub-stan jest **wymuszany OFF** i zwracane są
  zerowe wyjścia — jeden mechanizm gwarantujący prymat failsafe.
- Przy utracie RC: `sm_step` → FAILSAFE → `drive_spot_lock=false` →
  `throttle_mode = sm.throttle_target` (neutral) i `servo_mode = sm.servo_target` (center).
- `rc_valid` / `channel_valid` / `sm_inputs` / failsafe są **NIETKNIĘTE** przez GPS/IMU.
  GPS/IMU wchodzą tylko przez `apply_sensor_inputs` → `loop_inputs` jako wejścia regulatora.
- **Oracle test `test_failsafe_beats_spot_lock`** ma realną moc wyroczni: wchodzi w ACTIVE
  z dziobem ustawionym na ciąg (ESC byłby do przodu), po czym utrata RC → FAILSAFE; asercje
  `esc_us == ESC_NEUTRAL_US`, `servo_us == center`, `substate == OFF`. Test **FAILuje**, gdyby
  override działał poza ARMED (komentarz w teście to wprost stwierdza).

### 2. HARD CLAMP (SI-3) — ✅ OK
- Throttle SPOT_LOCK: `spot_lock_cmd` → `resolve_target` → `advance_ramp` →
  `map_to_esc_us` → `clamp_pwm_us(ESC_WINDOW)`. Nie omija toru clampu.
- Servo SPOT_LOCK: `spot_lock_cmd` → `map_normalized_to_us` → `slew_step` → trim →
  `clamp_pwm_us(window)`. Nie omija toru clampu.
- **Oracle test `test_spot_lock_output_passes_hard_clamp`**: ustawia `max_throttle_pct=100`
  i ekstremalny dryf (lat_e7=50000), nasycona komenda zmapowałaby do 3000 us; asercja
  `esc_us == 2000U` (twardy sufit). Silny oracle (FAILuje bez clampu).

### 3. ABORT (≤ 1 cykl) — ✅ OK
- `spot_lock_step` krok 1: `!armed || !ch3_on || !sticks_neutral` → OFF natychmiast.
- `test_spot_lock_ch3_off_returns_to_manual`: po 400 cyklach hold (ESC do przodu) CH3 OFF →
  `substate == OFF` w jednym cyklu.
- `test_spot_lock_stick_aborts_immediately`: baseline ESC do przodu, nudge drążka steru
  (CH1=2000) → OFF w jednym cyklu, manual wraca (ESC→neutral, servo śledzi drążek).
- `sticks_within_neutral` = `throttle_is_neutral && steer_is_neutral` — wspólna domena
  neutralności ze stickiem; `steer_is_neutral` używa tych samych kroków normalize+deadband
  co tor servo (spójny kontrakt "centered").

### 4. PAUZA (sensor loss) — ✅ OK
- `!gps_fresh || !imu_ok || !gps_has_fix` → PAUSED + `make_idle_output` (throttle 0, servo 0).
  Nie OFF, nie failsafe — cel zachowany, aktuatory rozluźnione (neutral+center przez chain).
- Re-walidacja `gps_has_fix` w każdym cyklu (poprawka z review fazy 2) potwierdzona w kodzie.
- `test_spot_lock_pauses_on_gps_loss`: asercje `state==ARMED`, `substate==PAUSED`,
  `esc_us==NEUTRAL`, `servo_us==center`.

### 5. SI-6 (parametry w settings) — ✅ OK
- POST nowych pól w ARMED → 409: brama w `params_decide.c` na **stanie** (`state != DISARMED`
  → `API_ERR_NOT_DISARMED`, http 409), nie na polach. Nowe pola nie omijają bramki.
- Bump schematu v5→v6 z migracją `blob_codec` (`FIELD_BYTES 53→61`, +4×u16);
  round-trip pokryty w `test_blob_codec.c`.
- Walidacja zakresów (`field_or_default`): defaults łagodne (deadband 3 m, max gaz 35%,
  throttle_gain 30, servo_gain 20). Testy out-of-range z mocą wyroczni: 200% → recover 35,
  500 m → recover 3; in-range zachowane; defaults sane przy świeżej/skorrumpowanej NVS.

### 6. Telemetria — ✅ OK
- `ws_telemetry.c::snapshot_to_json` dodaje `spot_lock_state`/`err_m`/`bearing_deg10` jako
  `%u` (ints only, zero floatów). Brak duplikacji logiki regulatora w `loop_step` —
  `loop_telemetry` tylko przenosi wyjścia `spot_lock_step`.

### 7. Synchronizacja full-scale
- `SPOT_LOCK_CMD_FULL_SCALE == SIGNAL_NORMALIZED_FULL_SCALE == 1000` — zgodne (nit P2 z fazy 2
  rozwiązany pod kątem wartości; pozostaje duplikacja stałej, patrz P3 niżej).

---

## Odchylenia od planu

Brak. Wszystkie pliki i scenariusze testowe z Unit 5/6/7 planu zadań zaimplementowane.
`loop_step.c` (299 l.) zmieścił się pod progiem 300; ekstrakcje helperów
(`build_spot_lock_inputs`/`build_spot_lock_params`/`resolve_spot_lock`/`spot_lock_drives`)
trzymają jeden poziom abstrakcji per funkcja.

---

## Findingi P3 (opcjonalne, nieblokujące)

- 🟡 [nit] **components/control_loop/src/control_loop.c** — 471 linii (>300, przerost
  istniejący sprzed fazy 3; urósł ~23 l. przez spójny helper `apply_sensor_inputs`). Nowy
  kod nie pogarsza czytelności. Jeśli plik rośnie dalej — rozważyć ekstrakcję adaptera
  `aux_switch` (CH3/CH4) + `sensor_inputs` do osobnego modułu HAL.
- 🟡 [nit] **components/control_loop/include/spot_lock.h:31** — `SPOT_LOCK_CMD_FULL_SCALE`
  duplikuje wartość `SIGNAL_NORMALIZED_FULL_SCALE` (oba 1000); zgodność trzymana wyłącznie
  komentarzem. Rozważyć `_Static_assert` wiążący obie stałe (chroni przed cichym rozjazdem
  mapowania normalized→us przy przyszłej zmianie skali).
- 🟡 [nit] **components/signal_chain/src/throttle_chain.c** — komenda spot-lock omija
  `apply_power_limit` (globalny `max_throttle_fwd_pct`); regulator używa własnego capu
  `spot_lock_max_throttle_pct` (zakres do 100%). To świadoma decyzja (R7: osobny cap), a
  hard clamp SI-3 i tak ogranicza wyjście. Jeśli `spot_lock_max_throttle_pct >
  max_throttle_fwd_pct`, spot-lock może dać większy ciąg przodem niż tryb manualny — warto
  udokumentować ten kontrakt przy polu w `settings_model.h`.

---

## Wynik

**✅ CZYSTE — GOTOWE DO KONTYNUACJI.** Zero P1, zero P2. Inwarianty bezpieczeństwa
(failsafe-precedence, hard-clamp, abort, pauza, SI-6) potwierdzone kodem i testami o realnej
mocy wyroczni. Host-tests 343/343 zielone, `idf.py build` (esp32s3) zielony. Pozostają 3 nity
P3 (czytelność/dokumentacja), wszystkie opcjonalne.
