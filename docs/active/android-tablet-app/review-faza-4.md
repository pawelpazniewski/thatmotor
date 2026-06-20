# Code Review — Faza 4 (Mapa offline)

Data: 2026-06-20
Branch: `feature/android-tablet-app`
Zakres: Unit 8 (MapLibre + warstwy offline), Unit 9 (marker łodzi + heading), Unit 11 (pipeline map)
Metoda: multi-agent (security, performance, architektura/jakość, test coverage). Analiza statyczna.

## Uwaga środowiskowa

Natywny Android (Kotlin/Gradle/Compose/MapLibre). Brak JDK/Gradle/Android SDK/emulatora w
środowisku — **build JVM, testy instrumentalne i E2E NIE mogły być uruchomione** (ograniczenie
środowiska, nie finding). Weryfikacja na sprzęcie/emulatorze pozostaje do wykonania ręcznie.

## Severity gate

⚠️ **KONTYNUUJ Z ZASTRZEŻENIAMI** — 6 problemów P2 do naprawy, 0 blokujących.

## Liczniki

- 🔴 P1 (blocking): **0**
- 🟠 P2 (important): **6**
- 🟡 P3 (nit): **6**
- 🌐 E2E: **N/A** (brak emulatora/przeglądarki)

Typy findingów: KOD = 4×P2 + 5×P3 · TEST = 2×P2 + 1×P3 · E2E = 0 (N/A)

## Pliki sprawdzone (10)

- `android/app/src/main/java/com/thatmotor/kayak/map/MapLibreView.kt`
- `android/app/src/main/java/com/thatmotor/kayak/map/MapController.kt`
- `android/app/src/main/java/com/thatmotor/kayak/map/OfflineStyle.kt`
- `android/app/src/main/java/com/thatmotor/kayak/map/BoatMarker.kt`
- `android/app/src/main/java/com/thatmotor/kayak/map/BoatMarkerProjection.kt`
- `android/app/src/main/java/com/thatmotor/kayak/map/LayerToggle.kt`
- `android/app/src/main/java/com/thatmotor/kayak/map/MapAssets.kt`
- `android/app/src/main/assets/style/style.json`
- `android/maps/README.md`
- `android/gradle/libs.versions.toml`
- testy: `OfflineStyleBuilderTest.kt`, `BoatMarkerProjectionTest.kt`

---

## 🟠 P2 — Important

### P2-1 [KOD] Warstwa `roads` jako `type: "fill"` — drogi się nie wyrenderują
**`OfflineStyle.kt:68` (+ `style.json:21`)**

`vectorLayer(MapIds.VECTOR_LAYER_ROADS, "transportation")` generuje warstwę `type: "fill"`.
Source-layer `transportation` w schemacie OpenMapTiles/Planetiler to geometrie **liniowe**
(LineString). Warstwa `fill` rysuje tylko polygony → drogi zostaną pominięte. Oczekiwany
`type: "line"`. Helper `vectorLayer` jest sztywno zahardcodowany na `"fill"` dla wszystkich
trzech warstw (`background-fill`/`water` OK, `transportation` nie).
Rekomendacja: dodać parametr typu do `vectorLayer` (lub osobny `lineLayer`); roads jako
`type:"line"`. Naprawić synchronicznie `style.json:21`. Dodać test z mocą wyroczni na `type`
warstwy roads (failuje gdy typ zły).

### P2-2 [KOD] Hot-path 10 Hz: `applyRasterVisible` + `setGeoJson` wołane bezwarunkowo co ramkę
**`MapController.kt:53-63`** (pokrewne: `MapLibreView.kt:52-54` — `update` woła `onState` co rekompozycję)

`onState` przy każdym update wywołuje `applyRasterVisible(...)` (JNI: `getLayer` + `setProperties`)
mimo że `showOrtho` zmienia się raz na minuty, oraz `boatMarker.update(...)` (`setGeoJson`)
nawet gdy `latestFrame == null` (pusty FeatureCollection co ramkę) lub gdy pozycja identyczna.
Przy ~10 Hz to ~10 zbędnych mutacji warstw/s na wątku UI. Narusza regułę "brak blokujących/
zbędnych operacji na hot-path". (Agent perf klasyfikował jako P1; konsolidacja: P2 — nie psuje
funkcjonalności, ale to wyraźne marnotrawstwo hot-path.)
Rekomendacja: strażniki stanu w `MapController` — `lastOrtho: Boolean?` i `lastPosition:
BoatPosition?` (`BoatPosition` to `data class`, `equals` darmowe); aplikuj efekty tylko przy
zmianie. Czyni `onState` idempotentnym i tanim.

### P2-3 [KOD] `recenter` używa skokowego `moveCamera` przy 10 Hz follow
**`MapController.kt:67`**

W trybie `followBoat` co ramkę leci `moveCamera(newLatLng(...))` (bez animacji) → mapa
"przeskakuje" rwanie, każdy `moveCamera` wymusza re-render kafli na wątku GL. Kryterium
akceptacji Unit 9 to "bez lagów przy 10 Hz".
Rekomendacja: `easeCamera(update, ~100 ms)` (≈ okres ramki) dla płynnego trackingu, ALBO
recenter tylko przy realnej zmianie pozycji (załatwione przez strażnik z P2-2).

### P2-4 [KOD] `onTrimMemory` mapuje KAŻDY poziom na pełny `onLowMemory()`
**`MapLibreView.kt:76`**

`onTrimMemory(level)` jest wołane bardzo często przy lekkich poziomach (`RUNNING_MODERATE`,
`UI_HIDDEN`); pełny `mapView.onLowMemory()` (zrzut cache kafli GL) przy każdym lekkim sygnale
powoduje przeładowanie kafli z MBTiles/PMTiles po każdym przejściu UI w tło — zbędny I/O i
re-dekodowanie. Może też pogłębiać "czarną mapę" po powrocie z tła.
Rekomendacja: reagować tylko na poważne poziomy, np. `if (level >=
ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW) controller.onLowMemory()`.

### P2-5 [TEST] Brak testu czystej funkcji `MapAssets.sources()`
**`MapAssets.kt:31-36`**

Czysta funkcja z realną logiką rozgałęziania schematów URI (`pmtiles://asset://` dla wektora,
`mbtiles://<absolutna ścieżka>` z `File(...).absolutePath` dla rastra, `asset://` dla
sprite/glyphs). Brak `MapAssetsTest`. To dokładnie typ "Pure ⊥ HAL", który reguły projektu
(learned-patterns) nakazują pokryć host-testem; ryzyko regresji (zamiana prefiksu / zgubienie
`absolutePath`) nie zostanie złapane.
Rekomendacja: dodać `MapAssetsTest.kt` — asercje na 4 URI; w szczególności że `rasterMbtilesUri`
zaczyna się od `mbtiles://` i zawiera `internalFilesDir.absolutePath` (podać `File("/tmp/x")`),
oraz że wektor ma `pmtiles://asset://`.

### P2-6 [TEST] Brak boundary case dla ujemnych lon/lat (zachodnia/południowa półkula)
**`BoatMarkerProjectionTest.kt`**

Testy używają wyłącznie współrzędnych dodatnich (52.0N/21.0E). Plan wymienia boundary
"lon/lat ujemne". Brak testu na ujemny `gpsLonE7`/`gpsLatE7` ani w `projectBoatPosition`, ani
w GeoJSON (znak minus w `coordinates`).
Rekomendacja: dodać przypadek z ujemnym lon/lat i asercję na ujemne wartości w `Positioned`
oraz obecność minusa w GeoJSON.

---

## 🟡 P3 — Nit

### P3-1 [KOD] `destroy()` woła `onStop()` przed `onDestroy()` — redundantny stop
**`MapController.kt:88-89`** — jeśli `ON_STOP` już odpalił `view.onStop()`, drugi `onStop()` jest
redundantny (MapLibre toleruje). Rekomendacja: w `destroy()` zostawić tylko `onDestroy()`.

### P3-2 [KOD] Interpolacja `Double` do JSON wrażliwa na locale (założenie niejawne)
**`BoatMarkerProjection.kt:58,62`, `OfflineStyle.kt:72-92`** — `Double.toString()` na JVM jest
locale-niezależny (zawsze kropka), więc **poprawne i bezpieczne** (injection niemożliwy: wartości
to `Double` z `Int` telemetrii). Kruche przy refaktorze. Rekomendacja: komentarz utrwalający
założenie "Double.toString jest locale-independent / wartości pochodzą z Int → bezpieczne
literały numeryczne". Bez zmiany kodu.

### P3-3 [KOD] `trimIndent()` w `boatPositionToGeoJson` (hot-path)
**`BoatMarkerProjection.kt:59-65`** — `trimIndent()` skanuje string co pozycjonowanie; zbędne dla
maszynowego GeoJSON. Po naprawie P2-2 wykonuje się tylko przy realnej zmianie. Rekomendacja:
jednolinijkowy string bez `trimIndent()`.

### P3-4 [KOD] style.json `mbtiles://ortho.mbtiles` (względny) vs runtime absolutny
**`style.json:13` vs `MapAssets.kt:33`** — style.json jest **tylko referencyjny** (runtime buduje
styl przez `buildOfflineStyleJson`), więc to nie bug. Przy bezpośrednim załadowaniu style.json
raster ortho się nie znajdzie. Rekomendacja: jedno zdanie w `style/README.md` o tym, że ścieżka
rastra w style.json jest placeholderem.

### P3-5 [KOD] `MapLibreView` nie jest wpięty w żaden ekran (zaplanowana integracja)
**`MapLibreView.kt:32`** — brak callera w `src/main` (potwierdzone grepem). Zgodne z fazowaniem
(nawigacja do mapy w późniejszej fazie / Faza 5). Klasyfikacja: zaplanowana integracja, nie dead
code. Rekomendacja: upewnić się, że kolejna faza podepnie `MapLibreView` (inaczej stanie się
martwym kodem). Bez TODO w kodzie produkcyjnym.

### P3-6 [TEST] Brak boundary heading-wrap (359.9) i `heading = 0.0` w GeoJSON
**`BoatMarkerProjectionTest.kt`** — brak pozytywnego testu `imuHeadingDeg10 = 3599 → 359.9`
(oracle na skalowanie /10) oraz brak przypadku `headingDeg = 0.0` (znane 0 ≠ null; kod poprawnie
emituje `"heading":0.0`, ale test tego nie pinuje — `if (heading > 0)` przeszłoby niezauważone).
Rekomendacja: dodać oba przypadki.

---

## Cross-reference z planem technicznym (Unit 8/9/11)

| Plan scenariusz | Test | Oracle |
|---|---|---|
| U8: builder — poprawne URI | `builder emits both sources...` + `...sprite and glyphs` | ✅ |
| U8: kolejność warstw | `raster layer is ordered above the vector layers` | ✅ failuje po odwróceniu |
| U8: toggle VISIBLE/NONE | `rasterVisibility maps the toggle...` | ✅ obie gałęzie |
| U9: fix → (lon,lat,heading) | `a frame with a fix projects...` | ✅ |
| U9: gps_fix=false → hidden | `no GPS fix hides the marker` | ✅ `assertEquals(Hidden)` |
| U9: imu_ok=false → heading null nie 0 | `imu not ok yields a null heading...` | ✅ `assertNull` |

Oba pliki testowe wymagane planem istnieją; 6/6 scenariuszy planu pokryte z mocną wyrocznią;
0 testów assertion-free. Pliki implementacji nietestowane host: `MapController.kt`, `BoatMarker.kt`
(cienkie HAL-adaptery MapLibre — zgodne z konwencją), `MapAssets.kt` (czysta logika — gap, P2-5).

**Bump MapLibre 11.5.2 → 11.7.0:** uzasadniony (natywne `pmtiles://` wymaga ≥ 11.7.0) i
odnotowany w `libs.versions.toml:13-15` (komentarz) + commit message + `maps/README.md:109`.
Brak znanych blokujących CVE (do jednorazowej weryfikacji advisory przy następnym audycie zależności).

**Atrybucja OSM/ODbL:** spełniona — `OSM_ATTRIBUTION` wpięta w źródło wektorowe
(`OfflineStyle.kt:54,82`), `style.json:10`, README licensing (ODbL + GUGiK, `maps/README.md:94-99`).

## Bezpieczeństwo

Czysto. Brak hardcoded sekretów/API keys (Geoportal WMTS to publiczny serwis bez klucza, używany
tylko w pipeline offline). JSON injection nieosiągalny (interpolowane wartości to stałe lub `Double`
z `Int`). `mbtiles://` wskazuje stałą nazwę w sandboxie `filesDir` — brak path traversal.

## E2E / Weryfikacja na sprzęcie (N/A w tym środowisku)

Niezaznaczone `Weryfikacja:` do wykonania ręcznie na urządzeniu/emulatorze:
- U8: (bez internetu) mapa renderuje wektor OSM; toggle ortofoto; **brak czarnej mapy po powrocie
  z tła** (sprawdzić też rotację — patrz P2-4 onTrimMemory).
- U9: marker rusza się wg telemetrii, strzałka wg kompasu, bez lagów przy 10 Hz (patrz P2-3).
- U11: kroki README produkują archiwa renderowane offline.

---

## Re-review (cykl 2) — weryfikacja po naprawie 6×P2

Data: 2026-06-20 · Commit naprawczy: `0aa51ee` · Metoda: analiza statyczna (brak
JDK/Gradle/Android SDK/emulatora — testów JVM/buildu/E2E NIE uruchomiono; ograniczenie
środowiska, nie finding).

### Severity gate: ✅ CZYSTE (kontynuuj)

Wszystkie 6 findingów P2 rozwiązane, brak regresji. Pozostają tylko P3 (nity, świadomie
odroczone).

### Liczniki (po naprawie)

- 🔴 P1: **0**
- 🟠 P2: **0** (było 6 → 0)
- 🟡 P3: **6** (bez zmian — odroczone, nieblokujące)

Typy: P3 = KOD ×4 + TEST ×2.

### Status poprzednich 6×P2

| # | Finding | Status | Weryfikacja |
|---|---------|--------|-------------|
| P2-1 | roads `fill`→`line` + test wyroczni | ✅ ROZWIĄZANE | `OfflineStyle.kt:69` `type="line"`; `vectorLayer` ma param `type`; `style.json:21` zsynchronizowany; test `roads layer is a line layer…` ma realną moc wyroczni |
| P2-2 | strażniki `lastOrtho`/`lastPosition` na hot-path | ✅ ROZWIĄZANE | `MapController.kt:33-34,66-78`; aplikacja tylko przy zmianie; reset w `destroy()` |
| P2-3 | `recenter` `easeCamera` zamiast `moveCamera` | ✅ ROZWIĄZANE | `MapController.kt:85-88` `easeCamera(update, 100ms)`; wołane tylko przy zmianie pozycji |
| P2-4 | `onTrimMemory` próg `TRIM_MEMORY_RUNNING_LOW` | ✅ ROZWIĄZANE | `MapLibreView.kt:80-84` reaguje tylko `level >= TRIM_MEMORY_RUNNING_LOW` |
| P2-5 | `MapAssetsTest` (URI sources) | ✅ ROZWIĄZANE | nowy `MapAssetsTest.kt`: 4 URI; raster `mbtiles://` + `absolutePath` (oracle na scheme i ścieżkę) |
| P2-6 | boundary ujemnych lon/lat | ✅ ROZWIĄZANE | nowy test Buenos Aires (-34.6/-58.4); asercja na minus w `Positioned` i GeoJSON |

### Weryfikacja szczegółowych ryzyk (z zlecenia re-review)

**1. Strażnik `lastPosition` nie blokuje aktualizacji markera — POTWIERDZONE.**
- Pierwszy update: `lastPosition = null`, projekcja ≠ null → `position != lastPosition` true →
  `boatMarker.update` wykonuje się. Pierwsza ramka przechodzi.
- `BoatPosition` = sealed interface; `Positioned` to `data class` (structural `equals` po
  lon/lat/heading), `Hidden` to `data object` (singleton). Zmiana którejkolwiek współrzędnej/
  headingu → `equals=false` → update fire. Działa poprawnie.
- `destroy()` resetuje `lastPosition`/`lastOrtho` na `null` (`MapController.kt:115-116`).
  Kontroler i tak jest `remember`-owany per-kompozycja, więc reset to dodatkowa poprawność.

**2. `easeCamera` 100 ms nie kumuluje się przy 10 Hz — AKCEPTOWALNE.**
- Każda realna zmiana pozycji startuje nowy `easeCamera(100ms)`. MapLibre anuluje trwającą
  animację kamery przy starcie nowej (`cancelTransitions` wewnętrznie) → ease NIE kolejkują
  się ani nie stackują; nowy zastępuje poprzedni. Efekt: ciągła, płynna interpolacja podążania.
- Przy dokładnie 10 Hz ease (100ms) nigdy w pełni się nie kończy przed zastąpieniem — to
  pożądane (płynny chase bez teleportacji i bez kumulacji). Połączone ze strażnikiem P2-2 ease
  odpala się tylko przy ruchu. Akceptowalne, zgodne z kryterium „bez lagów przy 10 Hz".

**3. Test `type` warstwy roads ma realną moc wyroczni — POTWIERDZONE.**
- Test wycina obiekt warstwy roads (okno od `"osm-roads"` do następnego `"id"`, tj. `"ortho"`
  rastra) i sprawdza, że jego `"type"` zawiera `"line"`. Regresja do `"fill"` → substring
  zawiera `"fill"`, nie `"line"` → test FAILuje. Granice okna poprawne (roads jest przed
  raster w kolejności warstw). Spełnia regułę „czy test FAILuje gdy usunę transformację".

**4. Brak test weakening — POTWIERDZONE.**
- Diff `0aa51ee` na plikach testowych to wyłącznie DODANIA nowych `@Test` (negative-coords,
  roads-type) + nowy `MapAssetsTest.kt`. Żaden istniejący test nie zmodyfikowany, żadna
  asercja nie osłabiona, zero assertion-free. Wszystkie nowe testy mają wyrocznię
  (sign-preservation, scheme/absolutePath, line-not-fill).

### Drobna obserwacja (nie finding, nieblokująca)

`onMapReady` ustawia `lastOrtho = pendingOrtho` bezwarunkowo, mimo że `applyRasterVisible` może
zwrócić `false` gdy brak warstwy rastra. W praktyce warstwa `ortho` jest zawsze w budowanym
JSON-ie, więc zawsze znaleziona — brak realnego defektu. Do ewentualnego rozważenia tylko gdyby
budowa stylu stała się warunkowa.

### Pozostałe P3 (odroczone, nieblokujące)

Bez zmian względem cyklu 1 (redundantny `onStop` w `destroy`, komentarz locale-independent
`Double.toString`, `trimIndent` w hot-path, placeholder ścieżki w `style.json`, niewpięty
`MapLibreView`, boundary heading-wrap/`0.0`). Wszystkie zaznaczone `[ ]` w zadaniach.

### Werdykt re-review

Faza 4 gotowa do kontynuacji. Pozostaje ręczna weryfikacja E2E na sprzęcie/emulatorze
(render offline, toggle ortho, brak czarnej mapy po tle, płynność 10 Hz) — niewykonalna w tym
środowisku.
