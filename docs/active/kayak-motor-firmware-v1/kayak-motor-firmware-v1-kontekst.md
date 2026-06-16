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

## Decyzje progowe Fazy 1 (konserwatywne wartości startowe — do dostrojenia po pomiarach)

Źródło nie podaje liczb, więc przyjęto wartości startowe (uczynione konfigurowalnymi, NIE hardcode):

- **Okres ramki RC** — NIE hardcode 20 ms. `rc_channel_cfg.period_expected_us` jest parametrem
  podawanym z konfiguracji (po pomiarze odbiornika). W testach hosta użyto placeholdera 20 ms ± 5 ms
  tolerancji tylko jako reprezentatywnego przypadku. Próg okresu wejdzie do walidacji z modelu ustawień.
- **N kolejnych złych ramek (debounce failsafe)** — `RC_DEBOUNCE_DEFAULT_THRESHOLD = 5` (dolny koniec
  zakresu „~5–10" z planu): jeden glitch nie wywoła failsafe, utrata sygnału łapana w kilka ramek.
- **Zakresy walidacji parametrów** — `components/settings/src/settings_ranges.h`. Konserwatywne,
  szerokie sanity windows; defaulty z R16: CH 1000/1500/2000, `escNeutralUs=1500` (okno 1400–1600),
  `max_throttle_pct=30`, łagodne rampy (up=5, down=10 µs/cykl), slew serwa=10, deadband steru=0,
  deadband gazu=80 µs, `failsafe_timeout_ms=200`, `reverseNeutralDwellMs=0` (wyłączony do pomiaru
  pluggingu WP880). Cross-field: RC monotoniczne min<mid<max; endpointy serwa rosnące; pasmo ESC
  forward rosnące, reverse malejące od neutralu.

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

## Review fazy 0 (2026-06-16)

Multi-agent code review commitu `866555e` (Unit 1+2). Severity gate: ⚠️ ZASTRZEŻENIA — P1=0, P2=2, P3=5. Raport: `review-faza-0.md`. Testy hosta 10/10 PASS; build idf.py + host przeszły w execute.

Kluczowe wnioski:
- SI-1 (boot-to-safe), SI-3 (hard clamp niebypassowalny), izolacja LEDC do `pwm_out`, separacja pure ⊥ HAL — wszystkie spełnione na poziomie source. Pokrycie wymaganych scenariuszy Unit 2 = 100%.
- 2× P2 do domknięcia w obrębie zadania (nie blokują startu Fazy 1): (1) `clamp_pwm_us` inwariant `min<=max` chroniony tylko przez `assert` (no-op w release) — utwardzić zanim wejdą okna z NVS/kalibracji (Unit 8/9); (2) realny code-path `pwm_out_write_us` (walidacja kanału + dowód clamp-before-convert) bez testu — ekstrakcja czystej logiki do `PURE_SOURCES`.
- Odchylenia od planu: testy w `test/host/` zamiast `components/*/test/` (świadoma decyzja zgodna z kontekst.md:14/zadania.md:21 — standalone host harness, NIE naruszenie); konwersja µs→duty round-to-nearest zamiast floor `<<16` (ulepszenie, wartości zgodne z kontraktem).
- [HW]/[E2E] checkboxy odroczone do known-issues (brak sprzętu/UI) — nie liczone jako findingi. Agent E2E = N/A (brak panelu WWW przed Unit 10).

## Źródła
- Requirements doc: docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md
- Plan techniczny: docs/plans/2026-06-16-001-feat-kayak-motor-firmware-v1-plan.md
