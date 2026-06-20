# Zadania: Aplikacja Android (tablet) — v1

Branch: `feature/android-tablet-app`
Ostatnia aktualizacja: 2026-06-20

Legenda: `[ ]` do zrobienia · prefix `Test:` = scenariusz testowy · prefix
`Weryfikacja:` = kryterium ukończenia. Nakład: S/M/L/XL.

---

## Faza 1 — Fundament i łączność

### Unit 1: Bootstrap projektu Android (S/M)
- [x] Stwórz `android/settings.gradle.kts`, `android/build.gradle.kts`, `android/gradle.properties`
- [x] Stwórz `android/app/build.gradle.kts` (zależności pinowane: Compose BOM, MapLibre, OkHttp 5.x, kotlinx.serialization, lifecycle-runtime-compose)
- [x] Ustal `minSdk`/`targetSdk` (rozwiąż odroczone pytanie minSdk) → minSdk 29, targetSdk/compileSdk 34
- [x] Stwórz `android/app/src/main/AndroidManifest.xml` (uprawnienia: INTERNET, CHANGE_NETWORK_STATE, ACCESS_NETWORK_STATE, ACCESS_WIFI_STATE, FOREGROUND_SERVICE, FOREGROUND_SERVICE_CONNECTED_DEVICE, [ACCESS_FINE_LOCATION jeśli minSdk ≤32])
- [x] Stwórz `android/app/src/main/java/<pkg>/MainActivity.kt` + `ui/theme/`
- [x] Stwórz `android/.gitignore` (build/, .gradle/, local.properties, *.pmtiles, *.mbtiles) i `android/README.md`
- [x] Pakiety wg warstw: `net`, `data`, `domain`, `ui`, `map`
- [ ] Weryfikacja: `./gradlew :app:assembleDebug` przechodzi; app startuje na emulatorze (pusty ekran z motywem)

### Unit 2: Adapter łączności z AP ESP32 (M)
- [x] Stwórz `net/ApConnectionManager.kt` (`requestNetwork`+`WifiNetworkSpecifier` bez `NET_CAPABILITY_INTERNET`; `onAvailable`→`bindProcessToNetwork`)
- [x] Stwórz `net/ApConnectionState.kt` (sealed: Idle/Connecting/Connected/Lost/Failed)
- [x] Wyciągnij czystą funkcję redukcji zdarzeń callbacku → `ApConnectionState` (`net/ApConnectionStateReducer.kt`)
- [x] Cleanup: `unregisterNetworkCallback` + `bindProcessToNetwork(null)`
- [x] Stwórz test `net/ApConnectionStateReducerTest.kt`
- [x] Test: sekwencja `Connecting → onAvailable → Connected`
- [x] Test: `Connected → onLost → Lost`; ponowne `onAvailable → Connected`
- [x] Test: `onUnavailable → Failed`
- [ ] Weryfikacja: po wskazaniu SSID ESP32 app → `Connected`; GET do `192.168.4.1` odpowiada mimo braku internetu

### Do poprawy po review fazy 1

Severity gate (re-review, cykl 1): ✅ GOTOWE DO KONTYNUACJI (0×P1, 0×P2, 8×P3).
Wszystkie 3 P2 z cyklu 0 ROZWIĄZANE bez regresji (commit `b3b0682`). Pełny raport:
`review-faza-1.md`. E2E: N/A (brak środowiska — emulator/przeglądarka).

P2 (rozwiązane w cyklu 1):
- [x] 🟠 [important] **net/ApConnectionManager.kt:45** — `boundNetwork` thread-unsafe → oznaczone `@Volatile`. ZWERYFIKOWANE: pojedyncza referencja, widoczność cross-thread OK.
- [x] 🟠 [important] **net/ApConnectionManager.kt:96** — `requestNetwork` bez timeoutu → dodano overload `requestNetwork(request, cb, timeoutMs)` (default 30 s) + `require(timeoutMs > 0)`. ZWERYFIKOWANE: API 26+, timeout → `onUnavailable` → `Failed` (R7).
- [x] 🟠 [important] **AndroidManifest.xml:26** — cleartext zawężony przez nowy `res/xml/network_security_config.xml` do `192.168.4.1` (base-config deny). ZWERYFIKOWANE: literał IP + `ws://`/`http://` pokryte.

P3 (otwarte, niepriorytetowe):
- [ ] 🟡 [nit] **net/ApConnectionStateReducer.kt:38** — `Unavailable` demuje `Connected → Failed` bezwarunkowo (asymetria względem strzeżonego `Lost`). Kontrakt Androida wyklucza ten scenariusz (P3, nie defensive code); jeśli symetria — z testami oracle-power.
- [ ] 🟡 [nit] **AndroidManifest.xml:20** — `allowBackup="true"` przed persystencją passphrase; ustaw `false`.
- [ ] 🟡 [nit] **net/ApConnectionStateReducer.kt:38 / ApConnectionState.kt:23** — `Failed` bez `reason`; po dodaniu timeoutu rozważ `Failed(reason: enum)` (timeout vs odrzucenie).
- [ ] 🟡 [nit] **net/ApConnectionState.kt:7 / ApConnectionStateReducer.kt:4** — KDoc-linki do typów Androida w warstwie pure (martwy link); zamień na zwykły tekst.
- [ ] 🟡 [nit] **net/ApConnectionStateReducer.kt:1-43** — dwie deklaracje top-level w jednym pliku; opcjonalnie wydziel `ApConnectionEvent.kt`.
- [ ] 🟡 [nit] **net/ApConnectionManager.kt:78-94** — anonimowy `NetworkCallback` podnosi rozmiar `connect()`; przy rozroście wyciągnij `buildApRequest()`.
- [ ] 🟡 [nit] **net/ApConnectionManager.kt:62** — `require(timeoutMs > 0)` bez testu; spójne z nietestowanymi `require(ssid…)` (Pure ⊥ HAL). Opcjonalnie wyekstrahuj `validateTimeout` + host-test.
- [ ] 🟡 [nit] **app/build.gradle.kts** — MapLibre/OkHttp-alpha: potwierdź potrzebę, zaplanuj R8 + ABI splits dla release.
- [ ] 🟡 [nit] **ApConnectionStateReducerTest.kt:19-29** — test o podwójnej odpowiedzialności (słabsza wyrocznia reconnectu); rozbić lub dodać komentarz.

---

## Faza 2 — Kontrakt danych i transport

### Unit 3: Modele danych i (de)serializacja (M)
- [x] Stwórz `data/TelemetryFrame.kt` (pola dokładnie wg `ws_telemetry.h`) — 27 pól 1:1 z `snapshot_to_json`
- [x] Stwórz `data/ApiEnvelope.kt` (`data`, `error{code,message}`) — generyczny `ApiEnvelope<T>` + `ApiError`
- [x] Stwórz `data/Command.kt` (arm/disarm/deploy/stow) + `CommandRequest`
- [x] Stwórz `domain/MotorState.kt` (mapowanie `state` 0–4 + `arm_reason` 0–4)
- [x] Czyste konwersje jednostek (`data/TelemetryUnits.kt`: `lat_e7→deg`, `heading_deg10→deg`, `speed_cms→m/s`)
- [x] Parser z `ignoreUnknownKeys = true` (`data/TelemetryJson.kt`); fixtures JSON w `src/test/resources/`
- [x] Stwórz testy `data/TelemetryFrameParseTest.kt`, `data/ApiEnvelopeParseTest.kt`
- [x] Test: parsowanie pełnej ramki → poprawne pola/konwersje (`gps_lat_e7=520000000→52.0°`, `imu_heading_deg10=900→90.0°`)
- [x] Test: ramka z nieznanym polem nie wywala parsera
- [x] Test: envelope błędu `{data:null,error:{code,message}}` zmapowany poprawnie
- [x] Test: `state` 0–4 → `MotorState`; nieznana wartość → ścieżka błędu (nie crash)
- [ ] Weryfikacja: testy JVM zielone; konwersje zgodne z firmware

### Unit 4: Klient REST + WebSocket (OkHttp) (M)
- [x] Stwórz `net/EspHttpClient.kt` (OkHttp z `socketFactory(network.socketFactory)`, `pingInterval` 7 s)
- [x] Stwórz `net/CommandApi.kt` (POST /api/command; wynik z envelope)
- [x] Stwórz `net/TelemetrySocket.kt` (WS → `Flow<TelemetryFrame>` przez `callbackFlow`)
- [x] Czysta funkcja: (status HTTP + envelope) → `CommandResult` (`net/CommandResult.kt`: `mapCommandResult`)
- [x] Stwórz test `net/CommandResultMapTest.kt`
- [x] Test: 200 + `error:null` → `Success`
- [x] Test: 409 + `SETTINGS_WRITE_REJECTED_NOT_DISARMED` → `Rejected(reason)`
- [x] Test: 400 + `VALIDATION_FAILED` → `Rejected(reason)`
- [ ] Weryfikacja: na ESP32 `arm`/`disarm` zmienia stan; WS dostarcza ramki ~10 Hz

### Unit 5: Repozytorium telemetrii + watchdog link-down (M)
- [x] Stwórz `data/TelemetryRepository.kt` (`StateFlow<TelemetryUiState>`) + `data/TelemetryUiState.kt`
- [x] Stwórz `domain/LinkWatchdog.kt` (czysta: ostatni-czas + teraz → `Live/Stale`)
- [x] Stwórz `domain/ConnectionState.kt` (sealed: Disconnected/Connecting/Live/Stale)
- [x] Reconnect z backoff (250 ms → 500 ms → 1 s → max 2 s) (`domain/ReconnectBackoff.kt`); wstrzymaj gdy AP `Lost` (`pauseReconnect`)
- [x] Różnica czasu w jednej domenie zegara monotonicznego (`SystemClock.elapsedRealtime`)
- [x] Stwórz test `domain/LinkWatchdogTest.kt` (test wokół granicy progu — oracle power) + `domain/ReconnectBackoffTest.kt`
- [x] Test: ramka tuż przed progiem → `Live`; po przekroczeniu progu → `Stale`
- [x] Test: po `Stale` nowa ramka → `Live`
- [x] Test: reconnect backoff rośnie i jest ograniczony do max
- [ ] Weryfikacja: odłączenie ESP32 → link-down < ~1 s; powrót → `Live`

### Do poprawy po review fazy 2

Severity gate (cykl 1): ✅ Wszystkie 6×P2 ROZWIĄZANE (3×KOD + 3×TEST), 8×P3 otwarte (nit).
Kontrakt 1:1 z firmware zweryfikowany (27 pól, stany, error codes, keywords). Pełny raport:
`review-faza-2.md`. Walidacja JVM/build: N/A (brak JDK/Gradle/Android SDK — weryfikacja
statyczna). E2E: N/A (brak UI/web w fazie; brak emulatora/ESP32 — Weryfikacja na sprzęcie).

P2 (rozwiązane w cyklu 1):
- [x] 🟠 [important] **data/TelemetryRepository.kt:9** — cykl warstw `data ⇄ net`; orkiestracja (`TelemetryRepository`/`TelemetryUiState`) wydzielona do nowego pakietu `repository`, w `data` zostały tylko modele DTO → graf acykliczny (`net → data`, `repository → net/data`).
- [x] 🟠 [important] **net/TelemetrySocket.kt:33,43** — dodano `.buffer(1, DROP_OLDEST)` (jawna polityka lossy) + wynik `trySend` sprawdzany i logowany (nie połykany).
- [x] 🟠 [important] **data/TelemetryRepository.kt:131-138** — `onFrame` używa `copy(latestFrame=…)` gdy już Live; dodano rozdzielone strumienie `connection` (`distinctUntilChanged`) vs `latestFrame` → brak 10 emisji/s phase floodu.
- [x] 🟠 [important] **domain/MotorState.kt:45** — `armReasonFromCode` pokryte testem (`MotorStateTest`): happy path 0→READY, 1..4, error 99→null.
- [x] 🟠 [important] **data/Command.kt:22-24** — `CommandRequest(Command)` + serializacja pokryte (`CommandTest`): `cmd=="arm"` i JSON `{"cmd":"arm"}`.
- [x] 🟠 [important] **data/Command.kt:13-18** — zestaw keywords zapięty testem regresji (`CommandTest`) zweryfikowanym wobec `command_parse.c` (arm/disarm/deploy/stow + sprawdzenie kompletu enuma).

P3 (opcjonalne):
- [ ] 🟡 [nit] **data/TelemetryRepository.kt:111** — `.catch {}` połyka throwable bez logu; zaloguj (coding-rules pkt 4)
- [ ] 🟡 [nit] **data/TelemetryRepository.kt:68-73** — watchdog `delay`-loop akumuluje drift + tiknie przy `hasFrame=false`; bramkuj do Live lub udokumentuj
- [ ] 🟡 [nit] **data/TelemetryRepository.kt:97-102** — pauza reconnectu jako poll 250 ms; przejdź na sterowanie sygnałem
- [ ] 🟡 [nit] **domain/MotorState.kt:39-45** — niespójny wzorzec unknown-code (`MotorStateResult` sealed vs `armReasonFromCode` nullable); ujednolić
- [ ] 🟡 [nit] **data/TelemetryRepository.kt:115 vs :127** — `ConnectionState.Stale` w dwóch znaczeniach (cisza vs reconnect); rozdziel lub udokumentuj
- [ ] 🟡 [nit] **data/ApiEnvelopeParseTest.kt:43** — `assertNull(data)` przy fixture `data:null` to tożsamość (słaba wyrocznia na `data`)
- [ ] 🟡 [nit] **domain/ReconnectBackoff.kt:15-16 / LinkWatchdog.kt:29** — `require(...)` guardy bez testu (boundary)
- [ ] 🟡 [nit] **net/CommandApi.kt:49** — surowy `IOException.message` w `TransportError`; ogólny komunikat dla UI + pełny log w debug

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

- Faza 1: ✅ (kod+testy; Weryfikacja na sprzęcie/emulatorze do review)  ·  Faza 2: ✅ (kod+testy; Weryfikacja na ESP32 do review)  ·  Faza 3: ☐  ·  Faza 4: ☐  ·  Faza 5: ☐

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md
- Plan techniczny: docs/plans/2026-06-20-001-feat-android-tablet-app-plan.md
