# Kontekst: Aplikacja Android (tablet) — v1

Branch: `feature/android-tablet-app`
Ostatnia aktualizacja: 2026-06-20

## Kontrakt API ESP32 (źródło prawdy)

- **WS `/ws`** — telemetria push ~10 Hz, `WS_TELEMETRY_PERIOD_MS = 100`.
  - `components/web_panel/src/ws_telemetry.c`, `.../include/ws_telemetry.h`
  - Pola: `state`, `arm_reason`, `rc_valid`, `ch1_us/ch2_us/ch3_us/ch4_us`,
    `ch1_period_us/ch2_period_us`, `ch1_valid/ch2_valid`, `servo_us`, `esc_us`,
    `servo_trim_us`, `source`, `settings_valid`, `calibrated`, `defaults_used`,
    `nvs_error`, `gps_fix`, `gps_sats`, `gps_lat_e7`, `gps_lon_e7`, `gps_speed_cms`,
    `imu_ok`, `imu_heading_deg10`, `imu_calib`.
  - Jednostki: `*_e7` = stopnie×1e7; `imu_heading_deg10` = stopnie×10 [0,3599];
    `gps_speed_cms` = cm/s.
- **POST `/api/command` `{"cmd":"..."}`** — `components/web_panel/include/command_parse.h`
  - v1: `arm`, `disarm`, `deploy`, `stow`. Poza v1: `calib_*`, `trim_*`.
- **Envelope** `{ data, error: { code, message } }` — `.../include/api_contract.h`.
  - Odrzucenia: 409 `SETTINGS_WRITE_REJECTED_NOT_DISARMED`, 400 `VALIDATION_FAILED`.
- **WiFi SoftAP** WPA2-PSK, IP `192.168.4.1` — `components/web_panel/src/wifi_ap.c`.
- **Stany maszyny:** `0=DISARMED, 1=ARMED, 2=FAILSAFE, 3=ESC_CALIBRATION, 4=DEPLOY`.
- **Web panel referencyjny:** `web/app.js`, `web/index.html` (wzorzec subskrypcji WS,
  prezentacji telemetrii, wysyłki komend).

## Struktura projektu (docelowa)

```
android/
  settings.gradle.kts, build.gradle.kts, gradle.properties
  app/
    build.gradle.kts
    src/main/AndroidManifest.xml
    src/main/java/<pkg>/
      MainActivity.kt
      net/   ApConnectionManager.kt, ApConnectionState.kt, EspHttpClient.kt,
             CommandApi.kt, TelemetrySocket.kt
      data/  TelemetryFrame.kt, ApiEnvelope.kt, Command.kt, TelemetryRepository.kt
      domain/ MotorState.kt, LinkWatchdog.kt, ConnectionState.kt,
              SafetyIndicators.kt
      ui/    TelemetryViewModel.kt, TelemetryScreen.kt, components/...
      map/   MapLibreView.kt, OfflineStyle.kt, LayerToggle.kt, BoatMarker.kt,
             BoatMarkerProjection.kt
      session/ TelemetryService.kt
    src/main/assets/style/ (style.json, sprite, glyphs)
    src/test/java/<pkg>/ (testy JVM)
  maps/  README.md (generowanie/wgrywanie kafli)
  README.md
```

## Decyzje techniczne

- **Stack:** Kotlin + Jetpack Compose (BOM 2026.04.x) + ViewModel/StateFlow +
  `collectAsStateWithLifecycle`. Stan jako sealed class (coding-rules pkt 13).
- **Mapa:** MapLibre Native View-based, `org.maplibre.gl:android-sdk` (przypiąć
  najnowszą stabilną z Maven Central; rozważyć wariant Vulkan pod GPU tabletu),
  osadzenie przez `AndroidView` + lifecycle bridge.
- **Offline:** lokalne archiwa PMTiles (wektor OSM) + MBTiles (raster ortofoto) +
  lokalny style JSON; sprite/glyphs jako `asset://`. NIE OfflineManager. MBTiles
  z assets wymaga skopiowania do internal storage przed `mbtiles:///abs/path`.
- **Pozycja łodzi:** `GeoJsonSource` + `SymbolLayer`, `iconRotate(get("heading"))`,
  update async `setGeoJson` na wątku UI (NIE synchronous). Pomiń LocationComponent.
- **Sieć:** OkHttp 5.x; `requestNetwork(WifiNetworkSpecifier)` bez
  `NET_CAPABILITY_INTERNET`; `onAvailable` → `bindProcessToNetwork(network)` +
  `socketFactory(network.socketFactory)`; cleanup w `onLost`/zamknięciu.
- **JSON:** kotlinx.serialization (`ignoreUnknownKeys=true`).
- **Link-down:** app-level watchdog (~500 ms bez ramki = STALE) obok ping/pong;
  różnica czasu w jednej domenie zegara monotonicznego (learned-patterns: recency).
- **Sesja:** `FLAG_KEEP_SCREEN_ON` na oknie (nie w service); foreground service typu
  `connectedDevice` (Android 14+) dla utrzymania WS; `PARTIAL_WAKE_LOCK` tylko gdy dropy.

## Uprawnienia (manifest)

`INTERNET`, `CHANGE_NETWORK_STATE`, `ACCESS_NETWORK_STATE`, `ACCESS_WIFI_STATE`,
`FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_CONNECTED_DEVICE`,
(`ACCESS_FINE_LOCATION` jeśli minSdk ≤ 32).

## Kafle offline (legalne)

- **OSM wektor → PMTiles** generowane Planetilerem dla bbox akwenu (ograniczony maxzoom).
  Licencja danych: ODbL → atrybucja „© OpenStreetMap" widoczna w UI.
- **Ortofoto → Geoportal GUGiK ORTO WMTS** →
  `https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMTS/StandardResolution`
  pre-cache do MBTiles raster (darmowe, dowolny cel, offline OK). Rate-limit przy pobieraniu.
- **NIE** Mapbox/Google (ToS zabrania cache offline).

## Zależności i wymagania wstępne

- **Sprzęt:** tablet jeszcze niekupiony (rekomendacja: Xiaomi Redmi Pad Pro 12,1" 8/256
  WiFi; budżet: Lenovo Tab M11). Po wyborze: finalny minSdk, wersja MapLibre, dostrojenie progów.
- **v2 (Spot Lock/Go To Point)** zależy od osobnego firmware regulatora pozycji — poza tym planem.
- **ESP32** z aktualnym firmware (kontrakt API jak wyżej) do testów integracyjnych na urządzeniu.

## Postawa wykonawcza

Logikę domenową (parsing telemetrii, konwersje jednostek, `LinkWatchdog`, redukcja
stanu połączenia, mapowanie envelope→wynik, projekcja markera, builder stylu) pisz jako
**czyste funkcje z testami JVM** przed/obok cienkich adapterów (Pure ⊥ HAL). Adaptery
Androida (sieć, mapa, service) weryfikuj manualnie na urządzeniu/emulatorze. Testy: min.
1 happy path + 1 error case per funkcja; fixtures JSON w `src/test/resources/`.

## Stan wykonania

### Faza 1 — Fundament i łączność (kod ukończony 2026-06-20)
- **minSdk = 29** (odroczone pytanie rozwiązane): ścieżka `requestNetwork` +
  `WifiNetworkSpecifier` bez `NET_CAPABILITY_INTERNET` wymaga API 29; na 29–32
  specyfikator wymaga `ACCESS_FINE_LOCATION` → uprawnienie w manifeście z
  `maxSdkVersion="32"`. targetSdk/compileSdk = 34. AGP 8.5.2, Kotlin 2.0.20.
- **Package:** `com.thatmotor.kayak`. Warstwy: `net`, `data`, `domain`, `ui`, `map`
  (puste pakiety zaznaczone `.gitkeep`, wypełniane w kolejnych fazach).
- **Wersje pinowane** w `gradle/libs.versions.toml` (version catalog) — Compose BOM
  2024.09.03, OkHttp 5.0.0-alpha.14, kotlinx.serialization 1.7.3, MapLibre 11.5.2.
- **Unit 2:** czysty reducer `reduceApConnectionState(current, event)` (testowany
  na JVM, 4 testy) + cienki adapter `ApConnectionManager` (HAL: `NetworkCallback`
  → `ApConnectionEvent` → reducer; bind + cleanup). `Lost` ignorowany poza stanem
  live (`Connecting`/`Connected`) — chroni wynik `Failed`/`Idle`.
- **Walidacja Gradle:** to środowisko NIE ma JDK/Gradle/Android SDK — `assembleDebug`
  i testy JVM nie zostały uruchomione (oczekiwane). Wymagają lokalnego SDK + JDK 17.
  `gradle-wrapper.jar` (binarny) nie commitowany — generowany przez `gradle wrapper`
  lub Android Studio (opisane w `android/README.md`).

### Code review Fazy 1 (2026-06-20)

Multi-agent review (security, performance, architecture, test coverage). E2E browser
verification: N/A — brak środowiska (emulator/przeglądarka) i brak web UI w tej fazie.
Raport: `review-faza-1.md`. Severity gate: ⚠️ ZASTRZEŻENIA (0×P1, 3×P2, 7×P3).

Kluczowe wnioski:
- Jakość kodu dobra: Pure ⊥ HAL wzorowo, testy z mocą wyroczni (oracle power), brak
  hardcoded secrets, brak `!!`/`as`, discriminated unions, pinowane wersje.
- 3×P2 do naprawy przed warstwą OkHttp: (1) `boundNetwork` thread-safe (`@Volatile`/Flow),
  (2) timeout `requestNetwork` (R7 — utknięcie w `Connecting`), (3) zawężenie cleartext
  do `192.168.4.1` przez `network_security_config`.
- Odchylenie `Network`→`boundNetwork` (zamiast `Connected(network)`) ocenione jako
  uzasadnione — chroni czystość reducera. Konsekwencja: `Failed` bez `reason` (P3, dług na v2).
- Pokrycie Unit 2 adekwatne (4 scenariusze z planu 1:1); brak testu adaptera HAL i Unit 1
  zgodny z planem. Testów JVM nie uruchomiono (brak JDK/Gradle) — ograniczenie środowiska.

### Re-review Fazy 1 po naprawie P2 (2026-06-20, cykl 1)

Commit naprawczy `b3b0682`. Severity gate: ✅ GOTOWE DO KONTYNUACJI (0×P1, 0×P2, 8×P3).
Wszystkie 3 P2 zweryfikowane jako ROZWIĄZANE bez regresji (analiza statyczna + weryfikacja
semantyki API Androida; build/testy nie uruchomione — brak JDK/Gradle/SDK):
- P2-1 `boundNetwork` → `@Volatile` (pojedyncza referencja, widoczność cross-thread OK).
- P2-2 `requestNetwork(request, cb, timeoutMs=30 s)` + `require(timeoutMs>0)`; API 26+,
  timeout → `onUnavailable` → `Failed` domyka R7.
- P2-3 nowy `res/xml/network_security_config.xml`: base-config deny + domain-config permit
  tylko `192.168.4.1`; literał IP i `ws://`/`http://` (web panel R8) pokryte.
- Regresje: brak. Czysty reducer i typy niezmienione → 4 testy reducera zachowują moc wyroczni.
- Nowy P3 (symetria reducera): `Unavailable` demuje `Connected→Failed` bezwarunkowo; kontrakt
  Androida wyklucza ten scenariusz → nie defensive code; ewentualna symetria z testami oracle-power.
- Pozostałe 7×P3 z cyklu 0 przeniesione (świadomie odroczone). Można przejść do Fazy 2.

### Faza 2 — Kontrakt danych i transport (kod ukończony 2026-06-21)

Zweryfikowano kontrakt 1:1 z firmware przed implementacją (źródła prawdy):
`ws_telemetry.c::snapshot_to_json`, `control_loop.h::control_loop_snapshot`,
`state_machine.h` (sm_state/sm_arm_reason), `api_contract.c` (stringi error code).

- **Unit 3 (modele/serializacja):** `data/TelemetryFrame.kt` — 27 pól dokładnie wg
  `snapshot_to_json` (`@SerialName` snake_case). Mapowanie szerokości C→Kotlin:
  `uint32_t`→`Long` (pulsy/okresy, mieszczą zakres > Int), `uint16_t`/`uint8_t`→`Int`,
  `int16_t`/`int32_t`→`Int`. Konwersje jednostek wydzielone do czystego
  `data/TelemetryUnits.kt` (`*_e7/1e7`, `deg10/10`, `cms/100`) — model wire pozostaje
  wiernym lustrem JSON. `data/ApiEnvelope.kt` generyczny `ApiEnvelope<T>` + `ApiError`
  (stałe `CODE_NOT_DISARMED`, `CODE_VALIDATION_FAILED` zweryfikowane z `api_contract.c`).
  `data/Command.kt` (enum arm/disarm/deploy/stow → keyword + `CommandRequest`).
  `domain/MotorState.kt` — `motorStateFromCode` zwraca discriminated `Known/Unknown`
  (nieznany kod = ścieżka błędu, nie crash); `armReasonFromCode` (0..4, null gdy nieznany).
  Parser współdzielony `data/TelemetryJson.kt` (`ignoreUnknownKeys = true`).
- **Unit 4 (REST+WS):** czysta `net/CommandResult.kt::mapCommandResult(status, error)`
  → `Success/Rejected/TransportError` (populated error → Rejected niezależnie od kodu
  HTTP; non-2xx bez envelope → TransportError). Cienkie adaptery: `net/EspHttpClient.kt`
  (OkHttp, `socketFactory(network.socketFactory)`, `pingInterval` 7 s, timeouty LAN),
  `net/CommandApi.kt` (POST /api/command, mapuje przez czysty rdzeń, łapie `IOException`
  → TransportError, nie rzuca), `net/TelemetrySocket.kt` (`callbackFlow` → `Flow<TelemetryFrame>`,
  zła ramka logowana i pomijana, `awaitClose { socket.cancel() }`).
- **Unit 5 (repo+watchdog):** czysty `domain/LinkWatchdog.kt::linkStatus` (jedna domena
  zegara monotonicznego, `elapsed > threshold` → STALE; granica == próg pozostaje LIVE,
  learned-patterns: recency). `domain/ConnectionState.kt` (sealed
  Disconnected/Connecting/Live/Stale). `domain/ReconnectBackoff.kt::reconnectDelayMs`
  (250→500→1000→max 2000, clamp bez overflow). `data/TelemetryRepository.kt` —
  `StateFlow<TelemetryUiState>`, dwie pętle (collect + watchdog tick 200 ms),
  `SystemClock.elapsedRealtime` jako zegar, `pause/resumeReconnect` (wstrzymanie gdy AP Lost).
- **Testy JVM (czyste rdzenie):** `TelemetryFrameParseTest` (4), `ApiEnvelopeParseTest` (2),
  `CommandResultMapTest` (4), `LinkWatchdogTest` (3, granica progu — oracle power),
  `ReconnectBackoffTest` (2, wzrost + cap). Fixtures JSON w `src/test/resources/`.
  Helper `TestFixtures.kt`. Adaptery HAL (OkHttp/socket) — weryfikacja manualna na ESP32.
- **Walidacja Gradle/JVM:** środowisko NIE ma realnego JRE (java to stub), brak Gradle,
  brak `gradle-wrapper.jar`, brak Android SDK (`ANDROID_HOME` pusty). `./gradlew test`
  i build NIE uruchomione (oczekiwane, nie błąd). Weryfikacja statyczna: API kotlinx
  .serialization/OkHttp/coroutines zgodne z pinowanymi wersjami; brak `any`/`!!`/`as`;
  zgodność pól/typów/jednostek z firmware potwierdzona przez odczyt źródeł.

### Code review Fazy 2 (2026-06-20)

Multi-agent review (security, performance, architecture/type-safety, test coverage).
E2E browser: N/A — brak UI/web w fazie, brak emulatora/ESP32/JVM. Raport: `review-faza-2.md`.
Severity gate: ⚠️ ZASTRZEŻENIA (0×P1, 6×P2, 8×P3).

Kluczowe wnioski:
- Kontrakt z firmware zweryfikowany 1:1 przed review: 27 pól `TelemetryFrame` =
  `snapshot_to_json`, `MotorState`/`ArmReason` = `sm_state`/`sm_arm_reason` (0..4), error
  codes = `api_contract`, keywords `Command` = `command_parse.c`. Brak rozjazdu kontraktu.
- Jakość rdzenia wysoka: Pure ⊥ HAL realne (domain bez `android.*`/`okhttp`/`data`/`net`),
  zero `!!`/`as`/`Any`, discriminated unions, testy z mocą wyroczni (konwersje, próg
  watchdoga `>` vs `>=`, cap backoffu — failują bez transformacji), poprawny cleanup
  `awaitClose`/cancel, bezpieczna deserializacja, brak pustych catch w `net`.
- 6×P2: (1) cykl warstw `data ⇄ net` — `TelemetryRepository` w `data` importuje `net`
  (wydzielić orkiestrację do osobnej warstwy); (2) `callbackFlow` bez polityki backpressure
  i ciche `trySend` (dodać `conflate`/`DROP_OLDEST`); (3) `onFrame` 10 emisji/s →
  recomposition flood w Fazie 3 (split connection/frame flow); (4-6) 3 luki testowe czystych
  funkcji/kontraktów: `armReasonFromCode`, `CommandRequest(Command)`+serializacja, zgodność
  keywords z firmware.
- Decyzja severity: finding backpressure WS zgłoszony przez performance jako P1, obniżony do
  P2 — steady-state działa poprawnie (collector non-suspending), nie blokuje, ale wymaga
  naprawy przed konsumentem UI (Faza 3).
- Build/testy JVM nie uruchomione (brak JDK/Gradle/SDK) — ograniczenie środowiska, nie finding.

### Re-review Fazy 2 po naprawie 6×P2 (2026-06-20, cykl 1)

Commit naprawczy `a98a661`. Severity gate: ✅ GOTOWE DO KONTYNUACJI (0×P1, 0×P2, 8×P3).
Wszystkie 6×P2 ZWERYFIKOWANE jako ROZWIĄZANE bez regresji (analiza statyczna; build/testy
nie uruchomione — brak JDK/Gradle/SDK):
- KOD-1 cykl `data ⇄ net`: `TelemetryRepository`/`TelemetryUiState` przeniesione do nowego
  pakietu `repository`. Graf acykliczny (grep): `data` bez importów `net`/`okhttp`/`android`,
  `domain` pure, `net → data`, `repository → net/data/domain`, brak `net → repository`.
  Zero martwych referencji do starego pakietu `data.TelemetryRepository`.
- KOD-2 backpressure WS: `.buffer(1, DROP_OLDEST)` (jawna polityka lossy) + wynik `trySend`
  sprawdzany/logowany (`isFailure && !isClosed` → `Log.w`), nie połykany.
- KOD-3 flood 10 Hz: `onFrame` używa `copy(latestFrame)` gdy Live; rozdzielone strumienie
  `connection` (`distinctUntilChanged`) vs `latestFrame` → brak phase floodu w Fazie 3.
- TEST-4 `armReasonFromCode`: happy (0→READY, 1..4 map-based) + error (99→null); oracle 1:1
  z `state_machine.h::sm_arm_reason`.
- TEST-5 `CommandRequest(Command)`: `cmd=="arm"` + serializacja `{"cmd":"arm"}` (kształt wire).
- TEST-6 keywords: completeness assert (`Command.entries.toSet()`) zweryfikowany wobec
  `command_parse.c::COMMAND_TABLE` (arm/disarm/deploy/stow) — moc wyroczni na regresję.
- Regresje: brak. Czyste rdzenie niezmienione → testy z cyklu 0 zachowują moc wyroczni.
- 8×P3 przeniesione (świadomie odroczone; ścieżki `TelemetryRepository` → pakiet `repository`).
  P3 watchdog-drift częściowo złagodzony (`tickWatchdog` guard `if (!hasFrame) return`).
  Można przejść do Fazy 3.

### Faza 3 — UI operacyjny (kod ukończony 2026-06-20)

Logika decyzyjna jako czyste funkcje testowane na JVM (Pure ⊥ HAL); Compose jako
cienka warstwa prezentacji bez decyzji.

- **Unit 6 (ekran + wskaźniki):**
  - `domain/SafetyIndicators.kt` — czysta `safetyIndicators(connection, frame?)` →
    `SafetyIndicators(linkDown, failsafe, armed, uncalibrated)`. `linkDown` zależy WYŁĄCZNIE
    od `ConnectionState` (Stale/Connecting/Disconnected → true), więc zamrożona „zdrowa"
    ramka przy martwym linku nie wygląda bezpiecznie. `failsafe`/`armed` z `motorStateFromCode`
    (nieznany kod → brak flagi, nie crash). `uncalibrated = !frame.calibrated`. Brak ramki →
    `noFrame(connection)` (tylko link znany).
  - `ui/TelemetryViewModel.kt` — `repository.state` → `TelemetryScreenState`
    (telemetria + wyliczone `indicators` + `availability`) przez `stateIn(WhileSubscribed)`;
    derywacje liczone raz na ramkę, nie na recomposition.
  - `ui/TelemetryScreen.kt` + `ui/components/`: `StatusBadge` (LIVE/CONNECTING/STALE/
    DISCONNECTED, solid high-contrast pill), `InfoCard`+`LabeledValue` (współdzielony
    scaffold), `GpsCard` (fix/sats/pozycja/prędkość przez konwersje z `TelemetryUnits`),
    `CompassCard` (heading „—" gdy `imu_ok=false`, nie mylące 0°), `SafetyBanner`
    (kolejność: FAILSAFE → LINK DOWN → ARMED → UNCALIBRATED; pusty → „DISARMED — drive
    stopped" SafeGreen), `WebPanelLink` (R8: `http://192.168.4.1` przez `ACTION_VIEW`,
    `ActivityNotFoundException` logowany nie crash).
  - Paleta wysokokontrastowa rozszerzona o `SurfaceRaised`/`OnSurfaceMuted` (czytelność w słońcu).
- **Unit 7 (komendy):**
  - `domain/CommandAvailability.kt` — czysta `commandAvailability(connection, frame?)`:
    wszystko wyłączone gdy nie `Live` lub brak ramki (komendy po martwym linku
    niepotwierdzalne). DISARMED→ARM+DEPLOY; ARMED/FAILSAFE→DISARM; DEPLOY→STOW;
    ESC_CALIBRATION→nic.
  - `ui/CommandFeedback.kt` — czysta `commandFeedback(command, CommandResult)` →
    `Success/Rejected/TransportError`; rejection pokazuje komunikat kontrolera (kontrakt
    firmware), transport error = ogólny komunikat (surowy detal tylko do logu, pkt 4).
  - `ui/components/CommandBar.kt` — 4 przyciski (enable wg `availability`); DEPLOY/STOW przez
    `AlertDialog` potwierdzenia (ruszają sprzętem).
  - `ui/TelemetryViewModel.sendCommand` → `CommandSender` (nowy `fun interface` w `net`,
    implementowany przez `CommandApi`) → `_feedback: StateFlow`. Ekran pokazuje feedback w
    `Snackbar` i czyści przez `dismissFeedback`. Seam `CommandSender` = jedyne zewnętrzne API
    do fake'owania w testach (pkt 2: mock TYLKO zewnętrznego API).
  - `MainActivity` — manualne DI (bez frameworka): `EspHttpClient.build(null)` (process-default;
    bound `Network` z Unit 2/Fazy 5 później) → `TelemetrySocket`/`CommandApi`/`TelemetryRepository`
    (start w `onCreate`, stop w `onDestroy`) → `TelemetryViewModel` → `TelemetryScreen`.
- **Testy JVM (czyste rdzenie + ViewModel):** `SafetyIndicatorsTest` (7: failsafe, Stale→linkDown
  niezależnie od ramki, calibrated true/false, armed, no-frame — oracle power), `CommandAvailabilityTest`
  (5: per-stan + stale/no-frame veto), `CommandActionTest` (3: success/rejected/transport — czysta
  `commandFeedback`), `TelemetryViewModelTest` (3: dispatch sukces/odrzucenie/dismiss z `FakeCommandSender`
  — fake TYLKO zewnętrznego API, repo realne ale niezbierane). Helpery: `telemetryFrame(...)` builder
  w `TestFixtures.kt` (zdrowe DISARMED defaults; override pola pod testem), `MainDispatcherRule`.
- **Walidacja Gradle/JVM:** środowisko bez realnego JRE (`java` to stub, „Unable to locate a Java
  Runtime"), brak Gradle, brak `gradle-wrapper.jar`, brak Android SDK (`ANDROID_HOME` pusty). `./gradlew
  test`/build NIE uruchomione (oczekiwane, nie błąd). Weryfikacja statyczna: wszystkie symbole theme/
  domain/ui rozwiązane, `Modifier.weight` w `RowScope`, `collectAsStateWithLifecycle` z dostępnego
  `lifecycle-runtime-compose`, `.entries` zgodne z konwencją, brak `any`/`!!`/`as`, discriminated unions,
  zero pustych catch.

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md
- Plan techniczny: docs/plans/2026-06-20-001-feat-android-tablet-app-plan.md
