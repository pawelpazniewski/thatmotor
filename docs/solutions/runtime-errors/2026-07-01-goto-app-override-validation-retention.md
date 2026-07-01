---
title: "Autonomiczna nawigacja goto: app-override bez naruszenia failsafe, walidacja celu przed castem, retencja celu w PAUSED"
date: 2026-07-01
category: runtime-errors
severity: high
stack:
  - ESP32
  - ESP-IDF
  - C
tags:
  - failsafe
  - comms-watchdog
  - input-validation
  - undefined-behavior
  - state-machine
  - spot-lock
  - goto
  - pure-core
status: verified
last_verified: 2026-07-01
---

# Autonomiczna nawigacja goto: app-override, walidacja celu, retencja w PAUSED

Feature `goto-waypoint-navigation` (firmware ESP32) dokłada autonomiczną nawigację do
punktu, reużywając istniejący silnik `spot_lock` (position hold). Źródłem celu staje
się link sieciowy z aplikacji iOS (nie odbiornik RC). Wprowadzenie nowego źródła
sterowania i untrusted danych z sieci odsłoniło trzy klasy cichych błędów, które
naprawiono w trakcie implementacji (Fazy 1-4, poprawki po review).

## Symptomy

- **(P1) Nowe źródło sterowania w ścieżce failsafe**: link sieciowy jako źródło celu
  kusi, by podpiąć jego świeżość do wejść maszyny stanów / `rc_valid`. Utrata WiFi
  mogłaby wtedy albo wywrócić failsafe RC, albo — odwrotnie — pozwolić na „ślepy bieg"
  po zniknięciu telefonu.
- **(P2) Untrusted JSON z aplikacji rzutowany na `int32` bez walidacji domenowej**:
  `lat`/`lon` przychodzą jako `double`. Naiwna kolejność „cast → sprawdź zakres na
  intcie" przepuszcza wartości poza `int32` (np. `4.39e9` zawija się do `~1e8`, które
  przechodzi zakres). `INF`/`NaN` rzutowane na `int32` to Undefined Behavior.
- **(P3) Null-island po pauzie linku**: gdy warstwa pętli zeruje `goto_lat/lon` przy
  utracie linku, a rdzeń `spot_lock` bezwarunkowo re-latchuje `ref_*` z wejścia, cel w
  stanie PAUSED zostaje nadpisany zerami → po wznowieniu łódź płynie na wyspę zerową
  (0,0) w Zatoce Gwinejskiej zamiast utrzymać ostatni cel.

## Root Cause

- **P1**: Brak strukturalnego rozdziału „źródło override'u" od „wejść failsafe".
  Świeżość linku i świeżość RC to różne domeny i nie mogą się mieszać.
- **P2**: Walidacja po castcie działa w domenie `int32`, więc widzi już zawiniętą
  wartość; a `(int32_t)INF` jest UB. Wyrocznia zakresu musi działać w domenie `double`
  PRZED castem.
- **P3**: Retencja celu była traktowana jako niejawny kontrakt na stabilny upstream
  („pętla nie wyzeruje goto_*"). Kontrakt niejawny = cichy regres, gdy upstream się
  zmieni.

## Rozwiązanie

### P1 — App-override z osobnym comms-watchdogiem, override liczony WYŁĄCZNIE w ARMED

Link sieciowy dostaje własny predykat świeżości (`comms_fresh`, ta sama semantyka co
`gps_ok`/`imu_ok`, reuse `sensor_is_fresh` w domenie `now_ms`). Wejścia goto wchodzą do
`spot_lock_step` jak GPS/IMU — nigdy do `rc_valid`/`channel_valid`/`sm_inputs`/failsafe.
`comms_fresh` bramkuje wyłącznie `SRC_GOTO`. Utrata linku = PAUSED (nie abort), latch
celu zachowany; wznowienie po powrocie linku. Latch goto kasuje TYLKO manualny stick
override lub fizyczny preempt CH3 — i to obliczane wyłącznie w gałęzi ARMED:

```c
/* loop_step.c — latch goto kasuje tylko override/CH3, tylko w ARMED.
 * Pauza linku/GPS/IMU celowo NIE kasuje latcha (transient loss wznawia). */
static bool goto_latch_should_clear(const loop_inputs *in,
                                    const settings_params *params,
                                    sm_state resolved_state)
{
    if (resolved_state != SM_STATE_ARMED || !in->goto_engage) {
        return false; /* poza ARMED goto nie działa: FAILSAFE nie kasuje latcha tędy */
    }
    return !sticks_within_neutral(in, params) || in->spot_lock_switch_on;
}
```

### P2 — Walidacja w domenie `double` PRZED castem na `int32`

```c
/* goto_target.c */
bool goto_target_from_double(double lat_d, double lon_d, int32_t *lat_e7,
                             int32_t *lon_e7)
{
    if (!isfinite(lat_d) || !isfinite(lon_d)) {
        return false; /* INF/NaN: cast na int32 byłby UB */
    }
    if (lat_d < GOTO_LAT_E7_MIN || lat_d > GOTO_LAT_E7_MAX ||
        lon_d < GOTO_LON_E7_MIN || lon_d > GOTO_LON_E7_MAX) {
        return false; /* poza zakresem w domenie double, PRZED zawinięciem przy castcie */
    }
    *lat_e7 = (int32_t)lat_d; /* gwarantowanie mieści się w int32: bez wrapu */
    *lon_e7 = (int32_t)lon_d;
    return true;
}
```

### P3 — Retencja celu jako własność czystego rdzenia (nie kontrakt na upstream)

```c
/* spot_lock.c — re-latch ref TYLKO na wejściu do SRC_GOTO albo gdy link świeży.
 * Podczas pauzy (comms_fresh == false) rdzeń ZACHOWUJE ostatni dobry cel i NIE
 * nadpisuje ref_* z wejścia → retencja jest własnością rdzenia, nie niejawnym
 * kontraktem na upstream latch (broni przed null-island gdy pętla zeruje goto_*). */
if (in->goto_engage) {
    bool is_entering_goto = st->target_source != SPOT_LOCK_SRC_GOTO;
    if (is_entering_goto || in->comms_fresh) {
        st->ref_lat_e7 = in->goto_lat_e7;
        st->ref_lon_e7 = in->goto_lon_e7;
    }
    st->target_source = SPOT_LOCK_SRC_GOTO;
    return hold_or_pause(in, p, st, true);
}
```

Telemetria goto jest osobnym widokiem: `goto_*` są niezerowe tylko gdy `SRC_GOTO`
posiada cel w danym cyklu (hold z CH3 czyta goto jako off/zero), więc aplikacja nie
pomyli fizycznego holdu z goto.

## Testy (moc wyroczni)

- **P2**: test z wejściem POZA `int32` (np. `4.39e9`) MUSI failować naiwny cast — jeśli
  usuniesz walidację double-domain, test przechodzi tylko przy poprawnej kolejności.
  Dodane w `test/host/test_settings_validate.c` + walidacja transportu.
- **P1**: test pierwszeństwa musi wchodzić w stan, który bez bramki by przeciekł —
  utrata linku podczas ARMED → PAUSED (nie abort), latch zachowany; FAILSAFE nie kasuje
  latcha. `test/host/test_loop_step.c` (+372 linii).
- **P3**: mutacja „bezwarunkowy re-latch" MUSI failować test retencji w PAUSED (cel po
  pauzie == ostatni dobry, nie 0,0). `test/host/test_spot_lock.c`.

## Komendy diagnostyczne

```bash
# host-testy logiki (bez sprzętu)
idf.py --preview build   # lub host test harness zgodnie z toolchain
grep -rn "isfinite\|SRC_GOTO\|comms_fresh\|goto_latch" components/
```

## Zapobieganie

- Nowe źródło sterowania (sieć/BLE/…) = własny predykat świeżości w domenie `now_ms`;
  NIGDY nie podpinaj go do `rc_valid`/`sm_inputs`/failsafe. Degradacja = PAUSE z
  zachowaniem latcha, nie abort.
- Untrusted liczby: waliduj `isfinite()` + zakres w domenie `double` PRZED castem na int.
- Retencja stanu = własność czystego rdzenia, nie niejawny kontrakt na stabilny upstream.

## Powiązane

- docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md
  (pierwszeństwo failsafe dla sensor-override — ten dokument rozszerza wzorzec na
  źródło sieciowe z comms-watchdogiem)
- docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md (moc wyroczni)
- docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md

## Kontekst

Branch `feature/kayak-motor-firmware-v1`, commity `4e0b4c0..458818c`. Feature reużywa
silnik `spot_lock` (position-only, forward-only point-and-shoot). Zakres per
`spot-lock-design` (auto memory [claude]): pivot na position-only. Domyślny
comms-watchdog `GOTO_COMMS_TIMEOUT_MS_DEFAULT = 1500 ms` (zakres 200..5000).
