---
title: "feat: Firmware V1 — sterownik ESP32 silnika kajaka (RC passthrough + failsafe + panel WWW)"
type: feat
status: active
date: 2026-06-16
origin: docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md
---

# feat: Firmware V1 — sterownik ESP32 silnika kajaka

## Przegląd

Budujemy firmware ESP32, który wchodzi w tor sterowania między odbiornik RC a aktuatory
(serwo skrętu + bidirectional brushed ESC „WP880") silnika trollingowego na kajaku. Firmware
czyta kanały RC, przetwarza sygnał (deadband, slew/serwo, soft-start/soft-stop gazu, limit mocy,
kalibracja), realizuje 4-stanową maszynę bezpieczeństwa (DISARMED / ARMED / FAILSAFE /
ESC_CALIBRATION), trwale przechowuje parametry w NVS i wystawia panel WWW przez WiFi AP
(WPA2-PSK) do podglądu live i strojenia.

Priorytet: **bezpieczeństwo i przewidywalność**. Plan jest świadomie *safety-first*: hard clamp
wyjścia jako nieobchodzalna granica (SI-3), boot zawsze do stanu bezpiecznego (SI-1), failsafe
ustalony (R6), pętla sterująca jako jedyny pisarz parametrów (SI-6).

Stack (rozstrzygnięty w planowaniu, na bazie researchu): **ESP-IDF 5.5.x** (idf.py/CMake/Kconfig),
**MCPWM capture** na wejście RC, **LEDC** na wyjście serwo/ESC, **raw NVS** (versioned blob +
CRC32), **esp_http_server + WebSocket**, **WPA2-PSK SoftAP**, **esp_task_wdt** w trybie panic +
brownout detector. Logika sterowania/bezpieczeństwa wydzielona do framework-agnostic modułów C
testowalnych na hoście (Unity / target `linux`).

## Ujęcie problemu

Surowy sygnał RC jest zbyt gwałtowny i nie ma żadnego zabezpieczenia przy utracie sygnału na
otwartej wodzie. ESP32 jest jedynym punktem toru sterowania (R1) → pojedynczy punkt awarii.
Ochrona w V1 jest **programowa** (watchdog recovery + boot do bezpiecznego stanu + clamp)
uzupełniona o **fizyczny e-stop** (R13, sprzęt). Najgroźniejszy tryb awarii to **zamrożony
poprawny PWM** (ESC widzi zdrowy sygnał i nie odpala własnego failsafe) — groźniejszy niż utrata
PWM (zob. źródło: requirements §Kluczowe decyzje p.2).

**Ustalenie krytyczne z researchu (rozszerza założenia źródła):** wyjścia ESP32 są w stanie
wysokiej impedancji (Hi-Z, bez wewnętrznego pull) podczas ~okna martwego bootu/resetu/brownoutu, a
przy zapadaniu napięcia (brownout-sag) rdzeń może wyemitować śmieciowy impuls **zanim** reset się
wyzwoli. Dlatego realną gwarancją bezpieczeństwa w tym oknie są **elementy sprzętowe** (pull-down
na GPIO18/19 + własny failsafe ESC przy utracie sygnału), a warstwy programowe (boot-do-DISARMED,
watchdog, pojedynczy clamp) siedzą na tej gwarancji. To musi trafić do BOM i Planu pomiarów.

## Śledzenie wymagań

- **R1.** Passthrough RC przez ESP32 (CH1 G34, CH2 G35 → serwo G18, ESC G19) → Unit 3, 7, 2
- **R2.** Model gazu dwukierunkowy, środek = stop, neutral *zmierzony* → Unit 5
- **R3.** Slew limiting serwa → Unit 5
- **R4.** Soft-start / soft-stop gazu (osobne rampy ↑/↓) → Unit 5
- **R5.** Deadband gazu (na targecie, przed skalowaniem/rampą) → Unit 5
- **R6.** Failsafe przy utracie RC (ustalony, serwo do centrum, ESC soft-stop do neutralu) → Unit 6, 5
- **R7.** Arming z wymogiem neutralu (bramkuje tylko ESC) → Unit 6
- **R8.** Sygnalizacja stanu: LED + panel → Unit 11, 10
- **R9.** Panel WWW po WiFi AP — podgląd live → Unit 10
- **R10.** Edycja parametrów z UI — tylko przy DISARMED → Unit 4, 8, 10
- **R11.** Trwały zapis ustawień (NVS) → Unit 8
- **R12.** CH4 — czytaj i pokaż, poza predykatem RC_valid → Unit 3, 4, 10
- **R13.** Fizyczny kill-switch (sprzęt) → BOM/Ryzyka + zachowanie FAILSAFE (Unit 6); brak kodu
- **R14.** AP = WPA2-PSK, nigdy otwarty → Unit 10
- **R15.** ESC_RANGE_CALIBRATION jako osobny stan, krokowy, z abortem → Unit 9
- **R16.** Bootstrap/walidacja NVS → bezpieczne defaulty, walidacja przy odczycie, flagi telemetrii → Unit 4, 8
- **R17.** Bramka zapisu = DISARMED, delayed commit, pętla = jedyny właściciel → Unit 7, 8, 10
- **SI-1.** Reset = stan bezpieczny → Unit 2, 6, 7
- **SI-2.** Neutral firmware ⇒ fizycznie zero ciągu (zmierzony neutral) → Unit 2 + Plan pomiarów
- **SI-3.** Hard clamp — żaden code path go nie omija → Unit 2 (egzekwowane we wszystkich)
- **SI-4.** Utrata CH1 lub CH2 → FAILSAFE → Unit 4, 6
- **SI-5.** ESC_CALIBRATION nigdy automatycznie → Unit 9
- **SI-6.** Pętla = jedyny pisarz aktywnych parametrów → Unit 7, 8

## Granice scope'u

- Brak logiki trybów na CH4 — tylko odczyt/wyświetlanie (V2).
- Brak chmury / internetu / OTA — wyłącznie lokalny AP WPA2.
- Brak logowania / kont / multi-user na panelu (ale AP nie otwarty).
- Brak GPS / autopilota / trzymania kursu / logowania telemetrii na SD.
- Brak sprzętowego bypassu sygnału PWM (programowy failsafe + fizyczny e-stop).
- Niezależne zasilanie odbiornika (żywe RC po e-stopie) — poza V1.
- E-stop (R13) i tap zasilania ESP32 przed kill-switchem to **sprzęt/BOM**, nie kod — plan tylko
  surfuje wymóg i jego konsekwencję (po e-stopie odbiornik martwy → ESP32 wchodzi w FAILSAFE i to
  raportuje, bez żywych wejść RC).

## Kontekst i research

### Relevantny kod i wzorce

- **Projekt greenfield** — brak istniejącego kodu, brak `docs/solutions/`. Konwencje zakładamy od
  zera: layout ESP-IDF (komponenty), pliki C w snake_case (konwencja frameworka wymusza —
  nadrzędne wobec reguły kebab-case), pliki źródłowe < 300 linii, funkcje < 50 linii, early return
  zamiast nestingu > 2 (reguły projektu z `.claude/rules/coding-rules.md`).
- **Reguły front-endu (React/Vite/Zod) NIE dotyczą firmware C** — dotyczą wyłącznie ewentualnego
  panelu; wybrany panel jest vanilla JS, więc reguły front-endu stosują się tylko częściowo
  (struktura, nazewnictwo handlerów `handle*`, brak `console.log` w produkcji).

### Wiedza instytucjonalna

- Brak `docs/solutions/`. Ten plan + jego wykonanie powinny wyprodukować pierwsze wpisy (Plan
  pomiarów WP880/ESP32/odbiornik to naturalni kandydaci do `/dev-compound`).

### Referencje zewnętrzne

- **RC input:** MCPWM capture (`driver/mcpwm_cap.h`) — sprzętowy edge-latch licznika (12.5 ns na
  klasycznym ESP32, 80 MHz APB), odporny na latencję ISR/WiFi/flash; 6 wejść capture (3 kanały w
  jednej grupie). Walidacja wzorowana na maszynie `NO_SIGNAL→UNSTABLE→STABLE` (rewegit/esp32-rmt-pwm-reader).
  [ESP-IDF MCPWM](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/mcpwm.html)
- **PWM out:** LEDC, **16-bit @ 50 Hz** (0.305 µs/LSB; 1000/1500/2000 µs = duty 3277/4915/6554;
  dzielnik 80e6/(50×65536) bez błędu częstotliwości). Zmiana **tylko duty** w locie
  (`ledc_set_duty_and_update` — latch na granicy okresu, glitch-free); nigdy nie zmieniać
  freq/resolution w locie. GPIO18/19 nie są pinami strapping (OK).
  [ESP-IDF LEDC](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html)
- **Boot/reset:** GPIO Hi-Z w oknie martwym (~300 ms, do zmierzenia); LEDC nie utrzymuje wyjścia
  przez reset. Konieczne **zewnętrzne pull-down ~10 kΩ na GPIO18/19** + failsafe sygnałowy ESC.
- **Failsafe (wzorce ArduPilot Rover/boat):** serwo → centrum (nie hold-last), gaz → soft-stop do
  neutralu (200–500 ms) i trzymaj, **latched** (bez auto-resume), wyjście tylko przez re-arm z
  warunkiem neutralu. Detekcja: N kolejnych złych/brakujących ramek (~5–10 ≈ 100–200 ms).
  [ArduPilot Rover Failsafes](https://ardupilot.org/rover/docs/rover-failsafes.html)
- **NVS:** wbudowane CRC32 + wear-leveling + atomowość. **Versioned struct blob + app-level CRC32**
  (`{u16 version; …; u32 crc32}`), `schema_version` jako pierwsze pole; mismatch/empty/corrupt →
  defaulty (preferuj reload-defaults nad migracją w safety-critical). Walidacja **przy odczycie**
  (NVS gwarantuje integralność bajtów, nie sensowność wartości). Dedykowana partycja `appcfg`
  (osobno od `nvs` współdzielonej z WiFi/PHY). Wzorzec init-recovery: `NO_FREE_PAGES /
  NEW_VERSION_FOUND → erase → re-init`.
  [ESP-IDF NVS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html)
- **Współdzielenie parametrów (SI-6):** Single-Writer + publikacja. Rekomendacja:
  **length-1 FreeRTOS „mailbox"** (`xQueueOverwrite` z WWW; `xQueuePeek`/non-blocking receive w
  pętli) — kopia całej struktury jako jednostka, brak torn-read, zero ręcznego memory-ordering.
  Alternatywa zero-copy: double-buffer + atomic pointer swap (C11 `<stdatomic.h>`, release/acquire;
  **nie** `volatile`). Config w wewnętrznym RAM, nie PSRAM. Apply przy granicy DISARMED/top-of-cycle.
- **Watchdog/brownout:** `esp_task_wdt` z `trigger_panic=true` (domyślnie tylko warn!), timeout
  ~500 ms–1 s (2–3× cyklu), **karmiony wyłącznie z ukończonej iteracji pętli** (nigdy z
  timera/ISR/heartbeat — to maskuje zawis). `esp_reset_reason()` tylko **logować**, nigdy nie
  bramkować nim stanu bezpiecznego. Brownout enabled (Kconfig), to ochrona *po fakcie*, nie
  gwarancja czystego wyjścia.
  [ESP-IDF Watchdogs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html)
- **AP/Web:** `WIFI_AUTH_WPA2_PSK`, hasło 8–63 znaków, kanał stały (1/6/11), `max_connection` 1–2;
  **assert przy boocie, że AP rozwiązał się do non-OPEN i fail-fast** (puste hasło → cicho OPEN).
  Serwer: `esp_http_server` + `CONFIG_HTTPD_WS_SUPPORT`. Telemetria: **WebSocket ~10 Hz** (5–20),
  producent lossy/non-blocking (jeden slot najnowszego snapshotu, drop gdy klient zostaje w tyle),
  push z innego tasku przez `httpd_queue_work()`. Bez TLS (WPA2 szyfruje link; TLS = ryzyko
  fragmentacji sterty). Egzekwuj edit-only-when-DISARMED **w firmware** (409 gdy ARMED), re-walidacja
  server-side, re-check DISARMED w momencie apply (TOCTOU). JSON: ArduinoJson v7 lub cJSON (w IDF).
  [esp_http_server](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html)

## Kluczowe decyzje techniczne

- **Framework = ESP-IDF 5.5.x (idf.py/CMake/Kconfig).** Najlepsza kontrola TWDT panic, brownout,
  MCPWM capture, kodów błędów NVS; logika host-testowalna. (decyzja użytkownika, potw. researchem)
- **RC input = MCPWM capture.** Sprzętowy edge-latch → determinizm niezależny od WiFi/flash.
  (decyzja użytkownika)
- **Panel = vanilla HTML/CSS/JS wbudowany w flash (EMBED_FILES).** Zero build-stepu, mały footprint,
  łatwy audyt. (decyzja użytkownika)
- **Architektura warstwowa:** *pure logic* (framework-agnostic C, host-testowalne) oddzielone od
  *HAL adapters* (MCPWM/LEDC/NVS/WiFi/HTTP/WDT). Granica = struktury danych (raw sample → validity →
  target → output), bez wołania peryferiów z logiki. Pozwala testować łańcuchy sygnału, walidację i
  maszynę stanów na hoście (reguła testów projektu + parytet integracyjny).
- **Single-writer parametrów (SI-6) = mailbox length-1 (`xQueueOverwrite`).** Prostsze i bez
  torn-read; double-buffer/atomic odłożony jako optymalizacja tylko jeśli pomiar pokaże jitter.
- **Hard clamp (SI-3) = jeden moduł, ostatni etap obu łańcuchów.** Każdy code path (RC, failsafe,
  kalibracja, override, skorumpowane NVS) przechodzi przez tę samą funkcję przed pinem.
- **Jedna pętla ~50 Hz, bez RTOS-tasków sterujących** domyślnie; pinning Core0(WiFi)/Core1(loop)
  **tylko jeśli** pomiar pokaże jitter/starvation. WWW/WiFi i tak na Core0.
- **NVS = versioned blob + CRC32 na dedykowanej partycji, walidacja przy odczycie, reload-defaults
  zamiast migracji.** Commit opóźniony (debounce 2–5 s) lub force „Save".
- **RC_valid liczony z CH1 AND CH2, z okresem ZMIERZONYM** (nie hardcode 20 ms/50 Hz); CH4 poza
  predykatem (R12, SI-4).

## Otwarte pytania

### Rozwiązane podczas planowania

- *Framework/build?* → ESP-IDF 5.5.x + idf.py/CMake (użytkownik).
- *Odczyt RC?* → MCPWM capture (użytkownik).
- *Stack panelu?* → vanilla JS embedded (użytkownik).
- *Transport telemetrii?* → WebSocket ~10 Hz, producent lossy (research).
- *Współdzielenie params WWW↔pętla bez mutexów?* → mailbox length-1 (`xQueueOverwrite`) (SI-6).
- *Schemat NVS?* → versioned blob + CRC32, dedykowana partycja, walidacja przy odczycie,
  reload-defaults zamiast migracji.
- *Wzory migania LED (R8 „do ustalenia")?* → propozycja w Unit 11 (DISARMED wolne, ARMED solid,
  FAILSAFE szybkie, CALIBRATION double-blink, UNCALIBRATED nakładka).

### Odroczone do implementacji / Planu pomiarów (świadomie)

- **Zmierzony neutral i pasmo stop WP880**, progi startu przód/tył, `escNeutralUs` realne — z Planu
  pomiarów; do tego czasu konserwatywny default (1500 µs w oknie sanity 1400–1600). (SI-2)
- **Czy potrzebny `reverseNeutralDwellMs`** — dodać parametr **tylko** jeśli pomiar szybkiej zmiany
  przód↔tył pokaże cutout/plugging/szarpnięcie (R4, źródło §Plan pomiarów p.7).
- **Zmierzony okres ramki odbiornika** + tolerancja w `RC_valid` — z pomiaru (nie zakładać 20 ms).
- **Długość okna martwego boot** (reset → re-init LEDC) jako liczba; **zachowanie pinów w brownout-sag**
  — z pomiaru oscyloskopem; wpływa na dobór pull-down i ocenę ryzyka.
- **Czy WP880 wymaga własnej kalibracji throttle-range** i jak współgra z R15 — z pomiaru.
- **Pinning Core0/Core1** — tylko jeśli zmierzony jitter/starvation pętli.
- **Konkretne zakresy walidacji każdego parametru** na granicy zapisu/odczytu — dopięte przy
  implementacji modelu (Unit 4), zaczynając od defaultów konserwatywnych z R16.
- **Dokładny próg N kolejnych złych ramek** dla failsafe — dostrojony po pomiarze okresu (start ~5–10).

## Implementation Units

> Postawa wykonawcza: kod safety-critical. Dla modułów *pure logic* (clamp, łańcuchy sygnału,
> walidacja NVS, maszyna stanów, predykat RC_valid) preferuj **test-first** — kontrakt i edge-case'y
> da się w pełni opisać testem hosta przed implementacją. Adaptery HAL i integracja weryfikowane na
> sprzęcie (oscyloskop / obserwacja ESC) zgodnie z Planem pomiarów.

### Faza 0 — Fundament i bezpieczne wyjście

- [ ] **Unit 1: Szkielet projektu ESP-IDF + harness testów hosta + konfiguracja bezpieczeństwa**

**Cel:** Zbudować strukturę projektu ESP-IDF z komponentami, dedykowaną partycją NVS, host-testowym
buildem (Unity / target `linux`) i `sdkconfig.defaults` ustawiającym bezpieczne domyślne (TWDT
panic ON, brownout ON, WS support, WiFi).

**Wymagania:** baza pod wszystkie; bezpośrednio R16 (partycja), SI-1 (sdkconfig watchdog/brownout).

**Zależności:** brak.

**Pliki:**
- Stwórz: `CMakeLists.txt` (root), `sdkconfig.defaults`, `partitions.csv` (dodaj partycję `appcfg`
  typu data/nvs, osobno od `nvs`), `main/CMakeLists.txt`, `main/app_main.c` (na razie tylko boot do
  bezpiecznego stanu — placeholder wywołujący Unit 2)
- Stwórz: `components/` (puste komponenty z `CMakeLists.txt`: `safety_clamp`, `pwm_out`, `rc_capture`,
  `rc_validity`, `settings`, `signal_chain`, `state_machine`, `control_loop`, `web_panel`, `led_status`)
- Stwórz: host-test harness — `test/host/CMakeLists.txt` + integracja Unity dla komponentów pure
  (alternatywnie target `linux`); plik `README` z komendami build/flash/test (opisowo, nie skrypt)
- Stwórz: `.gitignore` (build/, sdkconfig lokalny), `docs/hardware/bom-and-measurements.md`
  (szkielet: pull-downy G18/G19, tap 12V przed kill-switchem, decoupling, lista pomiarów ze źródła)

**Podejście:**
- Komponenty *pure logic* nie linkują do ESP-IDF driverów — kompilowalne na hoście.
- `sdkconfig.defaults`: `CONFIG_ESP_TASK_WDT_PANIC=y`, `CONFIG_ESP_TASK_WDT_EN=y`, brownout enabled +
  poziom do ustalenia po pomiarze, `CONFIG_HTTPD_WS_SUPPORT=y`. Pinować dokładny tag IDF.
- BOM doc surfuje ustalenie z researchu: bez pull-downów i failsafe ESC firmware nie pokrywa okna
  martwego — to zadanie sprzętowe, nie programowe.

**Wzorce do naśladowania:** standardowy layout ESP-IDF (`main` + `components/*` + `partitions.csv`).

**Scenariusze testowe:**
- [Unit] Host build kompiluje pusty komponent pure i przechodzi trywialny test Unity (dowód, że
  harness hosta działa).
- [E2E/HW] `idf.py build` przechodzi; flash bootuje; w logu widać reset reason i wejście do
  bezpiecznego placeholdera (oscyloskop pomiar w Unit 2).

**Weryfikacja:** projekt buduje się dla targetu ESP32 i dla hosta; partycja `appcfg` widoczna w
tablicy partycji; testy hosta uruchamialne jedną komendą.

---

- [ ] **Unit 2: Warstwa wyjścia (LEDC) + HARD CLAMP + bezpieczny boot**

**Cel:** Zaimplementować wyjście serwo/ESC przez LEDC (16-bit @ 50 Hz) oraz **jedyny, nieobchodzalny
hard clamp** (SI-3). Boot pisze neutral/centrum **przed czymkolwiek innym** (SI-1). To fundament
bezpieczeństwa — implementowany przed jakimkolwiek sterowaniem.

**Wymagania:** R1 (wyjścia), SI-1, SI-2 (struktura; wartość neutralu z pomiaru), SI-3.

**Zależności:** Unit 1.

**Pliki:**
- Stwórz: `components/safety_clamp/include/safety_clamp.h`, `components/safety_clamp/src/safety_clamp.c`
  (pure: `clamp_pwm_us(value, window)` — bezwarunkowe ograniczenie do okna sanity)
- Stwórz: `components/safety_clamp/test/test_safety_clamp.c` (host)
- Stwórz: `components/pwm_out/include/pwm_out.h`, `components/pwm_out/src/pwm_out.c` (HAL: init LEDC,
  `pwm_out_write_us(channel, us)` — woła clamp wewnętrznie przed konwersją na duty)
- Stwórz: `components/pwm_out/src/pwm_us_to_duty.c` (+ `.h`) — **pure** konwersja µs→duty (testowalna
  na hoście, bez LEDC)
- Stwórz: `components/pwm_out/test/test_pwm_us_to_duty.c` (host)
- Modyfikuj: `main/app_main.c` (najpierw init pwm_out + write neutral/center, potem reszta)

**Podejście:**
- `pwm_out_write_us` **musi** przejść przez `clamp_pwm_us` — to jedyna droga do pinu. Żaden inny
  moduł nie woła LEDC bezpośrednio (egzekwowane przez to, że tylko `pwm_out` linkuje driver LEDC).
- Konwersja µs→duty czysta: `duty = (us << 16) / 20000`. 1000/1500/2000 → 3277/4915/6554.
- Boot: konfiguracja LEDC z initial duty = neutral, write neutral/center na samej górze `app_main`.
- Okno PWM (sanity) i neutral z konfiguracji/defaultów — clamp nie zna stanu, tylko granice.

**Notatka wykonawcza:** test-first dla `clamp_pwm_us` i `pwm_us_to_duty` — kontrakt w pełni opisany
edge-case'ami (poniżej min, powyżej max, dokładnie na granicy, neutral).

**Wzorce do naśladowania:** `ESP32Servo` (16-bit/50 Hz), `ledc_set_duty_and_update` (glitch-free).

**Scenariusze testowe:**
- [Unit] `clamp_pwm_us`: value < min → min; value > max → max; value w oknie → bez zmian; na granicy
  → granica (inclusive).
- [Unit] `pwm_us_to_duty`: 1000/1500/2000 µs → 3277/4915/6554; wartości spoza okna **i tak**
  najpierw zclampowane (dowód, że clamp jest przed konwersją).
- [HW/Plan pomiarów] Oscyloskop: po boocie GPIO18/19 emitują neutral/centrum; zmierzyć długość okna
  martwego (reset→pierwszy impuls); zachowanie przy watchdog-resecie i brownout-sag; sprawdzić
  działanie pull-downów.

**Weryfikacja:** testy hosta przechodzą; na sprzęcie po boocie ESC widzi neutral (śmigło zdjęte),
serwo w centrum; brak code-path do LEDC z pominięciem clamp (audyt: tylko `pwm_out` linkuje driver).

### Faza 1 — Akwizycja sygnału i ważność RC

- [ ] **Unit 3: Wejście RC (MCPWM capture) + odczyt CH4**

**Cel:** Czytać 3 kanały RC (CH1 G34, CH2 G35, CH4 G32) przez MCPWM capture, produkując surowe,
sprzętowo-stemplowane próbki: szerokość impulsu + zmierzony okres ramki na kanał.

**Wymagania:** R1 (wejścia), R12 (CH4 odczyt), fundament SI-4.

**Zależności:** Unit 1.

**Pliki:**
- Stwórz: `components/rc_capture/include/rc_capture.h` (typy: `rc_channel_sample { width_us,
  period_us, last_edge_us, edge_seen }`), `components/rc_capture/src/rc_capture.c` (HAL: MCPWM cap
  timer + 3 kanały, callback `on_cap` liczy szerokość/okres z ticków 12.5 ns)
- Modyfikuj: `main/app_main.c` (init capture)
- Test (HW): brak host-unitu dla samego HAL; logika pomiaru szerokości/okresu z ticków wydzielona do
  `components/rc_capture/src/cap_math.c` (+`.h`, pure) i `components/rc_capture/test/test_cap_math.c`

**Podejście:**
- Callback przelicza ticki na µs i aktualizuje strukturę próbki (per kanał). Brak przetwarzania
  logiki ważności tutaj — to Unit 4.
- CH4 czytany identycznie, ale oznaczony jako diagnostyczny (poza RC_valid).
- 3 kanały mieszczą się w jednej grupie MCPWM (6 wejść capture dostępnych).

**Wzorce do naśladowania:** `driver/mcpwm_cap.h` (new_capture_timer → channel → register callbacks →
start); maszyna `NO_SIGNAL→UNSTABLE→STABLE` (logika w Unit 4).

**Scenariusze testowe:**
- [Unit] `cap_math`: konwersja ticków→µs (12.5 ns/tick); liczenie okresu z dwóch kolejnych zboczy;
  obsługa przepełnienia licznika.
- [HW] Oscyloskop równolegle z odczytem firmware: zmierzona szerokość/okres zgadza się z sygnałem
  odbiornika; **zmierzony okres ramki** zapisany do Planu pomiarów (próg okresu w R4/RC_valid).

**Weryfikacja:** firmware raportuje (log/telemetria) sensowne szerokości i okres dla CH1/CH2/CH4;
wartości zgodne z oscyloskopem.

---

- [ ] **Unit 4: Predykat ważności RC + model ustawień (defaulty + walidacja przy odczycie)**

**Cel:** (a) Pure predykat `channel_valid` / `RC_valid` (edge w ostatnich T ms AND szerokość ∈
[800,2200] AND okres ≈ zmierzony ± tolerancja), z debounce N kolejnych ramek; (b) model parametrów:
typy, **bezpieczne defaulty**, walidacja przy odczycie z per-field fallback i flagami telemetrii
(R16).

**Wymagania:** SI-4, R12 (CH4 poza predykatem), R16, baza pod R10.

**Zależności:** Unit 1 (typy współdzielone). Niezależny od HAL — pure.

**Pliki:**
- Stwórz: `components/rc_validity/include/rc_validity.h`, `.../src/rc_validity.c` (pure:
  `channel_valid(sample, cfg)`, `rc_valid(ch1, ch2)`)
- Stwórz: `components/rc_validity/test/test_rc_validity.c` (host)
- Stwórz: `components/settings/include/settings_model.h` (struktura parametrów: slew serwa, rampy
  ESC ↑/↓, deadband gazu, **deadband steru** default 0, timeout failsafe, limit mocy ±, endpointy
  serwa, kalibracja wejścia RC min/mid/max/kanał, flagi reverse, **kalibracja wyjścia ESC**
  `escNeutralUs/escForwardMinUs/escReverseMinUs/escForwardMaxUs/escReverseMaxUs/escNeutralBandUs`,
  warunkowo `reverseNeutralDwellMs`, `schema_version`)
- Stwórz: `components/settings/src/settings_defaults.c` (konserwatywne defaulty z R16: CH
  1000/1500/2000, szeroki deadband, niski max throttle, łagodne rampy, `escNeutralUs` 1400–1600)
- Stwórz: `components/settings/src/settings_validate.c` (+`.h`, pure: walidacja per-field +
  cross-field invariants; zwraca `settings_source`, `settings_valid`, `calibrated`, `defaults_used`,
  `nvs_error`)
- Stwórz: `components/settings/test/test_settings_validate.c` (host)

**Podejście:**
- `RC_valid := channel_valid(CH1) AND channel_valid(CH2)`; CH4 celowo poza. Okres oczekiwany =
  **zmierzony** (z konfiguracji po pomiarze), tolerancja hojna — NIE hardcode 50 Hz.
- Debounce: failsafe dopiero po N kolejnych nieważnych ramkach (~5–10, dostrojone po pomiarze);
  nigdy po jednym złym impulsie, ale nigdy nie rozszerzać pasma akceptacji żeby „naprawić" glitch.
- Walidacja zaczyna od struktury z bezpiecznymi defaultami, czyta zapisane, per-field zakres →
  fallback do defaultu; potem invarianty między polami. Wynik: flagi do telemetrii. Arming dozwolony
  na defaultach z flagą UNCALIBRATED (`calibrated=false`).

**Notatka wykonawcza:** test-first — predykat ważności i walidacja to czysty kontrakt z bogatymi
edge-case'ami; opisać testami przed implementacją.

**Wzorce do naśladowania:** maszyna `NO_SIGNAL→UNSTABLE→STABLE`; „validate-on-read, nigdy śmieci".

**Scenariusze testowe:**
- [Unit] `channel_valid`: brak edge w T ms → false; szerokość 700/2300 µs → false; okres poza
  tolerancją → false; ważny impuls → true.
- [Unit] `rc_valid`: CH1 zły → false; CH2 zły → false; oba dobre → true; CH4 zły → bez wpływu.
- [Unit] debounce: 1 zła ramka → wciąż valid; N kolejnych złych → invalid; powrót dobrej → reset
  licznika.
- [Unit] walidacja: empty/defaulty → `settings_source=DEFAULTS`, `defaults_used=true`,
  `calibrated=false`; jedno pole spoza zakresu → fallback tego pola, `MIXED_RECOVERED`; wartości OK
  → `NVS`, `settings_valid=true`.
- [Unit] cross-field: `escForwardMin > escForwardMax` → odrzucone/fallback.

**Weryfikacja:** testy hosta przechodzą; utrata CH1 *lub* CH2 (symulowana próbka) → `RC_valid=false`
po N ramkach; defaulty nigdy nie produkują wartości spoza okna sanity.

### Faza 2 — Łańcuchy przetwarzania sygnału

- [ ] **Unit 5: Łańcuchy gazu i serwa (pure)**

**Cel:** Zaimplementować oba łańcuchy przetwarzania ze źródła: gaz (normalizacja → deadband →
reverse → limit mocy → TARGET → rampa soft-start/stop → mapowanie wyjścia ESC) i serwo (normalizacja
→ deadband → reverse → endpointy → TARGET → slew → mapowanie). Override stanu działa na **target**;
clamp (Unit 2) zawsze ostatni.

**Wymagania:** R2, R3, R4, R5, R10 (parametry strojone), wsparcie R6 (rampa do neutralu/centrum).

**Zależności:** Unit 2 (clamp), Unit 4 (model parametrów). Pure — bez HAL.

**Pliki:**
- Stwórz: `components/signal_chain/include/signal_chain.h`
- Stwórz: `components/signal_chain/src/throttle_chain.c` (kroki 1–9 łańcucha gazu, zwraca µs przed
  clampem; clamp dokłada warstwa wyjścia)
- Stwórz: `components/signal_chain/src/servo_chain.c` (łańcuch serwa)
- Stwórz: `components/signal_chain/src/ramp.c` (+`.h`, rampa/slew — osobne tempa ↑/↓ dla gazu,
  symetryczne dla serwa; krok per cykl, dąży do targetu)
- Stwórz: `components/signal_chain/test/test_throttle_chain.c`, `test_servo_chain.c`, `test_ramp.c`
  (host)

**Podejście:**
- **Kolejność stała (ze źródła §Łańcuchy):** deadband na targecie **przed** skalowaniem/rampą;
  reverse **po** deadbandzie (neutral zostaje neutralem); limit mocy **przed** rampą; rampa dąży do
  już ograniczonego targetu; override na target (nie na bezpośredni output) → rampa realizuje płynne
  przejście do neutralu/centrum.
- Przejście przód↔tył samą rampą; `reverseNeutralDwellMs` tylko warunkowo (po pomiarze).
- Funkcje czyste: wejście = znormalizowany sygnał + stan poprzedni rampy + parametry; wyjście =
  nowy stan rampy + wartość µs. Bez globalnego stanu, bez I/O.

**Notatka wykonawcza:** test-first — kolejność etapów i własności (deadband-przed-skalowaniem,
reverse-po-deadbandzie, monotoniczność rampy) to dokładnie to, co chroni test.

**Wzorce do naśladowania:** soft-start/stop ramp (ArduPilot), deadband ± wokół środka bidirectional.

**Scenariusze testowe:**
- [Unit] deadband gazu: mały sygnał wokół środka → target 0; tuż za deadbandem → niezerowy.
- [Unit] reverse po deadbandzie: neutral z reverse=on → wciąż neutral.
- [Unit] limit mocy: target za limitem → ograniczony do limitu *przed* rampą.
- [Unit] rampa: skok targetu 0→max → wyjście rośnie krok-po-kroku z `rampUp`; max→0 z `rampDown`;
  osobne tempa ↑/↓; rampa nigdy nie przeskakuje targetu.
- [Unit] override target: stan FAILSAFE → target=0 → rampa schodzi soft-stopem do neutralu (nie skok).
- [Unit] slew serwa: duży skok CH1 → ograniczona prędkość zmiany; FAILSAFE → target=center, slew do
  środka.
- [Unit] endpointy serwa: wartości poza endpointami → ograniczone do min/max kąta.

**Weryfikacja:** testy hosta przechodzą; symulacja ciągu cykli pokazuje obserwowalne rampy i slew;
neutral po reverse zostaje neutralem.

### Faza 3 — Maszyna stanów i pętla sterująca

- [ ] **Unit 6: Maszyna stanów (DISARMED/ARMED/FAILSAFE/ESC_CALIBRATION) + arming + failsafe (pure)**

**Cel:** Czysta maszyna 4 stanów z regułami przejść, bramką arming (R7), ustalonym failsafe (R6) i
regułą serwa niezależną od arming. ESC_CALIBRATION jako pełnoprawny stan (szczegóły sekwencji w
Unit 9; tu tylko przejścia/guardy).

**Wymagania:** R6, R7, SI-1, SI-4; szkielet R15/SI-5.

**Zależności:** Unit 4 (RC_valid + flagi), Unit 5 (target override per stan). Pure.

**Pliki:**
- Stwórz: `components/state_machine/include/state_machine.h` (`enum state`, `struct sm_inputs`
  {rc_valid, throttle_neutral, calib_in_progress, settings_apply_in_progress, ui_arm_request,
  ui_calib_request, ui_cancel, timeout}, `struct sm_outputs` {state, throttle_target_mode,
  servo_target_mode})
- Stwórz: `components/state_machine/src/state_machine.c` (funkcja przejść — czysta:
  `(state, inputs) → (state, outputs)`)
- Stwórz: `components/state_machine/test/test_state_machine.c` (host)

**Podejście:**
- Boot → DISARMED bezwarunkowo (SI-1), niezależnie od reset reason (reset reason tylko log).
- DISARMED→ARMED guard (R7): `RC valid AND throttle neutral AND state!=ESC_CALIBRATION AND brak
  settings-apply-in-progress`. W DISARMED ESC=neutral niezależnie od drążka.
- Każdy stan z RC invalid → FAILSAFE (SI-4). FAILSAFE **ustalony**: trwa dopóki RC nieważne; wyjście
  **tylko** po odzyskaniu RC → DISARMED (nie po zakończeniu soft-stopu).
- Reguła serwa: `RC valid → serwo śledzi CH1`; `RC invalid → serwo centruje` — niezależna od arming.
- Override targetu (do Unit 5/7): DISARMED→throttle 0; FAILSAFE→throttle 0 (soft-stop), serwo center;
  ESC_CALIBRATION→throttle z sekwencji stałej.

**Notatka wykonawcza:** test-first — to serce bezpieczeństwa; każde przejście i guard opisane testem
przed implementacją (tabela przejść ze źródła §Maszyna stanów jako wyrocznia).

**Wzorce do naśladowania:** latched failsafe + re-arm-with-neutral (ArduPilot/PX4).

**Scenariusze testowe:**
- [Unit] boot → DISARMED (każdy reset reason).
- [Unit] DISARMED + wszystkie warunki arm → ARMED; brak neutralu → zostaje DISARMED; RC invalid →
  FAILSAFE.
- [Unit] ARMED + RC invalid → FAILSAFE; ARMED + ręczny disarm (jeśli dodany) → DISARMED.
- [Unit] FAILSAFE trwa przy RC wciąż invalid; RC valid → DISARMED (nie ARMED).
- [Unit] serwo: RC valid → śledzi; RC invalid → center, niezależnie od ARMED/DISARMED.
- [Unit] arming zablokowany gdy `calib_in_progress` lub `settings_apply_in_progress`.

**Weryfikacja:** testy hosta pokrywają całą tabelę przejść ze źródła; brak przejścia wyprowadzającego
z FAILSAFE bez RC valid.

---

- [ ] **Unit 7: Integracja pętli sterującej ~50 Hz + watchdog + single-writer**

**Cel:** Spiąć wszystko w jedną pętlę ~50 Hz: capture (Unit 3) → ważność (Unit 4) → maszyna stanów
(Unit 6) → łańcuchy (Unit 5) → wyjście+clamp (Unit 2). Pętla jest **jedynym pisarzem** aktywnych
parametrów (SI-6); odbiera pending z WWW przez mailbox. Watchdog karmiony **tylko** z ukończonej
iteracji.

**Wymagania:** R1, R6, R7, SI-1, SI-3, SI-6; integracja R2–R5.

**Zależności:** Unit 2, 3, 4, 5, 6.

**Pliki:**
- Stwórz: `components/control_loop/include/control_loop.h`, `.../src/control_loop.c` (orkiestracja
  cyklu; trzyma „active params"; non-blocking odbiór pending z mailboxa; apply tylko gdy DISARMED)
- Stwórz: `components/control_loop/src/loop_step.c` (+`.h`, **pure** krok logiczny:
  `(active_params, rc_samples, state, ramp_state, ui_events) → (new_state, servo_us, esc_us,
  telemetry)`) — testowalny na hoście bez timerów/HAL
- Stwórz: `components/control_loop/test/test_loop_step.c` (host)
- Modyfikuj: `main/app_main.c` (utworzenie mailboxa, start pętli, `esp_task_wdt_add` + reset co cykl)

**Podejście:**
- Rdzeń logiczny pętli (`loop_step`) czysty — integruje moduły bez I/O; HAL tylko na brzegach
  (czytanie próbek capture, pisanie pwm_out). To daje pokrycie integracyjne cross-layer na hoście.
- Apply pending: pętla woła non-blocking peek na mailboxie; jeśli jest pending **i** stan DISARMED →
  apply do active params (kopia całej struktury, bez torn-read) + zgłoś dirty (do Unit 8 commit).
  Inaczej pending czeka. Re-check DISARMED w momencie apply (TOCTOU).
- Watchdog: `esp_task_wdt_reset()` na końcu **udanej** iteracji; nigdy z osobnego heartbeatu.
- Domyślnie jedna pętla bez RTOS-tasków sterujących; pinning Core1 tylko jeśli pomiar pokaże jitter.

**Notatka wykonawcza:** `loop_step` test-first (integracja kontraktów modułów); pętla czasowa i WDT
weryfikowane na sprzęcie.

**Wzorce do naśladowania:** mailbox length-1 (`xQueueOverwrite`/`xQueuePeek`); WDT „feed only from
completed iteration".

**Scenariusze testowe:**
- [Unit] `loop_step` DISARMED: drążek gazu max → esc_us = neutral (bramka R7).
- [Unit] `loop_step` ARMED: gaz śledzi CH2 z rampą; serwo śledzi CH1 ze slew.
- [Unit] `loop_step` RC invalid → przejście FAILSAFE; esc soft-stop do neutralu; serwo do center.
- [Unit] apply pending tylko w DISARMED; w ARMED pending nie zmienia active params.
- [HW] Pomiar jittera pętli (czy ~50 Hz stabilne); zachowanie po watchdog-resecie (boot do DISARMED,
  neutral) — Plan pomiarów ESP32.

**Weryfikacja:** na sprzęcie serwo/ESC podążają za drążkami w ARMED, neutral w DISARMED; utrata RC →
obserwowalny soft-stop + serwo do środka + latch; WDT-reset wraca do DISARMED/neutral.

### Faza 4 — Trwałość parametrów

- [ ] **Unit 8: Persystencja NVS (versioned blob + CRC32, delayed commit, pending/apply)**

**Cel:** Trwały zapis/odczyt parametrów w dedykowanej partycji NVS jako versioned blob z app-level
CRC32; init-recovery; walidacja przy odczycie (deleguje do Unit 4); commit opóźniony (debounce 2–5 s)
lub force „Save". Egzekwuje bramkę zapisu = DISARMED (R17) na poziomie persystencji.

**Wymagania:** R11, R16, R17, SI-6.

**Zależności:** Unit 4 (model + walidacja), Unit 7 (dirty flag / stan DISARMED).

**Pliki:**
- Stwórz: `components/settings/src/nvs_store.c` (+ deklaracje w `settings_*`): `nvs_store_load()`
  (read blob → CRC check → walidacja Unit 4 → zwróć params + flagi), `nvs_store_commit()` (write
  blob + CRC), init-recovery (`NO_FREE_PAGES`/`NEW_VERSION_FOUND` → erase → re-init; rozdziel
  `NEW_VERSION_FOUND` jako alert)
- Stwórz: `components/settings/src/blob_codec.c` (+`.h`, **pure**: serializacja struktury ↔ blob,
  liczenie/weryfikacja CRC32, sprawdzenie `schema_version` i długości)
- Stwórz: `components/settings/test/test_blob_codec.c` (host)
- Stwórz: `components/settings/src/commit_debounce.c` (+`.h`, **pure**: logika debounce — kiedy
  flush; force vs timer) + `components/settings/test/test_commit_debounce.c`
- Modyfikuj: `main/app_main.c` (load przy boocie przed startem pętli), `components/control_loop`
  (wołanie commit gdy dirty + DISARMED + debounce upłynął)

**Podejście:**
- Blob: `{ uint16 schema_version; <pola>; uint32 crc32 }`, CRC po wszystkim oprócz CRC. Empty/corrupt/
  schema-mismatch → defaulty (reload-defaults zamiast migracji). Length-check na `nvs_get_blob`
  (`ESP_ERR_NVS_INVALID_LENGTH`).
- Commit **tylko** gdy DISARMED (R17) i opóźniony (debounce) — nie palić cykli flasha, nie lagować
  przy ruchu suwaka. „Własność akceptowana": zmiana zastosowana live ale niezacommitowana przepada
  przy power-cut — przy boocie wraca ostatnia dobra / default (oba bezpieczne).
- Pętla = jedyny pisarz active params (SI-6); NVS store nie mutuje active w locie — tylko ładuje przy
  boocie i commituje migawkę.

**Notatka wykonawcza:** test-first dla `blob_codec` (round-trip, corrupt CRC, zła długość, zły
schema) i `commit_debounce`.

**Wzorce do naśladowania:** kanoniczny init-recovery NVS; single versioned blob + CRC.

**Scenariusze testowe:**
- [Unit] `blob_codec` round-trip: serialize → deserialize → równe; zła CRC → odrzucone; zła długość →
  odrzucone; inny `schema_version` → odrzucone (→ defaulty).
- [Unit] `commit_debounce`: zmiana → brak commitu przed upływem debounce; kolejna zmiana resetuje
  timer; force → natychmiast; brak zmian → brak commitu.
- [HW] Zapisz parametr w DISARMED → restart → wartość przetrwała; power-cut przed commitem → wraca
  ostatnia dobra/default; pusty/zepsuty NVS → praca na defaultach z flagą UNCALIBRATED.

**Weryfikacja:** testy hosta przechodzą; na sprzęcie parametry przeżywają restart; skorumpowany NVS
nigdy nie daje wartości spoza okna sanity.

### Faza 5 — Tryb serwisowy

- [ ] **Unit 9: ESC_RANGE_CALIBRATION (osobny stan, krokowy, z abortem)**

**Cel:** Pełnoprawny stan kalibracji WP880 **do wyjścia ESP32**: sekwencja krokowa sterowana z
panelu (Emit neutral → full forward → full reverse → Done), wystawiająca stałe **1500/2000/1000 µs**,
pomijająca mapping/limit/rampę ale **zachowująca hard clamp** (SI-3). Wejście tylko z DISARMED z
jawnym potwierdzeniem; aborty wg reguł.

**Wymagania:** R15, SI-3, SI-5.

**Zależności:** Unit 6 (przejścia stanu), Unit 7 (integracja w pętli), Unit 2 (clamp).

**Pliki:**
- Stwórz: `components/state_machine/src/esc_calibration.c` (+`.h`, **pure**: sekwencja kroków, mapowanie
  kroku → stałe µs, reguły abortu/timeoutu; `(calib_state, ui_event, rc_valid, timeout) →
  (calib_state, esc_us_override, exit_to_state)`)
- Stwórz: `components/state_machine/test/test_esc_calibration.c` (host)
- Modyfikuj: `components/control_loop/src/loop_step.c` (gdy state==ESC_CALIBRATION → esc_us z
  sekwencji, pomija łańcuch gazu, przez clamp), `components/state_machine/src/state_machine.c`
  (guard wejścia), `components/web_panel` (endpointy kroków + ostrzeżenie — w Unit 10)

**Podejście:**
- Wejście (R15/SI-5): `DISARMED AND RC valid AND throttle neutral AND jawna akcja UI AND potwierdzone
  ostrzeżenie „propeller removed / motor disconnected"`. **Nigdy** automatycznie.
- Sekwencja sterowana **z panelu** (operator słucha potwierdzeń WP880) — brak automatu na timerze.
  Każdy krok wystawia stałą wartość przez clamp.
- Abort: `RC invalid → FAILSAFE`; `timeout bezczynności → DISARMED + ESC neutral`; `user cancel →
  DISARMED + ESC neutral`.

**Notatka wykonawcza:** test-first — sekwencja i aborty to czysty automat; opisać testem.

**Wzorce do naśladowania:** procedura kalibracji radia ESC (neutral→full fwd→full rev), ale do
stałego wyjścia ESP32 (nie do odbiornika).

**Scenariusze testowe:**
- [Unit] wejście dozwolone tylko gdy wszystkie warunki + potwierdzenie; brak potwierdzenia → brak
  wejścia.
- [Unit] kroki: neutral→1500, full fwd→2000, full rev→1000, Done→DISARMED+neutral.
- [Unit] abort RC invalid → FAILSAFE; timeout → DISARMED+neutral; cancel → DISARMED+neutral.
- [Unit] wartości kroków i tak przechodzą przez clamp (dowód SI-3 w trybie serwisowym).
- [HW, śmigło zdjęte] Operator przechodzi sekwencję, WP880 potwierdza zakres.

**Weryfikacja:** testy hosta przechodzą; na sprzęcie (śmigło zdjęte) WP880 kalibruje się krokowo;
utrata RC w trakcie → FAILSAFE; nigdy nie startuje bez jawnego potwierdzenia.

### Faza 6 — Łączność, panel, sygnalizacja

- [ ] **Unit 10: WiFi SoftAP (WPA2-PSK) + serwer WWW + telemetria WS + API parametrów + panel**

**Cel:** Postawić AP WPA2-PSK (nigdy otwarty, assert non-OPEN), `esp_http_server` z WebSocketem
telemetrii (~10 Hz, lossy) i REST-owym API parametrów (walidacja na granicy, pending przez mailbox,
bramka DISARMED z 409, format `{ data, error: { code, message } }`), oraz wbudowany panel vanilla JS
(podgląd live CH1/CH2/CH4 + wyjścia + status/flagi R16, edycja przy DISARMED, kroki ESC_CALIBRATION).

**Wymagania:** R8 (panel), R9, R10, R12 (pokaż CH4), R14, R16 (flagi), R17, SI-6.

**Zależności:** Unit 4 (walidacja), Unit 6/9 (stan + kroki kalibracji), Unit 7 (mailbox pending +
telemetria), Unit 8 (commit).

**Pliki:**
- Stwórz: `components/web_panel/src/wifi_ap.c` (+`.h`, WPA2-PSK, kanał stały, max_connection 1–2,
  **assert authmode != OPEN i fail-fast**)
- Stwórz: `components/web_panel/src/http_server.c` (+`.h`, `esp_http_server`, rejestracja URI),
  `components/web_panel/src/ws_telemetry.c` (push snapshotu przez `httpd_queue_work`, jeden slot,
  drop gdy klient w tyle), `components/web_panel/src/params_api.c` (+`.h`, parsowanie/serializacja
  JSON, walidacja → pending → mailbox; 409 gdy nie DISARMED z kodem
  `SETTINGS_WRITE_REJECTED_NOT_DISARMED`)
- Stwórz: `components/web_panel/src/api_contract.c` (+`.h`, **pure**: budowa odpowiedzi
  `{data,error}`, mapowanie kodów błędów) + `components/web_panel/test/test_api_contract.c` (host)
- Stwórz: panel — `web/index.html`, `web/app.js`, `web/style.css`; embed przez `EMBED_FILES` w
  `components/web_panel/CMakeLists.txt`
- Modyfikuj: `main/app_main.c` (start AP + serwera po init pętli/NVS)

**Podejście:**
- Telemetria: WebSocket ~10 Hz, producent lossy/non-blocking (jeden slot najnowszego snapshotu).
  Snapshot zawiera: CH1/CH2/CH4 raw, wyjście serwo, wyjście ESC, stan (R8), flagi R16
  (`settings_source/settings_valid/calibrated/defaults_used/nvs_error`), status RC/failsafe/arm.
- API parametrów: WWW **tylko waliduje** (granica API) i zgłasza pending; pętla aplikuje (SI-6).
  Egzekwuj edit-only-when-DISARMED **w firmware** (nie tylko UI): 409 gdy ARMED/FAILSAFE/CALIBRATION.
  Re-walidacja server-side każdego pola. Format odpowiedzi `{ data, error: { code, message } }`.
- AP: assert że hasło dało WPA2-PSK (puste → OPEN → fail-fast). Bez TLS (WPA2 szyfruje link).
  Domyślne hasło wkompilowane (V1), 8–63 znaki.
- Panel vanilla: podgląd live (WS), formularz parametrów aktywny tylko gdy DISARMED, sekcja
  ESC_CALIBRATION z ostrzeżeniem „Remove propeller / disconnect motor".

**Wzorce do naśladowania:** `esp_http_server` + `CONFIG_HTTPD_WS_SUPPORT`; lossy snapshot producer;
ArduinoJson v7 lub cJSON.

**Scenariusze testowe:**
- [Unit] `api_contract`: sukces → `{data,...,error:null}`; walidacja fail → `error.code/message`,
  brak data; zapis w ARMED → kod `SETTINGS_WRITE_REJECTED_NOT_DISARMED`.
- [HW/E2E przez przeglądarkę] AP widoczny jako WPA2 (nie otwarty); połącz → panel ładuje; live
  pokazuje CH1/CH2/CH4 i wyjścia; flagi R16 widoczne przy pustym NVS (UNCALIBRATED).
- [E2E] W DISARMED zmień parametr → zapis OK, widoczny po restarcie; w ARMED próba zapisu → 409 +
  kod; podgląd live działa zawsze.
- [HW] Assert non-OPEN: build z pustym hasłem → fail-fast (świadoma weryfikacja zabezpieczenia).

**Weryfikacja:** AP osiągalny po WPA2, panel live, edycja zablokowana poza DISARMED (egzekwowane w
firmware), zmiany przeżywają restart; CH4 widoczny bez wpływu na sterowanie.

---

- [ ] **Unit 11: Sygnalizacja LED (GPIO2) — wzory stanu**

**Cel:** Sterować wbudowaną diodą (GPIO2) wzorami migania odzwierciedlającymi stan; FAILSAFE
sygnalizowany przez cały czas trwania (R6/R8). Logika wzoru czysta, driver cienki.

**Wymagania:** R8 (część LED), wsparcie R6 (LED FAILSAFE cały czas), R16 (UNCALIBRATED).

**Zależności:** Unit 6 (stan), Unit 4 (flaga calibrated). Może iść równolegle z Unit 10.

**Pliki:**
- Stwórz: `components/led_status/src/led_pattern.c` (+`.h`, **pure**: `(state, calibrated, t_ms) →
  led_on_off`), `components/led_status/src/led_driver.c` (HAL GPIO2)
- Stwórz: `components/led_status/test/test_led_pattern.c` (host)
- Modyfikuj: `components/control_loop` (wołanie wzoru co cykl) lub osobny lekki tick

**Podejście (propozycja wzorów — rozwiązanie otwartego pytania R8 „do ustalenia"):**
- DISARMED: wolne miganie (np. 0.5 Hz). UNCALIBRATED (defaults): nakładka — krótki double-blink co
  cykl wolny.
- ARMED: dioda stała (solid on).
- FAILSAFE: szybkie miganie (np. 5 Hz) przez cały czas trwania FAILSAFE.
- ESC_CALIBRATION: charakterystyczny double-blink.

**Notatka wykonawcza:** test-first dla `led_pattern` (mapowanie stanu→wzór deterministyczne wg t_ms).

**Scenariusze testowe:**
- [Unit] każdy stan → oczekiwany wzór on/off w funkcji czasu; FAILSAFE wzór niezależny od poprzedniego
  stanu; UNCALIBRATED nakładka tylko gdy `calibrated=false`.
- [HW] Wizualna weryfikacja każdego stanu na płytce.

**Weryfikacja:** testy hosta przechodzą; na płytce LED odróżnia 4 stany + UNCALIBRATED; FAILSAFE
widoczny przez cały czas.

## Wpływ systemowy

- **Graf interakcji:** Capture callback (MCPWM ISR-context) → struktury próbek → pętla ~50 Hz
  (`loop_step`) → pwm_out (LEDC). WWW (Core0) → mailbox pending → pętla (apply przy DISARMED) →
  dirty → NVS commit. Telemetria: pętla → snapshot slot → WS push (Core0). LED: pętla → wzór → GPIO2.
- **Propagacja błędów:** RC invalid → `RC_valid=false` → maszyna stanów → FAILSAFE (target neutral,
  serwo center). NVS corrupt → walidacja przy odczycie → defaulty + flaga (nigdy crash, nigdy
  śmieci). Błąd API → `{data:null, error:{code,message}}`, nigdy nie mutuje active params. Zawis
  pętli → TWDT panic → reset → boot DISARMED (SI-1).
- **Ryzyka cyklu życia stanu:** „własność akceptowana" R17 (zmiana live niezacommitowana przepada
  przy power-cut — akceptowane, oba stany bezpieczne); pending vs apply TOCTOU (re-check DISARMED);
  okno martwe bootu (pull-downy sprzętowe, nie firmware).
- **Parytet surface:** hard clamp egzekwowany identycznie we wszystkich code-path (ARMED, FAILSAFE,
  ESC_CALIBRATION, defaulty) — jedna funkcja, jeden punkt wejścia do LEDC.
- **Pokrycie integracyjne:** `loop_step` jako czysty rdzeń integruje wszystkie moduły i jest
  host-testowalny end-to-end (capture sample → output µs) bez sprzętu — to pokrywa scenariusze
  cross-layer, których unit testy pojedynczych modułów nie udowodnią. Reszta (timing, jitter, LEDC
  glitche, WP880, brownout) wymaga Planu pomiarów na sprzęcie.

## Ryzyka i zależności

- **[Sprzęt, krytyczne] Okno martwe boot/reset/brownout** — firmware nie pokrywa; wymaga pull-downów
  ~10 kΩ na GPIO18/19 + własnego failsafe ESC przy utracie sygnału. Mitygacja: BOM (Unit 1) + Plan
  pomiarów (Unit 2) + wymóg konfiguracji failsafe odbiornika (twardy wymóg: brak PWM przy utracie RF,
  inaczej wymiana odbiornika).
- **[Sprzęt, R13] E-stop + tap 12V ESP32 przed kill-switchem** — poza kodem; konsekwencja: po
  e-stopie odbiornik martwy → ESP32 w FAILSAFE bez żywych RC. Surfowane w BOM, nie blokuje firmware.
- **[Pomiar blokuje część implementacji] WP880** — neutral/pasmo, progi, reverse-dwell, zachowanie
  przy utracie/zamrożeniu PWM. Mitygacja: zrównoleglić Plan pomiarów z Fazą 0–2; konserwatywne
  defaulty do czasu pomiaru; `reverseNeutralDwellMs` dodać warunkowo.
- **[Wydajność] Jitter pętli ~50 Hz przy aktywnym WiFi/WS** — mitygacja: telemetria lossy ~10 Hz,
  capture sprzętowy (MCPWM), pinning Core1 tylko jeśli pomiar wymusi.
- **[Bezpieczeństwo] AP otwarty przez puste hasło** — mitygacja: assert non-OPEN + fail-fast (Unit 10).
- **[Flash wear] Zbyt częsty commit NVS** — mitygacja: debounce 2–5 s + commit tylko changed + tylko
  w DISARMED.

## Dokumentacja / Notatki operacyjne

- `docs/hardware/bom-and-measurements.md` — BOM (pull-downy, tap 12V, decoupling) + checklista
  pomiarów ze źródła §Plan pomiarów; wypełniana w trakcie wykonania (kandydat do `/dev-compound`).
- Domyślne hasło AP wkompilowane na V1 — udokumentować jak je zmienić (zmienne później, nie OTA).
- Po wykonaniu: rozważyć wpis w `docs/solutions/` z pomiarami WP880/ESP32 i decyzją o
  `reverseNeutralDwellMs`.

## Rozważane alternatywy

- **Arduino-ESP32 3.x** — odrzucone: i tak siedzi na IDF 5.x (te same API C), brak wrappera MCPWM,
  słabsze testy host-side, gorsza kontrola TWDT panic/brownout. (decyzja użytkownika)
- **RMT RX zamiast MCPWM capture** — realny kandydat (HW glitch filter + frame-gap detect), ale
  użytkownik wybrał MCPWM (sprzętowy edge-latch, najlepsza determinacja). Moduł wejścia za
  interfejsem — zamiana możliwa bez ruszania logiki, jeśli pomiar pokaże przewagę RMT.
- **GPIO ISR + micros()** — odrzucone do produkcji: kilka µs jittera, gorzej z aktywnym WiFi; tylko
  prototyp.
- **Double-buffer + atomic pointer swap** zamiast mailboxa — odłożone jako optymalizacja; mailbox
  prostszy i wystarczający (edycje tylko w DISARMED).
- **TS/React panel** — odrzucone: build step + większy bundle + więcej do audytu dla prostego panelu.

## Fazowe dostarczanie

- **Faza 0–1 (Unit 1–4):** bezpieczne wyjście + clamp + akwizycja RC + ważność/model. Fundament
  bezpieczeństwa testowalny na hoście; pierwsze pomiary sprzętowe (okno boot, neutral WP880).
- **Faza 2–3 (Unit 5–8):** łańcuchy sygnału + maszyna stanów + pętla + NVS. Pełny tor sterowania z
  failsafe i trwałością — MVP sterowania na wodzie (bez panelu).
- **Faza 4–6 (Unit 9–11):** tryb kalibracji ESC + panel WWW + LED. Wygoda/strojenie/diagnostyka.

## Źródła i referencje

- **Dokument źródłowy:** [docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md](../requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md)
- Poprzednik (superseded): docs/dev-brainstorms/2026-06-16-kayak-motor-firmware-v1-requirements.md
- ESP-IDF MCPWM: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/mcpwm.html
- ESP-IDF LEDC: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html
- ESP-IDF NVS: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html
- ESP-IDF Watchdogs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html
- esp_http_server: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html
- ArduPilot Rover Failsafes: https://ardupilot.org/rover/docs/rover-failsafes.html
