# Kontekst: Telemetria w aplikacji iOS — HUD + arkusz + trim serwa

Branch: `feature/ios-telemetry-hud`
Ostatnia aktualizacja: 2026-07-03 (Unit 5)

## Postęp
- **Unit 1 (ukończony 2026-07-03):** `Telemetry` rozszerzony o `imuCalib`, `spotLockErrM`,
  `spotLockBearingDeg10`, `servoTrimUs` (klucze `imu_calib`, `spot_lock_err_m`,
  `spot_lock_bearing_deg10`, `servo_trim_us`, dekodowane `decodeIfPresent(...) ?? 0`).
  Dodano computed `spotLockBearingDegrees`. 3 nowe testy dekodowania (pełna ramka,
  brak pól = 0, ujemny trim). `swift test` w KayakKit: 53/53 zielone.
  **Review fazy 1 (2026-07-03):** severity gate ✅ CZYSTE (P1=0, P2=0, P3=1). E2E N/A
  (natywny iOS, brak UI w Unit 1). Klucze JSON zweryfikowane 1:1 wobec firmware
  `components/web_panel/src/telemetry_json.c`; `servo_trim_us` emitowane `%d` (ze znakiem)
  — dekodowanie do `Int` poprawne. Ryzyko R1 (rozjazd kluczy) zamknięte. Jedyny nit:
  brak testu granicznego bearing 3600. Raport: `review-faza-1.md`.
- **Unit 2 (ukończony 2026-07-03):** `Command` rozszerzony o `.trimLeft/.trimRight/.trimSave`
  (mapowanie `cmdName` → `trim_left/trim_right/trim_save`); `httpBody()` niezmieniony —
  lat/lon bramkowane wyłącznie w gałęzi `.goto`, więc trim serializuje samo `{"cmd":...}`.
  W `AppModel` dodano `trimLeft()/trimRight()/saveTrim()` wzorcem `disarm()` (try/catch z
  `ApiError` → `lastActionMessage`). 3 nowe testy w `HTTPCommandRequestTests` (trim_left,
  trim_save, brak lat/lon dla całej trójki + `count==1`). `swift test`: 56/56 zielone.
  App target (`KayakMotor.xcodeproj`) buduje się na iphonesimulator (BUILD SUCCEEDED).
- **Unit 3 (ukończony 2026-07-03):** nowy czysty moduł
  `ios/KayakKit/Sources/KayakContract/TelemetryDisplay.swift` (enum-namespace, BEZ SwiftUI).
  Funkcje: `gpsQualityText(fix:sats:)` (brak fixu → „brak fix", nie „0 sat"),
  `stateLabel(_:)`, `modeLabel(spotLock:goto:)` (goto ma pierwszeństwo, potem spot-lock,
  potem „Ręczny"), `targetText(errM:bearingDeg10:)` („%d m · %.0f°"),
  `speedHeadingText(speedCms:headingDeg10:)`, `trimText(servoTrimUs:)` (znak `%+d µs`),
  `displayed(_:isFresh:)` → myślnik „—" gdy stale, `isFresh(_ link:)` (fresh TYLKO `.connected`),
  `isTrimEnabled(state:)` (TYLKO `.disarmed`). Test-first: 9 testów w
  `TelemetryDisplayTests.swift` z mocą wyroczni (stale→myślnik, gpsQuality bez fixu,
  isTrimEnabled tylko DISARMED, isFresh tylko connected). `swift test`: 65/65 zielone.
  **Review fazy 3 (2026-07-03):** severity gate ✅ CZYSTE (P1=0, P2=0, P3=3). E2E N/A
  (czyste funkcje, brak UI). Format `"%d m · %.0f°"` potwierdzony 1:1 z `ControlBarView.swift:41`;
  brak importu SwiftUI (Pure⊥HAL); switche wyczerpujące dla `SystemState`(6) i `LinkState`(4);
  priorytet `modeLabel` goto>spot-lock>off poprawny. Moc wyroczni testów zweryfikowana —
  mutacje bramek staleness/trim/fresh oraz konwersji deg10/cm-s failują (wejścia POZA wyjściem,
  450≠45, 150≠1.5). Trzy nity opcjonalne: (1) test priorytetu modeLabel przy obu aktywnych,
  (2) domknięcie stateLabel na wszystkie stany, (3) inline literały konwersji. Ryzyka R1/R4/R6
  domknięte na poziomie logiki prezentacji. Raport: `review-faza-3.md`.
- **Unit 4 (ukończony 2026-07-03):** nowy widok
  `ios/KayakMotor/Features/Telemetry/HudView.swift` — kompaktowy HUD (lewy górny róg).
  Bierze `TelemetryStore` (`latest`/`linkState`) i składa 4 wiersze WYŁĄCZNIE przez
  `TelemetryDisplay` (widok cienki, zero logiki formatu): (1) jakość GPS, (2) stan systemu,
  (3) tryb + dystans/namiar z aktywnego źródła (goto gdy `gotoState` ≠ off/unknown, inaczej
  spot-lock, inaczej sam tryb bez celu), (4) prędkość + kurs. Staleness (R6): jeden predykat
  `isFresh = TelemetryDisplay.isFresh(linkState)`; każdy wiersz guardowany na `latest != nil`
  i przepuszczony przez `displayed(_:isFresh:)` → myślnik przy stale/braku ramki (bez
  zamrażania). Styl reużyty z SunlightTheme: `panelBackground` w
  `RoundedRectangle(cornerRadius: cornerRadius)`, border `hairline`, shadow `.black.opacity(0.25)`,
  `rounded(14,.semibold).monospacedDigit()`; ikony w kolorze `brand`. Cały panel tapowalny
  (`contentShape(Rectangle())` + `onTapGesture`), `accessibilityElement(.combine)` +
  `.isButton`. Brak animacji wartości (reduced-motion respektowany biernie). W `RootView`:
  nowy `@State showTelemetryDetail`, osobna nakładka ZStack `hudOverlay`
  (`.frame(maxWidth/maxHeight:.infinity, alignment:.topLeading)` + `.padding(.horizontal,14)`
  + `.padding(.top,56)` — poniżej chipu połączenia, nie koliduje z waypointami/zoomem),
  oraz placeholder `.sheet(isPresented:$showTelemetryDetail)` z `Text("Szczegóły — Unit 5")`
  i `.presentationDetents([.medium,.large])` (pełny arkusz + trim dojdą w Unit 5).
  Walidacja: app target `xcodebuild ... build` → BUILD SUCCEEDED (po `xcodegen generate` —
  nowy plik wchodzi do targetu przez glob; `.xcodeproj` jest gitignore, nie commitowany);
  `swift test` KayakKit: 65/65 zielone (bez zmian — Unit 4 to sam widok). Scenariusze [E2E]
  (HUD w rogu, tap→arkusz, rozłącz→myślniki) wymagają symulatora — pozostawione do
  weryfikacji ręcznej/review, NIE odznaczone.
  **Review fazy 4 (2026-07-03):** severity gate ⚠️ ZASTRZEŻENIA (P1=0, P2=1, P3=3).
  E2E: 3 scenariusze wymagają ręcznej weryfikacji na symulatorze (natywny iOS, brak
  przeglądarki) — logika zweryfikowana STATYCZNIE jako poprawna (hudOverlay `.topLeading`
  offset 56 pod chipem, brak kolizji z zoom/waypoints, środek mapy czysty; tap→`.sheet`
  z detentami [.medium,.large]; `isFresh` fresh tylko `.connected` → „—" poza tym).
  Widok cienki potwierdzony: cały format przez `TelemetryDisplay`, brak duplikacji formatu,
  brak force-unwrap/`any`/sekretów, R6 staleness OK. Security/Performance CZYSTE.
  Jedyny P2: `HudView.target()` (wybór aktywnego źródła goto>spot-lock) duplikuje priorytet
  z `modeLabel` i siedzi w nietestowalnym targecie — Unit 5 potrzebuje tej samej decyzji,
  więc wyciągnąć do `TelemetryDisplay` jako czystą host-testowalną funkcję przed/w trakcie
  Unit 5 (inaczej potrójna duplikacja). Nity: accessibilityLabel nadpisuje wartości dla
  VoiceOver (dane dostępne przez arkusz), brak max-width panelu, cornerRadius vs panelRadius.
  Raport: `review-faza-4.md`.
- **Unit 5 (ukończony 2026-07-03):** nowy widok
  `ios/KayakMotor/Features/Telemetry/TelemetryDetailView.swift` — dolny arkusz szczegółów
  w stylu `WaypointListView` (`NavigationStack` + `List`/`Section` + toolbar „Gotowe" z
  `dismiss`), podpięty w `RootView` przez `.sheet(isPresented:$showTelemetryDetail)` z
  `.presentationDetents([.medium,.large])` (usunięto placeholder „Szczegóły — Unit 5").
  Arkusz przyjmuje `store: TelemetryStore` (odczyt `latest`/`linkState`) + `model: AppModel`
  (akcje trimu). Kurowany zestaw pól (R4) w sekcjach: GPS (fix/satelity/prędkość/pozycja),
  Kompas (kurs/kalibracja IMU 0-3/czujnik OK), Spot-lock (stan/błąd/namiar), Goto
  (stan/błąd/namiar/cel/dotarto), Łącze-RC (RC poprawny/link świeży). Świadomie POMINIĘTO
  surowe µs RC, okresy, servo/esc µs, NVS/źródło ustawień (poza scope R4). Widok cienki:
  każdy wiersz składa surowy string i przepuszcza przez `TelemetryDisplay.displayed(_:isFresh:)`
  (R6 — myślniki przy nieświeżym łączu/braku ramki, bez zamrażania), `isFresh` = jeden predykat
  `TelemetryDisplay.isFresh(linkState)`. Do kontraktu dołożono dwa czyste helpery
  (host-testowalne, bez SwiftUI): `boolText(_:)` („Tak"/„Nie", ~7 użyć) i `holdStateLabel(_:)`
  (stan hold, unknown→myślnik) — logika switch/warunek poza widokiem. Format liczb/współrzędnych
  inline w widoku reużywa akcesorów `Telemetry` (`speedMetersPerSecond`, `headingDegrees`,
  `spotLockBearingDegrees`, `gotoBearingDegrees`, `boatLatLon`) oraz `LatLonE7.latDegrees/lonDegrees`
  z formatem `"%.5f, %.5f"` (spójnie z `WaypointListView` — zgodnie z podpowiedzią o reużyciu
  helpera współrzędnych). Sekcja Trim (R5): odczyt `servoTrimUs` przez `trimText` (jedno źródło
  prawdy z telemetrii — bez lokalnej kopii, nowa wartość przychodzi kolejną ramką ~100 ms),
  przyciski −/+ (`model.trimLeft()/trimRight()`, `.bordered`, cel dotykowy 44 pt) i „Zapisz"
  (`model.saveTrim()`); cała sekcja `.disabled(!isTrimEnabled(state:))` — aktywna TYLKO gdy
  `state==.disarmed`, poza tym wyszarzona z notką „Rozbrój, aby wyregulować neutral". Testy:
  `isTrimEnabled` pokryty w `TelemetryDisplayTests` (Unit 3, moc wyroczni); +2 nowe testy Unit 5
  (`boolText`, `holdStateLabel` exhaustive z unknown→myślnik). Walidacja: `xcodegen generate`
  → app target `xcodebuild ... build` → BUILD SUCCEEDED; `swift test` KayakKit: 71/71 zielone
  (69→71). Scenariusze [E2E] (tap „+" → wzrost trimu po ~100 ms, ARMED → wyszarzenie+notka,
  stale → myślniki, „Zapisz" → potwierdzenie w `lastActionMessage`) wymagają symulatora —
  pozostawione do weryfikacji ręcznej/review, NIE odznaczone.
  **Review fazy 5 (2026-07-03):** severity gate ✅ CZYSTE (P1=0, P2=0, P3=3). Wszystkie krytyczne
  wymagania zweryfikowane STATYCZNIE jako POPRAWNE: R5 gate — `.disabled(!isEnabled)` na `Section`
  propaguje na przyciski `−/+/Zapisz`, więc przy ARMED akcje `trimLeft/right/save` NIE odpalają;
  R5 jedno źródło prawdy — `servoTrimUs` czytany z telemetrii bez lokalnej kopii, odświeżany przez
  `@Observable TelemetryStore`; R4 scope — tylko kurowany zestaw (brak surowych µs/NVS); R6 —
  każdy wiersz przez `displayed(_,isFresh:)`, brak force-unwrap/`any`/pustego catch; widok cienki
  (switch/warunki w pure `TelemetryDisplay`, host-testowane). Format współrzędnych spójny z
  `WaypointListView` (`"%.5f, %.5f"` na `LatLonE7`). Security/Performance CZYSTE. `swift test` 71/71.
  Nity (P3): (1) gate przycisków na `state` bez `isFresh` — drobna niespójność UX przy stale
  (bez zagrożenia); (2) „Zapisz" bez jawnego 44 pt; (3) `degreesText` inline zamiast w kontrakcie.
  E2E (4 scenariusze) — logika OK statycznie, render/interakcja na symulatorze do ręcznego
  domknięcia. Raport: `review-faza-5.md`.

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-07-03-ios-telemetry-hud-requirements.md
- Plan techniczny: docs/plans/2026-07-03-002-feat-ios-telemetry-hud-plan.md

## Powiązane pliki

### Kontrakt (KayakContract — pure, testowalny)
- `ios/KayakKit/Sources/KayakContract/Telemetry.swift` — struct `Telemetry`, `CodingKeys`, enumy
  `SystemState/ArmReason/HoldState`, computed helpery. **Do rozszerzenia** (Unit 1).
- `ios/KayakKit/Sources/KayakContract/Command.swift` — enum `Command`, `cmdName`, `httpBody()`.
  **Do rozszerzenia** o trim (Unit 2).
- `ios/KayakKit/Sources/KayakContract/TelemetryDisplay.swift` — **NOWY** (Unit 3): format + staleness + `isTrimEnabled`.

### Aplikacja (KayakMotor)
- `ios/KayakMotor/App/AppModel.swift` — koordynator; metody komend. **Do rozszerzenia** (Unit 2/5).
- `ios/KayakMotor/Features/Telemetry/TelemetryStore.swift` — `latest`, `linkState`.
- `ios/KayakMotor/Networking/CommandClient.swift` — `send(_:)`, koperta błędu.
- `ios/KayakMotor/RootView.swift` — ZStack layout; **do modyfikacji** (HUD overlay + sheet).
- `ios/KayakMotor/Features/Telemetry/HudView.swift` — **NOWY** (Unit 4).
- `ios/KayakMotor/Features/Telemetry/TelemetryDetailView.swift` — **NOWY** (Unit 5).
- `ios/KayakMotor/DesignSystem/SunlightTheme.swift` — tokeny do reużycia.
- `ios/KayakMotor/Features/Goto/ControlBarView.swift` — wzorzec `statusPill` + `iconButton`.
- `ios/KayakMotor/Features/Waypoints/WaypointListView.swift` — wzorzec sheet + NavigationStack.

### Testy
- `ios/KayakKit/Tests/KayakContractTests/TelemetryDecodingTests.swift` — rozszerz (Unit 1).
- `ios/KayakKit/Tests/KayakContractTests/HTTPCommandRequestTests.swift` — rozszerz (Unit 2).
- `ios/KayakKit/Tests/KayakContractTests/TelemetryDisplayTests.swift` — **NOWY** (Unit 3/5).

### Firmware (referencja — bez zmian)
- `components/web_panel/src/command_parse.c` — słownik komend (`trim_left/right/save`).
- `components/control_loop/src/control_loop.c` — `apply_trim_events`, gate DISARMED.
- `components/settings/include/settings_model.h` — `SERVO_TRIM_STEP_US=7`, `SERVO_TRIM_MAX_US=300`.
- `components/signal_chain/src/servo_chain.c` — `servo_trim_stepped`, `apply_trim`.
- `components/web_panel/src/telemetry_json.c` + `control_loop_snapshot.h` — inwentaryzacja pól.

## Decyzje techniczne

1. **Logika w kontrakcie, widoki cienkie.** Format + staleness + gate jako czyste funkcje w
   `KayakContract` (host-testowalne), bo target aplikacji nie ma testów. Zgodne z regułą pure/HAL.
2. **Trim = krokowe komendy** `trimLeft/right/save` (bez parametru), nie set-value — model firmware.
3. **Gate trimu w UI** wg `state == .disarmed` z telemetrii (firmware cicho ignoruje przy ARMED,
   nie zwraca błędu — nie polegamy na odpowiedzi serwera).
4. **Dekodowanie nowych pól `decodeIfPresent` + 0** — tolerancja na starszy firmware.
5. **HUD jako osobna nakładka ZStack** `.topLeading`, nie w `topBar` — brak kolizji z chipem
   połączenia; niezależny wzrost w pionie.
6. **`servo_trim_us` = jedno źródło prawdy z telemetrii** — UI nie trzyma lokalnej wartości, czeka
   na kolejną ramkę (~100 ms).
7. **Chip połączenia zostaje osobny** od HUD (stan linku ≠ telemetria).

## Rewizja wymagania R5

Źródło zakładało trim „na żywo, niezależnie od stanu, ostrzeżenie bez blokady". Firmware stosuje
trim **tylko po rozbrojeniu**. Decyzja użytkownika (2026-07-03): dostosować aplikację do gate'u
firmware (sekcja aktywna tylko DISARMED, poza tym wyszarzona z notką), bez zmian w kodzie
safety-critical.

## Zależności
- Kolejność: Unit 1 ∥ 2 → 3 → 4 → 5.
- Brak nowych zależności zewnętrznych; SwiftUI `presentationDetents` (iOS 16+, target 17).
