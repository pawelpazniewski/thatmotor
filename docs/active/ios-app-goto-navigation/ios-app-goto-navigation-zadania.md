# Zadania: Aplikacja iOS — mapa offline + tap-to-goto

**Branch:** `feature/ios-app-goto-navigation`
**Ostatnia aktualizacja:** 2026-07-01

Legenda: `Test:` = scenariusz testowy · `Weryfikacja:` = kryterium ukończenia ·
`[E2E]` = manualna weryfikacja on-device (agent-browser nie steruje iPhonem).

> **Uwaga o strukturze (refinement planu):** czysta logika kontraktu żyje w Swift
> Package `ios/KayakKit/` (host-testowalna przez `swift test` bez Simulatora, reużywalna
> nawet przy Plan B/BLE — wzorzec „Pure ⊥ HAL"). Projekt Xcode generowany z
> `ios/project.yml` (XcodeGen). Środowisko: Xcode 26.6, sim iOS 26.2. Build target przez
> `xcodebuild -target ... -sdk iphonesimulator` (scheme-destination gubi się na SDK 26.5).

---

## Faza 0 — Bramka de-risk

### Unit 0: Bramka łączności (throwaway app) — S/M · (R1) · zależności: brak
- [x] Stwórz `ios/DeRiskProbe/` (osobny minimalny target SwiftUI, throwaway)
- [x] `DeRiskProbeApp.swift`, `ContentView.swift`, `ProbeModel.swift`
- [x] Info.plist `NSLocalNetworkUsageDescription` + capability Hotspot Configuration (entitlements)
- [x] `NEHotspotConfiguration(joinOnce:false)` join + pomiar czasu join
- [x] HTTP `URLSession` POST (fallback `NWConnection`/`.wifi` — do włączenia po teście)
- [x] WS Network.framework `NWProtocolWebSocket` pinowany `.wifi` do `ws://192.168.4.1/ws`
- [x] Stub wyniku: `docs/dev-brainstorms/2026-07-01-ios-derisk-gate-results.md` (do wypełnienia on-device)
- [x] Build Simulator: `BUILD SUCCEEDED` (kompilacja OK; działanie sieci wymaga urządzenia)
- [ ] Test: [E2E] LTE aktywne: Połącz → prompt Local Network → POST zwraca 200
  `{"data":null,"error":null}`; równolegle request do internetu przez LTE działa
- [ ] Test: [E2E] WS strumień ramek ~10 Hz płynie ≥60 s bez rozłączeń w foreground
- [ ] Test: [E2E] Odmowa Local Network → ciche failowanie → ekran-recovery (nie crash)
- [ ] Test: [E2E] Background 20 s → powrót → socket reconnectuje (lub jasny wynik do dok.)
- [ ] Weryfikacja: Dokument wyników = PASS (HTTP+WS do 192.168.4.1 przy żywym LTE,
  asocjacja przeżywa krótkie tło, działa fallback manual-join + recovery). **Bez PASS —
  plan zatrzymany na tej bramce.**

---

## Faza 1 — Fundament klienta

### Unit 1: Szkielet projektu, zależności, uprawnienia — M · (R1, R2, R10) · zależności: Unit 0 PASS · ✅ UKOŃCZONE (Simulator build green)
- [x] `KayakMotor` app target: `KayakMotorApp.swift`, `RootView.swift` (placeholder linkujący zależności)
- [x] Info.plist (`NSLocalNetworkUsageDescription`), entitlements (Hotspot Configuration), iOS 17
- [x] SwiftPM `maplibre-gl-native-distribution` (from 6.27.0), moduł `MapLibre` — **rozwiązany z GitHub, linkuje**
- [x] `KayakContract` jako framework target (te same źródła co pakiet host-testowy) — app linkuje
- [x] `ios/README.md` (build, capability, uwaga o Simulatorze i quirku scheme→`-target`)
- [x] Test: [Unit] Smoke — `xcodebuild -target KayakMotor -sdk iphonesimulator` → **BUILD SUCCEEDED**
- [x] Weryfikacja: `import MapLibre` + `import KayakContract` linkują; oba targety (app+probe) budują się;
  host-testy KayakKit zielone (31/31). Uwaga: build na URZĄDZENIU wymaga Team/signing (użytkownik)

### Unit 2: Modele kontraktu + konwersja współrzędnych — M · (R2, R3, R8) · zależności: Unit 1 ✅ UKOŃCZONE (host-tested)
- [x] `KayakKit/Sources/KayakContract/Telemetry.swift` (`Decodable` + enumy `SystemState`,`ArmReason`,`HoldState`)
- [x] `KayakKit/Sources/KayakContract/Command.swift` (`Command`, `CommandEnvelope`, `ApiError{code,message}`)
- [x] `KayakKit/Sources/KayakContract/Coordinate.swift` (`toE7`,`fromE7`,`LatLonE7` init? ±90/±180)
- [x] Testy: `TelemetryDecodingTests.swift`, `CoordinateConversionTests.swift`, `CommandEnvelopeTests.swift` (fixtures inline w testach)
- [x] Test: [Unit] Dekoduje ramkę WS: skalowania (heading deg10→deg, speed cms→m/s) OK;
  nieznany `state`=9 → `.unknown` bez crashu
- [x] Test: [Unit] `toE7(52.2297)`==`522297000`; `fromE7` round-trip stabilny
- [x] Test: [Unit] Moc wyroczni: `toE7(91.0)` → `nil` (FAIL bez walidacji double-domain; wejście POZA zakresem)
- [x] Test: [Unit] Dekoduje kopertę błędu 400 → `ApiError`; sukces `{"data":null,"error":null}` → `isSuccess`
- [x] Weryfikacja: **13/13 testów Contract zielonych** (`swift test`); walidacja w domenie double przed castem (moc wyroczni)

### Unit 3: Warstwa sieciowa — join, wymuszenie Wi‑Fi, HTTP, WS — L · (R1, R9) · zależności: Unit 0, Unit 2 · CZĘŚCIOWO (LinkState/request host-tested, reszta compile-verified)
- [x] `Networking/HotspotJoiner.swift` (`NEHotspotConfiguration` join + fallback manual)
- [x] `Networking/CommandClient.swift` (protokół `CommandSending`; impl `URLSession`; miejsce na `NWConnection`)
- [x] `Networking/TelemetrySocket.swift` (Network.framework WS pinowany `.wifi`, `AsyncStream<TelemetryStreamEvent>`)
- [x] `KayakContract/LinkState.swift` (`LinkStateMachine` — disconnected/joining/connected/stale)
- [x] `KayakContract/HTTPCommandRequest.swift` (czysty builder żądania) + testy
- [x] Test: [Unit] `goto(lat,lon)` → poprawny URL, `Content-Type`, body `{"cmd":"goto","lat_e7":..,"lon_e7":..}` (host)
- [ ] Test: [Unit] Odpowiedź 400 → typed `ApiError` — logika w `CommandClient` (app; wymaga mock URLProtocol, TODO)
- [x] Test: [Unit] LinkState: brak ramki > próg → `stale`; ramka → `connected`; zerwanie → `disconnected`.
  Moc wyroczni: usunięcie progu stale → test „stale" FAILuje (host)
- [ ] Test: [E2E] Połącz łączy z `kayak-motor`, WS zaczyna publikować telemetrię (device)
- [ ] Weryfikacja: część host-tested zielona (LinkState+request); na urządzeniu status linku realny; `goto_cancel`→200 (device)

### Unit 4: Store telemetrii + świeżość + status/HUD — M · (R2, R8, R9) · zależności: Unit 3 · CZĘŚCIOWO (logika host-tested)
- [x] `Features/Telemetry/TelemetryStore.swift` (`@Observable`; ostatnia ramka + LinkStateMachine + boat/readiness) — compile-verified
- [x] `KayakContract/GotoReadiness.swift` (czysta `(Telemetry)->GotoBlockReason?`)
- [~] `Features/Telemetry/StatusHUDView.swift` — na razie badge łącza + powód blokady w RootView; pełny HUD (sats/prędkość) TODO
- [x] Test: `GotoReadinessTests.swift` (host)
- [x] Test: [Unit] ARMED+fix → `nil` (gotowe)
- [x] Test: [Unit] DISARMED → powód; fix=false → „Brak fixu"; arm_reason→powód. Moc wyroczni:
  DISARMED mimo fixu → zwraca powód, nie `nil`. (Uwaga: „brak linku" = LinkState/transport, nie readiness)
- [ ] Test: [Unit] Brak ramki > próg → `isStale==true` — w TelemetryStore (app-target)
- [ ] Weryfikacja: HUD pokazuje stan i powód; po zerwaniu WS dane wygasają (część host-tested: readiness zielone)

---

## Faza 2 — Mapa i nawigacja

### Unit 5: Mapa offline (kontur + marker + jakość GPS) — L · (R2, R10) · zależności: Unit 4, Unit 1 · CZĘŚCIOWO (compile-verified; render device-gated)
- [x] `Map/LakeMapView.swift` (`UIViewRepresentable` + Coordinator; kontur + boat symbol layer + follow)
- [x] Ładowanie `blank-style.json` + wiązanie warstw (w Coordinatorze LakeMapView; osobny MapStyle.swift zbędny)
- [x] Zasoby: `Resources/blank-style.json`, `Resources/lake.geojson` (placeholder OSM); boat-icon = SF Symbol w kodzie
- [x] `Map/AttributionOverlay.swift` (stały „© OpenStreetMap contributors")
- [x] Test: parsowanie konturu — `LakeContourTests.swift` (host; GeoJSON→[GeoPoint], łączy się z geofence)
- [ ] Test: [E2E] Bez internetu (AP): kontur renderuje się, marker na pozycji GPS obraca się wg kursu; zero requestów (device)
- [ ] Test: [E2E] Marker płynny przy ~10 Hz (brak janku), kamera podąża (device)
- [ ] Weryfikacja: render offline + atrybucja + zero ruchu sieciowego (device; kompilacja + parser host-tested zielone)

### Unit 6: Tap-to-goto (pin, linia, wysłanie, err/bearing) — M · (R3) · zależności: Unit 5, Unit 3, Unit 4
- [ ] `Features/Goto/GotoTargetController.swift` (stan celu; źródło mapa vs waypoint)
- [x] `Features/Goto/GotoTargetController.swift` (@Observable; cel + sendState; źródło mapa/waypoint)
- [x] Modyfikuj `Map/LakeMapView.swift` (tap→coord; warstwy pin celu + polyline łódź→cel)
- [x] `Features/Goto/GotoControlsView.swift` (przycisk „Płyń do punktu"; `goto_err_m`/`goto_bearing_deg10`)
- [x] Test: [Unit] Cel poza ±90/±180 → odrzucony przed wysłaniem (`LatLonE7?` init, host — CoordinateConversionTests)
- [x] Test: [Unit] Po 400 → stan błędu (nie ciche zignorowanie) — `HTTPCommandResponseTests` (host, ApiError)
- [~] Test: [Unit] `GotoTargetControllerTests` (@MainActor — wymaga app test target; logika prosta, compile-verified)
- [ ] Test: [E2E] Dotknięcie → pin + linia; „Płyń do punktu" (ARMED+fix+neutral) → goto rusza, err maleje (device)
- [ ] Weryfikacja: (device) tap→cel→goto; część host-tested (walidacja celu, 400→ApiError) zielona; app BUILD SUCCEEDED

### Unit 7: Keepalive + STOP/Rozbrój + idle-timer + arrived→CH3 — M · (R5, R6) · zależności: Unit 6 · UKOŃCZONE (logika host-tested, UI compile-verified)
- [x] `KayakContract/Keepalive.swift` — czysta `KeepaliveDecision` (resend/idle) + `GotoTiming` (host-tested)
- [x] `Features/Goto/KeepaliveController.swift` (timer ~2 Hz; cleanup przy stop/deinit) — opakowuje decyzję (AppModel)
- [x] `Features/Goto/SafetyControlsView.swift` (wielki STOP zawsze widoczny; osobny „Rozbrój")
- [x] `App/AppModel.swift` (`isIdleTimerDisabled` tylko active; STOP→goto_cancel bez disarm; Rozbrój→disarm)
- [x] `Features/Goto/ArrivalHintView.swift` (po `goto_arrived` → „Włącz CH3 na RC")
- [x] Test: [Unit] Goto aktywne → resend < watchdog; po STOP → przestaje. Moc wyroczni: usunięcie
  warunku „tylko active/paused" → FAILuje (host — KeepaliveTests)
- [x] Test: [Unit] STOP → `goto_cancel`, NIE `disarm`; Rozbrój → `disarm` (AppModel, rozłączne — compile-verified)
- [x] Test: [Unit] Idle-timer: ON tylko podczas active; pauza/OFF przywraca (host: `shouldDisableIdleTimer`)
- [ ] Test: [E2E] Podczas goto ekran nie gaśnie; STOP natychmiast; utrata linku → pauza → wznowienie; CH3 hint (device)
- [ ] Weryfikacja: (device) keepalive utrzymuje `app_link_fresh=true`; STOP/Rozbrój natychmiastowe i rozłączne

---

## Faza 3 — Waypointy i barierki

### Unit 8: Waypointy (zapis realnej pozycji, trwałość, re-send) — M · (R4) · zależności: Unit 6, Unit 4 · CZĘŚCIOWO (store host-tested)
- [x] `KayakContract/Waypoint.swift` (`Codable {id,name,latE7,lonE7,createdAt}` + `fromBoat`)
- [x] `KayakContract/Waypoint.swift` → `WaypointStore` (`Codable`→plik atomowy; add/rename/delete; trwałość)
- [x] `Features/Waypoints/WaypointListView.swift` (lista, „Zapisz tę pozycję", tap→cel, usuwanie) — compile-verified
- [x] Test: `WaypointStoreTests.swift` (host)
- [x] Test: [Unit] Add→save→reload zwraca ten sam waypoint. Moc wyroczni: bez realnego zapisu
  reload zwraca pustą listę → FAIL
- [x] Test: [Unit] Rename/delete mutują i utrwalają
- [x] Test: [Unit] „Zapisz tę pozycję" (`fromBoat`) przy braku fixu → `nil` (nie zapisuje 0,0)
- [ ] Test: [E2E] Zapis pozycji → restart apki → waypoint nadal na liście → tap wysyła goto
- [ ] Weryfikacja: Waypointy przeżywają restart; tap re-wysyła cel (część host-tested: trwałość zielona)

### Unit 9: Miękkie ostrzeżenie geofence + polish słoneczny — M · (R7, R10) · zależności: Unit 6, Unit 5 · CZĘŚCIOWO (geofence host-tested)
- [x] `KayakContract/WaterGeofence.swift` (czysty ray-casting point-in-polygon)
- [x] Dialog potwierdzenia geofence w `RootView` (`confirmationDialog`, „Płyń mimo to" — NIE blokuje) — compile-verified
- [x] `DesignSystem/SunlightTheme.swift` (kontrast, rozmiary celów, panel nisko pod kciuk) — compile-verified
- [x] Test: `WaterGeofenceTests.swift` (host)
- [x] Test: [Unit] Punkt wewnątrz → true; poza → false. Moc wyroczni: punkt lądowy MUSI dać false;
  wklęsły L-kształt rozróżnia zatokę od lądu
- [ ] Test: [Unit] Ostrzeżenie NIE blokuje — po potwierdzeniu cel jest wysłany (UI, app-target)
- [ ] Test: [E2E] Dotknięcie lądu → dialog; potwierdzenie → goto rusza mimo to
- [ ] Weryfikacja: Cel na lądzie wyzwala potwierdzenie, ale nie blokuje (część host-tested: geofence zielone)
