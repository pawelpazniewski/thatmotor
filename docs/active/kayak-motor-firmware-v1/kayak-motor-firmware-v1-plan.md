# Plan implementacji: Firmware V1 — sterownik ESP32 silnika kajaka

**Branch:** `feature/kayak-motor-firmware-v1`
**Ostatnia aktualizacja:** 2026-06-16

## Cele i zakres

Firmware ESP32 (ESP-IDF 5.5.x) wchodzący w tor sterowania między odbiornik RC a aktuatory (serwo
skrętu + bidirectional brushed ESC „WP880") silnika trollingowego na kajaku. Firmware: czyta kanały
RC, przetwarza sygnał (deadband, slew serwa, soft-start/stop gazu, limit mocy, kalibracja),
realizuje 4-stanową maszynę bezpieczeństwa (DISARMED / ARMED / FAILSAFE / ESC_CALIBRATION), trwale
przechowuje parametry w NVS i wystawia panel WWW przez WiFi AP (WPA2-PSK) do podglądu live i
strojenia.

**Priorytet: bezpieczeństwo i przewidywalność.** Hard clamp wyjścia jako nieobchodzalna granica
(SI-3), boot zawsze do stanu bezpiecznego (SI-1), failsafe ustalony (R6), pętla sterująca jako
jedyny pisarz parametrów (SI-6).

**Stack:** ESP-IDF 5.5.x (idf.py/CMake/Kconfig) · MCPWM capture (RC in) · LEDC 16-bit@50Hz (servo/ESC
out) · raw NVS (versioned blob + CRC32) · esp_http_server + WebSocket · WPA2-PSK SoftAP ·
esp_task_wdt panic + brownout. Logika sterowania/bezpieczeństwa jako framework-agnostic moduły C
testowalne na hoście.

### Poza zakresem (V1)
Logika trybów CH4 (tylko odczyt) · chmura/internet/OTA · logowanie/konta/multi-user · GPS/autopilot ·
logowanie telemetrii na SD · sprzętowy bypass PWM · niezależne zasilanie odbiornika. E-stop (R13) i
tap 12V przed kill-switchem to sprzęt/BOM, nie kod.

## Ustalenie krytyczne (sprzęt, nie firmware)

Wyjścia ESP32 są Hi-Z (bez wewnętrznego pull) w oknie martwym boot/reset/brownout, a przy
brownout-sag rdzeń może wyemitować śmieciowy impuls przed resetem. Realna gwarancja bezpieczeństwa w
tym oknie = **pull-downy ~10 kΩ na GPIO18/19 + własny failsafe ESC przy utracie sygnału**. Warstwy
programowe (boot-do-DISARMED, watchdog, clamp) siedzą na tej gwarancji. → BOM + Plan pomiarów.

## Mapowanie pinów (ustalone)
GPIO34←CH1 (skręt) · GPIO35←CH2 (gaz) · GPIO32←CH4 (diag) · GPIO18→serwo · GPIO19→ESC · GPIO2→LED ·
wspólna masa ESP32/odbiornik/ESC.

## Fazy i Unity

### Faza 0 — Fundament i bezpieczne wyjście
- **Unit 1:** Szkielet ESP-IDF + harness testów hosta + sdkconfig bezpieczeństwa (TWDT panic,
  brownout, WS, dedykowana partycja NVS `appcfg`). [M]
- **Unit 2:** Warstwa wyjścia LEDC + HARD CLAMP (SI-3) + bezpieczny boot (SI-1). Fundament
  bezpieczeństwa — przed jakimkolwiek sterowaniem. [M]

### Faza 1 — Akwizycja sygnału i ważność RC
- **Unit 3:** Wejście RC (MCPWM capture) + odczyt CH4 (R1, R12). [M]
- **Unit 4:** Predykat RC_valid + model ustawień (defaulty + walidacja przy odczycie) (SI-4, R16). [L]

### Faza 2 — Łańcuchy przetwarzania sygnału
- **Unit 5:** Łańcuchy gazu i serwa, pure (R2–R5, R10, wsparcie R6). [L]

### Faza 3 — Maszyna stanów i pętla sterująca
- **Unit 6:** Maszyna stanów + arming + failsafe, pure (R6, R7, SI-1, SI-4). [L]
- **Unit 7:** Integracja pętli ~50 Hz + watchdog + single-writer (R1, R6, R7, SI-1/3/6). [L]

### Faza 4 — Trwałość parametrów
- **Unit 8:** Persystencja NVS (versioned blob + CRC32, delayed commit, pending/apply) (R11, R16,
  R17, SI-6). [L]

### Faza 5 — Tryb serwisowy
- **Unit 9:** ESC_RANGE_CALIBRATION — osobny stan, krokowy, z abortem (R15, SI-3, SI-5). [M]

### Faza 6 — Łączność, panel, sygnalizacja
- **Unit 10:** WiFi SoftAP WPA2-PSK + serwer WWW + telemetria WS + API parametrów + panel (R8, R9,
  R10, R12, R14, R16, R17, SI-6). [XL]
- **Unit 11:** Sygnalizacja LED GPIO2 — wzory stanu (R8, wsparcie R6, R16). [S]

## Kryteria akceptacji (z kryteriów sukcesu źródła)
- Serwo i ESC podążają za drążkami przez ESP32; bez ESP32 brak sterowania (R1).
- Ruch serwa widocznie wolniejszy/płynniejszy niż surowy RC (R3); rampy gazu obserwowalne (R4);
  drobne ruchy wokół środka nie ruszają silnika (R5).
- Utrata CH1 lub CH2 → gaz soft-stop do neutralu, serwo do centrum, LED FAILSAFE, stan trwa do
  powrotu RC (R6, R8, SI-4).
- Po starcie i po failsafe napęd nie rusza dopóki nie spełniony warunek arm (R7).
- W DISARMED i po soft-stop FAILSAFE śmigło fizycznie stoi na zmierzonym neutralu (SI-2).
- Panel WWW osiągalny po WPA2 AP, live, edycja zablokowana poza DISARMED, zmiany przeżywają restart
  (R9, R10, R11, R14, R17).
- Pusty/skorumpowany NVS → praca na bezpiecznych defaultach z flagą UNCALIBRATED, nigdy na śmieciach
  (R16).
- ESC_CALIBRATION kalibruje WP880 do wyjścia ESP32, krokowo, z abortem na utracie RC (R15).
- E-stop odcina napęd, ESP32 żyje i raportuje FAILSAFE (R13).

## Ryzyka (skrót — pełne w planie technicznym)
- [Sprzęt, krytyczne] Okno martwe boot/reset/brownout → pull-downy G18/G19 + failsafe ESC.
- [Pomiar blokuje implementację] WP880: neutral/progi/reverse-dwell — zrównoleglić Plan pomiarów.
- [Wydajność] Jitter pętli przy WiFi → telemetria lossy 10 Hz, capture HW, pinning Core1 warunkowo.
- [Bezpieczeństwo] AP otwarty przez puste hasło → assert non-OPEN + fail-fast.
- [Flash wear] Częsty commit NVS → debounce 2–5 s + tylko changed + tylko DISARMED.

## Zależności zadań
Unit 1 → 2 → (3,4) → 5 → 6 → 7 → 8 → 9 → 10; Unit 11 równolegle z 10. Plan pomiarów sprzętu
zrównoleglony z Fazą 0–2 (odblokowuje realne wartości neutralu/okresu/okna boot).

## Źródła
- Requirements doc: docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md
- Plan techniczny: docs/plans/2026-06-16-001-feat-kayak-motor-firmware-v1-plan.md
