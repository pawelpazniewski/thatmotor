# Podsumowanie ukończenia: Firmware V1 — sterownik ESP32 silnika kajaka

**Zadanie:** kayak-motor-firmware-v1
**Branch:** `feature/kayak-motor-firmware-v1`
**Data ukończenia:** 2026-06-17

## Status końcowy

- **198/198 testów hosta (Unity) PASS** — uruchamiane standalone CMake+Unity, jedną komendą.
- **`idf.py build` (target esp32) PASS** — bin 0xd90e0 B, ~43% partycji wolne.
- Wszystkie 7 faz (Unit 1–11) zaimplementowane i zreviewowane (multi-agent review na każdej fazie).
- Czynności `[HW]` (sprzęt/oscyloskop) i `[E2E]` (przeglądarka/żywy panel) **świadomie odroczone**
  do `known-issues.md` — brak fizycznego sprzętu w środowisku (decyzja użytkownika). To luki
  weryfikacji zależne od środowiska, NIE braki implementacji ani osłabione asercje.

## Co zostało dostarczone

Firmware ESP-IDF 5.5.x wchodzący w tor sterowania między odbiornik RC a aktuatory (serwo skrętu +
bidirectional ESC WP880) silnika trollingowego na kajaku:

- **Faza 0 (Unit 1–2):** szkielet ESP-IDF + standalone host harness Unity + sdkconfig bezpieczeństwa
  (TWDT panic, brownout, partycja `appcfg`); warstwa wyjścia LEDC z **hard clampem SI-3** i bezpiecznym
  bootem (SI-1).
- **Faza 1 (Unit 3–4):** wejście RC przez MCPWM capture (12.5 ns edge-latch) + odczyt CH4; predykat
  RC_valid z debounce; model ustawień z konserwatywnymi defaultami i walidacją per-field/cross-field.
- **Faza 2 (Unit 5):** czyste łańcuchy gazu (deadband→reverse→limit→rampa→mapowanie ESC) i serwa
  (deadband→reverse→endpointy→slew→mapowanie).
- **Faza 3 (Unit 6–7):** 4-stanowa maszyna bezpieczeństwa (DISARMED/ARMED/FAILSAFE/ESC_CALIBRATION),
  arming guard, failsafe; integracja pętli ~50 Hz + watchdog (fed tylko z ukończonej iteracji) +
  single-writer (mailbox length-1, SI-6); czysty rdzeń `loop_step`.
- **Faza 4 (Unit 8):** persystencja NVS — versioned blob + CRC32, delayed commit (debounce 2–5 s,
  tylko DISARMED + dirty), recovery do defaultów przy korupcji; czysta funkcja decyzyjna
  `resolve_provenance`.
- **Faza 5 (Unit 9):** ESC_RANGE_CALIBRATION — osobny stan, krokowy (neutral/fwd/rev), z abortem na
  utracie RC, przez clamp (SI-3 w trybie serwisowym), wejście tylko z jawnym potwierdzeniem (SI-5).
- **Faza 6 (Unit 10–11):** WiFi SoftAP WPA2-PSK (assert non-OPEN + fail-fast), serwer WWW, telemetria
  WS ~10 Hz lossy, API parametrów (`{data, error}`, 409 gdy nie DISARMED), embedded panel; sygnalizacja
  LED GPIO2 z wzorami stanu.

## Podjęte kluczowe decyzje

| Decyzja | Wybór |
|---|---|
| Architektura | pure logic ⊥ HAL; `loop_step` jako czysty rdzeń → pokrycie integracyjne cross-layer na hoście |
| RC input | MCPWM capture (sprzętowy edge-latch, odporny na latencję ISR/WiFi/flash) |
| PWM out | LEDC 16-bit @ 50 Hz, zmiana tylko duty (glitch-free) |
| Hard clamp SI-3 | nieobchodzalna granica wyjścia, niezależna od NDEBUG (bezwarunkowy fail-safe) |
| Epoch/recency RC | `last_edge_ticks` raw 32-bit capture + modular subtraction wrap-safe (zakaz `esp_timer_get_time()`) |
| Params WWW↔pętla | mailbox length-1 (`xQueueOverwrite`) — single-writer bez torn-read (SI-6) |
| NVS | versioned blob + CRC32, walidacja przy odczycie, reload-defaults zamiast migracji |
| Watchdog | esp_task_wdt panic, fed tylko z ukończonej iteracji |
| Panel | vanilla HTML/JS embed w flash (zero build-stepu, mały footprint) |

## Główne utworzone/zmodyfikowane pliki

- `components/safety_clamp/` — `clamp_pwm_us` (SI-3)
- `components/pwm_out/` — LEDC + `pwm_us_to_duty.c` (pure)
- `components/rc_capture/` — MCPWM capture + `cap_math.c` (pure)
- `components/rc_validity/` — `channel_valid`, `rc_valid` (epoch/wrap-safe recency)
- `components/settings/` — `settings_model.h`, `settings_defaults.c`, `settings_validate.c`,
  `blob_codec.c`, `commit_debounce.c`, `nvs_provenance.c` (pure) + `nvs_store.c` (HAL)
- `components/signal_chain/` — `throttle_chain.c`, `servo_chain.c`, `ramp.c`, `chain_math.c`
- `components/state_machine/` — `state_machine.c`, `esc_calibration.c`
- `components/control_loop/` — `loop_step.c` (pure rdzeń) + `control_loop.c` (HAL/timing)
- `components/web_panel/` — `api_contract.c`, `wifi_ap_config.c`, `command_parse`, `params_decide`
  (pure) + `wifi_ap.c`, `http_server.c`, `ws_telemetry.c`, `params_api.c`, `params_json.c` (HAL)
- `components/led_status/` — `led_pattern.c` (pure) + `led_driver.c` (HAL)
- `web/index.html`, `web/app.js`, `web/style.css` — embedded panel
- `test/host/` — 22 pliki testowe Unity (198 testów) + standalone CMake harness
- `main/app_main.c`, `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`

## Wyciągnięte wnioski

1. **Host-test harness pure-logic dla ESP-IDF bez sprzętu** — standalone CMake+Unity z rygorystyczną
   separacją pure ⊥ HAL pozwala pokryć logikę bezpieczeństwa/sterowania testami integracyjnymi
   cross-layer na hoście. Każda decyzja warta testu wyekstrahowana do czystej funkcji
   (`nvs_provenance`, `params_decide`, `command_parse`, `loop_step`).
2. **Epoch/wrap-safe recency w domenie licznika** — recency RC liczona przez modular unsigned
   subtraction na raw 32-bit capture ticks (NIE przez `esp_timer_get_time()`), bo absolutny licznik
   capture zawija co ~53,6 s; kontrakt epoch udokumentowany w nagłówku.
3. **Hard clamp jako nieobchodzalna granica** — clamp musi być bezwarunkowy (niezależny od `NDEBUG`),
   bo `assert()` kompiluje się do no-op w release. Dowód testowy wymaga mocy wyroczni: wartość poza
   oknem na ścieżce, nie tylko wartości == granicom okna.
4. **Single-writer przez mailbox length-1** — `xQueueOverwrite` + apply tylko w DISARMED + re-check
   TOCTOU eliminuje torn-read parametrów bez mutexów.
5. **Konserwatywne defaulty + placeholdery zamiast hardcode** — wartości zależne od pomiaru
   (okres ramki RC, neutral WP880, reverseNeutralDwellMs) są konfigurowalne z udokumentowanymi
   placeholderami, NIE zahardkodowane (np. "NIE zakładać 20 ms").

## Powiązane dokumenty

- Plan: `kayak-motor-firmware-v1-plan.md`
- Kontekst: `kayak-motor-firmware-v1-kontekst.md`
- Zadania (pełna lista z findingami review): `kayak-motor-firmware-v1-zadania.md`
- Known issues (odroczone `[HW]`/`[E2E]` + Plan pomiarów): `known-issues.md`
- Raporty review faz 0–6: `review-faza-{0..6}.md`
- Requirements: `docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md`
- Plan techniczny: `docs/plans/2026-06-16-001-feat-kayak-motor-firmware-v1-plan.md`
