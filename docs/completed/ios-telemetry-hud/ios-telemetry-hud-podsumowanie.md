# Podsumowanie: Telemetria w aplikacji iOS — HUD + arkusz + trim serwa

**Data ukończenia:** 2026-07-03
**Branch:** `feature/ios-telemetry-hud`

## Co zostało dostarczone

Dwupoziomowy widok telemetrii w aplikacji iOS, w całości reużywający istniejącego
strumienia WebSocket (~10 Hz) i design systemu (SunlightTheme):

1. **Kompaktowy HUD „rzut oka"** w lewym górnym rogu mapy (4 wiersze: jakość GPS,
   stan systemu, aktywny tryb + dystans/namiar do celu, prędkość + kurs).
2. **Dolny arkusz szczegółów** (`presentationDetents [.medium,.large]`) otwierany tapnięciem
   HUD — kurowane grupy (GPS / Kompas / Spot-lock / Goto / Łącze-RC) + sekcja regulacji
   neutrala serwa (trim), aktywna wyłącznie po rozbrojeniu.
3. **Rozszerzony kontrakt telemetrii** o `imuCalib`, `spotLockErrM`, `spotLockBearingDeg10`,
   `servoTrimUs` (tolerancyjne dekodowanie starszego firmware).
4. **Komendy trimu** `trim_left/right/save` + metody w `AppModel`.
5. **Czysty moduł prezentacji** `TelemetryDisplay` (format + staleness + gate trimu),
   host-testowalny bez SwiftUI.

Zrealizowano wszystkie 5 Unitów. Walidacja: host-testy KayakKit **71/71 zielone**,
app target iOS Simulator **BUILD SUCCEEDED**.

## Kluczowe decyzje

1. **Logika w kontrakcie, widoki cienkie.** Format + staleness + gate jako czyste funkcje
   w `KayakContract` (host-testowalne), bo target aplikacji nie ma testów (Pure⊥HAL).
2. **Trim = krokowe komendy** `trimLeft/right/save` (bez parametru), nie set-value —
   zgodnie z modelem firmware.
3. **Gate trimu w UI** wg `state == .disarmed` z telemetrii (firmware cicho ignoruje przy
   ARMED bez błędu — nie polegamy na odpowiedzi serwera).
4. **`servo_trim_us` = jedno źródło prawdy z telemetrii** — UI nie trzyma lokalnej kopii,
   nowa wartość przychodzi kolejną ramką (~100 ms).
5. **HUD jako osobna nakładka ZStack `.topLeading`** — brak kolizji z chipem połączenia
   / waypointami / zoomem; środek mapy pozostaje czysty.
6. **Dekodowanie nowych pól `decodeIfPresent(...) ?? 0`** — tolerancja na starszy firmware.
7. **Rewizja R5:** źródło zakładało trim „na żywo niezależnie od stanu"; firmware stosuje
   trim tylko po rozbrojeniu. Decyzja użytkownika: dostosować aplikację do gate'u firmware
   (sekcja aktywna tylko DISARMED, poza tym wyszarzona z notką), bez zmian w kodzie
   safety-critical.

## Utworzone / zmodyfikowane pliki (główne)

### Nowe
- `ios/KayakKit/Sources/KayakContract/TelemetryDisplay.swift` — format, staleness,
  `isTrimEnabled`, `activeTargetText` (wspólny wybór aktywnego źródła goto>spot-lock).
- `ios/KayakMotor/Features/Telemetry/HudView.swift` — kompaktowy HUD.
- `ios/KayakMotor/Features/Telemetry/TelemetryDetailView.swift` — dolny arkusz + trim.
- `ios/KayakKit/Tests/KayakContractTests/TelemetryDisplayTests.swift` — testy prezentacji.

### Zmodyfikowane
- `ios/KayakKit/Sources/KayakContract/Telemetry.swift` — nowe pola + computed helpery.
- `ios/KayakKit/Sources/KayakContract/Command.swift` — `.trimLeft/.trimRight/.trimSave`.
- `ios/KayakMotor/App/AppModel.swift` — `trimLeft()/trimRight()/saveTrim()`.
- `ios/KayakMotor/RootView.swift` — nakładka HUD + `.sheet` z detentami.
- `ios/KayakKit/Tests/KayakContractTests/TelemetryDecodingTests.swift` — testy nowych pól.
- `ios/KayakKit/Tests/KayakContractTests/HTTPCommandRequestTests.swift` — testy komend trimu.

## Wyciągnięte wnioski

- **Pure⊥HAL na iOS:** przy targecie aplikacji bez testów, całą logikę prezentacji/bramek
  wypchnięto do pakietu `KayakContract` (bez importu SwiftUI) i pokryto host-testami; widoki
  składają tylko stringi. Decyzja wyboru aktywnego źródła, która najpierw wylądowała w
  nietestowalnym widoku (`HudView.target`), została wyciągnięta do czystej `activeTargetText`
  ze wspólnym predykatem `isEngaged` — jedna reguła priorytetu, host-test z mocą wyroczni.
- **Moc wyroczni testów staleness/gate:** stale MUSI dać myślnik, `isTrimEnabled` tylko
  `.disarmed`, konwersje deg10/cm-s z wejściem POZA wyjściem — mutacja bramki failuje test.
- **Klucze JSON weryfikować 1:1 z firmware** (`telemetry_json.c`, `command_parse.c`) i znak
  `servo_trim_us` (`%d`) — zamyka ryzyko rozjazdu kontraktu.
- **Gate bezpieczeństwa w UI odwzorowuje gate firmware, nie zastępuje go:** `.disabled` na
  sekcji to UX-guard; realny gate DISARMED egzekwuje `control_loop.c` (`apply_trim_events`).
- **Jedno źródło prawdy z telemetrii** dla wartości sterowanej (trim) eliminuje rozjazd
  optimistic-UI vs stan urządzenia — UI czeka na kolejną ramkę.

## Świadomie niekompletne (ręczne follow-upy)

Niemożliwe do wykonania w tym środowisku (natywny iOS, brak interaktywnego
symulatora/urządzenia/wody) — pozostawione ODZNACZONE w `zadania.md`:

- **Scenariusze [E2E] symulatorowe (Unit 4 i 5):** tap HUD→arkusz, rozłącz→myślniki,
  ARMED→wyszarzenie sekcji trimu+notka, DISARMED tap „+" → wzrost `servo_trim_us` po ~100 ms,
  „Zapisz”→potwierdzenie w `lastActionMessage`. Logika zweryfikowana STATYCZNIE w review jako
  poprawna; render/interakcja na żywo wymaga symulatora/urządzenia.
- **Domknięcie:** ręczny przebieg na urządzeniu (HUD/arkusz/trim/staleness) oraz aktualizacja
  notki w README aplikacji iOS.
- **Nity P3 (opcjonalne, z review faz 4/5):** VoiceOver `accessibilityValue` z podsumowaniem
  wierszy (świadomie pominięty — dane w arkuszu); gate przycisków trimu na `isFresh` obok
  `state`; „Zapisz” bez jawnego celu dotykowego 44 pt; `degreesText` inline zamiast w kontrakcie;
  domknięcia testów `modeLabel` (priorytet przy obu aktywnych) i `stateLabel` (pełny zestaw stanów).
