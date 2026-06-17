---
title: "Recency RC liczona z absolutnego 32-bit licznika capture daje błędne wyniki po wrapie (~53,6 s)"
date: 2026-06-17
category: runtime-errors
severity: high
stack:
  - C
  - ESP-IDF
tags:
  - integer-wraparound
  - modular-arithmetic
  - timer
  - epoch-contract
  - rc-capture
  - silent-bug
  - firmware
status: verified
last_verified: 2026-06-17
---

# Wrap-safe recency w domenie licznika (nie w czasie ściennym)

## Symptomy

- `last_edge_us` (recency ostatniego zbocza RC) liczone jako absolutny czas; predykat `edge_recent`
  porównywał `now_us - last_edge_us`.
- 32-bit licznik capture MCPWM (12.5 ns/tick) zawija co ~53,6 s. Mieszanie domeny licznika capture
  z `esp_timer_get_time()` (inny epoch) daje **błędną recency** zaraz po starcie i wokół każdego wrapu.
- Objaw cichy: failsafe mógłby się nie wyzwolić (stary edge wyglądający jak świeży) albo wyzwolić
  fałszywie — błąd nie manifestuje się oczywistym crashem, ujawnia się dopiero w działaniu.

## Root Cause

Recency to różnica dwóch znaczników czasu, które MUSZĄ pochodzić z tego samego, monotonicznego
(modularnie) licznika. Konwersja raw capture ticks na absolutny `us` przez inny zegar
(`esp_timer_get_time()`) łamie wspólny epoch; dodatkowo naiwne odejmowanie na zawijającym liczniku
daje ujemną/ogromną wartość wokół wrapu.

## Rozwiązanie

Trzymaj recency w **domenie surowego licznika** i licz różnicę przez **unsigned modular subtraction**
(wrap-safe), dopiero potem konwertuj na µs do porównania z timeoutem:

```c
// Sample trzyma RAW tick, NIE konwertowany czas
typedef struct { uint32_t width_us; uint32_t period_us;
                 uint32_t last_edge_ticks; bool edge_seen; } rc_channel_sample;

// Wrap-safe: unsigned subtraction zawija poprawnie modulo 2^32
static uint32_t cap_ticks_elapsed(uint32_t now_ticks, uint32_t last_ticks) {
    return now_ticks - last_ticks;   // poprawne nawet gdy now < last (wrap)
}

bool channel_valid(const rc_channel_sample *s, uint32_t now_ticks, const cfg *c) {
    uint32_t age_us = cap_ticks_to_us(cap_ticks_elapsed(now_ticks, s->last_edge_ticks));
    return s->edge_seen && age_us <= c->edge_timeout_us && /* width/period checks */;
}
```

Kluczowe: `now_ticks` MUSI pochodzić z tego samego licznika capture co `last_edge_ticks`. Kontrakt
epoch udokumentowany w nagłówku — **zakaz `esp_timer_get_time()`** dla tej osi.

## Komendy diagnostyczne

```bash
# Host-testy wrapu (bez sprzętu)
./test/host/run.sh   # m.in. recency poprawna wokół wrapu + stary edge wokół wrapu → not recent
```

## Zapobieganie

- Różnicę czasu/licznika licz ZAWSZE przez unsigned subtraction w jednej domenie; nie mieszaj
  zegarów (capture ticks vs esp_timer vs ms).
- Trzymaj raw znacznik (tick), konwertuj na jednostki fizyczne dopiero przy porównaniu.
- Udokumentuj kontrakt epoch w nagłówku ("producent musi podać now z tego samego licznika").
- Dla każdego licznika z wrapem dodaj jawny host-test recency/elapsed wokół granicy wrapu — to
  cicha klasa błędów, niewidoczna bez testu na wartościach przy 2^N.
- Ta sama zasada dotyczy `now_ms = esp_timer_get_time()/1000` rzutowanego do uint32 (wrap ~49,7 dnia):
  debounce/elapsed wrap-safe, ale truncation int64→uint32 wymaga świadomego komentarza.

## Powiązane

- docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md
- docs/completed/kayak-motor-firmware-v1/review-faza-1.md
- docs/completed/kayak-motor-firmware-v1/review-faza-3.md

## Kontekst

kayak-motor-firmware-v1, Faza 1 (Unit 3/4) — finding P2 z review fazy 1, kontrakt domknięty na
warstwie pure (commit cyklu 1). RC_valid / SI-4. Licznik MCPWM capture 12.5 ns/tick, wrap ~53,6 s.
