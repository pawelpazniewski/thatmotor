---
title: "feat: Aplikacja Android (tablet) — panel operacyjny + mapa offline (v1)"
type: feat
status: active
date: 2026-06-20
origin: docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md
---

# feat: Aplikacja Android (tablet) — panel operacyjny + mapa offline (v1)

## Przegląd

Nowa natywna aplikacja Android (tablet) w podkatalogu `android/` repozytorium
`that_motor`. v1 replikuje **operacyjne** funkcje web panelu ESP32 (telemetria,
arm/disarm/deploy/stow) i dodaje **mapę offline z pozycją łodzi** (OSM wektor +
ortofoto). Autonomiczne pozycjonowanie (Spot Lock / Go To Point) jest świadomie
poza v1 — wymaga osobnego firmware regulatora pozycji na ESP32 i wchodzi do v2.

Manualna jazda zostaje na fizycznym nadajniku RC; aplikacja nie steruje gazem/skrętem.

## Ujęcie problemu

Sterowanie opiera się dziś o nadajnik RC (jazda) + web panel na ESP32 (arm/deploy,
telemetria, konfiguracja). Brakuje mapy pokazującej, gdzie łódź jest w terenie, mimo
że ESP32 ma GPS (NEO-M9N) i kompas (BNO085) i wystawia je w telemetrii. Tablet jako
duży, czytelny ekran z mapą offline i panelem operacyjnym wypełnia tę lukę i jest
fundamentem pod przyszłą autonomię (v2). (zob. źródło:
docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md)

## Śledzenie wymagań

Realizowane w v1:
- **R1.** Natywna aplikacja Android na tablet, łączy się z WiFi SoftAP ESP32
  (`192.168.4.1`, WPA2-PSK), działa bez internetu na wodzie.
- **R2.** Telemetria live z `WS /ws` (~10 Hz): stan, `rc_valid`, wyjścia servo/ESC,
  GPS (fix/sats/lat/lon/speed), kompas (heading/calib), flagi kalibracji/NVS.
- **R3.** Komendy operacyjne `arm`/`disarm`/`deploy`/`stow` przez `POST /api/command`
  z obsługą envelope `{data, error}` i odrzuceń (np. 409).
- **R4.** Mapa offline: OSM wektor (baza) + ortofoto/satelita (warstwa), pre-cache,
  pozycja łodzi z telemetrii, kursor orientacji z kompasu.
- **R7.** Wskaźniki bezpieczeństwa: FAILSAFE, stan uzbrojenia, utrata linku (brak
  świeżej telemetrii > próg). Aplikacja nie blokuje toru RC.
- **R8.** Konfiguracja (23 parametry, kalibracja ESC, trim) pozostaje na web panelu;
  aplikacja oferuje skrót do panelu.

Poza v1 (patrz Granice scope'u):
- **R5.** Spot Lock — v2 (wymaga firmware regulatora pozycji).
- **R6.** Go To Point — v2 (wymaga firmware regulatora pozycji).

## Granice scope'u

- **Brak wirtualnego joysticka / manualnej jazdy z tabletu** — gaz/skręt na RC.
- **Brak edytora 23 parametrów i kreatora kalibracji ESC** — zostają na web panelu.
- **Brak Spot Lock / Go To Point w v1** — v2, po firmwarze regulatora pozycji.
  W v1 mapa obsługuje tap→lat/lon w sposób forward-compatible, ale **nie** wysyła
  komendy celu (brak kontraktu po stronie ESP32).
- **Brak map żeglarskich / batymetrii.**
- **Brak iOS.**
- **Tablet bez własnego GPS** — pozycja wyłącznie z telemetrii łodzi.

## Kontekst i research

### Relevantny kod i wzorce

- **Kontrakt API ESP32 (źródło prawdy dla modeli danych):**
  - `WS /ws` — telemetria push ~10 Hz (`components/web_panel/src/ws_telemetry.c`,
    `components/web_panel/include/ws_telemetry.h`, `WS_TELEMETRY_PERIOD_MS = 100`).
    Pola: `state`, `arm_reason`, `rc_valid`, `ch1_us/ch2_us/ch3_us/ch4_us`,
    `ch1_period_us/ch2_period_us`, `ch1_valid/ch2_valid`, `servo_us`, `esc_us`,
    `servo_trim_us`, `source`, `settings_valid`, `calibrated`, `defaults_used`,
    `nvs_error`, `gps_fix`, `gps_sats`, `gps_lat_e7`, `gps_lon_e7`, `gps_speed_cms`,
    `imu_ok`, `imu_heading_deg10`, `imu_calib`.
  - `POST /api/command {"cmd": "..."}` — `command_parse.{c,h}` keywords:
    `arm`, `disarm`, `deploy`, `stow` (+ `calib_*`, `trim_*` — poza v1).
  - Envelope `{ data, error: { code, message } }` — `api_contract.{c,h}`.
  - WiFi SoftAP WPA2-PSK, IP `192.168.4.1` — `components/web_panel/src/wifi_ap.c`.
- **Stany maszyny:** `0=DISARMED, 1=ARMED, 2=FAILSAFE, 3=ESC_CALIBRATION, 4=DEPLOY`.
- **Web panel referencyjny (zachowania UI do odwzorowania):** `web/app.js`,
  `web/index.html` — sposób subskrypcji WS, prezentacja telemetrii, wysyłka komend.

### Wiedza instytucjonalna

- `.claude/rules/learned-patterns.md` — **Pure ⊥ HAL**: wyciągaj czyste funkcje
  decyzyjne zza adaptera i testuj je na hoście (tu: JVM), adapter trzymaj cienki.
  Plan stosuje to: logika (parsing telemetrii, watchdog link-down, redukcja stanu
  połączenia, mapowanie envelope→wynik) = czyste funkcje z testami JVM; MapLibre,
  OkHttp i ConnectivityManager = cienkie adaptery.
- `.claude/rules/coding-rules.md` — pkt 13 (async/race): cleanup `NetworkCallback`,
  wake locków, listenerów; stan ładowania jako discriminated union (sealed class),
  nie zlepek booleanów. Pkt 1: pliki < 300 linii, funkcje < 50.

### Referencje zewnętrzne

- **Łączenie z AP bez internetu:** `ConnectivityManager.requestNetwork()` z
  `WifiNetworkSpecifier` **bez** `NET_CAPABILITY_INTERNET`; w `onAvailable`
  `bindProcessToNetwork(network)` + OkHttp `socketFactory(network.socketFactory)`;
  cleanup w `onLost`/zamknięciu. Permissions: `INTERNET`, `CHANGE_NETWORK_STATE`,
  `ACCESS_NETWORK_STATE`, `ACCESS_WIFI_STATE`, (`ACCESS_FINE_LOCATION` dla API ≤32).
  [WifiNetworkSpecifier](https://developer.android.com/reference/android/net/wifi/WifiNetworkSpecifier),
  [OkHttp #3736](https://github.com/square/okhttp/issues/3736).
- **Mapa:** MapLibre Native Android `org.maplibre.gl:android-sdk` (linia 11.x–13.x —
  przypiąć najnowszą stabilną z Maven Central na starcie), View-based + `AndroidView`
  z ręcznym lifecycle bridge. Marker łodzi = własny `GeoJsonSource` + `SymbolLayer`
  z `iconRotate(get("heading"))`, update async `setGeoJson` (NIE synchronous),
  na wątku UI. Raster ortofoto = `RasterSource(mbtiles://)` + `RasterLayer`, toggle
  przez `visibility`. Tap → `addOnMapClickListener` (zwraca `LatLng`).
  [MapLibre Android](https://maplibre.org/maplibre-native/android/examples/getting-started/),
  [GeoJSON guide](https://maplibre.org/maplibre-native/android/examples/geojson-guide/).
- **Kafle (legalne offline):** OSM wektor → **PMTiles** generowane Planetilerem
  (ODbL, atrybucja © OpenStreetMap); ortofoto → **Geoportal GUGiK ORTO WMTS**
  (`https://mapy.geoportal.gov.pl/wss/service/PZGIK/ORTO/WMTS/StandardResolution`)
  pre-cache do **MBTiles raster** (darmowe, dowolny cel). **NIE** Mapbox/Google
  (ToS zabrania cache offline).
  [Protomaps/ODbL](https://github.com/protomaps/basemaps/blob/main/LICENSE_DATA.md),
  [Geoportal ORTO](https://www.geoportal.gov.pl/en/data/orthophotomap-orto/).
- **Sieć app:** OkHttp 5.x + WebSocket; kotlinx.serialization; WS przez `callbackFlow`
  → `StateFlow`; `pingInterval` + app-level watchdog (brak ramki > próg = link down);
  reconnect z backoff (krótki, LAN).
- **UI:** Jetpack Compose (BOM 2026.04.x), ViewModel + StateFlow +
  `collectAsStateWithLifecycle`.
- **Sesja:** `FLAG_KEEP_SCREEN_ON` na oknie (nie w service) + foreground service typu
  `connectedDevice` (Android 14+ wymaga `foregroundServiceType`) dla utrzymania WS.

## Kluczowe decyzje techniczne

- **Język/stack:** Kotlin + Jetpack Compose + ViewModel/StateFlow. Uzasadnienie:
  domyślny nowoczesny stack Androida, type-safe, sealed class na stan (zgodne z
  coding-rules pkt 13). (zob. źródło: odroczone pytanie o stack)
- **Mapa: MapLibre Native View-based + `AndroidView`**, nie `maplibre-compose` (v0.13,
  ~90% feature-complete — ryzyko brakujących funkcji) i nie osmdroid (rastrowy, brak
  wektora/rotacji). Uzasadnienie: pełna kontrola nad source/layer przy 10 Hz, dojrzałość.
- **Offline: gotowe lokalne archiwa (PMTiles wektor + MBTiles raster) + lokalny style
  JSON**, nie `OfflineManager`/Offline Region API. Uzasadnienie: aplikacja jest
  offline od startu; PMTiles nie wspiera offline-pack-download, co przy modelu pliku
  lokalnego nas nie dotyczy.
- **Pozycja łodzi: własny `GeoJsonSource` + `SymbolLayer`, nie `LocationComponent`.**
  Uzasadnienie: pozycja i heading pochodzą z zewnętrznej telemetrii, nie z GPS tabletu.
- **Sieć: `bindProcessToNetwork` + OkHttp `socketFactory`.** Uzasadnienie: bez tego
  Android odsyła ruch na sieć z internetem i nie dogada się z AP ESP32.
- **Link-down: app-level watchdog (brak ramki telemetrii > ~500 ms = STALE)** obok
  ping/pong OkHttp. Uzasadnienie: szybsze i bardziej miarodajne wykrycie utraty łodzi
  niż sam ping socketu; spójne z domeną failsafe firmware'u (failsafe_timeout 200 ms).
- **Lokalizacja projektu: `android/` w repo `that_motor`** (monorepo), Gradle projekt
  niezależny od ESP-IDF. (zob. źródło: odroczone pytanie o lokalizację)
- **Architektura warstw:** `net` (adapter sieci/AP) → `data` (modele + parsing +
  repo telemetrii/komend) → `domain` (czyste funkcje decyzyjne) → `ui` (Compose +
  ViewModel) → `map` (adapter MapLibre). Czyste funkcje bez importów Androida =
  testowalne na JVM.

## Otwarte pytania

### Rozwiązane podczas planowania

- Stack Androida → Kotlin + Compose + MapLibre (decyzja powyżej).
- Biblioteka map → MapLibre Native View-based.
- Format/źródło map offline → PMTiles (Planetiler/OSM) + MBTiles raster (Geoportal ORTO).
- Próg link-down → ~500 ms bez ramki telemetrii (do dostrojenia na sprzęcie).
- Lokalizacja projektu → `android/` w tym repo.

### Odroczone do implementacji

- **Dokładna wersja `org.maplibre.gl:android-sdk`** (11.x vs 13.x) i ewentualny wariant
  `android-sdk-vulkan` — zweryfikować na Maven Central i pod GPU docelowego tabletu
  przy starcie (tablet jeszcze nie kupiony).
- **`minSdk`** — docelowo Android 13–15; ustawić po wyborze tabletu (kandydat: 29–33;
  29 upraszcza zasięg, 33 zdejmuje wymóg FINE_LOCATION dla WiFi). Zdecydować w Unit 1.
- **Dostrojenie progów** watchdog/ping/backoff — wartości startowe w planie, kalibracja
  na realnym linku WiFi.
- **Bitmapa/ikona łodzi i strzałki headingu** — zasób graficzny, nieblokujący.
- **Kontrakt komendy celu (lat/lon)** dla Spot Lock/Go To Point — projektowany w v2
  razem z firmwarem regulatora pozycji.

## Implementation Units

> Postawa wykonawcza ogólna: logikę domenową (parsing, watchdog, redukcja stanu,
> mapowanie envelope) implementuj jako czyste funkcje z testami JVM **przed**/obok
> cienkich adapterów (Pure ⊥ HAL). Adaptery Androida (sieć, mapa, service) weryfikuj
> manualnie na urządzeniu/emulatorze.

### Faza 1 — Fundament i łączność

- [ ] **Unit 1: Bootstrap projektu Android**

**Cel:** Utworzyć pusty, budowalny projekt Compose w `android/` z manifestem,
uprawnieniami i strukturą modułów/pakietów.

**Wymagania:** R1

**Zależności:** Brak

**Pliki:**
- Stwórz: `android/settings.gradle.kts`, `android/build.gradle.kts`,
  `android/gradle.properties`, `android/app/build.gradle.kts`
- Stwórz: `android/app/src/main/AndroidManifest.xml`
- Stwórz: `android/app/src/main/java/<pkg>/MainActivity.kt`
- Stwórz: `android/app/src/main/java/<pkg>/ui/theme/` (Compose theme)
- Stwórz: `android/.gitignore` (build/, .gradle/, local.properties)
- Stwórz: `android/README.md` (build, wgranie map, uruchomienie)

**Podejście:**
- Pakiety wg warstw: `net`, `data`, `domain`, `ui`, `map`.
- Manifest: `INTERNET`, `CHANGE_NETWORK_STATE`, `ACCESS_NETWORK_STATE`,
  `ACCESS_WIFI_STATE`, `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_CONNECTED_DEVICE`,
  (`ACCESS_FINE_LOCATION` jeśli minSdk ≤ 32).
- Ustal `minSdk`/`targetSdk` (rozwiąż odroczone pytanie minSdk tutaj).
- Zależności: Compose BOM, MapLibre android-sdk, OkHttp 5.x, kotlinx.serialization,
  lifecycle-runtime-compose. Wersje pinowane (coding-rules pkt 8).

**Wzorce do naśladowania:** standardowy szablon Android Studio (Empty Compose Activity).

**Scenariusze testowe:**
- [Unit] Brak (bootstrap) — weryfikacja przez build.

**Weryfikacja:**
- `./gradlew :app:assembleDebug` przechodzi; aplikacja startuje na emulatorze i pokazuje
  pusty ekran z motywem.

- [ ] **Unit 2: Adapter łączności z AP ESP32**

**Cel:** Połączyć tablet z WiFi SoftAP ESP32 i zbindować ruch procesu do tej sieci,
tak by HTTP/WS do `192.168.4.1` działały bez internetu.

**Wymagania:** R1, R7 (wykrycie braku/utraty sieci)

**Zależności:** Unit 1

**Pliki:**
- Stwórz: `android/app/src/main/java/<pkg>/net/ApConnectionManager.kt`
- Stwórz: `android/app/src/main/java/<pkg>/net/ApConnectionState.kt` (sealed class:
  `Idle`, `Connecting`, `Connected(network)`, `Lost`, `Failed(reason)`)
- Test (unit): `android/app/src/test/java/<pkg>/net/ApConnectionStateReducerTest.kt`

**Podejście:**
- `requestNetwork(NetworkRequest + WifiNetworkSpecifier)` **bez**
  `NET_CAPABILITY_INTERNET`; `onAvailable` → `bindProcessToNetwork(network)` i
  emisja `Connected`; `onLost`/`onUnavailable` → `bindProcessToNetwork(null)` +
  emisja `Lost`/`Failed`.
- Eksponuj `Network` dla warstwy OkHttp (`network.socketFactory`).
- Wyciągnij czystą funkcję redukcji zdarzeń callbacku → `ApConnectionState`
  (testowalna na JVM, bez Androida).
- Cleanup: `unregisterNetworkCallback` + `bindProcessToNetwork(null)` (coding-rules 13).

**Notatka wykonawcza:** Reducer stanu połączenia napisz test-first (czysta funkcja).

**Wzorce do naśladowania:** wzorzec `requestNetwork`+`bindProcessToNetwork` z researchu.

**Scenariusze testowe:**
- [Unit] sekwencja `Connecting → onAvailable → Connected`.
- [Unit] `Connected → onLost → Lost`; ponowne `onAvailable → Connected`.
- [Unit] `onUnavailable → Failed`.

**Weryfikacja:**
- Na urządzeniu: po wskazaniu SSID ESP32 aplikacja przechodzi w `Connected`; `ping`/
  proste GET do `192.168.4.1` zwraca odpowiedź mimo braku internetu na WiFi.

### Faza 2 — Kontrakt danych i transport

- [ ] **Unit 3: Modele danych i (de)serializacja**

**Cel:** Zamodelować ramkę telemetrii, envelope odpowiedzi i komendy jako type-safe
struktury Kotlin z parsowaniem kotlinx.serialization.

**Wymagania:** R2, R3

**Zależności:** Unit 1

**Pliki:**
- Stwórz: `android/app/src/main/java/<pkg>/data/TelemetryFrame.kt`
- Stwórz: `android/app/src/main/java/<pkg>/data/ApiEnvelope.kt` (`data`, `error{code,message}`)
- Stwórz: `android/app/src/main/java/<pkg>/data/Command.kt` (enum/sealed: arm/disarm/deploy/stow)
- Stwórz: `android/app/src/main/java/<pkg>/domain/MotorState.kt` (sealed/enum mapujący
  `state` 0–4 + `arm_reason`)
- Test (unit): `android/app/src/test/java/<pkg>/data/TelemetryFrameParseTest.kt`
- Test (unit): `android/app/src/test/java/<pkg>/data/ApiEnvelopeParseTest.kt`

**Podejście:**
- Pola dokładnie wg `ws_telemetry.h`/`.c` (nazwy i jednostki: `*_e7`, `deg10`, `cms`).
- Czyste konwersje jednostek (`lat_e7→deg`, `heading_deg10→deg`, `speed_cms→m/s`) jako
  funkcje domenowe z testami.
- Parser odporny na brakujące/nadmiarowe pola (`ignoreUnknownKeys = true`).

**Notatka wykonawcza:** Parsing + konwersje jednostek test-first (fixtures JSON w
`src/test/resources/`, nie inline pełne payloady — coding-rules pkt 2).

**Wzorce do naśladowania:** kształt JSON z `web/app.js` i `ws_telemetry.c`.

**Scenariusze testowe:**
- [Unit] parsowanie pełnej ramki telemetrii → poprawne pola i konwersje (np.
  `gps_lat_e7=520000000 → 52.0°`, `imu_heading_deg10=900 → 90.0°`).
- [Unit] ramka z nieznanym dodatkowym polem nie wywala parsera.
- [Unit] envelope błędu `{data:null, error:{code,message}}` → poprawnie zmapowany.
- [Unit] mapowanie `state` 0–4 → `MotorState`; nieznana wartość → ścieżka błędu (nie crash).

**Weryfikacja:**
- Testy JVM zielone; konwersje jednostek zgodne z oczekiwaniami z firmware.

- [ ] **Unit 4: Klient REST + WebSocket (OkHttp)**

**Cel:** Transport: wysyłka komend `POST /api/command` i strumień telemetrii z `WS /ws`,
oba związane z siecią AP.

**Wymagania:** R2, R3

**Zależności:** Unit 2, Unit 3

**Pliki:**
- Stwórz: `android/app/src/main/java/<pkg>/net/EspHttpClient.kt` (OkHttp z
  `socketFactory(network.socketFactory)`)
- Stwórz: `android/app/src/main/java/<pkg>/net/CommandApi.kt` (POST /api/command)
- Stwórz: `android/app/src/main/java/<pkg>/net/TelemetrySocket.kt` (WS → `Flow<TelemetryFrame>`
  przez `callbackFlow`)
- Test (unit): `android/app/src/test/java/<pkg>/net/CommandResultMapTest.kt`

**Podejście:**
- OkHttp `pingInterval` (5–10 s, LAN); WS opakowany w `callbackFlow` emitujący ramki,
  błędy → zamknięcie flow do reconnectu wyżej.
- `CommandApi` zwraca wynik zmapowany z envelope (sukces vs `error.code/message`,
  z rozpoznaniem 409 „nie DISARMED").
- Czysta funkcja mapująca (status HTTP + envelope) → `CommandResult` (testowalna JVM).

**Notatka wykonawcza:** Mapowanie odpowiedzi komendy test-first (czysta funkcja).

**Wzorce do naśladowania:** sposób wołania API w `web/app.js`.

**Scenariusze testowe:**
- [Unit] 200 + `error:null` → `CommandResult.Success`.
- [Unit] 409 + `SETTINGS_WRITE_REJECTED_NOT_DISARMED` → `Rejected(reason)`.
- [Unit] 400 + `VALIDATION_FAILED` → `Rejected(reason)`.

**Weryfikacja:**
- Na urządzeniu z ESP32: `arm`/`disarm` zmienia stan łodzi (widoczne w telemetrii);
  strumień WS dostarcza ramki ~10 Hz.

- [ ] **Unit 5: Repozytorium telemetrii + watchdog link-down**

**Cel:** Złożyć strumień telemetrii w obserwowalny stan z reconnectem i wykrywaniem
utraty linku.

**Wymagania:** R2, R7

**Zależności:** Unit 4

**Pliki:**
- Stwórz: `android/app/src/main/java/<pkg>/data/TelemetryRepository.kt`
  (`StateFlow<TelemetryUiState>`)
- Stwórz: `android/app/src/main/java/<pkg>/domain/LinkWatchdog.kt` (czysta logika:
  ostatni-czas-ramki + teraz → `LinkStatus { Live, Stale }`)
- Stwórz: `android/app/src/main/java/<pkg>/domain/ConnectionState.kt` (sealed:
  `Disconnected`, `Connecting`, `Live(frame)`, `Stale(lastFrame)`)
- Test (unit): `android/app/src/test/java/<pkg>/domain/LinkWatchdogTest.kt`

**Podejście:**
- Watchdog: jeśli od ostatniej ramki minęło > próg (~500 ms) → `Stale`; nowa ramka → `Live`.
- Różnicę czasu licz w jednej domenie zegara, unsigned (learned-patterns:
  recency wrap-safe — tu monotoniczny `elapsedRealtime`).
- Reconnect z backoff (250 ms → 1 s → max 2 s); wstrzymaj reconnect gdy AP `Lost`.
- Stan jako discriminated union (coding-rules pkt 13), nie zlepek booleanów.

**Notatka wykonawcza:** `LinkWatchdog` test-first; dodaj test wokół granicy progu
(in/out), tak by test failował bez logiki progu (oracle power — learned-patterns).

**Scenariusze testowe:**
- [Unit] ramka tuż przed progiem → `Live`; brak ramki po przekroczeniu progu → `Stale`.
- [Unit] po `Stale` nowa ramka → `Live`.
- [Unit] reconnect backoff rośnie i jest ograniczony do max.

**Weryfikacja:**
- Odłączenie ESP32 powoduje przejście UI w `Stale`/link-down < ~1 s; ponowne
  połączenie wraca do `Live`.

### Faza 3 — UI operacyjny

- [ ] **Unit 6: Ekran telemetrii i wskaźniki bezpieczeństwa**

**Cel:** Compose UI prezentujące stan łodzi, telemetrię i jednoznaczne wskaźniki
bezpieczeństwa.

**Wymagania:** R2, R7, R8

**Zależności:** Unit 5

**Pliki:**
- Stwórz: `android/app/src/main/java/<pkg>/ui/TelemetryViewModel.kt`
- Stwórz: `android/app/src/main/java/<pkg>/ui/TelemetryScreen.kt`
- Stwórz: `android/app/src/main/java/<pkg>/ui/components/` (StatusBadge, GpsCard,
  CompassCard, SafetyBanner)
- Stwórz: `android/app/src/main/java/<pkg>/domain/SafetyIndicators.kt` (czysta:
  `(ConnectionState, frame) → {armedState, failsafe, linkDown, uncalibrated}`)
- Test (unit): `android/app/src/test/java/<pkg>/domain/SafetyIndicatorsTest.kt`

**Podejście:**
- ViewModel zbiera `StateFlow` z repo; UI przez `collectAsStateWithLifecycle`.
- Wyraźny baner: FAILSAFE (czerwony), LINK DOWN (gdy `Stale`/`Disconnected`),
  ARMED/DISARMED, UNCALIBRATED (`calibrated=false`).
- Skrót/odnośnik do web panelu (`http://192.168.4.1`) — R8.
- Prezentacja GPS (fix/sats/lat/lon/speed) i kompasu (heading/calib).

**Scenariusze testowe:**
- [Unit] `state=FAILSAFE` → wskaźnik failsafe aktywny.
- [Unit] `ConnectionState.Stale` → linkDown aktywny niezależnie od ostatniej ramki.
- [Unit] `calibrated=false` → wskaźnik UNCALIBRATED.

**Weryfikacja:**
- Na urządzeniu: telemetria odświeża się płynnie; wymuszony failsafe/odłączenie
  pokazują właściwe banery.

- [ ] **Unit 7: Komendy operacyjne (arm/disarm/deploy/stow)**

**Cel:** Przyciski komend z obsługą wyniku i odrzuceń.

**Wymagania:** R3

**Zależności:** Unit 4, Unit 6

**Pliki:**
- Modyfikuj: `android/app/src/main/java/<pkg>/ui/TelemetryViewModel.kt` (akcje komend)
- Stwórz: `android/app/src/main/java/<pkg>/ui/components/CommandBar.kt`
- Test (unit): `android/app/src/test/java/<pkg>/ui/CommandActionTest.kt`
  (mapowanie wyniku na komunikat UI; sieć zamockowana — mock TYLKO zewnętrznego API)

**Podejście:**
- arm/disarm/deploy/stow → `CommandApi`; wynik `Rejected` → czytelny komunikat (np.
  „można tylko w DISARMED").
- Potwierdzenie dla akcji wrażliwych (deploy/stow) — lekki dialog.
- Wyłącz przyciski niedostępne w danym stanie (np. arm gdy link down).

**Scenariusze testowe:**
- [Unit] sukces komendy → komunikat sukcesu, brak błędu.
- [Unit] `Rejected(NOT_DISARMED)` → komunikat o odrzuceniu, brak crasha.

**Weryfikacja:**
- Na urządzeniu: arm/disarm/deploy/stow działają i poprawnie raportują odrzucenia.

### Faza 4 — Mapa offline

- [ ] **Unit 8: Integracja MapLibre + warstwy offline**

**Cel:** Osadzić MapLibre w Compose i wyświetlić mapę offline (OSM wektor + ortofoto
raster) z przełączaniem warstw.

**Wymagania:** R4

**Zależności:** Unit 1

**Pliki:**
- Stwórz: `android/app/src/main/java/<pkg>/map/MapLibreView.kt` (`AndroidView` +
  lifecycle bridge: `DisposableEffect` + `LifecycleEventObserver`)
- Stwórz: `android/app/src/main/java/<pkg>/map/OfflineStyle.kt` (budowa style JSON z
  lokalnymi źródłami `pmtiles://`/`mbtiles://`, `asset://` sprite/glyphs)
- Stwórz: `android/app/src/main/java/<pkg>/map/LayerToggle.kt` (visibility rastra)
- Stwórz: `android/app/src/main/assets/style/` (style.json, sprite, glyphs)
- Test (unit): `android/app/src/test/java/<pkg>/map/OfflineStyleBuilderTest.kt`
  (poprawność URI/struktury źródeł w budowanym stylu — czysta logika)

**Podejście:**
- View-based MapLibre przez `AndroidView`; ręczne przekazanie `onStart/onResume/
  onPause/onStop/onDestroy/onLowMemory` (inaczej wyciek GL / czarna mapa po tle).
- Wektor OSM jako PMTiles (`pmtiles://asset://...` lub `file://` po skopiowaniu),
  ortofoto jako `RasterSource(mbtiles://...)` + `RasterLayer`, kolejność przez
  `addLayerBelow/Above`, toggle przez `PropertyFactory.visibility`.
- Atrybucja „© OpenStreetMap" widoczna na mapie (ODbL).

**Notatka wykonawcza:** Builder stylu (URI/warstwy) wydziel jako czystą funkcję z testem.

**Wzorce do naśladowania:** „maps in Compose" lifecycle bridge; MapLibre raster/geojson
guide z researchu.

**Scenariusze testowe:**
- [Unit] builder stylu produkuje źródła z poprawnymi URI i kolejnością warstw.
- [Unit] toggle ustawia `visibility` rastra na VISIBLE/NONE.

**Weryfikacja:**
- Na urządzeniu (samolotowy/bez internetu): mapa renderuje wektor OSM; przełącznik
  pokazuje/ukrywa ortofoto; brak czarnej mapy po powrocie z tła.

- [ ] **Unit 9: Marker pozycji łodzi + heading na mapie**

**Cel:** Pokazać pozycję łodzi i orientację z telemetrii, aktualizowane ~10 Hz.

**Wymagania:** R4

**Zależności:** Unit 5, Unit 8

**Pliki:**
- Stwórz: `android/app/src/main/java/<pkg>/map/BoatMarker.kt` (`GeoJsonSource` +
  `SymbolLayer`, `iconRotate(get("heading"))`)
- Stwórz: `android/app/src/main/java/<pkg>/map/BoatMarkerProjection.kt` (czysta:
  `frame → (lon, lat, heading)`; obsługa braku fixa)
- Modyfikuj: `android/app/src/main/java/<pkg>/map/MapLibreView.kt` (podpięcie do StateFlow)
- Test (unit): `android/app/src/test/java/<pkg>/map/BoatMarkerProjectionTest.kt`

**Podejście:**
- Update tylko `setGeoJson` na source (async, NIE synchronous), na wątku UI;
  warstwy nie przebudowuj.
- Gdy `gps_fix=false` → marker ukryty/wyszarzony (nie pokazuj fałszywej pozycji 0,0).
- Opcjonalny tryb „follow" (kamera centruje łódź); bearing kamery wg headingu opcjonalnie.
- Guard: nie wołać `setGeoJson` na nieaktywnym stylu (race — coding-rules 13).

**Scenariusze testowe:**
- [Unit] ramka z fixem → poprawne (lon,lat,heading) dla markera.
- [Unit] `gps_fix=false` → projekcja sygnalizuje brak pozycji (marker ukryty).
- [Unit] `imu_ok=false` → heading traktowany jako nieznany (np. brak rotacji), nie 0°.

**Weryfikacja:**
- Na urządzeniu z ESP32: marker łodzi rusza się zgodnie z telemetrią, strzałka obraca
  się wg kompasu; brak lagów przy 10 Hz.

### Faza 5 — Utrzymanie sesji

- [ ] **Unit 10: Keep-screen-on + foreground service WS**

**Cel:** Utrzymać ekran i połączenie WS podczas sesji na wodzie; posprzątać zasoby.

**Wymagania:** R1, R7

**Zależności:** Unit 5

**Pliki:**
- Stwórz: `android/app/src/main/java/<pkg>/session/TelemetryService.kt` (foreground,
  `foregroundServiceType="connectedDevice"`, notyfikacja)
- Modyfikuj: `android/app/src/main/java/<pkg>/MainActivity.kt` (`FLAG_KEEP_SCREEN_ON`
  na oknie ekranu nawigacji)
- Modyfikuj: `android/app/src/main/AndroidManifest.xml` (service + typ)

**Podejście:**
- `FLAG_KEEP_SCREEN_ON` na oknie (nie w service) — ekran nie gaśnie gdy app na wierzchu.
- Foreground service `connectedDevice` utrzymuje WS przy przygaśnięciu/tle; notyfikacja
  zgodnie z wymogiem Android 14+.
- `PARTIAL_WAKE_LOCK` tylko jeśli pojawią się dropy przy ekranie-off; zwalniaj po sesji.
- Cleanup: stop service, unregister `NetworkCallback`, `bindProcessToNetwork(null)`,
  zwolnienie wake locka (coding-rules pkt 13).

**Scenariusze testowe:**
- [Unit] logika cyklu sesji (start/stop, czy trzymać wake lock) jako czysta funkcja,
  jeśli wydzielona — w przeciwnym razie weryfikacja manualna.

**Weryfikacja:**
- Na urządzeniu: ekran nie gaśnie na ekranie nawigacji; po przełączeniu aplikacji w tło
  i powrocie telemetria nie spadła; po wyjściu z sesji brak wiszących callbacków/wake locków.

- [ ] **Unit 11: Pipeline i dokumentacja map offline**

**Cel:** Udokumentować i (skryptowo) ustandaryzować generowanie i wgrywanie kafli
offline na urządzenie.

**Wymagania:** R4

**Zależności:** Unit 8

**Pliki:**
- Stwórz: `android/maps/README.md` (kroki: Planetiler OSM→PMTiles dla bbox akwenu z
  ograniczonym maxzoom; Geoportal ORTO WMTS→MBTiles raster przez GDAL/rio-mbtiles;
  atrybucja ODbL; gdzie umieścić pliki — assets vs internal storage)
- Stwórz: `android/maps/` (miejsce na artefakty/skrypty pomocnicze; duże pliki poza git)

**Podejście:**
- Dokument operacyjny, nie kod aplikacji: jak zbudować małe regionalne archiwa i jak
  je wgrać (asset bundling vs push do internal storage; przy MBTiles wymagane kopiowanie
  z assets do storage przed użyciem `mbtiles:///abs/path`).
- Rate-limit przy pobieraniu WMTS (publiczna usługa Geoportal); tylko bbox akwenu.
- Wskaż, że duże `.pmtiles`/`.mbtiles` nie idą do git (dodać do `.gitignore`).

**Scenariusze testowe:**
- Brak (artefakt dokumentacyjno-operacyjny).

**Weryfikacja:**
- Wykonanie kroków z README produkuje działające archiwa, które Unit 8/9 renderują
  offline dla wybranego akwenu.

## Wpływ systemowy

- **Graf interakcji:** `ApConnectionManager` (Network) → `EspHttpClient`/
  `TelemetrySocket` → `TelemetryRepository` (StateFlow) → `TelemetryViewModel` → UI/Map.
  Komendy: UI → `CommandApi` → ESP32.
- **Propagacja błędów:** błędy sieci/parse → `ConnectionState.Stale/Disconnected`
  (nie crash); odrzucenia komend → `CommandResult.Rejected` z `error.code` do UI.
- **Ryzyka cyklu życia stanu:** wycieki `NetworkCallback`/wake locka/MapView GL —
  obsłużone cleanupem w Unit 2/8/10. Race przy `setGeoJson` na wymianie stylu — guard
  w Unit 9.
- **Parytet surface API:** brak — kontrakt ESP32 jest jedynym interfejsem; aplikacja
  konsumuje go bez modyfikacji firmware (v1).
- **Pokrycie integracyjne:** pełna ścieżka AP→WS→UI i AP→komenda→ESP32 weryfikowalna
  tylko na realnym ESP32 (manualnie); logika domenowa pokryta testami JVM.

## Ryzyka i zależności

- **Sprzęt jeszcze niekupiony** — finalne `minSdk`, wybór wersji MapLibre (OpenGL vs
  Vulkan) i dostrojenie progów po wyborze tabletu (rekomendacja: Xiaomi Redmi Pad Pro).
- **Czytelność w słońcu** — żaden budżetowy tablet > ~600 nit; mitygacja sprzętowa
  (matowa folia + osłona) + UI o wysokim kontraście (duże elementy, ciemny/jasny motyw).
- **Zależność WiFi bez internetu** — jeśli `bindProcessToNetwork` zawiedzie na konkretnym
  Androidzie, fallback: OkHttp `socketFactory(network.socketFactory)` per-socket.
- **v2 (Spot Lock/Go To Point)** zależy od osobnego firmware regulatora pozycji — poza
  tym planem; mapa w v1 przygotowana forward-compatible (tap→lat/lon).

## Fazowe dostarczanie

### Faza 1 — Fundament i łączność (Unit 1–2)
Budowalny projekt + niezawodne połączenie z AP ESP32. Odblokowuje wszystko inne.

### Faza 2 — Dane i transport (Unit 3–5)
Type-safe telemetria/komendy, strumień + watchdog. Rdzeń wartości operacyjnej.

### Faza 3 — UI operacyjny (Unit 6–7)
Pierwsza realna wartość na wodzie: podgląd + arm/deploy/stow (parytet operacyjny).

### Faza 4 — Mapa offline (Unit 8–9, 11)
Druga oś wartości: pozycja łodzi na mapie offline.

### Faza 5 — Utrzymanie sesji (Unit 10)
Twardnienie pod całodzienne użycie nad wodą.

## Dokumentacja / Notatki operacyjne

- `android/README.md` — build, uprawnienia, uruchomienie, połączenie z AP.
- `android/maps/README.md` — generowanie i wgrywanie kafli offline (Unit 11).
- Atrybucja © OpenStreetMap (ODbL) widoczna w UI mapy; Geoportal ORTO — darmowe,
  zachować rate-limit przy pobieraniu.
- `.gitignore` — wykluczyć `android/build/`, `*.pmtiles`, `*.mbtiles`, `local.properties`.

## Źródła i referencje

- **Dokument źródłowy:** [docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md](../dev-brainstorms/2026-06-20-android-tablet-app-requirements.md)
- Kontrakt API ESP32: `components/web_panel/include/ws_telemetry.h`,
  `components/web_panel/include/command_parse.h`, `components/web_panel/include/api_contract.h`,
  `web/app.js`
- Łączenie z AP bez internetu: https://developer.android.com/reference/android/net/wifi/WifiNetworkSpecifier ,
  https://github.com/square/okhttp/issues/3736
- MapLibre Android: https://maplibre.org/maplibre-native/android/examples/getting-started/ ,
  https://maplibre.org/maplibre-native/android/examples/geojson-guide/
- Kafle/licencje: https://github.com/protomaps/basemaps/blob/main/LICENSE_DATA.md ,
  https://www.geoportal.gov.pl/en/data/orthophotomap-orto/ ,
  https://github.com/onthegomap/planetiler
- OkHttp 5: https://github.com/square/okhttp/releases
