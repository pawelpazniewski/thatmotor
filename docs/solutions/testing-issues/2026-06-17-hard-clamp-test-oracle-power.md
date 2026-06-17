---
title: "Test bezpiecznego clampu bez mocy wyroczni — wartości na granicy okna przechodzą z clampem LUB bez niego"
date: 2026-06-17
category: testing-issues
severity: high
stack:
  - C
  - Unity
  - ESP-IDF
tags:
  - test-oracle
  - assertion-strength
  - safety
  - clamp
  - boundary-testing
  - firmware
status: verified
last_verified: 2026-06-17
---

# Test hard clampu bez mocy wyroczni

## Symptomy

- Test `test_calibration_output_stays_within_hard_clamp` używał stałych 1000/1500/2000 µs, które są
  **dokładnie granicami** okna clampu `[1000, 2000]`.
- Wynik: test przechodzi niezależnie od tego, czy `clamp_esc(...)` jest na ścieżce, czy nie.
  Usunięcie clampu z `run_calibration` NIE wywróciłoby testu.
- Plan żądał "dowodu SI-3 (hard clamp nieobchodzalny) w trybie serwisowym" — test nie dawał tego
  dowodu mimo zielonego statusu.

## Root Cause

Test bez mocy wyroczni: wartości wejściowe równe oczekiwanym wartościom wyjściowym sprawiają, że
testowana transformacja (clamp) jest tożsamościowa na tych danych. Asercja przechodzi dla obu
implementacji (z clampem i bez), więc nie odróżnia poprawnej od błędnej — coverage rośnie,
weryfikacja = 0 (anty-pattern: assertion-free / weak-oracle test).

## Rozwiązanie

Test bezpiecznej granicy MUSI podać wartość **poza oknem** na realnej ścieżce produkcyjnej, by clamp
miał coś do zrobienia. Dopiero wtedy obecność/brak clampu zmienia wynik:

```c
// SŁABO (bez wyroczni): wejście == granica okna, clamp tożsamościowy
TEST_ASSERT_EQUAL_UINT16(2000, run_calibration(STEP_FORWARD, ...));  // 2000 i tak == max

// MOCNO (z wyrocznią): osobny behawioralny dowód clampa wartością poza oknem
TEST_ASSERT_EQUAL_UINT16(2000, clamp_pwm_us(3000, window_1000_2000));  // 3000 → 2000
TEST_ASSERT_EQUAL_UINT16(1000, clamp_pwm_us(500,  window_1000_2000));  //  500 → 1000
// + przemianować test "stałych w oknie" by nie udawał dowodu clampa
```

Strukturalny dowód (single-return przez `clamp_esc`) uzupełnij behawioralnym dowodem clampa na
wartości spoza okna — najlepiej zarówno na izolowanej `clamp_pwm_us`, jak i na zintegrowanej ścieżce
(np. wstrzyknięta wartość poza oknem do `run_calibration`).

## Komendy diagnostyczne

```bash
# Mutacja-test ręcznie: usuń clamp ze ścieżki i sprawdź czy test FAILuje
# (jeśli nadal PASS → test nie ma mocy wyroczni)
./test/host/run.sh
```

## Zapobieganie

- Przy teście transformacji ograniczającej (clamp/saturate/limit) podawaj wejścia **poza** zakresem
  docelowym — wejście == oczekiwane wyjście oznacza tożsamość, nie dowód.
- Reguła kciuka: "czy ten test FAILuje, jeśli usunę testowaną transformację?". Jeśli nie — brak
  wyroczni.
- Dla inwariantów bezpieczeństwa: łącz dowód strukturalny (single-return przez bramkę) z dowodem
  behawioralnym (wartość naruszająca → wartość zclampowana).
- NIE osłabiaj asercji ani nie traktuj "fizycznie spełnione" jako substytutu dowodu (coding-rules §2,
  anty-pattern #2/#6).

## Powiązane

- docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md
- docs/completed/kayak-motor-firmware-v1/review-faza-5.md

## Kontekst

kayak-motor-firmware-v1, Faza 5 (Unit 9 ESC_RANGE_CALIBRATION). Finding P2 z review fazy 5,
rozwiązany w cyklu 1 (commit 861c878). SI-3 = hard clamp jako nieobchodzalna granica wyjścia PWM.
