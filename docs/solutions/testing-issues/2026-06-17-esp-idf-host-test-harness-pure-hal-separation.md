---
title: "Host-testowanie logiki firmware ESP-IDF bez sprzętu (standalone Unity, separacja pure ⊥ HAL)"
date: 2026-06-17
category: testing-issues
severity: high
stack:
  - ESP-IDF
  - C
  - Unity
  - CMake
tags:
  - host-testing
  - esp-idf
  - unit-testing
  - pure-logic
  - hal-separation
  - firmware
  - testability
status: verified
last_verified: 2026-06-17
---

# Host-testowanie logiki firmware ESP-IDF bez sprzętu

## Symptomy

- Logikę bezpieczeństwa/sterowania firmware ESP32 trzeba zweryfikować, ale brak fizycznego sprzętu
  (ESP32, oscyloskop, peryferia) w środowisku CI/autopilota.
- Naiwne testowanie wymagałoby flashowania płytki — niemożliwe bez sprzętu i wolne nawet z nim.
- Kod mieszający logikę z wywołaniami HAL (LEDC, MCPWM, NVS, esp_http_server) nie kompiluje się
  na hoście (brak nagłówków `driver/*.h`, `esp_*.h`).

## Root Cause

Testowalność jednostkowa wymaga, by logika decyzyjna była wolna od zależności sprzętowych. Gdy
funkcja jednocześnie podejmuje decyzję i woła HAL (`ledc_set_duty`, `nvs_get_blob`, `httpd_*`), nie
da się jej uruchomić ani zaobserwować na hoście — jedyną drogą weryfikacji zostaje sprzęt.

## Rozwiązanie

Rygorystyczna separacja **pure logic ⊥ HAL** + standalone host harness (CMake + Unity) niezależny
od `idf.py`:

1. **Każdy komponent ma czysty rdzeń bez include ESP-IDF** — np. `loop_step.c` (czysty krok
   `(active_params, rc_samples, state, ...) → (new_state, servo_us, esc_us, telemetry)`),
   `clamp_pwm_us`, `cap_math.c`, `chain_math.c`, `blob_codec.c`, `led_pattern.c`. Nagłówki tych
   modułów NIE includują `esp_err.h` ani `driver/*.h`.

2. **HAL adapter cienki** — woła czystą funkcję i tylko wykonuje I/O. Np. `nvs_store.c` mapuje
   surowy `esp_err_t` → własny enum `nvs_read_status`, dekoduje blob i deleguje decyzję do czystej
   `resolve_provenance(...)`.

3. **Wyekstrahuj czyste funkcje decyzyjne zza HAL**, gdy decyzja jest nietrywialna a oryginalna
   funkcja jest HAL-only: `resolve_provenance` (NVS empty/corrupt/schema/valid), `params_decide_write`
   (409/400/200), `command_parse` (exact-match cJSON). Nowy pure moduł = nowy host-test.

4. **Standalone harness** — `test/host/CMakeLists.txt` linkuje tylko `PURE_SOURCES` komponentów +
   Unity (nie cały ESP-IDF). Uruchamiany jedną komendą (`test/host/run.sh`), nie wymaga toolchaina
   Espressif.

```c
// HAL (nvs_store.c) — cienki, deleguje decyzję
nvs_read_status status = map_esp_err(err);
bool decoded = blob_decode(buf, len, &decoded_params);
resolve_provenance(status, decoded, &decoded_params, out);  // pure, host-testowalne

// pure (nvs_provenance.c) — zero include IDF, 7 host-testów
void resolve_provenance(nvs_read_status status, bool decode_ok,
                        const settings_params *decoded, settings_resolution *out);
```

## Komendy diagnostyczne

```bash
# Host-testy (bez sprzętu, bez idf.py)
./test/host/run.sh                          # 198/198 PASS

# Sanity build na target (wymaga ESP-IDF)
idf.py set-target esp32 && idf.py build     # EXIT=0

# Audyt separacji: żaden pure header nie może includować HAL
grep -rE '#include\s+"(esp_|driver/)' components/*/include/  # oczekiwane: puste dla pure
```

## Zapobieganie

- Dla każdej nietrywialnej decyzji za HAL: wyekstrahuj czystą funkcję `(inputs) → (outputs)` i
  pokryj host-testem (happy path + error case, coding-rules §2).
- Pure header NIGDY nie includuje `esp_*.h`/`driver/*.h` — to inwariant separacji, sprawdzalny grepem.
- Trzymaj HAL adapter cienki: mapuj `esp_err_t` na własny enum domenowy, decyzję deleguj do pure.
- HAL-only orkiestracja (timing/wiring) pozostaje niepokryta na hoście — zaloguj to jawnie jako
  known coverage gap do domknięcia na sprzęcie, nie udawaj że jest pokryta.

## Powiązane

- docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md
- docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md
- docs/completed/kayak-motor-firmware-v1/kayak-motor-firmware-v1-podsumowanie.md

## Kontekst

Projekt kayak-motor-firmware-v1 (ESP-IDF 5.5.x, ESP32). 198/198 host-testów Unity, 22 pliki
testowe, 10 komponentów. Separacja pure ⊥ HAL utrzymana przez wszystkie 7 faz; review każdej fazy
potwierdzał zero circular deps i czyste granice warstw.
