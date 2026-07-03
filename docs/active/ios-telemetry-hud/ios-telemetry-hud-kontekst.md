# Kontekst: Telemetria w aplikacji iOS — HUD + arkusz + trim serwa

Branch: `feature/ios-telemetry-hud`
Ostatnia aktualizacja: 2026-07-03

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
