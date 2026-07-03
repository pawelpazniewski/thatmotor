---
date: 2026-07-01
topic: ios-app
revised: 2026-07-01 (po sesji roast — chudy MVP, satelita → v1.1, waypointy w MVP)
---

# Aplikacja iOS — sterowanie silnikiem kajaka z mapą i nawigacją do punktu

## Problem
Operator kajaka chce wskazać punkt na mapie w telefonie i kazać łódce tam dopłynąć
(point-and-shoot), oraz widzieć na żywo, gdzie łódka jest i dokąd płynie — bez
ciągłego sterowania drążkami RC. Dziś jedyny interfejs to wbudowany panel WWW
serwowany z ESP32 (surowe pola/telemetria); brakuje **mapy** i wygodnego
**tap-to-goto** na urządzeniu mobilnym. Panel WWW nie może udźwignąć map — jest
wbudowany w firmware (`EMBED_FILES`), a same kafelki/kontury muszą żyć w telefonie.

Firmware jest gotowy i wystawia komplet potrzebnego API (patrz „Kontrakt firmware").
Ten dokument definiuje **CO** ma robić aplikacja iOS; **JAK** — w `/dev-plan`.

## Użytkownik i kontekst użycia
- Jeden operator, iPhone (iOS 17+), na wodzie, przy słońcu, często jedną ręką.
- Telefon połączony z **WiFi AP silnika** (`kayak-motor`) — czyli **bez internetu**
  i z **jednym radiem WiFi** zajętym przez AP. Dane mapy muszą działać **offline**.
- Aplikacja jest **wygodą i podglądem**, nie jedynym torem bezpieczeństwa: uzbrojenie
  i pełna kontrola manualna pozostają na fizycznym nadajniku RC.

## Filozofia MVP (kluczowa)
**MVP brutalnie chudy.** Budujemy tylko rdzeń nawigacji point-and-shoot; rozbudowa
dopiero po działającym PoC (patrz „Bramka de-risk"). Każdy dodatek, który nie jest
konieczny, żeby dopłynąć do punktu i to zobaczyć, ląduje w v1.1.

## Kluczowe ograniczenia (fundament — zweryfikowane)
1. **Brak internetu na wodzie + jedno radio WiFi.** Gdy iPhone jest w sieci
   `kayak-motor` (bez bramy do internetu), iOS domyślnie routuje ruch przez LTE i
   połączenie natywnej apki do `192.168.4.1` potrafi nie działać. Aplikacja MUSI
   wymusić interfejs Wi‑Fi (`Network.framework`, `requiredInterfaceType = .wifi`) i
   mieć uprawnienie `NSLocalNetworkUsageDescription`. **UWAGA:** to, że panel WWW
   działa w przeglądarce, **nie** dowodzi, że natywna apka trafi do `192.168.4.1` —
   Safari (same-origin) idzie inną ścieżką niż `URLSession`. To jest największe
   ryzyko projektu → osobna bramka de-risk.
2. **Dane mapy offline są obowiązkowe.** MapKit nie ma oficjalnego trybu offline.
   Wybrany stack: **SwiftUI + MapLibre GL Native**. W MVP tło = lekki **wektorowy
   kontur akwenu** (OSM). Satelita/ortofoto = v1.1 (patrz roadmapa).
3. **AP przyjmuje 1 klienta** (`CONFIG_KAYAK_AP_MAX_CONN=1`). Jeden telefon na raz.
4. **Aplikacja jako źródło komend nawigacji** — firmware pauzuje goto przy utracie
   linku (watchdog), więc aplikacja musi utrzymywać keepalive i sygnalizować stan linku.

## Dlaczego osobna apka natywna (a nie panel WWW / PWA)
- **Kafelki/kontury muszą żyć w telefonie i przetrwać** — ESP32 ich nie uniesie
  (partycja aplikacji ~580 KB wolnego), a internetu na wodzie nie ma. Natywna apka
  ma gwarantowany, trwały dysk; PWA na iOS może mieć wyczyszczone dane pod presją.
- **Wymuszenie interfejsu Wi‑Fi** do lokalnego IP wymaga `Network.framework`.
- **Trwały zapis waypointów** i roadmapa satelity domykają wybór natywnej apki.

## Kontrakt firmware (co aplikacja konsumuje — GOTOWE, niezmienne)
- **Sieć:** AP `kayak-motor` (WPA2, hasło `CHANGE-ME-kayak`), `http://192.168.4.1:80`.
- **Telemetria WS `GET /ws` (~10 Hz, JSON, lossy, 1 klient):** m.in.
  `state` (0-3), `arm_reason`, `rc_valid`, `gps_fix`, `gps_sats`,
  `gps_lat_e7`/`gps_lon_e7` (deg×1e7), `gps_speed_cms`, `imu_heading_deg10` (kurs
  dzioba, deg×10), `imu_ok`, oraz pola goto/spot-lock:
  `goto_state` (0=off/1=active/2=paused), `goto_target_lat_e7`/`goto_target_lon_e7`,
  `goto_err_m`, `goto_bearing_deg10`, `goto_arrived`, `app_link_fresh`,
  `spot_lock_state`.
- **Komendy `POST /api/command` (JSON, koperta `{data,error}`):**
  `{"cmd":"goto","lat_e7":<i32>,"lon_e7":<i32>}` (cel; walidacja → 400 przy błędzie),
  `{"cmd":"goto_cancel"}`, oraz `arm`/`disarm`/`deploy`/`stow`/`trim_*` (MVP używa
  tylko `goto`, `goto_cancel`, `disarm`).
- **Keepalive:** goto pauzuje po `goto_comms_timeout_ms` (~1,5 s) bez świeżej komendy;
  aplikacja ponawia `goto` ~2 Hz jako keepalive; `app_link_fresh` w telemetrii pokazuje stan.
- **Dwa tryby trzymania punktu (fakt z `spot_lock.c`):**
  - **Goto-hold (z apki)** jest bramkowane świeżością linku (`comms_gated=true`) —
    telefon zaśnie / apka w tło → **PAUZA**.
  - **CH3-hold (z RC)** trzyma autonomicznie, bez apki (*„Link freshness never gates
    SRC_HOLD"*). Długie, bezobsługowe trzymanie = CH3, nie apka.
- **Warunki uzbrojenia goto (niezmienne):** goto rusza tylko gdy `state==ARMED`
  (uzbrojenie z RC) + oba drążki w neutralu + świeży fix GPS. CH3 ma priorytet
  fizyczny (przerywa goto → trzymanie „tu i teraz"). Failsafe RC zawsze wygrywa.

## Strategia (spójna z firmware)
Point-and-shoot: dotknij punkt na mapie → „Płyń do punktu" → firmware celuje dziobem
i jedzie przodem (stożek ±60°), z pełną mocą przelotową i hamowaniem przy podejściu,
utrzymuje punkt po dojściu. **Aplikacja wozi, RC trzyma:** apka dowozi łódkę do
punktu (goto), a długie trzymanie operator włącza CH3 na nadajniku. Aplikacja **nie
liczy trasy** — wysyła jeden cel lat/lon i pokazuje postęp; cała logika ruchu i
bezpieczeństwa jest w firmware.

## Wymagania — MVP

- **R1. Łączność z AP silnika.** Aplikacja dołącza/prowadzi połączenie z siecią
  `kayak-motor` (`NEHotspotConfiguration`), wymusza interfejs Wi‑Fi dla ruchu do
  `192.168.4.1` (`Network.framework`), obsługuje uprawnienie local‑network. Widoczny
  status: połączono / rozłączono / brak AP.

- **R2. Mapa z konturem akwenu, pozycją i kursem łodzi (offline).** Tło = **wektorowy
  kontur akwenu** (OSM, atrybucja „© OpenStreetMap contributors"), renderowany
  lokalnie (MapLibre). Na mapie: marker łodzi na `gps_lat_e7/lon_e7` z orientacją wg
  `imu_heading_deg10`; centrowanie/podążanie za łodzią; wskaźnik jakości: `gps_fix`,
  `gps_sats`, prędkość (`gps_speed_cms`). Apka jest w pełni używalna bez fotomapy.

- **R3. Tap-to-goto.** Dotknięcie mapy stawia/przesuwa cel (pin). Przycisk „Płyń do
  punktu" wysyła `POST /api/command goto` z `lat_e7/lon_e7` (konwersja WGS84 →
  deg×1e7, spójna z `gps_lat_e7`). Nowy cel zastępuje poprzedni. Na mapie: pin celu +
  linia łódź→cel + odległość (`goto_err_m`) i namiar (`goto_bearing_deg10`).

- **R4. Waypointy (zapisane cele).** Przycisk **„Zapisz tę pozycję"** zapisuje
  **realną pozycję GPS łódki** (`gps_lat_e7/lon_e7`) jako nazwany punkt na liście w
  pamięci telefonu (przeżywa restart). Dotknięcie waypointu z listy → staje się
  aktywnym celem `goto` (zastępuje poprzedni). Zarządzanie: dodaj / nazwij / usuń.
  **Granica: pojedyncze cele do ponownego wysłania — ZERO sekwencji / tras / autopilota.**

- **R5. Podtrzymanie i anulowanie nawigacji.** Gdy goto aktywne, aplikacja ponawia
  `goto` ~2 Hz (keepalive), żeby firmware nie wpadł w pauzę linku. Aplikacja
  **blokuje auto-lock ekranu TYLKO podczas aktywnego przejazdu goto** (żeby
  przypadkowa pauza nie zatrzymała łódki w połowie drogi). Po `goto_arrived` apka
  **podpowiada włączenie CH3 na RC** do bezobsługowego trzymania. Pokazuje
  `goto_state` (off/aktywny/**pauza**), `goto_arrived`, `app_link_fresh`.

- **R6. STOP i Rozbrój (bezpieczeństwo).** Zawsze widoczny, duży **STOP** →
  natychmiast `goto_cancel`, **bez rozbrajania** (silnik zostaje uzbrojony, operator
  przejmuje drążkami RC), bez potwierdzenia. Osobny, mniejszy, celowy **„Rozbrój"** →
  `disarm` (kill silnika), gdy operator chce zatrzymać wszystko. Aplikacja może tylko
  **przerywać / rozbrajać** — **nigdy nie uzbraja**.

- **R7. Miękkie ostrzeżenie „poza akwenem" (NIE failsafe).** Gdy dotknięty cel wypada
  na lądzie lub w innym akwenie niż bieżący kontur, apka pokazuje **ostrzeżenie z
  potwierdzeniem** („Ten punkt wygląda na ląd / poza jeziorem — na pewno?"). To
  barierka UX na przypadkowe dotknięcia, **nie tor bezpieczeństwa** — prawdziwe
  bezpieczeństwo zostaje w firmware/RC/CH3/STOP. Bez twardej blokady (kontur OSM bywa
  niedokładny; twarda blokada dawałaby fałszywe poczucie bezpieczeństwa).

- **R8. Czytelny stan systemu i przyczyny.** Aplikacja pokazuje `state`
  (DISARMED/ARMED/FAILSAFE) oraz — gdy goto się nie uruchamia — czytelny powód z
  telemetrii (brak uzbrojenia / brak fixu GPS / drążki nie w neutralu / brak linku).
  Uzbrojenie robi operator na RC.

- **R9. Odporność linku i telemetrii.** Rozłączenie WS/AP: aplikacja pokazuje utratę
  łączności, wygasza „nieświeże" dane (nie udaje żywych), próbuje wznowić WS. Utrata
  aplikacji/linku podczas goto = firmware pauzuje (bezpiecznie) — apka to
  odzwierciedla, po powrocie wznawia keepalive.

- **R10. Czytelność na wodzie.** Wysoki kontrast/tryb słoneczny, duże cele dotykowe,
  obsługa jedną ręką, blokada przypadkowego wysłania celu (świadome „Płyń do punktu").

## Kryteria sukcesu (MVP)
- **Bramka de-risk zaliczona:** natywna apka na realnym iPhonie (iOS 17+, aktywne
  LTE) niezawodnie gada z `192.168.4.1` po WiFi (`URLSession`/WebSocket).
- Na wodzie **bez internetu** kontur akwenu renderuje się, a pozycja/kurs łodzi
  aktualizują się na żywo (~10 Hz WS).
- Dotknięcie punktu + „Płyń do punktu" uruchamia nawigację (gdy łódka ARMED + fix +
  drążki neutral); pin, linia i malejąca odległość widoczne; „Anuluj"/STOP natychmiast
  przerywają.
- Zapis pozycji łódki jako waypoint + ponowne wysłanie działa i przeżywa restart apki.
- Utrata linku podczas goto → apka pokazuje pauzę (`goto_state=2`,
  `app_link_fresh=false`), po powrocie wznawia; nic nie „udaje" żywego.
- Gdy goto się nie uruchamia, apka jasno mówi dlaczego (stan/fix/drążki/link).
- STOP zawsze dostępny i natychmiastowy; osobny Rozbrój dostępny.

## Granice scope'u (non-goals — MVP)
- **Brak satelity/ortofoto** — MVP tylko wektorowy kontur; fotomapa w v1.1.
- **Brak pobierania obszarów offline (dawne R3)** — kontur mały, zaszyty/dociągnięty
  raz; UX pobierania wraca z satelitą w v1.1.
- **Brak uzbrajania z aplikacji** — arm tylko RC (app może tylko disarm/STOP/cancel).
- **Brak twardej blokady geofence** — tylko miękkie ostrzeżenie (R7).
- **Brak trzymania punktu z samej apki na długo** — długie trzymanie = CH3 z RC.
- **Brak tras / sekwencji / autopilota trosy** — jeden aktywny cel; waypointy to
  pojedyncze zapisane cele.
- **Brak strojenia parametrów z aplikacji** — na razie panel WWW (ESP32).
- **iPad, iOS < 17, Android** — poza MVP.
- **Brak kont/chmury/telemetrii do internetu** — wszystko lokalne, offline.

## Roadmapa v1.1+ (świadomie odłożone)
- **Satelita/ortofoto GUGiK jako przełączalna nakładka.** Licencja **potwierdzona:
  ortofotomapa geoportal.gov.pl jest otwarta i bezpłatna** — legalne offline,
  redystrybucja OK, brak DRM (GeoTIFF), cała PL, rozdz. do 25/10 cm. Pipeline:
  WMTS/GeoTIFF → `gdal2tiles` → MBTiles → MapLibre. Wraca wtedy pobieranie obszarów.
- Auto-wykrywanie wielu jezior (który akwen jest bieżący).
- Napięcie pakietu / bateria na ekranie (jeśli w telemetrii).
- Dźwięk/wibracja na dotarcie i na utratę linku.
- Orientacja mapy (północ/dziób w górze), jednostki (m/węzły).
- Track recording / historia celów.
- Strojenie parametrów z apki (zastąpienie panelu WWW).
- iPad / większy layout, tryby mapy (OSM nawigacyjna).

## Kluczowe decyzje (ustalone w sesji roast)
- **SwiftUI + MapLibre GL Native**, iPhone, iOS 17+.
- **MVP = wektorowy kontur akwenu (OSM)**; satelita → v1.1. ESP32 nie tykamy.
- **Aplikacja wozi, RC trzyma** — goto dowozi, CH3 spot-lock trzyma bezobsługowo.
- **Aplikacja nigdy nie uzbraja** — może tylko przerwać (`goto_cancel`) i rozbroić
  (`disarm`); uzbrojenie i failsafe zostają na RC.
- **STOP = cancel bez rozbrojenia**; osobny Rozbrój na kill.
- **Geofence = miękkie ostrzeżenie, nie failsafe** — bezpieczeństwo w firmware/RC.
- **Waypointy = zapis realnej pozycji łódki**, pojedyncze cele, zero tras.
- **Cała logika ruchu/bezpieczeństwa w firmware** — aplikacja to cienki klient
  kontraktu WS/HTTP; nie duplikuje regulatora ani bramek.
- **Keepalive ~2 Hz** dla goto (spójny z watchdogiem firmware).

## Zależności / założenia
- Firmware `feature/goto-waypoint-navigation` (dostarczony): WS telemetria + `POST
  /api/command` goto/goto_cancel + comms‑watchdog + dwa tryby hold. Kontrakt pól
  `goto_*`/`gps_*`/`imu_*` stabilny i **niezmienny w MVP**.
- **Ryzyko binarne:** iOS 17 musi niezawodnie wymusić interfejs Wi‑Fi do lokalnego IP
  przy AP bez internetu (do potwierdzenia bramką). **Plan B = BLE**, ale to oznacza
  dotknięcie firmware — trzymany w odwodzie, nie w MVP.
- Kontur akwenu z OSM (`natural=water`) — dostępny, licencja ODbL (atrybucja).

## Otwarte pytania (do planowania)
- **Konwersja współrzędnych** — mapa (WGS84 stopnie) → `lat_e7/lon_e7` i z powrotem;
  spójność z `gps_lat_e7` (deg×1e7).
- **Wykrycie „innego akwenu"** dla ostrzeżenia R7 — najbliższy polygon vs bieżący
  kontur; jak zaszyć/dostarczyć kontur(y).
- **`NEHotspotConfiguration`** — czy apka dołącza do `kayak-motor` sama, czy operator
  robi to w Ustawieniach iOS (sprawdzić w bramce razem z Wi‑Fi‑binding).

## Następne kroki
→ **Bramka de-risk (Faza 0):** goła natywna apka — `NEHotspotConfiguration` +
`Network.framework` + `URLSession`/WebSocket do `192.168.4.1` z aktywnym LTE.
Zielone → `/dev-plan` (klient WS/HTTP → mapa+kontur → tap-to-goto → STOP/keepalive →
waypointy + ostrzeżenie). Czerwone → stop, rozmowa o BLE.
