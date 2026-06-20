# Plan: Aplikacja Android (tablet) — panel operacyjny + mapa offline (v1)

Branch: `feature/android-tablet-app`
Ostatnia aktualizacja: 2026-06-20

## Cele i zakres

Natywna aplikacja Android (tablet) w podkatalogu `android/` repozytorium `that_motor`.
v1 replikuje **operacyjne** funkcje web panelu ESP32 i dodaje **mapę offline z pozycją
łodzi**. Manualna jazda zostaje na nadajniku RC; aplikacja nie steruje gazem/skrętem.

**W zakresie v1 (R1–R4, R7, R8):**
- Łączność z WiFi SoftAP ESP32 (`192.168.4.1`, WPA2-PSK), praca offline.
- Telemetria live z `WS /ws` (~10 Hz).
- Komendy operacyjne `arm`/`disarm`/`deploy`/`stow` przez `POST /api/command`.
- Mapa offline (OSM wektor + ortofoto raster), pozycja łodzi + heading z telemetrii.
- Wskaźniki bezpieczeństwa (FAILSAFE, uzbrojenie, utrata linku).
- Konfiguracja (parametry, kalibracja ESC) zostaje na web panelu (skrót w app).

**Poza zakresem (v2 / non-goals):**
- Spot Lock / Go To Point (R5/R6) — wymagają firmware regulatora pozycji na ESP32.
- Wirtualny joystick / manualna jazda z tabletu.
- Edytor 23 parametrów i kreator kalibracji ESC.
- Mapy żeglarskie/batymetria, iOS, GPS tabletu.

## Kluczowe decyzje (skrót)

- Stack: Kotlin + Jetpack Compose + ViewModel/StateFlow.
- Mapa: MapLibre Native View-based + `AndroidView` (nie maplibre-compose, nie osmdroid).
- Offline: lokalne archiwa PMTiles (wektor) + MBTiles (raster), lokalny style JSON.
- Pozycja łodzi: własny `GeoJsonSource` + `SymbolLayer` (nie LocationComponent).
- Sieć: `requestNetwork(WifiNetworkSpecifier)` bez `NET_CAPABILITY_INTERNET` +
  `bindProcessToNetwork` + OkHttp `socketFactory`.
- Link-down: app-level watchdog (~500 ms bez ramki = STALE).
- Architektura: `net → data → domain → ui/map`; logika domenowa = czyste funkcje
  testowane na JVM (Pure ⊥ HAL), adaptery cienkie.

Pełne uzasadnienia: patrz plan techniczny w Źródłach.

## Fazy wdrożenia

### Faza 1 — Fundament i łączność
- **Unit 1: Bootstrap projektu Android** (S/M) — budowalny projekt Compose w `android/`,
  manifest, uprawnienia, minSdk/targetSdk, pinowane zależności.
  - *Kryteria akceptacji:* `assembleDebug` przechodzi; app startuje na emulatorze.
- **Unit 2: Adapter łączności z AP ESP32** (M) — połączenie z SoftAP + bindowanie ruchu.
  - *Kryteria akceptacji:* przejście w `Connected`; GET do `192.168.4.1` działa bez internetu.

### Faza 2 — Kontrakt danych i transport
- **Unit 3: Modele danych i (de)serializacja** (M) — telemetria, envelope, komendy + konwersje jednostek.
  - *Kryteria akceptacji:* testy JVM zielone; konwersje zgodne z firmware.
- **Unit 4: Klient REST + WebSocket (OkHttp)** (M) — komendy + strumień telemetrii.
  - *Kryteria akceptacji:* arm/disarm zmienia stan łodzi; WS dostarcza ramki ~10 Hz.
- **Unit 5: Repozytorium telemetrii + watchdog link-down** (M) — StateFlow + reconnect + STALE.
  - *Kryteria akceptacji:* odłączenie ESP32 → link-down < ~1 s; powrót → `Live`.

### Faza 3 — UI operacyjny
- **Unit 6: Ekran telemetrii i wskaźniki bezpieczeństwa** (M) — Compose UI + banery.
  - *Kryteria akceptacji:* telemetria odświeża się płynnie; failsafe/odłączenie → właściwe banery.
- **Unit 7: Komendy operacyjne (arm/disarm/deploy/stow)** (S/M) — przyciski + obsługa odrzuceń.
  - *Kryteria akceptacji:* komendy działają i raportują odrzucenia.

### Faza 4 — Mapa offline
- **Unit 8: Integracja MapLibre + warstwy offline** (L) — `AndroidView` + lifecycle, style, toggle.
  - *Kryteria akceptacji:* mapa renderuje wektor offline; toggle ortofoto; brak czarnej mapy po tle.
- **Unit 9: Marker pozycji łodzi + heading** (M) — GeoJSON + SymbolLayer, update 10 Hz.
  - *Kryteria akceptacji:* marker rusza się wg telemetrii, strzałka wg kompasu, bez lagów.
- **Unit 11: Pipeline i dokumentacja map offline** (M) — generowanie/wgrywanie kafli.
  - *Kryteria akceptacji:* kroki README produkują archiwa renderowane offline.

### Faza 5 — Utrzymanie sesji
- **Unit 10: Keep-screen-on + foreground service WS** (M) — utrzymanie ekranu i WS + cleanup.
  - *Kryteria akceptacji:* ekran nie gaśnie; telemetria przeżywa tło; brak wiszących callbacków/wake locków.

## Sekwencjonowanie

Unit 1 → 2 → (3 → 4 → 5) → (6, 7) ; Mapa: 8 → 9 (+ 11) równolegle po Unit 1/5 ;
Unit 10 po Unit 5. Faza 3 daje pierwszą wartość operacyjną, Faza 4 dokłada mapę.

## Ryzyka (skrót)

- Sprzęt jeszcze niekupiony → finalny minSdk, wersja MapLibre (OpenGL/Vulkan), progi po wyborze tabletu.
- Czytelność w słońcu → mitygacja sprzętowa (folia/osłona) + UI wysokiego kontrastu.
- `bindProcessToNetwork` zawodne na danym Androidzie → fallback OkHttp `socketFactory` per-socket.

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-06-20-android-tablet-app-requirements.md
- Plan techniczny: docs/plans/2026-06-20-001-feat-android-tablet-app-plan.md
