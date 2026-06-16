# Kontekst: Firmware V1 — sterownik ESP32 silnika kajaka

**Branch:** `feature/kayak-motor-firmware-v1`
**Ostatnia aktualizacja:** 2026-06-16

## Powiązane pliki (do utworzenia)

Layout ESP-IDF (komponenty). Pliki C w snake_case (konwencja frameworka — nadrzędna wobec reguły
kebab-case). Pliki < 300 linii, funkcje < 50 linii, early return zamiast nestingu > 2.

**Root / build:**
- `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv` (partycja `appcfg` data/nvs), `.gitignore`
- `main/CMakeLists.txt`, `main/app_main.c`
- `test/host/CMakeLists.txt` (harness Unity / target `linux`)
- `docs/hardware/bom-and-measurements.md` (BOM + checklista Planu pomiarów)

**Komponenty pure logic (host-testowalne):**
- `components/safety_clamp/` — `clamp_pwm_us` (SI-3)
- `components/rc_validity/` — `channel_valid`, `rc_valid` (SI-4)
- `components/settings/` — `settings_model.h`, `settings_defaults.c`, `settings_validate.c`,
  `blob_codec.c`, `commit_debounce.c` (pure) + `nvs_store.c` (HAL)
- `components/signal_chain/` — `throttle_chain.c`, `servo_chain.c`, `ramp.c`
- `components/state_machine/` — `state_machine.c`, `esc_calibration.c`
- `components/control_loop/` — `loop_step.c` (pure rdzeń) + `control_loop.c` (HAL/timing)
- `components/web_panel/` — `api_contract.c` (pure) + `wifi_ap.c`, `http_server.c`, `ws_telemetry.c`,
  `params_api.c` (HAL)
- `components/led_status/` — `led_pattern.c` (pure) + `led_driver.c` (HAL)

**Komponenty HAL adapters:**
- `components/pwm_out/` — LEDC + `pwm_us_to_duty.c` (pure)
- `components/rc_capture/` — MCPWM capture + `cap_math.c` (pure)

**Panel (embed w flash przez EMBED_FILES):**
- `web/index.html`, `web/app.js`, `web/style.css`

## Decyzje techniczne

| Decyzja | Wybór | Uzasadnienie |
|---|---|---|
| Framework + build | ESP-IDF 5.5.x, idf.py/CMake/Kconfig | Kontrola TWDT panic/brownout/MCPWM/NVS; testy host-side. |
| RC input | MCPWM capture (`driver/mcpwm_cap.h`) | Sprzętowy edge-latch (12.5 ns), odporny na latencję ISR/WiFi/flash. |
| PWM out | LEDC 16-bit @ 50 Hz | 1000/1500/2000 µs = duty 3277/4915/6554; zmiana tylko duty (glitch-free). |
| Panel | vanilla HTML/JS embed w flash | Zero build-stepu, mały footprint, łatwy audyt. |
| Telemetria | WebSocket ~10 Hz, lossy (jeden slot) | Człowiek nie odróżni szybciej; nie obciążać pętli. |
| Params WWW↔pętla | mailbox length-1 (`xQueueOverwrite`) | Single-writer bez torn-read, bez mutexów (SI-6). |
| NVS | versioned blob + CRC32, partycja `appcfg`, walidacja przy odczycie | Reload-defaults zamiast migracji; nigdy śmieci. |
| Watchdog | esp_task_wdt panic, fed tylko z ukończonej iteracji | Hang → reset; nie maskować zawisu. |
| Architektura | pure logic ⊥ HAL; `loop_step` czysty rdzeń | Pokrycie integracyjne cross-layer na hoście. |

## Otwarte / odroczone (do implementacji lub Planu pomiarów)
- Zmierzony neutral/pasmo WP880, progi startu, realne `escNeutralUs` (SI-2).
- Czy potrzebny `reverseNeutralDwellMs` (po pomiarze szybkiej zmiany przód↔tył).
- Zmierzony okres ramki odbiornika + tolerancja w RC_valid (NIE zakładać 20 ms).
- Długość okna martwego boot; zachowanie pinów w brownout-sag (oscyloskop).
- Pinning Core0/Core1 — tylko jeśli zmierzony jitter.
- Konkretne zakresy walidacji parametrów (start od defaultów R16).
- Próg N kolejnych złych ramek dla failsafe (start ~5–10, dostroić po pomiarze).

## Zależności zewnętrzne
- ESP-IDF 5.5.x (pinować dokładny tag). ArduinoJson v7 (component registry) lub cJSON (w IDF).
- Sprzęt: ESP32 DevKit, WP880 ESC, serwo DS3240, odbiornik RC (z konfigurowalnym failsafe —
  twardy wymóg: brak PWM przy utracie RF), LiFePO4 12V 100Ah, buck 5V, pull-downy G18/G19, e-stop.

## Źródła
- Requirements doc: docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md
- Plan techniczny: docs/plans/2026-06-16-001-feat-kayak-motor-firmware-v1-plan.md
