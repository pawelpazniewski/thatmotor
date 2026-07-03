# Kontekst: Aplikacja iOS — mapa offline + tap-to-goto

**Branch:** `feature/ios-app-goto-navigation`
**Ostatnia aktualizacja:** 2026-07-01

## Powiązane pliki

### Nowy projekt iOS (do stworzenia)
- `ios/DeRiskProbe/` — throwaway app bramki de-risk (Unit 0)
- `ios/KayakMotor/` — produkcyjny projekt SwiftUI:
  - `App/KayakMotorApp.swift`, `App/RootView.swift`
  - `Contract/Telemetry.swift`, `Contract/Command.swift`, `Contract/Coordinate.swift`
  - `Networking/HotspotJoiner.swift`, `Networking/CommandClient.swift`,
    `Networking/TelemetrySocket.swift`, `Networking/LinkState.swift`
  - `Features/Telemetry/TelemetryStore.swift`, `Features/Telemetry/GotoReadiness.swift`,
    `Features/Telemetry/StatusHUDView.swift`
  - `Map/LakeMapView.swift`, `Map/MapStyle.swift`, `Map/AttributionOverlay.swift`
  - `Features/Goto/GotoTargetController.swift`, `Features/Goto/GotoControlsView.swift`,
    `Features/Goto/KeepaliveController.swift`, `Features/Goto/SafetyControlsView.swift`,
    `Features/Goto/ArrivalHintView.swift`, `Features/Goto/WaterGeofence.swift`
  - `Persistence/Waypoint.swift`, `Persistence/WaypointStore.swift`,
    `Features/Waypoints/WaypointListView.swift`
  - `DesignSystem/SunlightTheme.swift`
  - `Resources/blank-style.json`, `Resources/lake.geojson`, `Resources/boat-icon`
  - `KayakMotorTests/*` (Swift Testing) + `KayakMotorTests/Fixtures/*.json`

### Kontrakt firmware (źródło prawdy — NIE modyfikować w MVP)
- `components/web_panel/src/ws_telemetry.c` (`snapshot_to_json`) — serializacja WS
- `components/web_panel/include/ws_telemetry.h` — okres 100 ms
- `components/web_panel/src/command_parse.c`, `http_server.c` — `POST /api/command`
- `components/web_panel/src/api_contract.c` — koperta `{data,error}`
- `components/web_panel/include/goto_target.h` — zakres walidacji ±90/±180 e7
- `components/web_panel/src/wifi_ap.c`, `sdkconfig` — AP `kayak-motor`
- `components/control_loop/src/control_loop.c`, `components/settings/src/settings_ranges.h`
  — comms-watchdog (`GOTO_COMMS_TIMEOUT_MS_DEFAULT=1500`)

## Decyzje techniczne
- **Cienki klient** — zero logiki regulatora/bramek po stronie apki; źródło prawdy = firmware.
- **Lokalizacja:** `ios/KayakMotor/` w tym repo (współdzielony kontrakt WS/HTTP).
- **Testy:** Swift Testing dla czystej logiki (konwersja, dekodowanie, readiness,
  keepalive, point-in-polygon). Mockujemy TYLKO transport (URLSession/NWConnection).
- **Transport HTTP:** `URLSession` (fallback `NWConnection`+`requiredInterfaceType=.wifi`
  +`prohibitExpensivePaths`) za protokołem `CommandSending` — podmiana bez ruszania warstw.
- **Transport WS:** Network.framework `NWProtocolWebSocket` pinowany `.wifi`.
- **Join AP:** `NEHotspotConfiguration(joinOnce:false)` + fallback „dołącz ręcznie".
- **Konwersja:** `Int32((deg*1e7).rounded())` / `Double(e7)/1e7`; walidacja PRZED wysłaniem.
- **Marker 10 Hz:** `MLNSymbolStyleLayer` + podmiana `MLNShapeSource.shape` (nie annotation views).
- **Mapa offline:** bundlowy `blank-style.json` (`file://`, `"sources":{}`) + `MLNShapeSource`
  na `lake.geojson`; zero requestów sieciowych; atrybucja OSM stały overlay.
- **Waypointy:** `Codable`→plik w Application Support (prostota > SwiftData).
- **Geofence R7:** ray-casting point-in-polygon; poza → potwierdzenie, NIGDY twarda blokada.
- **STOP** = `goto_cancel` bez potwierdzenia i bez disarm; **Rozbrój** = `disarm`; app nigdy nie uzbraja.
- **Idle-timer** OFF tylko podczas aktywnego goto.

## Zależności / uwagi
- **iOS 17+**, iPhone. MapLibre `maplibre-gl-native-distribution` v6.27.0 (binary xcframework,
  `import MapLibre`, prefix `MLN*`). Tylko realny Xcode/urządzenie (Simulator ograniczony
  dla hotspot/WS/MapLibre).
- Capability **Hotspot Configuration** (`com.apple.developer.networking.HotspotConfiguration`);
  Info.plist `NSLocalNetworkUsageDescription`.
- **Ryzyko binarne:** łączność iPhone↔`192.168.4.1` przy AP bez internetu — rozstrzyga Unit 0.
- Kontrakt firmware „niezmienny w MVP" — modele dekodują tolerancyjnie (nieznane pola/wartości
  nie crashują).

## Wiedza instytucjonalna
- `docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md` —
  walidacja double przed castem; utrata linku = PAUSE z retencją celu (transient loss
  wznawia), nie abort → po stronie apki keepalive to obowiązek, PAUSED nie „udaje" postępu.

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-07-01-ios-app-requirements.md
- Plan techniczny: docs/plans/2026-07-01-002-feat-ios-app-goto-navigation-plan.md
