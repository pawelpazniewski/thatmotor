---
date: 2026-06-16
topic: kayak-motor-firmware-v1
version: 2
status: ready-for-dev-plan
supersedes: docs/dev-brainstorms/2026-06-16-kayak-motor-firmware-v1-requirements.md
---

# Firmware V1 — silnik elektryczny do kajaka (v2)

> Wersja 2 powstała po sesji stress-testu (`zroastuj-mnie`) na bazie v1.
> Rozstrzyga 15 gałęzi drzewa decyzyjnego dotyczących bezpieczeństwa, maszyny
> stanów, walidacji RC, bootstrapu NVS, kalibracji wyjścia ESC, kolejności
> przetwarzania sygnału, trybu serwisowego i architektury zasilania.

## Problem
Surowy sygnał RC sterujący serwem (skręt) i ESC (gaz) silnika trollingowego jest
zbyt gwałtowny i nie ma żadnego zabezpieczenia przy utracie sygnału na wodzie.
ESP32 wchodzi między odbiornik RC a aktuatory, żeby: wygładzić ruch serwa, zrobić
soft-start/soft-stop gazu, dodać deadband i failsafe, oraz dać prosty panel WWW
do podglądu live i strojenia parametrów. Użytkownik = budowniczy/operator (jedna
osoba), środowisko = otwarta woda. Priorytetem jest bezpieczeństwo i
przewidywalność, a dopiero potem wygoda.

**Założenie nadrzędne:** ESP32 jest jedynym punktem w torze sterowania (R1), więc
jest pojedynczym punktem awarii. Ochrona przed jego padem jest w V1 **programowa**
(watchdog + boot do bezpiecznego stanu + clamp) **uzupełniona o fizyczny e-stop**
(R13). Sprzętowego bypassu sygnału PWM w V1 nie ma — świadomie.

## Wymagania

### Tor sterowania

- **R1. Passthrough RC przez ESP32.** ESP32 czyta CH1 (skręt, GPIO34) i CH2 (gaz,
  GPIO35) z odbiornika i generuje sygnał serwa (GPIO18) oraz ESC (GPIO19). Bez
  ESP32 w torze nie ma sterowania — cały tor przechodzi przez firmware. ESP32
  pozostaje **jedynym źródłem sygnału dla ESC** (istotne dla R15).

- **R2. Model gazu dwukierunkowy ze środkiem = stop.** CH2 w środku = stop, w górę
  = przód, w dół = tył. WP880 jako bidirectional brushed ESC. Deadband wokół środka
  (R5). „Środek" to **zmierzony** neutral WP880, nie założone 1500 µs (patrz R10
  kalibracja wyjścia ESC, inwariant SI-2).

- **R3. Slew limiting serwa.** Wyjście serwa zmienia się z ograniczoną prędkością.
  Konfigurowalne (R10).

- **R4. Soft-start / soft-stop gazu.** Zmiany gazu przez rampy narastania i opadania
  (osobne ↑/↓), bez szarpnięć. Konfigurowalne (R10). Przejście przód↔tył przez zero
  realizowane samą rampą; obowiązkowy postój w neutralu **tylko jeśli** pomiar
  WP880 go wymusi (patrz Plan pomiarów, opcjonalny `reverseNeutralDwellMs`).

- **R5. Deadband wokół neutralu gazu.** Małe wychylenia wokół środka nie dają
  wyjścia na ESC. Szerokość konfigurowalna (R10). Deadband działa na
  znormalizowanym targecie **przed** skalowaniem i rampą (patrz Łańcuch gazu).

### Bezpieczeństwo i stany

- **R6. Failsafe przy utracie RC (PRZEPISANE).** Po wykryciu `RC invalid` firmware
  przechodzi w FAILSAFE: ESC dostaje target neutralny i schodzi **soft-stopem** do
  zmierzonego neutralu, potem trzyma; serwo dostaje target = środek i wraca tam
  **normalnym slew-limitem**, potem trzyma. Serwo **NIE** trzyma ostatniej pozycji
  (to dawało kręcenie kajaka przy utracie RC w skręcie). FAILSAFE jest stanem
  **ustalonym**: trwa dopóki RC nieważne; LED/UI pokazują FAILSAFE przez cały czas.
  Wyjście z FAILSAFE **tylko** po odzyskaniu RC → DISARMED (nie po zakończeniu
  soft-stopu).

- **R7. Arming z wymogiem neutralu (DOPRECYZOWANE).** Napęd rozbrojony po starcie
  i po wejściu w failsafe. Arming bramkuje **wyłącznie ESC** (energię), nie serwo
  (mechanikę). Warunek przejścia DISARMED → ARMED:
  ```
  RC valid
  AND throttle in neutral (±deadband)
  AND no calibration in progress (state != ESC_CALIBRATION)
  AND no settings application in progress
  ```
  W DISARMED wyjście ESC = neutral niezależnie od drążka.

- **R8. Sygnalizacja stanu: LED + panel.** Wbudowana dioda (GPIO2) pokazuje stan
  wzorami migania (wzory do ustalenia w planowaniu). Panel WWW pokazuje stan
  szczegółowo. LED pokazuje FAILSAFE przez cały czas trwania FAILSAFE (R6).

- **R13. Fizyczny kill-switch (NOWE, sprzęt).** Mechaniczny e-stop w zasięgu ręki
  operatora, odcinający **tylko zasilanie napędu** (WP880). Nie elektronika, nie
  bypass PWM — twardy wyłącznik. Zasilanie ESP32 jest **niezależne i podłączone
  przed** kill-switchem (osobny tap 12V), więc ESP32 przeżywa e-stop i zachowuje
  telemetrię/diagnostykę. **Konsekwencja przy obecnym sprzęcie:** odbiornik i serwo
  zasilane z BEC WP880, więc e-stop zabija też odbiornik i serwo → ESP32 wchodzi
  w FAILSAFE i to raportuje; po e-stopie telemetria pokazuje stan ESP32 + FAILSAFE,
  ale **bez żywych wejść RC** (odbiornik martwy). Żywe RC po e-stopie wymagałoby
  niezależnego zasilania odbiornika (opcjonalna zmiana sprzętowa, poza V1).
  Prawdziwy master power-off to **osobny** rozłącznik niż e-stop.

### Panel, parametry, trwałość

- **R9. Panel WWW po WiFi AP — podgląd live.** ESP32 stawia AP i serwuje panel
  pokazujący na żywo: wejścia RC (CH1, CH2, CH4), wyjście do serwa, wyjście do ESC,
  status RC / failsafe / arm + flagi diagnostyczne (R16). AP aktywny od startu.

- **R14. AP = WPA2-PSK (NOWE).** AP **nigdy otwarty**. Bez logowania / kont /
  sesji / multi-user. Domyślne hasło wkompilowane na V1, zmienne później.
  Uzasadnienie: panel zmienia parametry bezpieczeństwa napędu — otwarte radio jest
  niedopuszczalne; WPA2-PSK to minimalna higiena, nie autoryzacja użytkowników.

- **R10. Edycja parametrów z UI — tylko przy disarm.** Podgląd live zawsze. Zapis
  bramkowany przez R17. Parametry tunowalne:
  - prędkość serwa (slew rate)
  - rampa ESC narastanie (soft-start)
  - rampa ESC opadanie (soft-stop)
  - szerokość deadbandu **gazu**
  - **deadband steru** (NOWE; default `0` = wyłączony, dopóki nie udowodniony
    hunting serwa; osobny parametr od deadbandu gazu)
  - timeout failsafe
  - limit mocy gazu (max % przód/tył)
  - endpointy serwa (min/max kąt)
  - kalibracja wejścia RC (min/mid/max na kanał)
  - flagi reverse (serwo i/lub gaz)
  - **kalibracja wyjścia ESC (NOWE):** `escNeutralUs`, `escForwardMinUs`,
    `escReverseMinUs`, `escForwardMaxUs`, `escReverseMaxUs`, `escNeutralBandUs`
  - **(warunkowo)** `reverseNeutralDwellMs` — dodać **tylko** jeśli pomiar WP880
    pokaże problem przy zmianie przód↔tył.

- **R11. Trwały zapis ustawień.** Parametry przeżywają wyłączenie zasilania (NVS).
  Po restarcie firmware używa zapisanych wartości (z walidacją R16). Strategia
  zapisu — R17.

- **R16. Bootstrap / walidacja NVS (NOWE).** NVS empty / corrupt / schema-mismatch
  → wkompilowane **bezpieczne defaulty**. Walidacja **przy odczycie**, nie tylko
  przy zapisie. Fallback per-field albo pełny — **nigdy użycie śmieci**. Defaulty
  konserwatywne: CH min/mid/max = 1000/1500/2000 µs, szeroki deadband neutralu,
  niski domyślny max throttle, łagodne rampy, `escNeutralUs` w oknie sanity
  (np. 1400–1600 µs). Arming dozwolony na defaultach z flagą UNCALIBRATED.
  Telemetria/UI eksponuje:
  ```
  settings_source: DEFAULTS | NVS | MIXED_RECOVERED
  settings_valid:  bool
  calibrated:      bool
  defaults_used:   bool
  nvs_error:       bool
  ```

- **R17. Bramka zapisu i strategia commitu (NOWE).** Zapis dozwolony **wyłącznie**
  gdy `state == DISARMED` (NIE w ARMED, NIE w FAILSAFE, NIE w ESC_CALIBRATION).
  Pętla sterująca jest **jedynym właścicielem** aktywnego zestawu parametrów; WWW
  tylko **waliduje** (granica API) i zgłasza pending. Przepływ:
  ```
  UI request → walidacja → pending → (main loop, jeśli DISARMED) apply live
            → dirty flag → delayed commit do NVS
  ```
  Commit do flasha **opóźniony** (debounce 2–5 s po ostatniej zmianie) lub forsowany
  przyciskiem Save — żeby nie palić cykli flasha i nie lagować przy ruchu suwaka.
  Zapis odrzucony w innym stanie: `SETTINGS_WRITE_REJECTED_NOT_DISARMED`. Format
  odpowiedzi API: `{ data, error: { code, message } }`. **Własność akceptowana:**
  zmiana zastosowana live, ale niezacommitowana, przepada przy power-cut/kill —
  przy następnym boocie wraca ostatnia dobra wartość albo bezpieczny default (oba
  bezpieczne).

### Tryb serwisowy

- **R15. ESC_RANGE_CALIBRATION (NOWE — osobny stan).** Kalibracja WP880 **do
  stabilnego wyjścia ESP32**, nie do odbiornika (odbiornik odseparowany przez
  kalibrację wejścia RC; zmiana pilota nie wymaga rekalibracji ESC). Pełnoprawny
  stan maszyny (nie pod-tryb DISARMED) z własnymi regułami, timeoutem i abortem.
  - **Wejście:** `state == DISARMED AND RC valid AND throttle neutral AND jawna
    akcja UI AND potwierdzenie ostrzeżenia "propeller removed / motor disconnected"`.
  - **Sekwencja krokowa** sterowana z panelu (operator słucha potwierdzeń WP880):
    `Emit neutral` → czekaj na WP880 → `Emit full forward` → czekaj → `Emit full
    reverse` → czekaj → `Done`. **Brak** automatu na timerze.
  - **W trybie:** pomija normalny mapping gazu, limit mocy i rampy; **zachowuje
    finalny hard clamp** (SI-3); wystawia stałe `1500 / 2000 / 1000 µs`.
  - **Abort:** `RC invalid → FAILSAFE`; `timeout bezczynności → DISARMED + ESC
    neutral`; `user cancel → DISARMED + ESC neutral`.
  - **Nigdy** nie uruchamia się automatycznie; UI wymaga jawnego potwierdzenia
    i pokazuje ostrzeżenie `Remove propeller / disconnect motor before ESC
    calibration`.

### Diagnostyka

- **R12. CH4 — czytaj i pokaż, bez akcji.** ESP32 czyta CH4 (GPIO32) i pokazuje
  surową wartość w panelu, nie steruje niczym. **CH4 poza predykatem `RC_valid`** —
  jego utrata nie wywołuje failsafe (tylko status „CH4: brak" w UI). Grunt pod tryby
  V2.

## Maszyna stanów (finalna, 4 stany)

| Stan | Warunek | ESC | Serwo | LED/UI | Główne wyjścia |
|---|---|---|---|---|---|
| **DISARMED** | RC valid, rozbrojony | `escNeutralUs` | śledzi CH1 (slew) | DISARMED (+UNCALIBRATED jeśli defaults) | → ARMED (warunek R7); → ESC_CALIBRATION (R15); → FAILSAFE (RC invalid) |
| **ARMED** | RC valid, uzbrojony | śledzi CH2 (deadband/limit/rampa) | śledzi CH1 (slew) | ARMED | → FAILSAFE (RC invalid); → DISARMED (ręczny disarm, jeśli dodany) |
| **FAILSAFE** | RC invalid | soft-stop → neutral, trzymaj | slew → center, trzymaj | FAILSAFE (cały czas) | → DISARMED (RC valid) |
| **ESC_CALIBRATION** | wejście z DISARMED (R15) | sekwencja stała 1500/2000/1000 | jak DISARMED | ESC_CALIBRATION + ostrzeżenie | → DISARMED (Done/cancel/timeout); → FAILSAFE (RC invalid) |

**Reguła serwa, niezależna od arming:** `RC valid → serwo śledzi CH1`;
`RC invalid → serwo centruje`.

**Predykat ważności RC:**
```
channel_valid(ch) := edge widziany w ostatnich T ms
                  AND szerokość impulsu ∈ [800, 2200] µs
                  AND okres ramki ≈ oczekiwany ± tolerancja
                     (oczekiwany = ZMIERZONY dla danego odbiornika; default 20 ms,
                      tolerancja hojna — NIE hardkodować 50 Hz)

RC_valid := channel_valid(CH1) AND channel_valid(CH2)
            # CH4 celowo poza predykatem (R12)
```
Utrata **CH1 lub CH2** (brak edge / zła szerokość / zły okres) → `RC_valid = false`
→ FAILSAFE.

## Łańcuchy przetwarzania sygnału

**Gaz (CH2 → ESC):**
```
1. raw CH2 PWM
2. walidacja kanału
3. normalizacja przez kalibrację wejścia RC → signed [-1000, +1000]
4. deadband gazu                     (na targecie, PRZED skalowaniem/rampą)
5. reverse gazu (jeśli włączony)
6. limit mocy przód/tył              (PRZED rampą)
   ── TARGET ──
7. override stanu (na TARGET):
     DISARMED        → target = 0
     FAILSAFE        → target = 0
     ARMED           → target z kroku 6
     ESC_CALIBRATION → target z sekwencji stałej (pomija 1–6)
8. rampa soft-start / soft-stop do targetu
9. mapowanie na PWM ESC przez kalibrację wyjścia WP880
10. HARD CLAMP (ostatni, bezwarunkowy — SI-3)
11. LEDC → GPIO19
```

**Serwo (CH1 → serwo):**
```
1. raw CH1 PWM
2. walidacja kanału
3. normalizacja → [-1000, +1000]
4. deadband steru (default 0)
5. reverse steru (jeśli włączony)
6. endpointy / trim serwa
7. override stanu (na TARGET):
     RC valid  → target z CH1
     FAILSAFE  → target = center
8. slew-limit do targetu
9. mapowanie na PWM serwa
10. HARD CLAMP (ostatni, bezwarunkowy — SI-3)
11. LEDC → GPIO18
```

Zasady stałe: override działa na **target** (nie bezpośredni output) → rampa/slew
realizuje płynne przejście do neutralu/centrum; clamp zawsze ostatni; deadband przed
limitami i rampą; reverse po deadbandzie (neutral zostaje neutralem); limity przed
rampą (rampa dąży do już ograniczonego targetu).

## Inwarianty bezpieczeństwa

- **SI-1. Reset = stan bezpieczny.** Każdy boot (power-on, watchdog, brownout,
  crash) → DISARMED, ESC neutral, serwo center, wymagane ponowne armowanie. TWDT to
  mechanizm **recovery, nie bezpieczeństwa** — nie chroni przed niekontrolowanym
  gazem, tylko odzyskuje system.
- **SI-2. Neutral firmware ⇒ fizycznie zero ciągu na WP880.** Używamy **zmierzonego**
  neutralu/pasma stop, nie teoretycznego 1500 µs. Weryfikacja eksperymentalna
  (Plan pomiarów): w DISARMED i po soft-stop FAILSAFE śmigło fizycznie stoi.
- **SI-3. Hard clamp to granica bezpieczeństwa, nie transformacja.**
  **No code path may bypass the output clamp.** Żaden błąd logiki, skorumpowane NVS,
  override ani tryb serwisowy nie wypchnie sygnału poza dozwolone okno PWM.
- **SI-4. Utrata któregokolwiek z CH1/CH2 → FAILSAFE.**
- **SI-5. ESC_CALIBRATION nigdy nie odpala się automatycznie** i zawsze wymaga
  zdjętego śmigła / odłączonego silnika + jawnego potwierdzenia.
- **SI-6. Pętla sterująca jest jedynym pisarzem aktywnych parametrów;** WWW tylko
  zgłasza zwalidowany pending (eliminuje wyścig WWW↔pętla bez mutexów).

## Kryteria sukcesu
- Serwo i ESC podążają za drążkami przez ESP32 (R1) — bez ESP32 brak sterowania.
- Ruch serwa widocznie wolniejszy/płynniejszy niż surowy RC (R3).
- Rampy gazu obserwowalne, brak szarpnięć przy starcie i zatrzymaniu (R4).
- Drobne ruchy wokół środka nie ruszają silnika (R5).
- Utrata CH1 **lub** CH2 → gaz soft-stopuje do neutralu, serwo zjeżdża do środka,
  LED sygnalizuje failsafe, stan trwa do powrotu RC (R6, R8, SI-4).
- Po starcie i po failsafe napęd nie rusza dopóki nie spełniony warunek arm (R7).
- W DISARMED i po soft-stop FAILSAFE śmigło **fizycznie stoi** na zmierzonym
  neutralu (SI-2).
- Panel WWW osiągalny po WPA2 AP, live, edycja zablokowana poza DISARMED, zmiany
  przeżywają restart (R9, R10, R11, R14, R17).
- Pusty/skorumpowany NVS → praca na bezpiecznych defaultach z flagą UNCALIBRATED,
  nigdy na śmieciach (R16).
- ESC_CALIBRATION kalibruje WP880 do wyjścia ESP32, krokowo, z abortem na utracie
  RC (R15).
- E-stop odcina napęd, ESP32 żyje i raportuje FAILSAFE (R13).

## Granice scope'u
- Brak logiki trybów na CH4 — tylko odczyt/wyświetlanie (tryby = V2).
- Brak chmury / internetu — wyłącznie lokalny AP (WPA2).
- Brak logowania / kont / multi-user na panelu (ale AP **nie** otwarty — R14).
- Brak GPS / autopilota / nawigacji / trzymania kursu.
- Brak logowania/zapisu telemetrii na SD lub w chmurze.
- Brak OTA — poza V1.
- Brak sprzętowego bypassu sygnału PWM (programowy failsafe + fizyczny e-stop).
- Niezależne zasilanie odbiornika (żywe RC po e-stopie) — opcjonalne, poza V1.

## Kluczowe decyzje (rozstrzygnięcia v2)
1. SPOF ESP32: programowo (TWDT recovery + boot do DISARMED + re-arm + clamp) +
   fizyczny e-stop. Bez bypassu PWM.
2. Najgroźniejszy tryb awarii = **zamrożony poprawny PWM** (ESC widzi zdrowy sygnał,
   własny failsafe ESC się nie odpala) — ważniejszy niż utrata PWM.
3. Odbiornik **musi** gasić PWM przy utracie RF (twardy wymóg; inaczej wymiana).
4. Serwo w failsafe: slew do środka, nie hold-last.
5. Serwo odsprzężone od arming (RC valid → żyje; arming bramkuje tylko energię ESC).
6. Wyjście z FAILSAFE na warunku „RC odzyskane", nie „soft-stop zakończony".
7. `RC_valid = CH1 AND CH2`, z walidacją szerokości i okresu; CH4 wykluczone.
8. NVS bootstrap → bezpieczne defaulty + UNCALIBRATED; walidacja przy odczycie.
9. AP WPA2-PSK, bez logowania.
10. Kalibracja wyjścia ESC jako realny parametr/pomiar, nie założenie 1500 µs.
11. Override na target przed rampą/slew; clamp ostatni; deadband przed skalowaniem.
12. Przejście przód↔tył samą rampą; neutral-dwell tylko jeśli pomiar wymusi.
13. Bramka zapisu = DISARMED; delayed commit NVS; pętla = jedyny właściciel params.
14. WP880 kalibrowany do ESP32 (stały zakres), nie do odbiornika; tryb krokowy.
15. ESC_CALIBRATION jako osobny stan, krokowy, z abortem na utracie RC.

## Zależności / Założenia (sprzęt)
- Mapowanie pinów (ustalone wejście): GPIO34←CH1, GPIO35←CH2, GPIO32←CH4,
  GPIO18→serwo, GPIO19→ESC, wspólna masa ESP32/odbiornik/ESC.
- GPIO34/35/32 wejściowe-tylko (bez wewn. pull-up) — OK dla odczytu RC (~50 Hz PWM).
- Zasilanie: **ESP32 z osobnego tapu 12V przed kill-switchem** (R13); buck 5V→ESP32;
  BEC 6V z WP880→odbiornik i serwo (downstream WP880 → ginie z e-stopem); LiFePO4
  12V 100Ah. Kill-switch tnie napęd (WP880), zostawia ESP32.
- Serwo DS3240 (40 kg, 270°) — pełny zakres mechaniczny większy niż ster, stąd
  endpointy serwa w R10.
- WP880 ma własną procedurę kalibracji radia (neutral → full fwd → full rev),
  obsługiwaną przez tryb ESC_CALIBRATION (R15).
- Jeden operator, otwarta woda, brak wymagań certyfikacyjnych.

## Plan pomiarów (przed implementacją odpowiednich części)

**WP880 (na realnej parze DevKit + WP880, oscyloskop + obserwacja ESC):**
1. Podaj 1500 µs — czy silnik fizycznie stoi; wyznacz faktyczny neutral i pasmo.
2. Próg startu przód (rosnące PWM), próg startu tył (malejące PWM).
3. Zachowanie przy **utracie PWM** (odłącz sygnał) — do neutralu? po jakim czasie?
4. Zachowanie przy **zamrożonym poprawnym PWM** (zatrzymaj rdzeń, LEDC leci ~70%) —
   dowód, że okno TWDT to okno niekontrolowanego gazu.
5. Czy WP880 wymaga własnej kalibracji throttle range (i jak współgra z R15).
6. Zachowanie przy bootowym neutralu.
7. **Szybka zmiana full forward → full reverse** — cutout? skok prądu? plugging?
   szarpnięcie mechaniczne? → decyzja o `reverseNeutralDwellMs`.
8. Akceptacja: DISARMED i po soft-stop FAILSAFE — śmigło nie kręci się (SI-2).

**ESP32:**
- Stan GPIO18/GPIO19 podczas reset / boot / bootloadera (oscyloskop).
- Zachowanie LEDC po watchdog-resecie (peryferium vs CPU reset, glitch).
- **Brownout-sag** — stan pinów podczas zapadania napięcia (najmniej udokumentowane).
- **Długość martwego okna boot** (reset → re-init LEDC) jako liczba.
- Pull-down na GPIO19 jako ruch bez ryzyka (linia ESC nie może pływać).

**Odbiornik:**
- Zachowanie przy utracie RF: gaśnie / hold-last / preset (oscyloskop CH1/CH2).
  Wymóg: brak PWM przy utracie RF (inaczej wymiana). Skonfigurować failsafe
  odbiornika.
- **Zmierzony okres ramki** (do progu okresu w `RC_valid`) — NIE zakładać 20 ms.

## Odłożone do planowania (zadania wykonawcze, nie decyzje produktowe)
- Transport telemetrii (WebSocket vs polling) + akceptowalna częstotliwość/latencja.
- Sposób odczytu PWM (interrupty / RMT / pulseIn) i generacji (LEDC) bez zaburzania
  pętli ~50 Hz; ewentualny pinning Core0/Core1 — **tylko jeśli** pomiar pokaże
  jitter/starvation (domyślnie jedna pętla, bez RTOS-tasków).
- Stos WWW na ESP32 (async web server) i format wymiany parametrów.
- Schemat NVS + wersjonowanie/migracja.
- Konkretne wzory migania LED (uzbrojony / rozbrojony / failsafe / uncalibrated /
  calibration).
- Konkretne zakresy walidacji parametrów na granicy zapisu i odczytu.

## Następne kroki
→ `/dev-plan` do planowania technicznego implementacji (podział modułów, API,
  testy integracyjne i bezpieczeństwa), zrównoleglony z Planem pomiarów sprzętu.
