---
title: "Sensor-driven override (spot-lock) wprowadzony do loop bez naruszenia kontraktu failsafe"
date: 2026-06-29
category: runtime-errors
severity: high
stack:
  - ESP-IDF
  - C
  - firmware
tags:
  - failsafe
  - control-loop
  - state-machine
  - sensor-fusion
  - gps
  - imu
  - sensor-freshness
  - test-oracle
status: verified
last_verified: 2026-06-29
---

# Sensor-driven override (spot-lock) w loop bez naruszenia failsafe

## Symptomy

- Nowy regulator (spot-lock: hold pozycji na GPS+IMU) musiał sterować ESC/serwem,
  ale NIE mógł osłabić istniejącego failsafe (utrata RC / błąd → neutral).
- Ryzyko cichego regresu: gdyby override zasilał `rc_valid` / `sm_inputs` albo
  liczył się przed `sm_step`, wynik FAILSAFE mógłby zostać nadpisany przez
  komendę sensoryczną — silnik trzymałby ciąg mimo utraty łączności.
- Drugi cichy błąd: spot-lock "uzbrajał się" / trzymał na nieświeżym, ale wciąż
  mieszczącym się w oknie świeżości odczycie (seed-fresh), bo predykat
  `gps_fresh` był prawdziwy, choć realny fix był stracony.

## Root Cause

1. **Kolejność i miejsce override'u decydują o pierwszeństwie failsafe.** Jeśli
   override liczy się przed maszyną stanów albo wpływa na jej wejścia, traci się
   gwarancję, że failsafe zawsze wygrywa. Pierwszeństwo musi być wymuszone
   strukturalnie, nie przez warunki rozsiane po kodzie.
2. **`fresh` ≠ `valid fix`.** Okno świeżości (timestamp ostatniego dobrego
   odczytu) pozostaje "świeże" przez próg czasowy nawet gdy fix właśnie zniknął
   (seed-fresh). Bramkowanie tylko po `fresh` przepuszcza hold na utraconym
   fixie. Jakość fixa trzeba re-walidować KAŻDY cykl, nie tylko przy wejściu.

## Rozwiązanie

Override liczony PO `sm_step` i wykonywany WYŁĄCZNIE w gałęzi `ARMED`; poza
`ARMED` (DISARMED/FAILSAFE/kalibracja/deploy) spot-lock jest forsowany `OFF` i
bezwarunkowo ustępuje — to jedyny mechanizm pierwszeństwa failsafe:

```c
sm_outputs sm = sm_step(state->state, &si);   /* failsafe rozstrzygnięty TU */

/* override PO sm_step; resolve_spot_lock zwraca OFF gdy sm.state != ARMED */
spot_lock_outputs sl = resolve_spot_lock(in, params, sm.state, &state->spot_lock);
bool drive_spot_lock = spot_lock_drives(sl.substate);
throttle_target_mode throttle_mode =
    drive_spot_lock ? THROTTLE_TARGET_SPOT_LOCK : sm.throttle_target;
```

```c
static spot_lock_outputs resolve_spot_lock(const loop_inputs *in,
        const settings_params *params, sm_state resolved_state,
        spot_lock_state *st) {
    if (resolved_state != SM_STATE_ARMED) {     /* failsafe zawsze wygrywa */
        st->substate = SPOT_LOCK_OFF;
        spot_lock_outputs off = {.substate = SPOT_LOCK_OFF};
        return off;
    }
    /* ... spot_lock_step(...) ... */
}
```

Kontrakt failsafe nietknięty: GPS/IMU wchodzą do `loop_inputs` jako wejścia
WYŁĄCZNIE spot-locka; `rc_valid` / `sm_inputs` / wejścia failsafe nie są
zasilane sensorami.

Re-walidacja jakości fixa co cykl (poprawka po review fazy 2):

```c
/* Pauza przy utracie sensora; fix re-walidowany KAŻDY cykl — stale fix może
 * wciąż mieścić się w oknie świeżości (seed-fresh), więc hold byłby niebezpieczny */
if (!in->gps_fresh || !in->imu_ok || !in->gps_has_fix) {
    st->substate = SPOT_LOCK_PAUSED;
    return make_idle_output(SPOT_LOCK_PAUSED);
}
```

Czyste helpery świeżości (host-testable, jeden punkt wrap-safe subtraction):

```c
bool sensor_is_fresh(uint32_t now_ms, uint32_t last_ms, uint32_t threshold_ms) {
    return (uint32_t)(now_ms - last_ms) < threshold_ms;   /* modular, wrap-safe */
}
/* stamp przesuwa się TYLKO przy ważnym odczycie — nieważny nie resetuje timera */
uint32_t sensor_freshness_stamp(uint32_t prev_ms, uint32_t now_ms, bool reading_valid) {
    return reading_valid ? now_ms : prev_ms;
}
```

## Komendy diagnostyczne

```bash
# host-testy (oracle-power: test failsafe-precedence wchodzi w stan,
# który BEZ bramki ARMED przeciekłby na override)
cd test/host && cmake -B build && cmake --build build && ctest --test-dir build

# build na target
idf.py build   # esp32s3
```

## Zapobieganie

- Override sterowania licz PO maszynie stanów i wykonuj tylko w bezpiecznej
  gałęzi (`ARMED`); poza nią forsuj `OFF`. Pierwszeństwo failsafe wymuszaj
  strukturalnie (jedna bramka), nie warunkami rozsianymi po kodzie.
- Sensory podpinaj jako wejścia override'u, NIGDY do wejść failsafe / maszyny
  stanów (`rc_valid`, `sm_inputs`).
- Warunek jakości (np. `gps_has_fix`) re-waliduj co cykl w trakcie hold, nie
  tylko przy wejściu — okno świeżości nie świadczy o realnym fixie.
- Test bramki/pierwszeństwa musi wchodzić w stan, który BEZ bramki by przeciekł
  (moc wyroczni) — inaczej przejdzie też po usunięciu bramki.

## Powiązane

- docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md — wrap-safe recency w jednej domenie zegara
- docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md — moc wyroczni testu transformacji
- docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md — czyste funkcje zza HAL
- docs/plans/2026-06-29-001-feat-spot-lock-position-hold-plan.md — plan zadania

## Kontekst

Firmware silnika kajakowego ESP32-S3. Feature spot-lock-position-hold
(point-and-shoot, position-only). Commity: c16c924 (integracja override w
loop_step), f612827 (re-walidacja gps_has_fix), c06795f (licznik świeżości +
predykat). 388 host-testów + build esp32s3 zielone.
