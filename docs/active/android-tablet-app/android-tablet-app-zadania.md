# Zadania: Aplikacja Android (tablet) — v1

Branch: `feature/android-tablet-app`
Ostatnia aktualizacja: 2026-06-20 (faza 4)

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

Severity gate (re-review, cykl 1): ✅ GOTOWE DO KONTYNUACJI (0×P1, 0×P2, 8×P3).
Wszystkie 6×P2 (3×KOD + 3×TEST) ROZWIĄZANE i ZWERYFIKOWANE bez regresji (commit `a98a661`).
Graf importów acykliczny po przeniesieniu orkiestracji do pakietu `repository`
(`data` nie importuje `net`; `net → data`; `repository → net/data/domain`); zero martwych
referencji do starego pakietu. Kontrakt 1:1 z firmware re-zweryfikowany (keywords
`command_parse.c`, arm_reason 0..4 `state_machine.h`). Pełny raport: `review-faza-2.md`.
Walidacja JVM/build: N/A (brak JDK/Gradle/Android SDK — weryfikacja statyczna). E2E: N/A
(brak UI/web w fazie; brak emulatora/ESP32 — Weryfikacja na sprzęcie).

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
- [x] Stwórz `ui/TelemetryViewModel.kt` (zbiera StateFlow z repo)
- [x] Stwórz `ui/TelemetryScreen.kt` + `ui/components/` (StatusBadge, GpsCard, CompassCard, SafetyBanner)
- [x] Stwórz `domain/SafetyIndicators.kt` (czysta: (ConnectionState, frame) → wskaźniki)
- [x] Baner: FAILSAFE / LINK DOWN / ARMED-DISARMED / UNCALIBRATED
- [x] Skrót/odnośnik do web panelu (`http://192.168.4.1`) — R8
- [x] Stwórz test `domain/SafetyIndicatorsTest.kt`
- [x] Test: `state=FAILSAFE` → wskaźnik failsafe aktywny
- [x] Test: `ConnectionState.Stale` → linkDown aktywny niezależnie od ostatniej ramki
- [x] Test: `calibrated=false` → wskaźnik UNCALIBRATED
- [ ] Weryfikacja: telemetria odświeża się płynnie; failsafe/odłączenie → właściwe banery

### Unit 7: Komendy operacyjne (arm/disarm/deploy/stow) (S/M)
- [x] Modyfikuj `ui/TelemetryViewModel.kt` (akcje komend)
- [x] Stwórz `ui/components/CommandBar.kt` (przyciski + potwierdzenie deploy/stow)
- [x] Wyłącz przyciski niedostępne w danym stanie (np. arm gdy link down)
- [x] Stwórz test `ui/CommandActionTest.kt` (mock TYLKO zewnętrznego API)
- [x] Test: sukces komendy → komunikat sukcesu, brak błędu
- [x] Test: `Rejected(NOT_DISARMED)` → komunikat o odrzuceniu, brak crasha
- [ ] Weryfikacja: na urządzeniu arm/disarm/deploy/stow działają i raportują odrzucenia

### Do poprawy po review fazy 3

Severity gate (cykl 0): ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI (0×P1, 8×P2, 7×P3).
Brak blokerów. Wymóg z briefu spełniony: `SafetyIndicators.linkDown` zależy WYŁĄCZNIE od
`ConnectionState` (mocny test wyroczni); komendy veto przy link down; deploy/stow za
dialogiem; mock TYLKO `CommandSender`. Pełny raport: `review-faza-3.md`. Build/testy JVM/
Compose preview/E2E: N/A (brak JDK/Gradle/Android SDK/emulatora/przeglądarki).

Severity gate (cykl 1): wszystkie 8×P2 (4×KOD + 4×TEST) ROZWIĄZANE. Weryfikacja
statyczna (brak JDK/Gradle/Android SDK — testów JVM/buildu nie uruchomiono).

P2 — KOD (rozwiązane w cyklu 1):
- [x] 🟠 [important] **MainActivity.kt:33** — ViewModel przez `by viewModels { TelemetryViewModelFactory() }`; lifecycle ViewModeli przeżywa configuration change. Wiring zależności w `TelemetryViewModelFactory`; repozytorium startowane w `init` ViewModelu, stop w `onCleared`.
- [x] 🟠 [important] **TelemetryViewModel.kt** — `sendCommand` re-waliduje availability ze świeżego `source.state.value` (`commandAvailability(...).isEnabled(command)`); jeśli niedostępne → publikuje `Rejected`, nie wysyła. Defense-in-depth dla okna dialog→klik (link spada Live→Stale).
- [x] 🟠 [important] **CommandBar.kt / TelemetryViewModel.kt** — guard in-flight: `isSending` StateFlow; `sendCommand` no-op gdy in-flight, `finally` zwalnia; bar dezaktywowany (`enabled && !isSending`). Brak równoległych POST.
- [x] 🟠 [important] **TelemetryScreen.kt** — rozbicie na pochodne StateFlow (`connection`/`indicators`/`availability`/`latestFrame`) z `distinctUntilChanged().stateIn`; ekran rozbity na sekcje czytające osobne slice'y → 10×/s rekomponują tylko karty GPS/Compass.

P2 — TEST (rozwiązane w cyklu 1):
- [x] 🟠 [important] **CommandAvailabilityTest.kt** — dodane testy: ESC_CALIBRATION → wszystkie komendy off, nieznany kod (99) → off, Connecting → off (każdy z wyrocznią vs zdrowa DISARMED ramka).
- [x] 🟠 [important] **SafetyIndicatorsTest.kt** — dodany test nieznanego kodu (99, Live) → failsafe=false, armed=false, linkDown=false.
- [x] 🟠 [important] **SafetyIndicators/CommandAvailability** — dodany wariant `ConnectionState.Connecting` w obu testach (linkDown aktywny / veto wszystkich komend ze zdrową ramką).
- [x] 🟠 [important] **CommandActionTest.kt** — dodane testy successMessage dla DEPLOY ("Deployed — motor raised"), DISARM ("Disarmed"), STOW ("Stowed").

P3 (opcjonalne):
- [ ] 🟡 [nit] **CommandBar.kt:91** — `Color` fully-qualified inline zamiast importu.
- [ ] 🟡 [nit] **CommandFeedback.kt** — pure/host-testowana w pakiecie `ui`; spójniej w `domain`.
- [ ] 🟡 [nit] **WebPanelLink.kt:14 / CommandApi.kt:37** — `"http://192.168.4.1"` zduplikowany; wspólna stała `EspAp.BASE_URL`.
- [ ] 🟡 [nit] **GpsCard.kt:33,37 / CompassCard.kt:29** — `String.format` per-rekompozycja na hot-path 10 Hz; `remember(frame)`.
- [ ] 🟡 [nit] **SafetyBanner.kt:37,62-70** — `activeBanners` alokuje listę w ciele composable; `remember(indicators)`.
- [ ] 🟡 [nit] **TelemetryScreen.kt:46-50** — `_feedback` może zgubić komunikat przy serii komend; `Channel`/`SharedFlow(replay=0)`.
- [ ] 🟡 [nit] **CommandFeedback.kt:33 → TelemetryScreen.kt:103** — surowy `message` z firmware bez limitu długości w snackbarze; `take(120)`.

---

## Faza 4 — Mapa offline

### Unit 8: Integracja MapLibre + warstwy offline (L)
- [x] Stwórz `map/MapLibreView.kt` (`AndroidView` + lifecycle bridge: DisposableEffect + LifecycleEventObserver, przekazanie onStart/onResume/onPause/onStop/onDestroy/onLowMemory) — lifecycle przez `MapController`; onLowMemory przez `ComponentCallbacks2` (Lifecycle.Event nie ma low-memory)
- [x] Stwórz `map/OfflineStyle.kt` (style JSON: `pmtiles://`/`mbtiles://`, `asset://` sprite/glyphs) — czysty `buildOfflineStyleJson` + `MapIds`
- [x] Stwórz `map/LayerToggle.kt` (visibility rastra; kolejność `addLayerBelow/Above`) — `rasterVisibility` (czysta) + `applyRasterVisible`; kolejność warstw (raster nad wektorem) w builderze stylu
- [x] Stwórz `app/src/main/assets/style/` (style.json, sprite, glyphs) — style.json referencyjny + README; sprite/glyphs jako generowane binaria (placeholdery + opis w Unit 11)
- [x] Atrybucja „© OpenStreetMap" widoczna na mapie — `OSM_ATTRIBUTION` wpięta w źródło wektorowe stylu
- [x] Stwórz test `map/OfflineStyleBuilderTest.kt`
- [x] Test: builder stylu produkuje źródła z poprawnymi URI i kolejnością warstw
- [x] Test: toggle ustawia `visibility` rastra na VISIBLE/NONE — `rasterVisibility` oba branche (oracle)
- [ ] Weryfikacja: (bez internetu) mapa renderuje wektor OSM; toggle ortofoto; brak czarnej mapy po powrocie z tła

### Unit 9: Marker pozycji łodzi + heading (M)
- [x] Stwórz `map/BoatMarker.kt` (GeoJsonSource + SymbolLayer, `iconRotate(get("heading"))`)
- [x] Stwórz `map/BoatMarkerProjection.kt` (czysta: frame → (lon,lat,heading); obsługa braku fixa) — `BoatPosition` (Hidden / Positioned z headingDeg nullable)
- [x] Modyfikuj `map/MapLibreView.kt` (podpięcie do StateFlow; guard na nieaktywny styl) — `latestFrame` param + `MapController.onState`; guard `style == null` przed update markera/rastra
- [x] Update tylko `setGeoJson` (async) na wątku UI; opcjonalny tryb „follow" — `BoatMarker.update`; `followBoat` → `moveCamera`
- [x] Stwórz test `map/BoatMarkerProjectionTest.kt`
- [x] Test: ramka z fixem → poprawne (lon,lat,heading)
- [x] Test: `gps_fix=false` → projekcja sygnalizuje brak pozycji (marker ukryty)
- [x] Test: `imu_ok=false` → heading nieznany (brak rotacji), nie 0°
- [ ] Weryfikacja: na ESP32 marker rusza się wg telemetrii, strzałka wg kompasu, bez lagów przy 10 Hz

### Unit 11: Pipeline i dokumentacja map offline (M)
- [x] Stwórz `android/maps/README.md` (Planetiler OSM→PMTiles dla bbox; Geoportal ORTO WMTS→MBTiles przez GDAL/rio-mbtiles; atrybucja ODbL; asset vs internal storage)
- [x] Stwórz `android/maps/` (artefakty/skrypty; duże pliki poza git) — katalog z README; `*.pmtiles`/`*.mbtiles` w `.gitignore` (faza 1)
- [x] Rate-limit przy pobieraniu WMTS; tylko bbox akwenu — sekcja „Rate limiting (mandatory)" + bbox w obu pipeline'ach
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

- Faza 1: ✅ (kod+testy; Weryfikacja na sprzęcie/emulatorze do review)  ·  Faza 2: ✅ (kod+testy; Weryfikacja na ESP32 do review)  ·  Faza 3: ✅ (kod+testy; Weryfikacja na ESP32/emulatorze do review)  ·  Faza 4: ✅ (kod+testy; Weryfikacja na sprzęcie/emulatorze do review)  ·  Faza 5: ☐

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md
- Plan techniczny: docs/plans/2026-06-20-001-feat-android-tablet-app-plan.md
