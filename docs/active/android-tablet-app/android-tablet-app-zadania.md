# Zadania: Aplikacja Android (tablet) — v1

Branch: `feature/android-tablet-app`
Ostatnia aktualizacja: 2026-06-20

Legenda: `[ ]` do zrobienia · prefix `Test:` = scenariusz testowy · prefix
`Weryfikacja:` = kryterium ukończenia. Nakład: S/M/L/XL.

---

## Faza 1 — Fundament i łączność

### Unit 1: Bootstrap projektu Android (S/M)
- [ ] Stwórz `android/settings.gradle.kts`, `android/build.gradle.kts`, `android/gradle.properties`
- [ ] Stwórz `android/app/build.gradle.kts` (zależności pinowane: Compose BOM, MapLibre, OkHttp 5.x, kotlinx.serialization, lifecycle-runtime-compose)
- [ ] Ustal `minSdk`/`targetSdk` (rozwiąż odroczone pytanie minSdk)
- [ ] Stwórz `android/app/src/main/AndroidManifest.xml` (uprawnienia: INTERNET, CHANGE_NETWORK_STATE, ACCESS_NETWORK_STATE, ACCESS_WIFI_STATE, FOREGROUND_SERVICE, FOREGROUND_SERVICE_CONNECTED_DEVICE, [ACCESS_FINE_LOCATION jeśli minSdk ≤32])
- [ ] Stwórz `android/app/src/main/java/<pkg>/MainActivity.kt` + `ui/theme/`
- [ ] Stwórz `android/.gitignore` (build/, .gradle/, local.properties, *.pmtiles, *.mbtiles) i `android/README.md`
- [ ] Pakiety wg warstw: `net`, `data`, `domain`, `ui`, `map`
- [ ] Weryfikacja: `./gradlew :app:assembleDebug` przechodzi; app startuje na emulatorze (pusty ekran z motywem)

### Unit 2: Adapter łączności z AP ESP32 (M)
- [ ] Stwórz `net/ApConnectionManager.kt` (`requestNetwork`+`WifiNetworkSpecifier` bez `NET_CAPABILITY_INTERNET`; `onAvailable`→`bindProcessToNetwork`)
- [ ] Stwórz `net/ApConnectionState.kt` (sealed: Idle/Connecting/Connected/Lost/Failed)
- [ ] Wyciągnij czystą funkcję redukcji zdarzeń callbacku → `ApConnectionState`
- [ ] Cleanup: `unregisterNetworkCallback` + `bindProcessToNetwork(null)`
- [ ] Stwórz test `net/ApConnectionStateReducerTest.kt`
- [ ] Test: sekwencja `Connecting → onAvailable → Connected`
- [ ] Test: `Connected → onLost → Lost`; ponowne `onAvailable → Connected`
- [ ] Test: `onUnavailable → Failed`
- [ ] Weryfikacja: po wskazaniu SSID ESP32 app → `Connected`; GET do `192.168.4.1` odpowiada mimo braku internetu

---

## Faza 2 — Kontrakt danych i transport

### Unit 3: Modele danych i (de)serializacja (M)
- [ ] Stwórz `data/TelemetryFrame.kt` (pola dokładnie wg `ws_telemetry.h`)
- [ ] Stwórz `data/ApiEnvelope.kt` (`data`, `error{code,message}`)
- [ ] Stwórz `data/Command.kt` (arm/disarm/deploy/stow)
- [ ] Stwórz `domain/MotorState.kt` (mapowanie `state` 0–4 + `arm_reason`)
- [ ] Czyste konwersje jednostek (`lat_e7→deg`, `heading_deg10→deg`, `speed_cms→m/s`)
- [ ] Parser z `ignoreUnknownKeys = true`; fixtures JSON w `src/test/resources/`
- [ ] Stwórz testy `data/TelemetryFrameParseTest.kt`, `data/ApiEnvelopeParseTest.kt`
- [ ] Test: parsowanie pełnej ramki → poprawne pola/konwersje (`gps_lat_e7=520000000→52.0°`, `imu_heading_deg10=900→90.0°`)
- [ ] Test: ramka z nieznanym polem nie wywala parsera
- [ ] Test: envelope błędu `{data:null,error:{code,message}}` zmapowany poprawnie
- [ ] Test: `state` 0–4 → `MotorState`; nieznana wartość → ścieżka błędu (nie crash)
- [ ] Weryfikacja: testy JVM zielone; konwersje zgodne z firmware

### Unit 4: Klient REST + WebSocket (OkHttp) (M)
- [ ] Stwórz `net/EspHttpClient.kt` (OkHttp z `socketFactory(network.socketFactory)`, `pingInterval` 5–10 s)
- [ ] Stwórz `net/CommandApi.kt` (POST /api/command; wynik z envelope)
- [ ] Stwórz `net/TelemetrySocket.kt` (WS → `Flow<TelemetryFrame>` przez `callbackFlow`)
- [ ] Czysta funkcja: (status HTTP + envelope) → `CommandResult`
- [ ] Stwórz test `net/CommandResultMapTest.kt`
- [ ] Test: 200 + `error:null` → `Success`
- [ ] Test: 409 + `SETTINGS_WRITE_REJECTED_NOT_DISARMED` → `Rejected(reason)`
- [ ] Test: 400 + `VALIDATION_FAILED` → `Rejected(reason)`
- [ ] Weryfikacja: na ESP32 `arm`/`disarm` zmienia stan; WS dostarcza ramki ~10 Hz

### Unit 5: Repozytorium telemetrii + watchdog link-down (M)
- [ ] Stwórz `data/TelemetryRepository.kt` (`StateFlow<TelemetryUiState>`)
- [ ] Stwórz `domain/LinkWatchdog.kt` (czysta: ostatni-czas + teraz → `Live/Stale`)
- [ ] Stwórz `domain/ConnectionState.kt` (sealed: Disconnected/Connecting/Live/Stale)
- [ ] Reconnect z backoff (250 ms → 1 s → max 2 s); wstrzymaj gdy AP `Lost`
- [ ] Różnica czasu w jednej domenie zegara monotonicznego (`elapsedRealtime`)
- [ ] Stwórz test `domain/LinkWatchdogTest.kt` (test wokół granicy progu — oracle power)
- [ ] Test: ramka tuż przed progiem → `Live`; po przekroczeniu progu → `Stale`
- [ ] Test: po `Stale` nowa ramka → `Live`
- [ ] Test: reconnect backoff rośnie i jest ograniczony do max
- [ ] Weryfikacja: odłączenie ESP32 → link-down < ~1 s; powrót → `Live`

---

## Faza 3 — UI operacyjny

### Unit 6: Ekran telemetrii i wskaźniki bezpieczeństwa (M)
- [ ] Stwórz `ui/TelemetryViewModel.kt` (zbiera StateFlow z repo)
- [ ] Stwórz `ui/TelemetryScreen.kt` + `ui/components/` (StatusBadge, GpsCard, CompassCard, SafetyBanner)
- [ ] Stwórz `domain/SafetyIndicators.kt` (czysta: (ConnectionState, frame) → wskaźniki)
- [ ] Baner: FAILSAFE / LINK DOWN / ARMED-DISARMED / UNCALIBRATED
- [ ] Skrót/odnośnik do web panelu (`http://192.168.4.1`) — R8
- [ ] Stwórz test `domain/SafetyIndicatorsTest.kt`
- [ ] Test: `state=FAILSAFE` → wskaźnik failsafe aktywny
- [ ] Test: `ConnectionState.Stale` → linkDown aktywny niezależnie od ostatniej ramki
- [ ] Test: `calibrated=false` → wskaźnik UNCALIBRATED
- [ ] Weryfikacja: telemetria odświeża się płynnie; failsafe/odłączenie → właściwe banery

### Unit 7: Komendy operacyjne (arm/disarm/deploy/stow) (S/M)
- [ ] Modyfikuj `ui/TelemetryViewModel.kt` (akcje komend)
- [ ] Stwórz `ui/components/CommandBar.kt` (przyciski + potwierdzenie deploy/stow)
- [ ] Wyłącz przyciski niedostępne w danym stanie (np. arm gdy link down)
- [ ] Stwórz test `ui/CommandActionTest.kt` (mock TYLKO zewnętrznego API)
- [ ] Test: sukces komendy → komunikat sukcesu, brak błędu
- [ ] Test: `Rejected(NOT_DISARMED)` → komunikat o odrzuceniu, brak crasha
- [ ] Weryfikacja: na urządzeniu arm/disarm/deploy/stow działają i raportują odrzucenia

---

## Faza 4 — Mapa offline

### Unit 8: Integracja MapLibre + warstwy offline (L)
- [ ] Stwórz `map/MapLibreView.kt` (`AndroidView` + lifecycle bridge: DisposableEffect + LifecycleEventObserver, przekazanie onStart/onResume/onPause/onStop/onDestroy/onLowMemory)
- [ ] Stwórz `map/OfflineStyle.kt` (style JSON: `pmtiles://`/`mbtiles://`, `asset://` sprite/glyphs)
- [ ] Stwórz `map/LayerToggle.kt` (visibility rastra; kolejność `addLayerBelow/Above`)
- [ ] Stwórz `app/src/main/assets/style/` (style.json, sprite, glyphs)
- [ ] Atrybucja „© OpenStreetMap" widoczna na mapie
- [ ] Stwórz test `map/OfflineStyleBuilderTest.kt`
- [ ] Test: builder stylu produkuje źródła z poprawnymi URI i kolejnością warstw
- [ ] Test: toggle ustawia `visibility` rastra na VISIBLE/NONE
- [ ] Weryfikacja: (bez internetu) mapa renderuje wektor OSM; toggle ortofoto; brak czarnej mapy po powrocie z tła

### Unit 9: Marker pozycji łodzi + heading (M)
- [ ] Stwórz `map/BoatMarker.kt` (GeoJsonSource + SymbolLayer, `iconRotate(get("heading"))`)
- [ ] Stwórz `map/BoatMarkerProjection.kt` (czysta: frame → (lon,lat,heading); obsługa braku fixa)
- [ ] Modyfikuj `map/MapLibreView.kt` (podpięcie do StateFlow; guard na nieaktywny styl)
- [ ] Update tylko `setGeoJson` (async) na wątku UI; opcjonalny tryb „follow"
- [ ] Stwórz test `map/BoatMarkerProjectionTest.kt`
- [ ] Test: ramka z fixem → poprawne (lon,lat,heading)
- [ ] Test: `gps_fix=false` → projekcja sygnalizuje brak pozycji (marker ukryty)
- [ ] Test: `imu_ok=false` → heading nieznany (brak rotacji), nie 0°
- [ ] Weryfikacja: na ESP32 marker rusza się wg telemetrii, strzałka wg kompasu, bez lagów przy 10 Hz

### Unit 11: Pipeline i dokumentacja map offline (M)
- [ ] Stwórz `android/maps/README.md` (Planetiler OSM→PMTiles dla bbox; Geoportal ORTO WMTS→MBTiles przez GDAL/rio-mbtiles; atrybucja ODbL; asset vs internal storage)
- [ ] Stwórz `android/maps/` (artefakty/skrypty; duże pliki poza git)
- [ ] Rate-limit przy pobieraniu WMTS; tylko bbox akwenu
- [ ] Weryfikacja: kroki README produkują archiwa renderowane offline przez Unit 8/9

---

## Faza 5 — Utrzymanie sesji

### Unit 10: Keep-screen-on + foreground service WS (M)
- [ ] Stwórz `session/TelemetryService.kt` (foreground, `foregroundServiceType="connectedDevice"`, notyfikacja)
- [ ] Modyfikuj `MainActivity.kt` (`FLAG_KEEP_SCREEN_ON` na oknie ekranu nawigacji)
- [ ] Modyfikuj `AndroidManifest.xml` (service + typ)
- [ ] `PARTIAL_WAKE_LOCK` tylko jeśli dropy przy ekranie-off; zwalniaj po sesji
- [ ] Cleanup: stop service, unregister NetworkCallback, `bindProcessToNetwork(null)`, zwolnienie wake locka
- [ ] Weryfikacja: ekran nie gaśnie na ekranie nawigacji; telemetria przeżywa tło→powrót; brak wiszących callbacków/wake locków po wyjściu z sesji

---

## Postęp

- Faza 1: ☐  ·  Faza 2: ☐  ·  Faza 3: ☐  ·  Faza 4: ☐  ·  Faza 5: ☐

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md
- Plan techniczny: docs/plans/2026-06-20-001-feat-android-tablet-app-plan.md
