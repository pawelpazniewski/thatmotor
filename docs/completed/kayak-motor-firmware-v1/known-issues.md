# Known issues — kayak-motor-firmware-v1 (po autopilocie)

**Branch:** `feature/kayak-motor-firmware-v1`
**Data:** 2026-06-17
**Kontekst:** Autopilot zrealizował i zweryfikował całą logikę firmware na hoście (198/198 testów
Unity PASS) oraz kompilację na target ESP32 (`idf.py build` PASS, bin 0xd90e0 B, 43% partycji wolne).
Pozostałe pozycje wymagają **fizycznego sprzętu** (ESP32 DevKit, WP880 ESC, serwo, odbiornik RC,
oscyloskop) lub **przeglądarki z żywym panelem WWW** — niedostępnych w środowisku CI/autopilota.
Decyzja użytkownika: odroczyć je do tego dokumentu i domykać fazy na bazie testów `[Unit]`.

**To NIE są błędy implementacji ani osłabione asercje** — to udokumentowane luki weryfikacji
zależne od środowiska. Logika, którą da się sprawdzić bez sprzętu, jest pokryta testami hosta.

---

## 1. Weryfikacja na sprzęcie `[HW]` (wymaga ESP32 + peryferia + oscyloskop)

- 🔧 **Faza 0 / Unit 1** — `idf.py build` przechodzi (✅ zweryfikowane); flash bootuje; log pokazuje
  reset reason + wejście do bezpiecznego placeholdera. *(flash/boot — wymaga płytki)*
- 🔧 **Faza 0 / Unit 2** — Oscyloskop: po boocie G18/G19 emitują neutral/centrum; zmierzyć okno
  martwe (reset→pierwszy impuls); zachowanie przy watchdog-resecie i brownout-sag; sprawdzić pull-downy.
- 🔧 **Faza 1 / Unit 3** — Oscyloskop równolegle z odczytem: zmierzona szerokość/okres zgodne z
  odbiornikiem; zapisać zmierzony okres ramki do Planu pomiarów.
- 🔧 **Faza 3 / Unit 7** — Pomiar jittera pętli (~50 Hz stabilne?); zachowanie po watchdog-resecie
  (boot DISARMED, neutral).
- 🔧 **Faza 4 / Unit 8** — Zapis w DISARMED→restart→wartość przetrwała; power-cut przed commitem→
  ostatnia dobra/default; pusty/zepsuty NVS→defaulty + UNCALIBRATED.
- 🔧 **Faza 5 / Unit 9** — (śmigło zdjęte) Operator przechodzi sekwencję, WP880 potwierdza zakres.
- 🔧 **Faza 6 / Unit 10** — Assert non-OPEN: build z pustym hasłem→fail-fast. *(logika pokryta
  host-testem `wifi_ap_config_valid`; pozostaje weryfikacja realnego build-fail z pustym Kconfig)*
- 🔧 **Faza 6 / Unit 11** — Wizualna weryfikacja każdego stanu LED na płytce.

## 2. Weryfikacja E2E `[E2E]` (wymaga przeglądarki + żywego panelu na sprzęcie)

- 🌐 **Faza 6 / Unit 10** — AP widoczny jako WPA2 (nie otwarty); połącz→panel ładuje; live pokazuje
  CH1/CH2/CH4 i wyjścia; flagi R16 widoczne przy pustym NVS (UNCALIBRATED).
- 🌐 **Faza 6 / Unit 10** — W DISARMED zmień parametr→zapis OK, widoczny po restarcie; w ARMED próba
  zapisu→409 + kod; podgląd live działa zawsze.
  *(Logika 409 / edit-blocked / single-writer / kontrakt {data,error} pokryta host-testami
  `api_contract`, `params_decide`, `command_parse`; pozostaje weryfikacja w przeglądarce na sprzęcie.)*

## 3. Kryteria ukończenia `Weryfikacja:` — część sprzętowa odroczona

Część każdego kryterium dot. **testów hosta przeszła** (✅ 198/198). Odroczona jest tylko część
„na sprzęcie":

- Unit 1: projekt buduje się dla ESP32 i hosta ✅; partycja `appcfg` w tablicy ✅; testy hosta jedną
  komendą ✅ — **spełnione**.
- Unit 2: testy hosta ✅; *na sprzęcie* ESC=neutral/serwo=center po boocie — odroczone; brak
  code-path do LEDC z pominięciem clamp ✅ (audyt + test).
- Unit 3: *firmware na sprzęcie* raportuje sensowne szerokości/okres zgodne z oscyloskopem — odroczone.
- Unit 4: testy hosta ✅; symulacja utraty CH1/CH2→RC_valid=false ✅; defaulty w oknie sanity ✅ — **spełnione**.
- Unit 5: testy hosta ✅ (rampy/slew, neutral-po-reverse) — **spełnione**.
- Unit 6: testy hosta pokrywają tabelę przejść ✅; brak wyjścia z FAILSAFE bez RC valid ✅ — **spełnione**.
- Unit 7: *na sprzęcie* serwo/ESC w ARMED, neutral w DISARMED, soft-stop+latch, WDT-reset→DISARMED — odroczone (logika pokryta `loop_step`).
- Unit 8: testy hosta ✅; *parametry przeżywają restart na sprzęcie* — odroczone; skorumpowany NVS nigdy poza oknem sanity ✅.
- Unit 9: testy hosta ✅; *na sprzęcie WP880 kalibruje krokowo* — odroczone; nigdy bez potwierdzenia ✅.
- Unit 10: *AP/panel/edycja/restart w przeglądarce* — odroczone (logika egzekwowana w firmware, host-testowana).
- Unit 11: testy hosta ✅ (4 stany + UNCALIBRATED, FAILSAFE zawsze) — **spełnione**.

## 4. Plan pomiarów sprzętu (odblokowuje realne wartości — wszystkie odroczone)

Wartości obecnie na konserwatywnych defaultach/placeholderach (udokumentowane w kodzie i kontekst.md).
Po pomiarze należy dostroić: `escNeutralUs`/pasmo, progi startu, `reverseNeutralDwellMs`, próg okresu
ramki w RC_valid (obecnie placeholder 20 ms ± 8 ms — **NIE zakładać 20 ms na stałe**), próg N złych
ramek (obecnie 5), długość okna martwego boot.

**WP880 ESC:** neutral/pasmo @1500µs; próg startu przód/tył; zachowanie przy utracie PWM; przy
zamrożonym PWM (~70% LEDC, rdzeń stop); czy wymaga własnej kalibracji throttle range (R15); zachowanie
przy bootowym neutralu; szybka zmiana fwd→rev (plugging? → `reverseNeutralDwellMs`); akceptacja SI-2
(śmigło stoi w DISARMED i po soft-stop FAILSAFE).
**ESP32:** stan G18/G19 podczas reset/boot/bootloadera (oscyloskop); LEDC po WDT-resecie (glitch?);
brownout-sag — stan pinów; długość okna martwego boot (liczba); pull-down na G19.
**Odbiornik:** zachowanie przy utracie RF (twardy wymóg: brak PWM) + konfiguracja failsafe odbiornika;
zmierzony okres ramki.

## 4b. Spot-lock (CH3 GPS position hold) — luki hardware/E2E `[HW]`/`[E2E]`

Funkcja spot-lock (branch `feature/spot-lock-position-hold`) ma całą logikę decyzyjną pokrytą
host-testami (geo_math, spot_lock_step, integracja loop_step: wejście/hold/failsafe/abort/pauza/clamp).
Pozostałe wymagają realnego sprzętu lub żywego panelu i są tu odłożone:

- 🔧 `[HW]` **Akwizycja fixu + realne utrzymanie pozycji** — przy ARMED + świeży fix + drążki na
  zerze + CH3 ON kajak utrzymuje się w okolicy punktu (rząd kilku metrów) bez interwencji. Nastawy
  regulatora (deadband 3 m, max gaz 35%, gainy) strojone w terenie — defaults są celowo łagodne.
- 🔧 `[HW]` **Punkt za rufą** — |błąd kierunku| > 60° → jeden łagodny zawrót, brak „donutów"
  (efekt bramki ±60°, nie osobnej logiki). Wymaga obserwacji na wodzie.
- 🔧 `[HW]` **Reakcja na utratę fixu/heading w trakcie hold** — stop (neutral) bez szarpania;
  powrót danych wznawia tryb z tym samym celem (PAUSED↔ACTIVE). Świeżość GPS ≤1,5 s.
- 🔧 `[HW]` **Brak zakłócenia kompasu przez silnik** — heading wiarygodny przy pracującym silniku
  (założenie planu; zweryfikować na sprzęcie, bo decyduje o bramce kierunku).
- 🖥️ `[E2E]` **Panel spot-lock** — blok „Spot-lock (CH3)" pokazuje off→active po CH3 ON, błąd[m]
  maleje przy dopływaniu do punktu, paused przy utracie GPS; pola `spot_lock_*` w JSON telemetrii
  (ints). JSON serializowany przez `snapshot_to_json` (HAL, nie host-testowany) — kontrakt int/bool
  zweryfikowany przez `idf.py build` + wymaga wizualnej weryfikacji w przeglądarce.

## 5. Pozostałe nity `[P3]` z review (świadomie nieadresowane — „pomiń P3")

Pełna lista w sekcjach „Do poprawy po review fazy N" w `*-zadania.md` (linie 268–360). Wszystkie są
nity (stylistyka, named constants, testy HAL-only, kontynuacje konwencji nazewniczej PascalCase vs
`_t`, udokumentowane świadome decyzje). Żaden nie blokuje bezpieczeństwa ani poprawności. Wybrane
warte domknięcia przy realnym sprzęcie/integracji:
- `RC_PERIOD_EXPECTED_US 20000` placeholder → realna wartość po pomiarze okresu ramki.
- Testy HAL-only wiringu (`maybe_apply_pending`, `maybe_commit_params`) — pokryć przy testach na sprzęcie.
- Ujednolicenie konwencji typów (`PwmWindow` → `pwm_window_t`) jeśli ma być zgodne z idiomem ESP-IDF.
- Brak auth/CSRF na panelu — świadome założenie zaufania (asocjacja do SoftAP == pełna władza); zapisać jawnie w docs operatora.
