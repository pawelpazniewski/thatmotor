---
title: "feat: Telemetria w aplikacji iOS — HUD + arkusz szczegółów + trim serwa"
type: feat
status: active
date: 2026-07-03
origin: docs/dev-brainstorms/2026-07-03-ios-telemetry-hud-requirements.md
---

# feat: Telemetria w aplikacji iOS — HUD + arkusz szczegółów + trim serwa

## Przegląd

Aplikacja iOS już odbiera strumień telemetrii WebSocket (~10 Hz) i używa części pól, ale
nie prezentuje najważniejszych danych na wodzie. Dodajemy **dwupoziomowy** widok telemetrii,
w całości reużywając istniejącego strumienia i design systemu (SunlightTheme):

1. **Kompaktowy HUD „rzut oka"** w wolnym lewym górnym rogu mapy — jakość GPS, stan systemu,
   aktywny tryb + dystans/namiar do celu, prędkość + kurs.
2. **Dolny arkusz** (`presentationDetents`) otwierany tapnięciem HUD — kurowany zestaw
   szczegółów + regulacja neutrala serwa (trim).

Środek mapy pozostaje czysty. Panel WWW zostaje narzędziem warsztatowym (surowe µs / NVS).

## Ujęcie problemu

Telemetria jest dziś tylko w panelu WWW — pulpit „przy biurku". Na wodzie sterujemy z telefonu,
ale nie widać w locie czy GPS trzyma, czy silnik uzbrojony, w jakim trybie jest łódka, ile do
celu. Dane już płyną — brakuje prezentacji. Dodatkowo chcemy móc wyregulować neutral serwa
(żeby łódka jechała prosto) bez sięgania po laptop i panel WWW.
(zob. źródło: docs/dev-brainstorms/2026-07-03-ios-telemetry-hud-requirements.md)

## Śledzenie wymagań

- **R1.** Kompaktowy HUD zawsze widoczny w lewym górnym rogu: jakość GPS (fix+satelity),
  stan systemu (DISARMED/ARMED/FAILSAFE), tryb (spot-lock/goto off/aktywny/pauza) + dystans (m)
  + namiar (°) do celu, prędkość (m/s) + kurs (°).
- **R2.** HUD nie zasłania środka mapy; siedzi tylko w lewym górnym rogu, nie nachodzi na chip
  połączenia (górny środek), waypointy (góra-prawo), zoom (prawa krawędź), pasek sterowania (dół).
- **R3.** Tapnięcie HUD otwiera natywny dolny arkusz (detenty pół/pełna wysokość), chowany gestem.
- **R4.** Arkusz (kurowany): pełny GPS (fix/sat/prędkość/pozycja), kalibracja IMU + OK, pełny
  spot-lock (stan/błąd/namiar), pełne goto (stan/błąd/namiar/cel/dotarto), RC valid, świeżość
  linku. Wyklucza surowe µs RC, okresy, servo/esc µs, NVS/źródło ustawień.
- **R5 (ZREWIDOWANE).** Regulacja neutrala serwa krokowo (−/+ , `trim_left`/`trim_right`) +
  zapis (`trim_save`), z bieżącym odczytem `servo_trim_us`. **Aktywna wyłącznie gdy DISARMED**;
  przy ARMED/innym stanie sekcja wyszarzona z notką „rozbrój, aby wyregulować".
  *Rewizja względem źródła:* źródło zakładało „na żywo, niezależnie od stanu, ostrzeżenie bez
  blokady". Firmware stosuje trim tylko po rozbrojeniu (`loop_should_apply_pending` → tylko
  `SM_STATE_DISARMED`; przy ARMED komendy wracają 200 OK, ale są po cichu ignorowane, brak
  zapisu NVS). Wybór użytkownika (2026-07-03): dostosować aplikację do gate'u firmware, bez
  zmian w kodzie safety-critical.
- **R6.** Nieświeżość: gdy link stale/rozłączony (lub brak ramki), HUD i arkusz pokazują
  myślniki / wyszarzenie zamiast zamrożonych liczb.
- **R7.** Reużycie SunlightTheme: mrożone panele, SF Pro rounded, cyfry monospace, kontrast pod
  słońce, touch-targety ≥44 pt, poszanowanie reduced-motion.

## Granice scope'u

- Brak pełnego parytetu z panelem WWW — surowe µs RC, okresy, servo/esc µs, flagi NVS / źródło
  ustawień zostają wyłącznie w panelu WWW.
- Brak komendy „arm" z aplikacji (cienki klient bez zmian).
- Brak nowych pól telemetrii ani zmian w firmware (poza konsumpcją istniejących pól).
- **Brak zmian w gate'cie DISARMED trimu** (świadome, wynik decyzji R5).
- Brak wykresów/historii/logów — tylko wartości chwilowe.

## Kontekst i research

### Relevantny kod i wzorce

- **Kontrakt telemetrii:** `ios/KayakKit/Sources/KayakContract/Telemetry.swift` — struct
  `Telemetry: Decodable`, jawne `CodingKeys`, computed helpery (`headingDegrees`,
  `speedMetersPerSecond`, `gotoBearingDegrees`). Enumy `SystemState`, `ArmReason`, `HoldState`
  (mają case `.unknown = -1`). Obecnie **NIE** dekoduje: `imu_calib`, `spot_lock_err_m`,
  `spot_lock_bearing_deg10`, `servo_trim_us`, `app_link_fresh` jest już obecne, `spot_lock_state`
  już jest.
- **Kontrakt komend:** `ios/KayakKit/Sources/KayakContract/Command.swift` — enum `Command`
  (`goto`/`gotoCancel`/`disarm`/`hold`), `cmdName` + `httpBody()` (JSONSerialization). Wysyłka:
  `ios/KayakMotor/Networking/CommandClient.swift` (`send(_:)`, walidacja koperty błędu).
- **Koordynator:** `ios/KayakMotor/App/AppModel.swift` (`@MainActor @Observable`) — trzyma
  `telemetry: TelemetryStore`, metody `requestGoto`/`stopGoto`/`disarm`/`requestSpotLock`
  wołające `commands.send(...)` w try/catch z `ApiError`.
- **Store:** `ios/KayakMotor/Features/Telemetry/TelemetryStore.swift` — `latest: Telemetry?`,
  `linkState: LinkState` (`.disconnected/.joining/.connected/.stale`, próg
  `GotoTiming.commsTimeoutSeconds`).
- **Layout:** `ios/KayakMotor/RootView.swift` — `ZStack(alignment: .top)`: `LakeMapView`
  (`.ignoresSafeArea`), `topBar` (HStack: statusChip lewo / waypoints prawo), `zoomControls`
  (VStack prawa krawędź), `console` (VStack dół). Sheet waypointów: `.sheet(isPresented:)` bez
  detentów — wzorzec do rozszerzenia o `.presentationDetents([.medium, .large])`.
- **Wzorzec renderu statusu:** `ios/KayakMotor/Features/Goto/ControlBarView.swift` —
  `statusPill(_:color:icon:)` z `SunlightTheme.rounded(13,.semibold).monospacedDigit()`,
  format `"%d m · %.0f°"`. To jest wzorzec, który HUD i arkusz mają naśladować.
- **Design tokeny:** `ios/KayakMotor/DesignSystem/SunlightTheme.swift` — `panelBackground`,
  `hairline`, `panelRadius`/`cornerRadius`, `minHitTarget=56`, `rounded(size,weight)`, kolory
  semantyczne (`gotoColor`, `disarmColor`, `stopColor`), shadow `.black.opacity(0.25)`.

### Kontrakt komendy trimu (firmware — do reużycia 1:1)

- Endpoint: `POST /api/command`, body `{"cmd":"trim_left"}` / `{"cmd":"trim_right"}` /
  `{"cmd":"trim_save"}` (`web/app.js`, `components/web_panel/src/command_parse.c`).
- Krok: `SERVO_TRIM_STEP_US = 7` µs; zakres `SERVO_TRIM_MAX_US = ±300` µs; domyślnie 0
  (`components/settings/include/settings_model.h`).
- Gating: `control_loop.c apply_trim_events` → `loop_should_apply_pending(state)` == tylko
  `SM_STATE_DISARMED`. Przy ARMED: 200 OK, ale **cicho ignorowane**. `trim_save` wymusza commit
  NVS (poza debounce); left/right commit po debounce (~300 ms) — też tylko DISARMED.
- Nowa wartość `servo_trim_us` wraca w następnej ramce telemetrii (~10 Hz) → odczyt „na żywo".

### Wiedza instytucjonalna

- **Pure ⊥ HAL / testowalność:** logika formatu i staleness ma być czystymi funkcjami w
  `KayakContract` (target ma testy: `ios/KayakKit/Tests/KayakContractTests/`), a widoki SwiftUI
  cienkie. Zgodne z `docs/solutions/testing-issues/...pure-hal-separation` i regułą projektu
  „jeden moduł = jedna odpowiedzialność; logika biznesowa poza plikiem widoku".
- **Moc wyroczni w testach:** testy formatu/staleness muszą failować po usunięciu transformacji
  (np. stale→myślnik testuj wejściem stale, nie fresh).

### Referencje zewnętrzne

- Nie prowadzono — codebase ma silne lokalne wzorce (istniejący HUD-owy `statusPill`, sheet
  waypointów, kontrakt telemetrii). SwiftUI `presentationDetents` to standard iOS 16+ (target 17).

## Kluczowe decyzje techniczne

- **Logika w kontrakcie, widoki cienkie:** formatowanie pól (dystans, namiar, prędkość, kurs,
  stan→etykieta, jakość GPS) i decyzja „pokaż wartość vs myślnik przy stale" jako czyste funkcje
  w `KayakContract` (nowy `TelemetryDisplay.swift`), testowane w `KayakContractTests`. HUD i
  arkusz tylko składają gotowe stringi. Uzasadnienie: target app nie ma testów; logika w
  kontrakcie jest host-testowalna, spójna z resztą projektu.
- **Trim = krokowe komendy, nie set-value:** dodajemy `Command.trimLeft/.trimRight/.trimSave`
  (bez parametrów) — model natywny firmware. Odrzucamy sugerowany `setServoTrim(Int)`: firmware
  nie przyjmuje wartości, tylko inkrementy.
- **Gate trimu po stronie UI = odczyt `state == .disarmed`** z telemetrii; przy innym stanie
  sekcja `.disabled(true)` + notka. Nie polegamy na tym, że firmware „zwróci błąd" (bo nie
  zwraca — cicho ignoruje).
- **HUD jako osobna nakładka ZStack** z `.frame(maxWidth:.infinity,maxHeight:.infinity,
  alignment:.topLeading)`, nie wewnątrz `topBar` — żeby nie kolidować z chipem połączenia
  (górny środek) i rosnąć w pionie niezależnie (R2).
- **Dekodowanie nowych pól przez `decodeIfPresent` + wartości domyślne** — spójne z obecną
  filozofią „nieznane/niekompletne ramki nie wywalają dekodera"; chroni przed starszym firmware.
- **Chip połączenia zostaje osobny** od HUD (stan linku ≠ telemetria).
- **Dublet tryb+dystans+namiar:** HUD jest pojedynczym domem tej informacji dla obu trybów
  (spot-lock i goto). Pasek goto na dole zachowuje własną linię statusu jako kontekst akcji
  (blokady/błędy wysyłki) — świadomy, wąski dublet tylko dla aktywnego goto (patrz Odroczone).

## Otwarte pytania

### Rozwiązane podczas planowania

- **Jak aplikacja ustawia trim?** → krokowe komendy `trim_left/right/save` na `POST /api/command`
  (potwierdzone w firmware). Nie ma set-value.
- **Czy trim działa przy ARMED?** → nie; tylko DISARMED. UI gate'uje wg `state` (decyzja R5).
- **Które pola dołożyć do `Telemetry`?** → `imu_calib`, `spot_lock_err_m`,
  `spot_lock_bearing_deg10`, `servo_trim_us` (reszta potrzebnych pól już jest).
- **Jednostka prędkości?** → m/s (spójnie z istniejącym `speedMetersPerSecond`).

### Odroczone do implementacji

- Dokładny układ wierszy/typografia HUD (ile w jednej linii, ikony vs etykiety) — dopracować na
  żywym ekranie; wymaganie: czytelne pod słońcem, monospace na cyfrach.
- Ostateczna decyzja o dublecie tryb+dystans między HUD a paskiem goto — czy zwęzić linię paska
  goto do samych blokad/błędów, gdy HUD i tak pokazuje dystans. Rozstrzygnąć wizualnie w trakcie.
- Nazwy nowych typów/helperów (`TelemetryDisplay`, `HudView`, `TelemetryDetailView`) — robocze.
- Czy `trim_save` osobnym przyciskiem, czy auto po serii klików — MVP: osobny „Zapisz",
  spójnie z panelem WWW; ewentualne auto-save do rozważenia po testach.

## Implementation Units

- [ ] **Unit 1: Rozszerz kontrakt telemetrii o brakujące pola**

**Cel:** Dekodować `imu_calib`, `spot_lock_err_m`, `spot_lock_bearing_deg10`, `servo_trim_us`
i wystawić computed helpery do prezentacji.

**Wymagania:** R1, R4, R5

**Zależności:** Brak

**Pliki:**
- Modyfikuj: `ios/KayakKit/Sources/KayakContract/Telemetry.swift`
- Test (unit): `ios/KayakKit/Tests/KayakContractTests/TelemetryDecodingTests.swift`

**Podejście:**
- Dodaj stored properties: `imuCalib: Int`, `spotLockErrM: Int`, `spotLockBearingDeg10: Int`,
  `servoTrimUs: Int`. Dodaj do `CodingKeys` (`imu_calib`, `spot_lock_err_m`,
  `spot_lock_bearing_deg10`, `servo_trim_us`).
- W `init(from:)` dekoduj nowe pola przez `decodeIfPresent(..., forKey:) ?? default` (0),
  by starsze/niekompletne ramki nie wywalały dekodera.
- Dodaj computed: `spotLockBearingDegrees`, ewentualnie `servoTrimMicroseconds` (jeśli warto).

**Wzorce do naśladowania:**
- Istniejące `CodingKeys` + computed w tym samym pliku (`gotoBearingDegrees`).

**Scenariusze testowe:**
- [Unit] Pełna ramka z nowymi polami → `imuCalib/spotLockErrM/spotLockBearingDeg10/servoTrimUs`
  zdekodowane poprawnie (wartości ≠ 0, by test miał moc wyroczni).
- [Unit] Ramka BEZ nowych pól (stary firmware) → dekoder nie rzuca, nowe pola = 0.
- [Unit] `servo_trim_us` ujemny (np. −140) → zachowany znak (int16 semantyka).

**Weryfikacja:** `swift test` w KayakKit zielony; nowe pola dostępne na `Telemetry`.

---

- [ ] **Unit 2: Rozszerz słownik komend o trim + metody w AppModel**

**Cel:** Umożliwić wysłanie `trim_left`/`trim_right`/`trim_save` z aplikacji.

**Wymagania:** R5

**Zależności:** Brak (równolegle do Unit 1)

**Pliki:**
- Modyfikuj: `ios/KayakKit/Sources/KayakContract/Command.swift`
- Modyfikuj: `ios/KayakMotor/App/AppModel.swift`
- Test (unit): `ios/KayakKit/Tests/KayakContractTests/HTTPCommandRequestTests.swift`

**Podejście:**
- Dodaj `case trimLeft, trimRight, trimSave` do `Command`. Mapuj `cmdName` →
  `"trim_left"/"trim_right"/"trim_save"`. `httpBody()` bez dodatkowych pól (jak `disarm`/`hold`).
- W `AppModel` dodaj metody `trimLeft()/trimRight()/saveTrim()` wołające `commands.send(...)`
  w try/catch (wzorzec `disarm()`), z `lastActionMessage` na błąd/potwierdzenie.

**Wzorce do naśladowania:**
- `Command.hold`/`.disarm` (bezparametrowe) + `AppModel.disarm()`.

**Scenariusze testowe:**
- [Unit] `Command.trimLeft.httpBody()` → `{"cmd":"trim_left"}` (posortowane klucze).
- [Unit] `.trimSave.httpBody()` → `{"cmd":"trim_save"}`.
- [Unit] Żaden trim-command nie dokłada `lat_e7`/`lon_e7`.

**Weryfikacja:** Testy zielone; `AppModel` eksponuje trzy metody trimu.

---

- [ ] **Unit 3: Czysty moduł prezentacji telemetrii (format + staleness)**

**Cel:** Host-testowalna logika: mapowanie pól na etykiety/stringi oraz decyzja
„wartość vs myślnik" zależna od świeżości.

**Wymagania:** R1, R4, R6

**Zależności:** Unit 1 (pola)

**Pliki:**
- Stwórz: `ios/KayakKit/Sources/KayakContract/TelemetryDisplay.swift`
- Test (unit): `ios/KayakKit/Tests/KayakContractTests/TelemetryDisplayTests.swift`

**Podejście:**
- Czyste funkcje (bez SwiftUI): `gpsQualityText(fix,sats)`, `stateLabel(SystemState)`,
  `modeLabel(spotLock:goto:)` + `targetText(errM:bearingDeg10:)` (format `"%d m · %.0f°"`),
  `speedHeadingText(...)`, `trimText(servoTrimUs)`.
- Funkcja bramkująca świeżość: `displayed(_ value:String, isFresh:Bool) -> String` zwracająca
  `"—"` gdy `!isFresh`. `isFresh` wyprowadzone z `LinkState` (fresh tylko dla `.connected`).
- Enum/util mapujący `LinkState` → `isFresh: Bool` (jeśli nie istnieje).

**Notatka wykonawcza:** Implementuj test-first — najpierw testy formatu i staleny, potem funkcje
(moc wyroczni: stale MUSI dać myślnik, nie da się przejść bez transformacji).

**Wzorce do naśladowania:**
- Format `"%d m · %.0f°"` z `ControlBarView.statusLine`.
- Istniejące pure typy w KayakContract (`AutonomousModeReconciler`, `GotoReadiness`).

**Scenariusze testowe:**
- [Unit] `stateLabel(.armed)` → „ARMED"; `.failsafe` → „FAILSAFE".
- [Unit] `targetText(errM:123,bearingDeg10:450)` → „123 m · 45°".
- [Unit] `displayed("5.0 m/s", isFresh:false)` → „—"; `isFresh:true` → „5.0 m/s".
- [Unit] `modeLabel` dla spot-lock aktywny vs goto pauza vs off → różne, poprawne etykiety.
- [Unit] `gpsQualityText(fix:false, sats:0)` → czytelne „brak fix" (nie „0 sat" mylące).

**Weryfikacja:** Testy zielone; widoki mogą składać stringi bez własnej logiki.

---

- [ ] **Unit 4: Kompaktowy HUD w lewym górnym rogu**

**Cel:** Zawsze widoczny HUD (R1) w wolnej strefie (R2), degradujący przy stale (R6),
tapnięcie otwiera arkusz (R3).

**Wymagania:** R1, R2, R3, R6, R7

**Zależności:** Unit 1, Unit 3

**Pliki:**
- Stwórz: `ios/KayakMotor/Features/Telemetry/HudView.swift`
- Modyfikuj: `ios/KayakMotor/RootView.swift`
- Test (e2e): `Scenariusz: uruchom aplikację → HUD widoczny w lewym górnym rogu; środek mapy
  czysty; tap w HUD otwiera dolny arkusz; przy braku danych HUD pokazuje myślniki.`

**Podejście:**
- `HudView` czyta `TelemetryStore` (`latest`, `linkState`), składa 4 wiersze przez
  `TelemetryDisplay`: GPS (fix+sat), stan systemu, tryb+dystans/namiar, prędkość+kurs.
  Panel: `SunlightTheme.panelBackground` w `RoundedRectangle(cornerRadius: panelRadius)`,
  `hairline` border, shadow, `rounded(...).monospacedDigit()`, wysoki kontrast.
- W `RootView` dodaj do `ZStack` osobną nakładkę z `.frame(maxWidth:.infinity,
  maxHeight:.infinity, alignment:.topLeading)` + `.padding(.horizontal,14)` i offset pod chipem
  połączenia, by nie kolidować z górnym środkiem. `onTapGesture` → `showTelemetryDetail = true`.
- Cały HUD to touch-target otwierający arkusz (≥44 pt); respektuj reduced-motion (bez zbędnych
  animacji wartości).

**Wzorce do naśladowania:**
- `statusChip` (panel w RootView) + `statusPill` (ControlBarView) dla stylu i typografii.

**Scenariusze testowe:**
- [E2E] Start → HUD w lewym górnym rogu, nie zasłania środka mapy, nie nachodzi na zoom/waypoints.
- [E2E] Tap HUD → wysuwa się dolny arkusz; swipe w dół → chowa, mapa znów czysta.
- [E2E] Rozłącz link (lub brak ramki) → wartości HUD zmieniają się w „—", nie zamrażają.
- [Unit] (pośrednio) logika stringów pokryta w Unit 3.

**Weryfikacja:** HUD renderuje 4 wiersze z żywej telemetrii; myślniki przy stale; tap otwiera
arkusz; środek mapy niezasłonięty.

---

- [ ] **Unit 5: Dolny arkusz szczegółów + sekcja trimu serwa**

**Cel:** Kurowany zestaw szczegółów (R4) + regulacja neutrala serwa gated DISARMED (R5),
z myślnikami przy stale (R6).

**Wymagania:** R3, R4, R5, R6, R7

**Zależności:** Unit 1, Unit 2, Unit 3, Unit 4

**Pliki:**
- Stwórz: `ios/KayakMotor/Features/Telemetry/TelemetryDetailView.swift`
- Modyfikuj: `ios/KayakMotor/RootView.swift` (dodaj `.sheet(isPresented:$showTelemetryDetail)`
  z `.presentationDetents([.medium, .large])`)
- Modyfikuj: `ios/KayakMotor/App/AppModel.swift` (ekspozycja telemetrii do widoku + metody trimu
  z Unit 2; ewentualny stan `showTelemetryDetail` jeśli trzymany w modelu)
- Test (unit): `ios/KayakKit/Tests/KayakContractTests/TelemetryDisplayTests.swift` (gate trimu
  jako czysta funkcja `isTrimEnabled(state:) -> Bool`)
- Test (e2e): `Scenariusz: otwórz arkusz → widoczne grupy GPS/kompas/spot-lock/goto/RC; sekcja
  trimu aktywna tylko gdy DISARMED, w innym stanie wyszarzona z notką.`

**Podejście:**
- Arkusz w stylu `WaypointListView` (NavigationStack + List/sekcje, `.navigationTitle`,
  przycisk „Gotowe"). Grupy sekcji: **GPS** (fix, sat, prędkość, lat/lon), **Kompas** (kurs,
  kalibracja IMU 0–3, OK), **Spot-lock** (stan, błąd m, namiar °), **Goto** (stan, błąd, namiar,
  cel lat/lon, dotarto), **Link/RC** (RC valid, świeżość linku). Wszystkie wartości przez
  `TelemetryDisplay`, myślniki przy stale.
- **Sekcja Trim serwa:** odczyt `servoTrimUs` (µs), przyciski −/+ (`trimLeft/trimRight`) i
  „Zapisz" (`trimSave`). Cała sekcja `.disabled(!isTrimEnabled(state:))`; gdy wyłączona — notka
  „Rozbrój, aby wyregulować neutral". `isTrimEnabled(state:) = state == .disarmed` jako czysta
  funkcja w KayakContract (testowalna).
- Bez surowych µs RC / okresów / NVS (granica scope'u R4).

**Wzorce do naśladowania:**
- `WaypointListView` (struktura sheet + NavigationStack + toolbar „Gotowe").
- `iconButton`/`.disabled(!enabled).opacity(...)` z `ControlBarView` dla przycisków trimu.

**Scenariusze testowe:**
- [Unit] `isTrimEnabled(.disarmed)` == true; `.armed`/`.failsafe`/`.escCalibration`/`.deploy`
  == false (moc wyroczni: mutacja na „zawsze true" MUSI failować).
- [E2E] DISARMED → tap „+" → po ~100 ms `servo_trim_us` w arkuszu rośnie o krok (odczyt z ramki).
- [E2E] ARMED → sekcja trimu wyszarzona, przyciski nieaktywne, notka widoczna.
- [E2E] Otwórz arkusz przy stale linku → pola pokazują „—", nie stare liczby.
- [E2E] „Zapisz" (DISARMED) → brak błędu (koperta 200), potwierdzenie/`lastActionMessage`.

**Weryfikacja:** Arkusz pokazuje kurowany zestaw; trim działa tylko DISARMED z żywym odczytem;
myślniki przy stale; detenty pół/pełna działają.

## Wpływ systemowy

- **Graf interakcji:** nowe komendy trafiają w istniejący `CommandClient.send` → `POST
  /api/command` → `command_parse` → control loop. Zero nowych ścieżek sieciowych; zero zmian
  firmware.
- **Propagacja błędów:** trim przy ARMED zwraca 200 (brak `ApiError`) mimo braku efektu — dlatego
  gate jest po stronie UI (nie oczekujemy błędu z serwera). Błędy sieci/koperty → `lastActionMessage`
  wzorcem `disarm()`.
- **Ryzyka cyklu życia stanu:** telemetria jest lossy (~10 Hz, drop przy zaległościach) — HUD i
  arkusz muszą tolerować brak ramki (R6, `latest == nil` → myślniki). `servo_trim_us` po kliknięciu
  wraca dopiero w kolejnej ramce — UI nie zakłada natychmiastowej wartości lokalnej, tylko odczyt
  z telemetrii (jedno źródło prawdy).
- **Parytet surface API:** panel WWW pozostaje niezmieniony; oba klienty dzielą ten sam endpoint
  i słownik komend.
- **Pokrycie integracyjne:** gate DISARMED, staleness→myślnik i tap→arkusz to zachowania cross-layer
  — pokryte scenariuszami [E2E] powyżej; czysta logika (format/gate) w testach [Unit].

## Ryzyka i zależności

- **Rozbieżność kontraktu:** jeśli nazwy pól w firmware JSON zmienią się, dekoder cicho da 0 —
  mitygacja: test dekodowania na realnej przykładowej ramce (Unit 1) i użycie dokładnych kluczy z
  inwentaryzacji.
- **Target app bez testów:** logika w widokach byłaby nietestowalna — mitygacja: cała logika w
  `KayakContract` (Unit 3), widoki cienkie. Scenariusze UI jako [E2E].
- **Detenty i bezpieczne obszary:** arkusz `.medium` nie może chować kluczowych akcji sterowania,
  gdy otwarty na wodzie — użytkownik chowa gestem; sterowanie (STOP/goto) i tak wraca po zamknięciu.
- **Sekwencjonowanie:** Unit 1 i 2 równolegle; 3 po 1; 4 po 1+3; 5 po 1+2+3+4.

## Dokumentacja / Notatki operacyjne

- Zaktualizować krótką notkę w README aplikacji iOS (jeśli istnieje) o HUD + arkuszu i o tym,
  że trim działa tylko po rozbrojeniu.
- Brak migracji/rolloutu — czysto kliencka zmiana w aplikacji, kompatybilna z obecnym firmware.

## Źródła i referencje

- **Dokument źródłowy:** [docs/dev-brainstorms/2026-07-03-ios-telemetry-hud-requirements.md](../dev-brainstorms/2026-07-03-ios-telemetry-hud-requirements.md)
- Kontrakt telemetrii/komend: `ios/KayakKit/Sources/KayakContract/{Telemetry,Command}.swift`
- Layout/wzorce UI: `ios/KayakMotor/RootView.swift`, `Features/Goto/ControlBarView.swift`,
  `DesignSystem/SunlightTheme.swift`, `Features/Waypoints/WaypointListView.swift`
- Kontrakt trimu (firmware): `components/web_panel/src/command_parse.c`,
  `components/control_loop/src/control_loop.c` (`apply_trim_events`),
  `components/settings/include/settings_model.h`, `components/signal_chain/src/servo_chain.c`
- Inwentaryzacja telemetrii WS: `components/web_panel/src/telemetry_json.c`,
  `components/control_loop/include/control_loop_snapshot.h`
