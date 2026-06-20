# Podsumowanie ukończenia: Aplikacja Android (tablet) — panel operacyjny + mapa offline (v1)

**Zadanie:** android-tablet-app
**Branch:** `feature/android-tablet-app`
**Data ukończenia:** 2026-06-20

## Status końcowy

- **Wszystkie 5 faz (Unit 1–11) zaimplementowane i zreviewowane** (multi-agent review na każdej
  fazie). Severity gate per faza: ✅ CZYSTE (0×P1, 0×P2) po cyklach naprawczych.
- **Weryfikacja statyczna:** 45 plików Kotlin (`src/main`), 18 plików testowych (`src/test`),
  82 `@Test`. Logika domenowa wyekstrahowana do czystych funkcji testowanych na JVM (Pure ⊥ HAL).
- **Walidacja build/test JVM: N/A** — środowisko nie ma toolchainu Android (JDK/Gradle/SDK;
  `java -version` → „Unable to locate a Java Runtime", brak `gradle`/`ANDROID_HOME`).
  `./gradlew assembleDebug`/`test` nie uruchomione. To ograniczenie środowiska zależne od
  sprzętu/SDK, NIE braki implementacji ani osłabione asercje.
- **Czynności `Weryfikacja:` (sprzęt/emulator/ESP32)** świadomie odroczone do testów na
  urządzeniu po zakupie tabletu i wgraniu firmware. P3 (nity) świadomie odroczone.

## Co zostało dostarczone

Natywna aplikacja Android (tablet) w `android/`, replikująca operacyjne funkcje web panelu ESP32
i dodająca mapę offline z pozycją łodzi. Manualna jazda zostaje na nadajniku RC — aplikacja nie
steruje gazem/skrętem.

- **Faza 1 (Unit 1–2):** bootstrap projektu Compose (`minSdk 29`, target/compile 34, AGP 8.5.2,
  Kotlin 2.0.20, version catalog `libs.versions.toml`); adapter łączności z SoftAP ESP32 —
  `requestNetwork(WifiNetworkSpecifier)` bez `NET_CAPABILITY_INTERNET` + `bindProcessToNetwork`,
  czysty reducer `reduceApConnectionState`, `network_security_config` zawężony do `192.168.4.1`.
- **Faza 2 (Unit 3–5):** kontrakt danych 1:1 z firmware (`TelemetryFrame` 27 pól wg
  `snapshot_to_json`, `ApiEnvelope<T>`, `Command`), czyste konwersje jednostek; klient OkHttp 5.x
  (REST `/api/command` + WS `/ws` przez `callbackFlow` z `.buffer(1, DROP_OLDEST)`); repozytorium
  telemetrii (`StateFlow`) + watchdog link-down (`LinkWatchdog`, jedna domena zegara monotonicznego)
  + reconnect z backoffem.
- **Faza 3 (Unit 6–7):** ekran telemetrii Compose (StatusBadge/GpsCard/CompassCard/SafetyBanner),
  czysta `safetyIndicators` (`linkDown` zależy WYŁĄCZNIE od `ConnectionState`), komendy
  arm/disarm/deploy/stow przez seam `CommandSender` (deploy/stow za dialogiem, veto przy link down,
  guard in-flight); pochodne StateFlow eliminują recomposition flood przy 10 Hz.
- **Faza 4 (Unit 8/9/11):** integracja MapLibre Native (`AndroidView` + lifecycle bridge,
  `MapController`), style offline (`pmtiles://` wektor OSM + `mbtiles://` raster ortofoto,
  atrybucja ODbL), marker łodzi (`GeoJsonSource` + `SymbolLayer`, `iconRotate(get("heading"))`,
  obsługa braku fixa i `imu_ok=false`), pipeline kafli offline (`android/maps/README.md`:
  Planetiler + Geoportal ORTO WMTS, rate-limit obowiązkowy).
- **Faza 5 (Unit 10):** keep-screen-on (`FLAG_KEEP_SCREEN_ON`) + foreground service WS
  (`connectedDevice`, `START_STICKY`), czysta `SessionPolicy` (keep-screen-on / wake-lock matrix /
  status notyfikacji); serwis wpięty w lifecycle Activity (bind/unbind sparowane, `isBound` guard,
  teardown-once na `isFinishing`, wake lock zwalniany w `finally`, brak wycieków ServiceConnection).

## Podjęte kluczowe decyzje

| Decyzja | Wybór |
|---|---|
| Stack | Kotlin + Jetpack Compose + ViewModel/StateFlow; stan jako sealed class |
| Architektura | `net → data → domain → ui/map`; logika domenowa = czyste funkcje testowane na JVM (Pure ⊥ HAL), adaptery cienkie |
| Mapa | MapLibre Native View-based + `AndroidView` (nie maplibre-compose, nie osmdroid); bump 11.5.2 → 11.7.0 dla natywnego `pmtiles://` |
| Offline | lokalne archiwa PMTiles (wektor OSM) + MBTiles (raster ortofoto) + lokalny style JSON; MBTiles ekstrahowany do internal storage |
| Pozycja łodzi | własny `GeoJsonSource` + `SymbolLayer` (nie LocationComponent); brak fixa → Hidden, `imu_ok=false` → heading null (nie 0°) |
| Sieć | `requestNetwork(WifiNetworkSpecifier)` bez `NET_CAPABILITY_INTERNET` + `bindProcessToNetwork` + OkHttp `socketFactory` |
| Link-down | app-level watchdog (~500 ms bez ramki = STALE) w jednej domenie zegara monotonicznego (`SystemClock.elapsedRealtime`) |
| Sesja | `FLAG_KEEP_SCREEN_ON` na oknie (nie w service); FGS `connectedDevice`; wake lock TYLKO `ACTIVE && !screenOn` |
| Komendy | seam `CommandSender` (`fun interface`) = jedyne zewnętrzne API do fake'owania w testach (mock TYLKO zewnętrznego API) |

## Główne utworzone pliki (`android/`)

- `net/` — `ApConnectionManager.kt`, `ApConnectionState.kt`, `ApConnectionStateReducer.kt`,
  `EspHttpClient.kt`, `CommandApi.kt`, `CommandResult.kt`, `TelemetrySocket.kt`
- `data/` — `TelemetryFrame.kt`, `TelemetryUnits.kt`, `ApiEnvelope.kt`, `Command.kt`,
  `TelemetryJson.kt`
- `domain/` — `MotorState.kt`, `LinkWatchdog.kt`, `ConnectionState.kt`, `ReconnectBackoff.kt`,
  `SafetyIndicators.kt`, `CommandAvailability.kt`
- `repository/` — `TelemetryRepository.kt`, `TelemetryUiState.kt` (orkiestracja wydzielona z `data`)
- `ui/` — `TelemetryViewModel.kt`, `TelemetryScreen.kt`, `CommandFeedback.kt`, `components/`
  (StatusBadge, GpsCard, CompassCard, SafetyBanner, CommandBar, WebPanelLink), `theme/`
- `map/` — `MapLibreView.kt`, `MapController.kt`, `OfflineStyle.kt`, `LayerToggle.kt`,
  `BoatMarker.kt`, `BoatMarkerProjection.kt`, `MapAssets.kt`
- `session/` — `SessionPolicy.kt`, `SessionState.kt`, `TelemetryService.kt`
- `MainActivity.kt` (manualne DI + lifecycle wiring serwisu)
- `app/src/main/AndroidManifest.xml`, `res/xml/network_security_config.xml`,
  `app/src/main/assets/style/`, `gradle/libs.versions.toml`
- `android/maps/README.md` (pipeline kafli offline), `android/README.md`
- `app/src/test/` — 18 plików testowych JVM (82 `@Test`), fixtures w `src/test/resources/`

## Wyciągnięte wnioski

1. **Pure ⊥ HAL na Androidzie umożliwia testy bez emulatora** — każda nietrywialna decyzja
   (parsing telemetrii, konwersje jednostek, `LinkWatchdog`, reducer stanu połączenia,
   `mapCommandResult`, `safetyIndicators`, `commandAvailability`, `projectBoatPosition`,
   `buildOfflineStyleJson`, `SessionPolicy`) wyekstrahowana do czystej funkcji testowanej na JVM;
   adaptery Androida (sieć/mapa/service) cienkie, weryfikowane manualnie na urządzeniu.
2. **`linkDown` musi zależeć WYŁĄCZNIE od stanu połączenia, nie od treści ramki** — zamrożona
   „zdrowa" ramka przy martwym linku nie może wyglądać bezpiecznie. Test wyroczni: zdrowa ramka +
   `Stale` → `linkDown=true`.
3. **Dwa niezależne „unknown" markera mapują się na dwa różne zachowania** — brak fixa →
   marker ukryty (`Hidden`); fix + `imu_ok=false` → heading `null` (brak rotacji, NIE 0° które
   fałszywie znaczyłoby „na północ").
4. **Wake lock tylko gdy `ACTIVE && !screenOn`** — przy ekranie on CPU już czuwa, więc wake lock
   to zbędny drenaż baterii; re-acquire co 3 h przed 4 h timeoutem (release→acquire bez gubienia).
5. **Foreground service: integracja w lifecycle jest częścią scope** — sama implementacja serwisu
   bez wpięcia start/bind/teardown w Activity to dead code. Bind/unbind sparowane przez guard
   `isBound`; teardown dokładnie raz na `isFinishing` (nie config change); wake lock w `finally`.
6. **`pmtiles://` natywnie dopiero od MapLibre Android 11.7.0** — bump pinu (nie nowa zależność)
   wymagany dla wektora offline; odnotowany wprost (coding-rules pkt 8).
7. **Cross-phase debt jawnie udokumentowany, nie ukryty** — teardown AP jest forward-compatible,
   ale `ApConnectionManager.connect()` nie jest jeszcze wołany z UI (wymaga flow wyboru SSID/
   passphrase). Pozostaje otwarte jako dług Fazy 1 / Unit 2 (`zadania.md:33,296`), nie regresja.

## Znane otwarte punkty (do dalszych prac)

- **Dług Unit 2 / Faza 1:** wpięcie `ApConnectionManager.connect(ssid, passphrase)` z UI
  + przekazanie `boundNetwork`/`socketFactory` do `EspHttpClient.build(...)` (do tego czasu
  teardown AP w Unit 10 jest inertny). Weryfikacja „po wskazaniu SSID → Connected" otwarta.
- **Weryfikacja na sprzęcie:** wszystkie kryteria `Weryfikacja:` (emulator/tablet/ESP32) — do
  wykonania po zakupie tabletu i finalnym dostrojeniu (minSdk, wersja MapLibre OpenGL/Vulkan, progi).
- **P3 (nity)** świadomie odroczone — spis w `android-tablet-app-zadania.md` (sekcje „Do poprawy").
- **Poza zakresem v1 (v2):** Spot Lock / Go To Point (R5/R6, wymaga firmware regulatora pozycji),
  wirtualny joystick, edytor parametrów + kreator kalibracji ESC, mapy żeglarskie/batymetria, iOS.

## Powiązane dokumenty

- Plan: `android-tablet-app-plan.md`
- Kontekst: `android-tablet-app-kontekst.md`
- Zadania (pełna lista z findingami review): `android-tablet-app-zadania.md`
- Raporty review faz 1–5: `review-faza-{1..5}.md`
- Requirements: `docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md`
- Plan techniczny: `docs/plans/2026-06-20-001-feat-android-tablet-app-plan.md`
