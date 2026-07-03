# Zadania: Telemetria w aplikacji iOS — HUD + arkusz + trim serwa

Branch: `feature/ios-telemetry-hud`
Ostatnia aktualizacja: 2026-07-03

Legenda: `Test:` = scenariusz testowy, `Weryfikacja:` = kryterium ukończenia. Nakład: S/M/L/XL.

---

## Unit 1: Rozszerz kontrakt telemetrii (S) — R1, R4, R5

Implementacja:
- [x] Dodaj pola `imuCalib`, `spotLockErrM`, `spotLockBearingDeg10`, `servoTrimUs` do `Telemetry` (`ios/KayakKit/Sources/KayakContract/Telemetry.swift`)
- [x] Dodaj klucze `imu_calib`, `spot_lock_err_m`, `spot_lock_bearing_deg10`, `servo_trim_us` do `CodingKeys`
- [x] W `init(from:)` dekoduj nowe pola przez `decodeIfPresent(...) ?? 0`
- [x] Dodaj computed `spotLockBearingDegrees` (i ew. `servoTrimMicroseconds`)

Testy (`ios/KayakKit/Tests/KayakContractTests/TelemetryDecodingTests.swift`):
- [x] Test: pełna ramka z nowymi polami → wszystkie zdekodowane (wartości ≠ 0)
- [x] Test: ramka BEZ nowych pól (stary firmware) → dekoder nie rzuca, nowe pola = 0
- [x] Test: `servo_trim_us` ujemny (−140) → znak zachowany

Weryfikacja:
- [x] Weryfikacja: `swift test` w KayakKit zielony (53/53); nowe pola dostępne na `Telemetry`

### Do poprawy po review fazy 1

Review: `review-faza-1.md` — severity gate: ✅ CZYSTE (P1=0, P2=0, P3=1). E2E: N/A (natywny iOS).
Klucze JSON zweryfikowane 1:1 wobec firmware `telemetry_json.c`, znak `servo_trim_us` OK.

- [ ] 🟡 [nit] **TelemetryDecodingTests.swift** — brak testu granicznego dla `spot_lock_bearing_deg10` na krawędzi zakresu (3600 → 360.0°/0); opcjonalne, do rozważenia przy Unit 3.

---

## Unit 2: Słownik komend trimu + AppModel (S) — R5

Implementacja:
- [x] Dodaj `case trimLeft, trimRight, trimSave` do `Command` + mapowanie `cmdName` (`ios/KayakKit/Sources/KayakContract/Command.swift`)
- [x] `httpBody()` dla trimu bez `lat_e7`/`lon_e7`
- [x] Metody `trimLeft()/trimRight()/saveTrim()` w `AppModel` wzorcem `disarm()` (`ios/KayakMotor/App/AppModel.swift`)

Testy (`ios/KayakKit/Tests/KayakContractTests/HTTPCommandRequestTests.swift`):
- [x] Test: `Command.trimLeft.httpBody()` → `{"cmd":"trim_left"}`
- [x] Test: `Command.trimSave.httpBody()` → `{"cmd":"trim_save"}`
- [x] Test: żaden trim-command nie dokłada `lat_e7`/`lon_e7`

Weryfikacja:
- [x] Weryfikacja: testy zielone (56/56); `AppModel` eksponuje trzy metody trimu

### Do poprawy po review fazy 2

Review: `review-faza-2.md` — severity gate: ✅ CZYSTE (P1=0, P2=0, P3=2). E2E: N/A (natywny iOS).
Nazwy komend zweryfikowane 1:1 wobec firmware `command_parse.c`; bramka DISARMED potwierdzona w `control_loop.c` (`apply_trim_events`) — ryzyko R5 zamknięte.

- [ ] 🟡 [nit] **Command.swift / AppModel.swift** — asymetria nazewnicza `.trimSave` (case) vs `saveTrim()` (metoda) przy symetrycznych `trimLeft`/`trimRight`; zgodne z planem, do rozważenia ujednolicenie przy Unit 5.
- [ ] 🟡 [nit] **AppModel.swift** — `trimLeft()`/`trimRight()`/`saveTrim()` bez testów jednostkowych (spójne z nietestowanym `disarm()`/`stopGoto()`); zachowanie pokryte E2E w Unit 5.

---

## Unit 3: Czysty moduł prezentacji `TelemetryDisplay` (M) — R1, R4, R6

> Notatka wykonawcza: test-first (moc wyroczni — stale MUSI dać myślnik).

Implementacja (`ios/KayakKit/Sources/KayakContract/TelemetryDisplay.swift`):
- [x] `gpsQualityText(fix:sats:)`, `stateLabel(_:)`, `modeLabel(spotLock:goto:)`
- [x] `targetText(errM:bearingDeg10:)` (format `"%d m · %.0f°"`), `speedHeadingText(...)`, `trimText(servoTrimUs:)`
- [x] `displayed(_ value:isFresh:) -> String` → „—" gdy `!isFresh`
- [x] Mapowanie `LinkState` → `isFresh: Bool` (fresh tylko `.connected`)
- [x] `isTrimEnabled(state:) -> Bool` (== `.disarmed`)

Testy (`ios/KayakKit/Tests/KayakContractTests/TelemetryDisplayTests.swift`):
- [x] Test: `stateLabel(.armed)` → „ARMED"; `.failsafe` → „FAILSAFE"
- [x] Test: `targetText(errM:123,bearingDeg10:450)` → „123 m · 45°"
- [x] Test: `displayed("5.0 m/s", isFresh:false)` → „—"; `isFresh:true` → „5.0 m/s"
- [x] Test: `modeLabel` — spot-lock aktywny vs goto pauza vs off → różne etykiety
- [x] Test: `gpsQualityText(fix:false, sats:0)` → „brak fix" (nie mylące „0 sat")
- [x] Test: `isTrimEnabled(.disarmed)`==true; pozostałe stany==false (mutacja „zawsze true" failuje)

Weryfikacja:
- [x] Weryfikacja: testy zielone (65/65); widoki mogą składać stringi bez własnej logiki

### Do poprawy po review fazy 3

Review: `review-faza-3.md` — severity gate: ✅ CZYSTE (P1=0, P2=0, P3=3). E2E: N/A (czyste funkcje, brak UI/przeglądarki).
Format `"%d m · %.0f°"` zweryfikowany 1:1 z `ControlBarView.swift:41`; brak importu SwiftUI (Pure⊥HAL); wszystkie case'y `SystemState`/`LinkState` pokryte w switchach; moc wyroczni testów potwierdzona (mutacje bramek/konwersji failują). Nity opcjonalne — do rozważenia przy Unit 4/5.

- [ ] 🟡 [nit] **TelemetryDisplayTests.swift** — `modeLabel` nie asertuje priorytetu goto>spot-lock przy OBU aktywnych; mutacja odwracająca priorytet nie zostałaby złapana. Dodać case `modeLabel(spotLock:.active, goto:.active) == "Goto"`.
- [ ] 🟡 [nit] **TelemetryDisplayTests.swift** — `stateLabel` pokrywa 3/6 stanów; brak asercji dla `.escCalibration`/`.deploy`/`.unknown`→"—". Domknąć pętlą jak w `trimEnabledOnlyDisarmed`.
- [ ] 🟡 [nit] **TelemetryDisplay.swift** — literały konwersji `10.0`/`100.0` inline w `targetText`/`speedHeadingText` (spójne z `Telemetry.swift`); ewentualne nazwanie stałej jeśli powtórzy się w widokach Unit 4/5.

---

## Unit 4: Kompaktowy HUD w lewym górnym rogu (M) — R1, R2, R3, R6, R7

Implementacja:
- [x] `HudView` czyta `TelemetryStore`, składa 4 wiersze przez `TelemetryDisplay` (`ios/KayakMotor/Features/Telemetry/HudView.swift`)
- [x] Styl panelu: `panelBackground` + `hairline` + shadow + `rounded(...).monospacedDigit()`
- [x] Dodaj nakładkę do ZStack w `RootView` z `.frame(..., alignment:.topLeading)` + padding; offset pod chipem połączenia
- [x] `onTapGesture` HUD → `showTelemetryDetail = true`; respektuj reduced-motion (placeholder `.sheet` z detentami [.medium,.large] — treść w Unit 5)

Testy:
- [ ] Test (E2E): start → HUD w lewym górnym rogu; środek mapy czysty; brak kolizji z zoom/waypoints  _(wymaga symulatora — do weryfikacji ręcznej)_
- [ ] Test (E2E): tap HUD → wysuwa się dolny arkusz; swipe w dół → chowa  _(wymaga symulatora — do weryfikacji ręcznej)_
- [ ] Test (E2E): rozłącz link → wartości HUD → „—", nie zamrażają  _(wymaga symulatora — do weryfikacji ręcznej)_

Weryfikacja:
- [ ] Weryfikacja: HUD renderuje 4 wiersze z żywej telemetrii; myślniki przy stale; tap otwiera arkusz; środek mapy niezasłonięty

---

## Unit 5: Dolny arkusz szczegółów + sekcja trimu (L) — R3, R4, R5, R6, R7

Implementacja:
- [ ] `TelemetryDetailView` w stylu `WaypointListView` (NavigationStack + sekcje + „Gotowe") (`ios/KayakMotor/Features/Telemetry/TelemetryDetailView.swift`)
- [ ] Grupy: GPS (fix/sat/prędkość/lat-lon), Kompas (kurs/kalibracja/OK), Spot-lock (stan/błąd/namiar), Goto (stan/błąd/namiar/cel/dotarto), Link-RC (RC valid/świeżość)
- [ ] Wartości przez `TelemetryDisplay` (myślniki przy stale)
- [ ] Sekcja Trim: odczyt `servoTrimUs`, przyciski −/+ (`trimLeft/trimRight`), „Zapisz" (`trimSave`)
- [ ] Sekcja Trim `.disabled(!isTrimEnabled(state:))` + notka „Rozbrój, aby wyregulować" gdy wyłączona
- [ ] `.sheet(isPresented:$showTelemetryDetail)` z `.presentationDetents([.medium,.large])` w `RootView`

Testy:
- [ ] Test (Unit): `isTrimEnabled(.disarmed)`==true; `.armed/.failsafe/.escCalibration/.deploy`==false
- [ ] Test (E2E): DISARMED → tap „+" → po ~100 ms `servo_trim_us` w arkuszu rośnie o krok
- [ ] Test (E2E): ARMED → sekcja trimu wyszarzona, przyciski nieaktywne, notka widoczna
- [ ] Test (E2E): arkusz przy stale linku → pola „—", nie stare liczby
- [ ] Test (E2E): „Zapisz" (DISARMED) → brak błędu, potwierdzenie w `lastActionMessage`

Weryfikacja:
- [ ] Weryfikacja: arkusz pokazuje kurowany zestaw; trim działa tylko DISARMED z żywym odczytem; myślniki przy stale; detenty pół/pełna działają

---

## Domknięcie
- [ ] `swift test` (KayakKit) zielony — całość
- [ ] Ręczny przebieg na urządzeniu: HUD, arkusz, trim po rozbrojeniu, staleness
- [ ] Aktualizacja notki w README aplikacji iOS (HUD/arkusz + trim tylko DISARMED)
