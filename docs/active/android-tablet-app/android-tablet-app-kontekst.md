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

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md
- Plan techniczny: docs/plans/2026-06-20-001-feat-android-tablet-app-plan.md
