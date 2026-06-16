# Zadania: Firmware V1 — sterownik ESP32 silnika kajaka

**Branch:** `feature/kayak-motor-firmware-v1`
**Ostatnia aktualizacja:** 2026-06-16

Legenda: `Test:` = scenariusz testowy · `Weryfikacja:` = kryterium ukończenia ·
`[Unit]` test hosta · `[HW]` weryfikacja na sprzęcie/Plan pomiarów · `[E2E]` przeglądarka.

---

## Faza 0 — Fundament i bezpieczne wyjście

### Unit 1: Szkielet ESP-IDF + harness testów hosta + sdkconfig bezpieczeństwa [M]
Wymagania: baza; R16 (partycja), SI-1 (watchdog/brownout). Zależności: brak.

Implementacja:
- [x] `CMakeLists.txt` (root), `main/CMakeLists.txt`, `main/app_main.c` (placeholder boot do bezpiecznego stanu)
- [x] `sdkconfig.defaults`: `CONFIG_ESP_TASK_WDT_PANIC=y`, `CONFIG_ESP_TASK_WDT_EN=y`, brownout enabled, `CONFIG_HTTPD_WS_SUPPORT=y`; pinować tag IDF
- [x] `partitions.csv` z dedykowaną partycją `appcfg` (data/nvs) osobno od `nvs`
- [x] Puste komponenty z `CMakeLists.txt` (safety_clamp, pwm_out, rc_capture, rc_validity, settings, signal_chain, state_machine, control_loop, web_panel, led_status)
- [x] `test/host/CMakeLists.txt` + integracja Unity dla komponentów pure (lub target `linux`)
- [x] `.gitignore` (build/, lokalny sdkconfig)
- [x] `docs/hardware/bom-and-measurements.md` (pull-downy G18/G19, tap 12V przed kill-switchem, decoupling, lista pomiarów ze źródła)

Testy:
- [x] Test: [Unit] Host build kompiluje pusty komponent pure i przechodzi trywialny test Unity
- [ ] Test: [HW] `idf.py build` przechodzi; flash bootuje; log pokazuje reset reason + wejście do bezpiecznego placeholdera

Weryfikacja:
- [ ] Weryfikacja: projekt buduje się dla targetu ESP32 i dla hosta; partycja `appcfg` w tablicy partycji; testy hosta uruchamialne jedną komendą

### Unit 2: Warstwa wyjścia LEDC + HARD CLAMP + bezpieczny boot [M]
Wymagania: R1, SI-1, SI-2 (struktura), SI-3. Zależności: Unit 1.

Implementacja:
- [x] `components/safety_clamp/include/safety_clamp.h` + `src/safety_clamp.c` — `clamp_pwm_us(value, window)` bezwarunkowy
- [x] `components/pwm_out/include/pwm_out.h` + `src/pwm_out.c` — init LEDC, `pwm_out_write_us` (woła clamp wewnętrznie)
- [x] `components/pwm_out/src/pwm_us_to_duty.c` (+`.h`) — pure konwersja µs→duty
- [x] Modyfikuj `main/app_main.c` — najpierw init pwm_out + write neutral/center, potem reszta
- [x] Audyt: tylko `pwm_out` linkuje driver LEDC (żaden inny moduł nie woła LEDC bezpośrednio)

Testy:
- [x] Test: [Unit] `clamp_pwm_us`: <min→min; >max→max; w oknie→bez zmian; na granicy→granica (inclusive)
- [x] Test: [Unit] `pwm_us_to_duty`: 1000/1500/2000 µs → 3277/4915/6554; wartości spoza okna najpierw zclampowane
- [ ] Test: [HW/Plan pomiarów] Oscyloskop: po boocie G18/G19 emitują neutral/centrum; zmierzyć okno martwe (reset→pierwszy impuls); zachowanie przy watchdog-resecie i brownout-sag; sprawdzić pull-downy

Weryfikacja:
- [ ] Weryfikacja: testy hosta przechodzą; na sprzęcie po boocie ESC=neutral (śmigło zdjęte), serwo=center; brak code-path do LEDC z pominięciem clamp

---

## Faza 1 — Akwizycja sygnału i ważność RC

### Unit 3: Wejście RC (MCPWM capture) + odczyt CH4 [M]
Wymagania: R1, R12, fundament SI-4. Zależności: Unit 1.

Implementacja:
- [x] `components/rc_capture/include/rc_capture.h` — typ `rc_channel_sample {width_us, period_us, last_edge_us, edge_seen}`
- [x] `components/rc_capture/src/rc_capture.c` — MCPWM cap timer + 3 kanały (G34/G35/G32), callback `on_cap`
- [x] `components/rc_capture/src/cap_math.c` (+`.h`) — pure konwersja ticki→µs, liczenie okresu
- [x] CH4 czytany jako diagnostyczny (oznaczony poza RC_valid)
- [x] Modyfikuj `main/app_main.c` — init capture

Testy:
- [x] Test: [Unit] `cap_math`: konwersja ticki→µs (12.5 ns/tick); okres z dwóch kolejnych zboczy; obsługa przepełnienia licznika
- [ ] Test: [HW] Oscyloskop równolegle z odczytem: zmierzona szerokość/okres zgodne z odbiornikiem; zapisać zmierzony okres ramki do Planu pomiarów

Weryfikacja:
- [ ] Weryfikacja: firmware raportuje sensowne szerokości i okres dla CH1/CH2/CH4; zgodne z oscyloskopem

### Unit 4: Predykat RC_valid + model ustawień (defaulty + walidacja przy odczycie) [L]
Wymagania: SI-4, R12, R16, baza R10. Zależności: Unit 1.

Implementacja:
- [x] `components/rc_validity/include/rc_validity.h` + `src/rc_validity.c` — `channel_valid(sample, cfg)`, `rc_valid(ch1, ch2)`, debounce N ramek
- [x] `components/settings/include/settings_model.h` — struktura parametrów (slew serwa, rampy ESC ↑/↓, deadband gazu, deadband steru=0, timeout failsafe, limit mocy ±, endpointy serwa, kalibracja wejścia RC, flagi reverse, kalibracja wyjścia ESC esc*Us, warunkowo `reverseNeutralDwellMs`, `schema_version`)
- [x] `components/settings/src/settings_defaults.c` — konserwatywne defaulty (CH 1000/1500/2000, szeroki deadband, niski max throttle, łagodne rampy, escNeutralUs 1400–1600)
- [x] `components/settings/src/settings_validate.c` (+`.h`) — walidacja per-field + cross-field; flagi `settings_source/settings_valid/calibrated/defaults_used/nvs_error`

Testy:
- [x] Test: [Unit] `channel_valid`: brak edge w T ms→false; szerokość 700/2300→false; okres poza tolerancją→false; ważny impuls→true
- [x] Test: [Unit] `rc_valid`: CH1 zły→false; CH2 zły→false; oba dobre→true; CH4 zły→bez wpływu
- [x] Test: [Unit] debounce: 1 zła ramka→valid; N kolejnych złych→invalid; powrót dobrej→reset licznika
- [x] Test: [Unit] walidacja: empty→DEFAULTS/defaults_used/!calibrated; jedno pole spoza zakresu→fallback pola + MIXED_RECOVERED; OK→NVS/valid
- [x] Test: [Unit] cross-field: `escForwardMin > escForwardMax`→odrzucone/fallback

Weryfikacja:
- [ ] Weryfikacja: testy hosta przechodzą; utrata CH1 lub CH2 (symulacja)→RC_valid=false po N ramkach; defaulty nigdy poza oknem sanity

---

## Faza 2 — Łańcuchy przetwarzania sygnału

### Unit 5: Łańcuchy gazu i serwa (pure) [L]
Wymagania: R2, R3, R4, R5, R10, wsparcie R6. Zależności: Unit 2 (clamp), Unit 4 (model).

Implementacja:
- [x] `components/signal_chain/include/signal_chain.h`
- [x] `components/signal_chain/src/throttle_chain.c` — kroki 1–9 (normalizacja→deadband→reverse→limit→TARGET→rampa→mapowanie ESC)
- [x] `components/signal_chain/src/servo_chain.c` — łańcuch serwa (normalizacja→deadband→reverse→endpointy→TARGET→slew→mapowanie)
- [x] `components/signal_chain/src/ramp.c` (+`.h`) — rampa/slew, osobne tempa ↑/↓ gazu, krok per cykl, dąży do targetu
- [x] Kolejność stała: deadband przed skalowaniem/rampą; reverse po deadbandzie; limit przed rampą; override na target

Testy:
- [x] Test: [Unit] deadband gazu: mały sygnał wokół środka→target 0; tuż za deadbandem→niezerowy
- [x] Test: [Unit] reverse po deadbandzie: neutral z reverse=on→wciąż neutral
- [x] Test: [Unit] limit mocy: target za limitem→ograniczony przed rampą
- [x] Test: [Unit] rampa: 0→max rośnie z rampUp; max→0 z rampDown; osobne tempa; nigdy nie przeskakuje targetu
- [x] Test: [Unit] override target: FAILSAFE→target=0→rampa soft-stop do neutralu (nie skok)
- [x] Test: [Unit] slew serwa: duży skok CH1→ograniczona prędkość; FAILSAFE→target=center, slew do środka
- [x] Test: [Unit] endpointy serwa: wartości poza endpointami→ograniczone do min/max kąta

Weryfikacja:
- [ ] Weryfikacja: testy hosta przechodzą; symulacja ciągu cykli pokazuje rampy/slew; neutral po reverse zostaje neutralem

---

## Faza 3 — Maszyna stanów i pętla sterująca

### Unit 6: Maszyna stanów + arming + failsafe (pure) [L]
Wymagania: R6, R7, SI-1, SI-4; szkielet R15/SI-5. Zależności: Unit 4, Unit 5.

Implementacja:
- [x] `components/state_machine/include/state_machine.h` — `enum state`, `struct sm_inputs`, `struct sm_outputs`
- [x] `components/state_machine/src/state_machine.c` — czysta funkcja przejść `(state, inputs)→(state, outputs)`
- [x] Boot→DISARMED bezwarunkowo; guard DISARMED→ARMED (R7); FAILSAFE ustalony; reguła serwa niezależna od arming; override targetu per stan

Testy:
- [x] Test: [Unit] boot→DISARMED (każdy reset reason)
- [x] Test: [Unit] DISARMED + wszystkie warunki arm→ARMED; brak neutralu→DISARMED; RC invalid→FAILSAFE
- [x] Test: [Unit] ARMED + RC invalid→FAILSAFE; ARMED + ręczny disarm→DISARMED
- [x] Test: [Unit] FAILSAFE trwa przy RC invalid; RC valid→DISARMED (nie ARMED)
- [x] Test: [Unit] serwo: RC valid→śledzi; RC invalid→center, niezależnie od ARMED/DISARMED
- [x] Test: [Unit] arming zablokowany gdy `calib_in_progress` lub `settings_apply_in_progress`

Weryfikacja:
- [ ] Weryfikacja: testy hosta pokrywają całą tabelę przejść ze źródła; brak wyjścia z FAILSAFE bez RC valid

### Unit 7: Integracja pętli ~50 Hz + watchdog + single-writer [L]
Wymagania: R1, R6, R7, SI-1, SI-3, SI-6; integracja R2–R5. Zależności: Unit 2, 3, 4, 5, 6.

Implementacja:
- [x] `components/control_loop/include/control_loop.h` + `src/control_loop.c` — orkiestracja cyklu, „active params", non-blocking odbiór pending, apply tylko DISARMED
- [x] `components/control_loop/src/loop_step.c` (+`.h`) — pure krok logiczny `(active_params, rc_samples, state, ramp_state, ui_events)→(new_state, servo_us, esc_us, telemetry)`
- [x] Modyfikuj `main/app_main.c` — mailbox length-1, start pętli, `esp_task_wdt_add` + reset co cykl
- [x] Watchdog reset tylko na końcu udanej iteracji; re-check DISARMED w momencie apply (TOCTOU)

Testy:
- [x] Test: [Unit] `loop_step` DISARMED: drążek max→esc_us=neutral (bramka R7)
- [x] Test: [Unit] `loop_step` ARMED: gaz śledzi CH2 z rampą; serwo śledzi CH1 ze slew
- [x] Test: [Unit] `loop_step` RC invalid→FAILSAFE; esc soft-stop do neutralu; serwo do center
- [x] Test: [Unit] apply pending tylko w DISARMED; w ARMED pending nie zmienia active params
- [ ] Test: [HW] Pomiar jittera pętli (~50 Hz stabilne?); zachowanie po watchdog-resecie (boot DISARMED, neutral)

Weryfikacja:
- [ ] Weryfikacja: na sprzęcie serwo/ESC podążają w ARMED, neutral w DISARMED; utrata RC→soft-stop + serwo center + latch; WDT-reset→DISARMED/neutral

---

## Faza 4 — Trwałość parametrów

### Unit 8: Persystencja NVS (versioned blob + CRC32, delayed commit, pending/apply) [L]
Wymagania: R11, R16, R17, SI-6. Zależności: Unit 4, Unit 7.

Implementacja:
- [ ] `components/settings/src/nvs_store.c` — `nvs_store_load` (read→CRC→walidacja Unit 4→params+flagi), `nvs_store_commit`, init-recovery (rozdziel `NEW_VERSION_FOUND` jako alert)
- [ ] `components/settings/src/blob_codec.c` (+`.h`) — pure serializacja struct↔blob, CRC32, sprawdzenie schema_version i długości
- [ ] `components/settings/src/commit_debounce.c` (+`.h`) — pure logika debounce (force vs timer)
- [ ] Modyfikuj `main/app_main.c` — load przy boocie przed startem pętli; `control_loop` — commit gdy dirty + DISARMED + debounce upłynął

Testy:
- [ ] Test: [Unit] `blob_codec` round-trip: serialize→deserialize równe; zła CRC→odrzucone; zła długość→odrzucone; inny schema_version→odrzucone (→defaulty)
- [ ] Test: [Unit] `commit_debounce`: zmiana→brak commitu przed upływem; kolejna zmiana resetuje timer; force→natychmiast; brak zmian→brak commitu
- [ ] Test: [HW] Zapis w DISARMED→restart→wartość przetrwała; power-cut przed commitem→ostatnia dobra/default; pusty/zepsuty NVS→defaulty + UNCALIBRATED

Weryfikacja:
- [ ] Weryfikacja: testy hosta przechodzą; parametry przeżywają restart; skorumpowany NVS nigdy nie daje wartości spoza okna sanity

---

## Faza 5 — Tryb serwisowy

### Unit 9: ESC_RANGE_CALIBRATION (osobny stan, krokowy, z abortem) [M]
Wymagania: R15, SI-3, SI-5. Zależności: Unit 6, Unit 7, Unit 2.

Implementacja:
- [ ] `components/state_machine/src/esc_calibration.c` (+`.h`) — pure: sekwencja kroków, mapowanie krok→stałe µs, reguły abortu/timeoutu
- [ ] Modyfikuj `loop_step.c` — state==ESC_CALIBRATION→esc_us z sekwencji, pomija łańcuch gazu, przez clamp
- [ ] Modyfikuj `state_machine.c` — guard wejścia (DISARMED + RC valid + throttle neutral + jawna akcja UI + potwierdzenie ostrzeżenia)

Testy:
- [ ] Test: [Unit] wejście dozwolone tylko przy wszystkich warunkach + potwierdzenie; brak potwierdzenia→brak wejścia
- [ ] Test: [Unit] kroki: neutral→1500, full fwd→2000, full rev→1000, Done→DISARMED+neutral
- [ ] Test: [Unit] abort: RC invalid→FAILSAFE; timeout→DISARMED+neutral; cancel→DISARMED+neutral
- [ ] Test: [Unit] wartości kroków przechodzą przez clamp (SI-3 w trybie serwisowym)
- [ ] Test: [HW, śmigło zdjęte] Operator przechodzi sekwencję, WP880 potwierdza zakres

Weryfikacja:
- [ ] Weryfikacja: testy hosta przechodzą; na sprzęcie (śmigło zdjęte) WP880 kalibruje krokowo; utrata RC→FAILSAFE; nigdy nie startuje bez jawnego potwierdzenia

---

## Faza 6 — Łączność, panel, sygnalizacja

### Unit 10: WiFi SoftAP WPA2-PSK + serwer WWW + telemetria WS + API parametrów + panel [XL]
Wymagania: R8, R9, R10, R12, R14, R16, R17, SI-6. Zależności: Unit 4, 6/9, 7, 8.

Implementacja:
- [ ] `components/web_panel/src/wifi_ap.c` (+`.h`) — WPA2-PSK, kanał stały, max_connection 1–2, **assert authmode != OPEN + fail-fast**
- [ ] `components/web_panel/src/http_server.c` (+`.h`) — `esp_http_server`, rejestracja URI
- [ ] `components/web_panel/src/ws_telemetry.c` — push snapshotu przez `httpd_queue_work`, jeden slot, drop gdy klient w tyle
- [ ] `components/web_panel/src/params_api.c` (+`.h`) — JSON parse/serialize, walidacja→pending→mailbox; 409 gdy nie DISARMED z kodem `SETTINGS_WRITE_REJECTED_NOT_DISARMED`
- [ ] `components/web_panel/src/api_contract.c` (+`.h`) — pure budowa `{data, error:{code,message}}`
- [ ] `web/index.html`, `web/app.js`, `web/style.css` — embed przez EMBED_FILES; live CH1/CH2/CH4 + wyjścia + status/flagi R16; edycja przy DISARMED; sekcja ESC_CALIBRATION z ostrzeżeniem „Remove propeller / disconnect motor"
- [ ] Modyfikuj `main/app_main.c` — start AP + serwera po init pętli/NVS

Testy:
- [ ] Test: [Unit] `api_contract`: sukces→`{data,error:null}`; walidacja fail→`error.code/message`, brak data; zapis w ARMED→kod `SETTINGS_WRITE_REJECTED_NOT_DISARMED`
- [ ] Test: [E2E] AP widoczny jako WPA2 (nie otwarty); połącz→panel ładuje; live pokazuje CH1/CH2/CH4 i wyjścia; flagi R16 widoczne przy pustym NVS (UNCALIBRATED)
- [ ] Test: [E2E] W DISARMED zmień parametr→zapis OK, widoczny po restarcie; w ARMED próba zapisu→409 + kod; podgląd live działa zawsze
- [ ] Test: [HW] Assert non-OPEN: build z pustym hasłem→fail-fast

Weryfikacja:
- [ ] Weryfikacja: AP osiągalny po WPA2, panel live, edycja zablokowana poza DISARMED (egzekwowane w firmware), zmiany przeżywają restart; CH4 widoczny bez wpływu na sterowanie

### Unit 11: Sygnalizacja LED GPIO2 — wzory stanu [S]
Wymagania: R8, wsparcie R6, R16. Zależności: Unit 6, Unit 4 (równolegle z Unit 10).

Implementacja:
- [ ] `components/led_status/src/led_pattern.c` (+`.h`) — pure `(state, calibrated, t_ms)→led_on_off`
- [ ] `components/led_status/src/led_driver.c` — HAL GPIO2
- [ ] Wzory: DISARMED wolne miganie (0.5 Hz) + UNCALIBRATED double-blink nakładka; ARMED solid; FAILSAFE szybkie (5 Hz) cały czas; ESC_CALIBRATION double-blink
- [ ] Modyfikuj `control_loop` — wołanie wzoru co cykl

Testy:
- [ ] Test: [Unit] każdy stan→oczekiwany wzór on/off w funkcji t_ms; FAILSAFE niezależny od poprzedniego stanu; UNCALIBRATED nakładka tylko gdy !calibrated
- [ ] Test: [HW] Wizualna weryfikacja każdego stanu na płytce

Weryfikacja:
- [ ] Weryfikacja: testy hosta przechodzą; LED odróżnia 4 stany + UNCALIBRATED; FAILSAFE widoczny cały czas

---

## Plan pomiarów sprzętu (zrównoleglony z Fazą 0–2 — odblokowuje realne wartości)
- [ ] WP880: podaj 1500 µs — czy stoi; wyznacz faktyczny neutral i pasmo (SI-2)
- [ ] WP880: próg startu przód/tył
- [ ] WP880: zachowanie przy utracie PWM (do neutralu? po jakim czasie?)
- [ ] WP880: zachowanie przy zamrożonym poprawnym PWM (~70% LEDC, rdzeń zatrzymany)
- [ ] WP880: czy wymaga własnej kalibracji throttle range (współgra z R15)
- [ ] WP880: zachowanie przy bootowym neutralu
- [ ] WP880: szybka zmiana full fwd→full rev — cutout/skok prądu/plugging? → decyzja `reverseNeutralDwellMs`
- [ ] WP880: akceptacja — DISARMED i po soft-stop FAILSAFE śmigło nie kręci się (SI-2)
- [ ] ESP32: stan G18/G19 podczas reset/boot/bootloadera (oscyloskop)
- [ ] ESP32: zachowanie LEDC po watchdog-resecie (glitch?)
- [ ] ESP32: brownout-sag — stan pinów podczas zapadania napięcia
- [ ] ESP32: długość martwego okna boot (reset→re-init LEDC) jako liczba
- [ ] ESP32: pull-down na G19 (linia ESC nie może pływać)
- [ ] Odbiornik: zachowanie przy utracie RF (gaśnie/hold-last/preset) + konfiguracja failsafe odbiornika
- [ ] Odbiornik: zmierzony okres ramki (do progu okresu w RC_valid — NIE zakładać 20 ms)

## Do poprawy po review fazy 0

Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI (P1=0, P2=2, P3=5). Raport: `review-faza-0.md`.

- [x] 🟠 [important] **components/safety_clamp/src/safety_clamp.c:7** — `assert(min<=max)` to jedyna ochrona inwariantu, kompiluje się do no-op w release (`NDEBUG`); header obiecuje „fail-fast" którego w release nie ma. Zamienić na bezwarunkowy fail-safe niezależny od `NDEBUG` (normalizacja okna lub `abort()` aktywny w release) — najpóźniej przed wprowadzeniem okien z NVS/kalibracji (Unit 8/9).
- [x] 🟠 [important] **components/pwm_out/src/pwm_out.c:68-82** — realny code-path `pwm_out_write_us` nieprzetestowany (walidacja kanału → `ESP_ERR_INVALID_ARG` + dowód clamp-before-convert na produkcyjnej funkcji). Wyekstrahować czystą logikę do `PURE_SOURCES` i dodać happy path + error case (coding-rules §2).
- [ ] 🟡 [nit] **components/pwm_out/src/pwm_out.c:70** — tautologiczne `channel < 0` dla enum bez wartości ujemnych (potencjalny `-Wtype-limits`); usunąć dolne porównanie lub rzutować na `unsigned`.
- [ ] 🟡 [nit] **components/pwm_out/src/pwm_us_to_duty.c:8** — komentarz „fits in uint64_t (max ~ 20000 * 65536)" niedoszacowany; powołać się na `UINT32_MAX * 65536` (funkcja nie limituje zakresu wejścia).
- [ ] 🟡 [nit] **components/pwm_out/src/pwm_out.c:20,25** — niespójny prefix `CHANNEL_MAP`/`GPIO_MAP` vs `PWM_OUT_*`; ujednolicić do `PWM_OUT_CHANNEL_MAP`/`PWM_OUT_GPIO_MAP`.
- [ ] 🟡 [nit] **sdkconfig.defaults:18** — `CONFIG_HTTPD_WS_SUPPORT=y` przedwczesny w Fazie 0 (martwy do Unit 10); świadoma decyzja zgodna z zadania.md:18 — do rozważenia minimalizacja attack surface.
- [ ] 🟡 [nit] **components/*/include** — typy PascalCase (`PwmWindow`, `PwmOutChannel`) vs konwencja ESP-IDF `snake_case_t`; zgodne z literą reguł §7 — potwierdzić spójność w kolejnych fazach.

---

## Do poprawy po review fazy 1

Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI (P1=0, P2=2, P3=5). Raport: `review-faza-1.md`. Testy hosta 42/42 PASS; build idf.py EXIT=0 — oba na żywo.

- [x] 🟠 [important] **components/rc_capture/src/rc_capture.c:56 + components/rc_validity/src/rc_validity.c:29** — epoch/wrap `last_edge_us`: konwersja absolutnego 32-bit licznika capture zawija co ~53,6 s; `edge_recent` wymaga `now_us` w tym samym epoku ticków (nagłówek to obiecuje, ale producenta brak). Naiwne `esp_timer_get_time()` da błędną recency po starcie i wokół wrapu. Domknąć w Unit 7 (integracja pętli): `now_us` z tego samego licznika + porównanie modularne, albo recency liczona jako wiek/licznik ramek w ISR. → **NAPRAWIONE (cykl 1):** kontrakt domknięty na warstwie pure. `rc_channel_sample.last_edge_us`→`last_edge_ticks` (raw 32-bit capture tick, NIE konwertowany); `channel_valid` przyjmuje `now_ticks` w domenie licznika capture; recency liczona przez `cap_ticks_elapsed` (modular subtraction wrap-safe) → `cap_ticks_to_us` → porównanie z `edge_timeout_us`. Kontrakt epoch udokumentowany w nagłówku (zakaz `esp_timer_get_time()`). Testy: recency poprawna wokół wrapu + stary edge wokół wrapu → not recent.
- [x] 🟠 [important] **test/host/test_rc_validity.c** — brak testu granicy inclusive: predykat używa `>=`/`<=`, testy sprawdzają tylko wartości wyraźnie poza/wewnątrz. Dodać asercje boundary: width == 800/2200 → true; period == expected±tol → true; expected±(tol+1) → false. → **NAPRAWIONE (cykl 1):** dodano `test_width_at_min_boundary_is_valid` (800), `test_width_at_max_boundary_is_valid` (2200), `test_period_at_upper_tolerance_boundary_is_valid` (expected+tol), `test_period_at_lower_tolerance_boundary_is_valid` (expected-tol), `test_period_one_us_past_tolerance_is_invalid` (expected+tol+1 → false).
- [ ] 🟡 [nit] **components/rc_capture/src/rc_capture.c:130** — torn read w `rc_capture_read` (kopia 16-bajtowej struktury współbieżnie z ISR może zmieszać pola). Świadomie udokumentowany "best-effort snapshot" + downstream debounce; utwardzić przy integracji pętli (seqlock / krótkie disable IRQ).
- [ ] 🟡 [nit] **components/settings/src/settings_validate.c:122-166** — `nvs_error` zawsze `false` (własność warstwy NVS, Unit 8). Pole kontraktu bez realnego użycia/testu do Unit 8.
- [ ] 🟡 [nit] **components/settings/src/settings_validate.c:24-85** — `validate_fields` ~60 linii (> 50, coding-rules §1); płaska lista przypisań, rozważyć podział na grupy rc/servo/esc/safety jeśli przyrośnie.
- [ ] 🟡 [nit] **components/rc_capture/src/rc_capture.c:19** — typy PascalCase (`RcCaptureChannel`) vs konwencja ESP-IDF `snake_case_t` (kontynuacja nitu z fazy 0); potwierdzić jednolitą konwencję projektu.
- [ ] 🟡 [nit] **components/rc_capture/src/rc_capture.c:118-122** — brak deinit/rollback przy częściowym sukcesie init (timer/kanały nie zwalniane przy fail kanału 2); akceptowalne dla zasobu na całe życie urządzenia.

---

## Do poprawy po review fazy 2

Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI (P1=0, P2=2, P3=4). Raport: `review-faza-2.md`. Testy hosta 81/81 PASS; build idf.py EXIT=0 — oba na żywo. E2E: N/A (Unit 5 pure, brak UI).

- [x] 🟠 [important] **components/signal_chain/src/chain_math.c:59** — asymetria deadbandu przy off-center mid: `deadband_us_to_normalized` zwija deadband do bliższej (min) połowy, a `normalize_us` skaluje każdą połowę niezależnie. Przykład `rc_min/mid/max=1000/1300/2000`, deadband 80 µs → wyjście z deadbandu po 81 µs (niska strona) vs 188 µs (wysoka strona), 2,3× asymetria. Niepokryte testem (wszystkie testy używają wyśrodkowanego 1000/1500/2000). Liczyć deadband per-połowa spójnie z `normalize_us` albo udokumentować jako świadomą decyzję z uzasadnieniem.
- [x] 🟠 [important] **components/settings/src/settings_validate.c:107-120** — brak cross-field invariantu `esc_reverse_max < esc_neutral < esc_forward_max`. Config `esc_neutral=1600`, `esc_forward_max=1000` przechodzi walidację, a `map_normalized_to_us` (chain_math.c:77) daje ujemny span → odwrócone mapowanie (full-forward poniżej neutralu). Hard clamp utrzymuje bezpieczeństwo (output w [1000,2000]), ale mapowanie cicho błędne. Dodać invariant (domknąć w Unit 5/8).
- [ ] 🟡 [nit] **components/signal_chain/include/signal_chain.h:58,74** — niespójna numeracja kroków: nagłówek "Steps 3-10" vs inline "Step 9"+clamp vs enum-doc "step 7". Ujednolicić.
- [ ] 🟡 [nit] **components/signal_chain/include/signal_chain.h:80-83 + servo_chain.c:51** — kontrakt seed `slew_state` (µs, wartość center) vs `ramp_state` (znormalizowane, 0) tylko w prozie; oba `int32_t*`. Udokumentować wymaganą wartość seed serwa (center) przed integracją Unit 7 (ryzyko startowego transjentu jeśli zaseedowane 0).
- [ ] 🟡 [nit] **components/signal_chain/src/throttle_chain.c:11-12 + servo_chain.c:10-11** — zduplikowana stała okna SI-3 `1000/2000` w dwóch plikach; rozważyć wspólną definicję.
- [ ] 🟡 [nit] **test/host/test_throttle_chain.c** — brak testów off-center `rc_mid` (asymetrie deadband/reverse/limit niepokryte); drobny duplikat: `test_deadband_small_signal_maps_to_neutral` i `test_deadband_at_threshold_is_inclusive_neutral` oba używają 1580 µs.

---

## Źródła
- Requirements doc: docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md
- Plan techniczny: docs/plans/2026-06-16-001-feat-kayak-motor-firmware-v1-plan.md
