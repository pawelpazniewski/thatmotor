# Kayak Motor Firmware — sterownik ESP32 silnika kajaka

Firmware **ESP-IDF (ESP32)** wchodzący w tor sterowania **między odbiornik RC a aktuatory** silnika
trollingowego na kajaku: serwo skrętu + bidirectional brushed ESC (Hobbywing QuicRun **WP880**).
Czyta kanały RC, przetwarza sygnał (martwa strefa, slew, miękki start/stop, limit mocy, kalibracja),
realizuje maszynę stanów bezpieczeństwa, trwale trzyma parametry w NVS i wystawia **panel WWW** przez
WiFi (WPA2 SoftAP) do podglądu live i strojenia.

> **Priorytet: bezpieczeństwo i przewidywalność.** Boot zawsze do stanu bezpiecznego, nieobchodzalny
> hard clamp wyjścia, ustalony failsafe, pętla sterująca jako jedyny pisarz parametrów.

---

## Funkcje

- **Akwizycja RC** (MCPWM capture, sprzętowy edge-latch) — CH1/CH2/CH4, szeroki zakres okresu ramki (≈33–500 Hz).
- **Łańcuch gazu**: martwa strefa → asymetryczny limit mocy (przód/tył) → rampa zależna od magnitudy
  (łagodny rozruch, szybki zjazd) → **menedżer zmiany kierunku** (zjazd do neutralu + postój anti-plugging → łagodny rewers).
- **Łańcuch serwa**: martwa strefa → endpointy → slew → **kalibracja neutralu (trim)**.
- **Maszyna stanów**: `DISARMED · ARMED · FAILSAFE · ESC_CALIBRATION · DEPLOY`.
- **CH4 jako przycisk gestów**: 1 klik = arm/disarm (natychmiastowy disarm w ARMED), 3 kliki = deploy/stow.
- **Tryb DEPLOY**: ręczne podnoszenie silnika — gaz off, serwo na zadany kąt, ignoruje drążek.
- **Persystencja NVS**: versioned blob + CRC32, walidacja przy odczycie, debounced commit, defaulty przy korupcji.
- **WiFi SoftAP (WPA2) + panel WWW**: live telemetria (WebSocket), edycja parametrów (tylko DISARMED),
  komendy arm/disarm/deploy/stow, kalibracja neutralu serwa, diagnostyka RC.
- **Sygnalizacja LED** (GPIO2) — osobny wzór dla każdego stanu.
- **CH3** — nasłuch pod przyszły *spot lock* (na razie tylko podgląd w telemetrii).
- **GPS** (u-blox NEO-M9N, UART/NMEA) i **kompas** (BNO085, I2C/SHTP rotation vector) — pozycja, prędkość
  i kurs w telemetrii panelu. **Ściśle poza failsafe** (osobne taski, tylko podgląd — zero wpływu na
  uzbrojenie/sterowanie), pod przyszły Spot-Lock.

---

## Sprzęt

### Mapowanie pinów (ustalone)

| GPIO | Kierunek | Funkcja |
|------|----------|---------|
| 34 | wejście (MCPWM) | CH1 — skręt |
| 35 | wejście (MCPWM) | CH2 — gaz |
| 32 | wejście (MCPWM) | CH4 — przycisk arm/disarm/deploy |
| 27 | wejście (MCPWM, grupa 1) | CH3 — spot lock (podgląd) |
| 18 | wyjście (LEDC 50 Hz) | serwo skrętu |
| 19 | wyjście (LEDC 50 Hz) | ESC (WP880) |
| 2 | wyjście | LED statusu |
| 16 | wejście (UART1 RX) | GPS TXD (NEO-M9N, 38400) — poza failsafe |
| 17 | wyjście (UART1 TX) | GPS RXD (konfiguracja, opcjonalnie) |
| 21 | I2C SDA | BNO085 (adres 0x4A) — poza failsafe |
| 22 | I2C SCL | BNO085 |
| 23 | wejście | BNO085 INT (data-ready) |
| 25 | wyjście | BNO085 RST |

**Wymagana wspólna masa** odbiornik ↔ ESP32 ↔ ESC ↔ GPS ↔ kompas.
**GPS** zasilaj **5 V** (ma regulator), **BNO085** koniecznie **3,3 V** (max 3,6 V), PS0/PS1 i ADO → GND.

> **Krytyczne (sprzęt, nie firmware):** w oknie martwym boot/reset/brownout piny są Hi-Z. Realna
> gwarancja bezpieczeństwa to **pull-downy ~10 kΩ na GPIO18/19** + własny failsafe ESC przy utracie sygnału.
> Szczegóły: [`docs/hardware/bom-and-measurements.md`](docs/hardware/bom-and-measurements.md).

### Komponenty
ESP32 DevKit · serwo cyfrowe (np. 270°, 500–2500 µs) · ESC WP880 · odbiornik RC (z konfigurowalnym
failsafe — twardy wymóg: brak PWM przy utracie RF) · LiFePO4 12V (4S) z BMS · buck 5V · pull-downy G18/G19.

### Ustawienia ESC (WP880) pod ten projekt
- **Running Mode → Fwd/Rev** (single-click, bez kroku brake) — współgra z firmware'owym postojem anti-plugging.
- **Battery Type → NiMH** (pod LiFePO4 — unika przedwczesnego odcięcia LiPo; ochronę zapewnia BMS).
- **Max Reverse → 100%** (limit wstecz reguluj w firmware).

---

## Architektura

**Czysta logika ⊥ HAL.** Cała logika sterowania/bezpieczeństwa to framework-agnostic moduły C,
testowane na hoście; adaptery HAL (LEDC/MCPWM/NVS/WiFi/HTTP) są cienkie.

```
components/
  safety_clamp/   clamp_pwm_us (SI-3, nieobchodzalny)
  pwm_out/        LEDC + per-kanałowe okna clamp (serwo 500–2500, ESC 1000–2000)
  rc_capture/     MCPWM capture (grupa 0: CH1/CH2/CH4, grupa 1: CH3) + cap_math (pure)
  rc_validity/    channel_valid, rc_valid, debounce, ch4_switch, click_counter (pure)
  settings/       model + defaults + validate + blob_codec(CRC32) + nvs_store + nvs_provenance
  signal_chain/   throttle_chain, servo_chain, ramp, chain_math (pure)
  state_machine/  state_machine, esc_calibration (pure)
  control_loop/   loop_step (pure rdzeń) + control_loop (HAL/timing, single-writer SI-6)
  web_panel/      wifi_ap, http_server, ws_telemetry, params_api, command_parse, api_contract
  led_status/     led_pattern (pure) + led_driver (HAL)
  gps/            nmea_parse (pure) + gps_reader (UART HAL, osobny task) — poza failsafe
  imu/            quat_to_yaw (pure) + bno085 (I2C/SHTP HAL, osobny task) — poza failsafe
web/              index.html, app.js, style.css (embed w flash)
```

### Niezmienniki bezpieczeństwa
- **SI-1** — boot zawsze do DISARMED, wyjścia na neutral przed czymkolwiek.
- **SI-3** — hard clamp wyjścia jako ostatni, bezwarunkowy krok (żadna ścieżka do LEDC go nie omija).
- **SI-4** — failsafe sterowany wyłącznie ważnością CH1+CH2 (utrata RC → soft-stop + serwo center, latched).
- **SI-6** — pętla sterująca to jedyny pisarz aktywnych parametrów (mailbox length-1, apply tylko w DISARMED).

---

## Build, testy i flash

Wymaga **ESP-IDF 5.5.x** (sourcing `export.sh`) oraz **CMake/Ninja**.

```bash
# Build firmware + flash + monitor (target ESP32-S3 N16R8)
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor      # lewy USB-C (COM/UART); podstaw swój port

# Testy hosta (pure-logic, standalone CMake + Unity — bez ESP-IDF)
./test/host/run.sh
```

Host-testy pokrywają całą logikę pure (**289 testów Unity**): clamp, łańcuchy sygnału, maszyna stanów,
walidacja/CRC NVS, click-counter, kalibracja, wzory LED, parser NMEA (GPS), konwersja kwaternion→kurs (kompas).

---

## Panel WWW

Po boocie ESP32 stawia SoftAP **WPA2** (SSID/hasło z `menuconfig` → `CONFIG_KAYAK_AP_PASSWORD`).
Połącz się i wejdź na **`http://192.168.4.1`**:

- **Live**: stan, CH1/CH2/CH3/CH4, wyjścia serwo/ESC, flagi R16, diagnostyka RC (okres + ważność per kanał).
- **GPS**: Fix / Sats / Lat / Lon / Speed (diagnostyka, poza failsafe).
- **Compass (BNO085)**: Heading / Calib (0–3) / OK (diagnostyka, poza failsafe).
- **Commands**: Arm / Disarm / Deploy / Stow (z wynikiem i powodem blokady uzbrojenia).
- **Parameters**: edycja w DISARMED, zapis do NVS (live + read-back).
- **Servo neutral calibration**: Step Left / Step Right / Save (kalibracja zera serwa).
- **ESC range calibration**: sekcja z ostrzeżeniem „Remove propeller".

---

## Sterowanie i sygnalizacja

### Gesty CH4 (przycisk monostabilny)
| Stan | Gest | Akcja |
|------|------|-------|
| ARMED | 1 klik | natychmiastowy DISARM |
| DISARMED | 1 klik | ARM (przez bramkę: gaz neutral + RC ważne) |
| DISARMED | 3 kliki | wejście DEPLOY |
| DEPLOY | 3 kliki | wyjście → DISARMED |

### Wzory LED (GPIO2)
| Stan | LED |
|------|-----|
| ARMED | świeci ciągle |
| FAILSAFE | szybkie miganie ~5 Hz |
| DISARMED (skalibrowany) | wolne 0,5 Hz |
| DISARMED (UNCALIBRATED) | wolne + podwójny błysk |
| ESC_CALIBRATION / DEPLOY | wzory podwójno-/potrójno-błyskowe |

---

## Konfiguracja (defaulty)

Parametry edytowalne w panelu (DISARMED), trwałe w NVS. Domyślne: serwo **833/2167 µs** (≈180° na
serwie 270°), max throttle **fwd 90% / rev 50%**, deploy **2167 µs**, trim **0**, rampy łagodne,
postój przy zmianie kierunku **400 ms**, próg CH4 **1500 µs**, okno kliknięć **500 ms**.

> Zmiana NVS schema → przy starcie pamięć wraca do defaultów (versioned blob, nigdy śmieci).

---

## Roadmap

- **Monitor napięcia LiFePO4** (dzielnik + ADC, alarm w panelu).
- **Spot-Lock / AutoPilot** — GPS i kompas już nasłuchiwane (podgląd w panelu); pozostaje pętla nawigacji
  + silnik ciągły z enkoderem. CH3 jako przełącznik trybu.
- **Sterowanie wentylatorem** (czujnik temp + MOSFET / standalone termostat).

---

## Źródła
- Requirements: [`docs/requirements/`](docs/requirements/)
- Plan techniczny: [`docs/plans/`](docs/plans/)
- Dokumentacja sprzętowa + diagramy: [`docs/hardware/`](docs/hardware/)
