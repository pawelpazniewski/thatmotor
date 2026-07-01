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

### Unit 1: Szkielet projektu, zależności, uprawnienia — M · (R1, R2, R10) · zależności: Unit 0 PASS
- [ ] Stwórz `ios/KayakMotor/KayakMotor.xcodeproj`, `App/KayakMotorApp.swift`, `App/RootView.swift`
- [ ] Info.plist (`NSLocalNetworkUsageDescription`), entitlements (Hotspot Configuration), iOS 17
- [ ] SwiftPM `maplibre-gl-native-distribution` (from 6.27.0), moduł `MapLibre`
- [ ] Struktura folderów: `App/ Contract/ Networking/ Map/ Features/ Persistence/ DesignSystem/`
- [ ] `ios/KayakMotor/README.md` (build na urządzeniu, capability, uwaga o Simulatorze)
- [ ] `ios/KayakMotorTests/SmokeTests.swift` (Swift Testing)
- [ ] Test: [Unit] Smoke — aplikacja buduje się i startuje (RootView renderuje placeholder)
- [ ] Weryfikacja: Projekt buduje się na urządzeniu; `import MapLibre` linkuje; capability
  i klucz Local Network obecne; target Swift Testing zielony

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

### Unit 3: Warstwa sieciowa — join, wymuszenie Wi‑Fi, HTTP, WS — L · (R1, R9) · zależności: Unit 0, Unit 2
- [ ] `Networking/HotspotJoiner.swift` (`NEHotspotConfiguration` + fallback manual)
- [ ] `Networking/CommandClient.swift` (protokół `CommandSending`; impl `URLSession`; miejsce na `NWConnection`)
- [ ] `Networking/TelemetrySocket.swift` (Network.framework WS pinowany `.wifi`, reconnect + backoff)
- [ ] `Networking/LinkState.swift` (`enum` dyskryminowany: disconnected/joining/connected/stale)
- [ ] Testy: `CommandClientTests.swift`, `LinkStateMachineTests.swift`
- [ ] Test: [Unit] `goto(lat,lon)` → poprawny URL, `Content-Type`, body `{"cmd":"goto","lat_e7":..,"lon_e7":..}`
- [ ] Test: [Unit] Odpowiedź 400 → typed `ApiError`, nie ignoruje
- [ ] Test: [Unit] LinkState: brak ramki > próg → `stale`; ramka wraca → `connected`; zerwanie
  → `disconnected`. Moc wyroczni: usunięcie progu stale → test „stale" FAILuje
- [ ] Test: [E2E] Połącz łączy z `kayak-motor`, WS zaczyna publikować telemetrię
- [ ] Weryfikacja: Testy zielone; na urządzeniu status linku odzwierciedla realny stan; `goto_cancel` → 200

### Unit 4: Store telemetrii + świeżość + status/HUD — M · (R2, R8, R9) · zależności: Unit 3
- [ ] `Features/Telemetry/TelemetryStore.swift` (`@Observable`; ostatnia ramka + `isStale`)
- [ ] `Features/Telemetry/GotoReadiness.swift` (czysta `(Telemetry)->GotoBlockReason?`)
- [ ] `Features/Telemetry/StatusHUDView.swift` (stan, gps_fix/sats, prędkość, `app_link_fresh`, powód)
- [ ] Testy: `GotoReadinessTests.swift`, `TelemetryStaleTests.swift`
- [ ] Test: [Unit] ARMED+fix+link+neutral → `nil` (gotowe)
- [ ] Test: [Unit] DISARMED → „Uzbrój na RC"; fix=false → „Brak fixu"; link=false → „Brak
  linku" (priorytet). Moc wyroczni: telemetria, która bez bramki by „przeciekła" → zwraca powód, nie `nil`
- [ ] Test: [Unit] Brak ramki > próg → `isStale==true`
- [ ] Weryfikacja: HUD pokazuje stan i powód; po zerwaniu WS dane wygasają; testy zielone

---

## Faza 2 — Mapa i nawigacja

### Unit 5: Mapa offline (kontur + marker + jakość GPS) — L · (R2, R10) · zależności: Unit 4, Unit 1
- [ ] `Map/LakeMapView.swift` (`UIViewRepresentable` + Coordinator)
- [ ] `Map/MapStyle.swift` (ładowanie `blank-style.json`, wiązanie warstw)
- [ ] Zasoby: `Resources/blank-style.json`, `Resources/lake.geojson` (OSM, atrybucja), `Resources/boat-icon`
- [ ] `Map/AttributionOverlay.swift` (stały „© OpenStreetMap contributors")
- [ ] Test: `MapStyleResourcesTests.swift`
- [ ] Test: [Unit] `lake.geojson` i `blank-style.json` w bundlu i parsują się; GeoJSON zawiera polygon
- [ ] Test: [E2E] Bez internetu (AP): kontur renderuje się, marker na pozycji GPS obraca się
  wg kursu; zero requestów sieciowych (tryb samolotowy + AP / Instruments)
- [ ] Test: [E2E] Marker płynny przy ~10 Hz (brak janku), kamera podąża
- [ ] Weryfikacja: Bez internetu mapa i pozycja/kurs żyją; atrybucja OSM widoczna; zero ruchu sieciowego

### Unit 6: Tap-to-goto (pin, linia, wysłanie, err/bearing) — M · (R3) · zależności: Unit 5, Unit 3, Unit 4
- [ ] `Features/Goto/GotoTargetController.swift` (stan celu; źródło mapa vs waypoint)
- [ ] Modyfikuj `Map/LakeMapView.swift` (tap→coord; warstwy pin + polyline łódź→cel)
- [ ] `Features/Goto/GotoControlsView.swift` (przycisk „Płyń do punktu"; `goto_err_m`/`goto_bearing_deg10`)
- [ ] Test: `GotoTargetControllerTests.swift`
- [ ] Test: [Unit] Postawienie celu ustawia współrzędne; drugi tap zastępuje (jeden cel)
- [ ] Test: [Unit] Cel poza ±90/±180 → odrzucony przed wysłaniem
- [ ] Test: [Unit] Po sukcesie `goto` stan = „wysłany"; po 400 → stan błędu (nie ciche zignorowanie)
- [ ] Test: [E2E] Dotknięcie → pin + linia; „Płyń do punktu" (ARMED+fix+neutral) → nawigacja
  rusza, `goto_err_m` maleje, `goto_state=1`
- [ ] Weryfikacja: Tap stawia cel, wysłanie startuje goto, malejąca odległość i linia widoczne;
  nieprawidłowy cel nie wysłany

### Unit 7: Keepalive + STOP/Rozbrój + idle-timer + arrived→CH3 — M · (R5, R6) · zależności: Unit 6
- [ ] `Features/Goto/KeepaliveController.swift` (timer ~2 Hz; cleanup przy STOP/tło/deinit)
- [ ] `Features/Goto/SafetyControlsView.swift` (wielki STOP zawsze widoczny; osobny „Rozbrój")
- [ ] Modyfikuj `App/RootView.swift` (`isIdleTimerDisabled` tylko podczas aktywnego goto)
- [ ] `Features/Goto/ArrivalHintView.swift` (po `goto_arrived` → „Włącz CH3 na RC")
- [ ] Test: `KeepaliveControllerTests.swift`
- [ ] Test: [Unit] Goto aktywne → keepalive wysyła cel < watchdog; po STOP → natychmiast
  przestaje. Moc wyroczni: usunięcie warunku „tylko gdy aktywne" → test „STOP zatrzymuje" FAILuje
- [ ] Test: [Unit] STOP → `goto_cancel`, NIE `disarm`; Rozbrój → `disarm`
- [ ] Test: [Unit] Idle-timer: ON tylko podczas active; pauza/OFF przywraca
- [ ] Test: [E2E] Podczas goto ekran nie gaśnie; STOP natychmiast przerywa; utrata linku →
  pauza → powrót wznawia; po dotarciu podpowiedź CH3
- [ ] Weryfikacja: Keepalive utrzymuje `app_link_fresh=true` w ruchu; STOP i Rozbrój natychmiastowe
  i rozłączne; auto-lock blokowany tylko w goto

---

## Faza 3 — Waypointy i barierki

### Unit 8: Waypointy (zapis realnej pozycji, trwałość, re-send) — M · (R4) · zależności: Unit 6, Unit 4
- [ ] `Persistence/Waypoint.swift` (`Codable {id,name,lat_e7,lon_e7,createdAt}`)
- [ ] `Persistence/WaypointStore.swift` (`Codable`→plik Application Support; add/rename/delete; trwałość)
- [ ] `Features/Waypoints/WaypointListView.swift` (lista, „Zapisz tę pozycję", tap→cel)
- [ ] Test: `WaypointStoreTests.swift`
- [ ] Test: [Unit] Add→save→reload zwraca ten sam waypoint. Moc wyroczni: bez realnego zapisu
  reload zwraca pustą listę → FAIL
- [ ] Test: [Unit] Rename/delete mutują i utrwalają
- [ ] Test: [Unit] „Zapisz tę pozycję" przy braku fixu → odrzucone (nie zapisuje 0,0)
- [ ] Test: [E2E] Zapis pozycji → restart apki → waypoint nadal na liście → tap wysyła goto
- [ ] Weryfikacja: Waypointy przeżywają restart; tap re-wysyła cel; brak zapisu bez fixu

### Unit 9: Miękkie ostrzeżenie geofence + polish słoneczny — M · (R7, R10) · zależności: Unit 6, Unit 5
- [ ] `Features/Goto/WaterGeofence.swift` (czysty ray-casting point-in-polygon)
- [ ] Modyfikuj `Features/Goto/GotoControlsView.swift` (dialog potwierdzenia gdy cel poza konturem)
- [ ] `DesignSystem/SunlightTheme.swift` (kontrast, rozmiary celów, układ pod jedną rękę)
- [ ] Test: `WaterGeofenceTests.swift`
- [ ] Test: [Unit] Punkt wewnątrz → brak ostrzeżenia; poza → ostrzeżenie. Moc wyroczni: punkt
  lądowy MUSI zwrócić „poza"; uwzględnić punkt na krawędzi
- [ ] Test: [Unit] Ostrzeżenie NIE blokuje — po potwierdzeniu cel jest wysłany
- [ ] Test: [E2E] Dotknięcie lądu → dialog; potwierdzenie → goto rusza mimo to
- [ ] Weryfikacja: Cel na lądzie wyzwala potwierdzenie, ale nie blokuje; UI czytelne przy słońcu, jedną ręką
