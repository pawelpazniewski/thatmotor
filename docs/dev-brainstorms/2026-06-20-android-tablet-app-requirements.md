---
date: 2026-06-20
topic: android-tablet-app
---

# Aplikacja natywna Android (tablet) — panel sterowania + nawigacja GPS

## Problem
Dziś sterowanie silnikiem kajaka opiera się na fizycznym nadajniku RC (jazda) oraz
web panelu na ESP32 (arm/deploy, telemetria, konfiguracja). Brakuje **mapy z pozycją
łodzi** i platformy do **autonomicznego pozycjonowania** (Spot Lock, Go To Point).
Web panel nie pokazuje, gdzie łódź jest w terenie, mimo że ESP32 ma już GPS (NEO-M9N)
i kompas (BNO085) i wystawia te dane w telemetrii.

Celem jest natywna aplikacja Android na tablet, która replikuje operacyjne funkcje
web panelu i dodaje warstwę mapową z pozycjonowaniem łodzi.

## Wymagania

- **R1. Aplikacja natywna Android na tablet.** Łączy się z WiFi SoftAP ESP32
  (`192.168.4.1`, WPA2-PSK). Działa bez dostępu do internetu na wodzie.
- **R2. Telemetria live.** Subskrypcja `WS /ws` (~10 Hz, JSON). Prezentacja:
  stan (DISARMED/ARMED/FAILSAFE/ESC_CALIBRATION/DEPLOY), `rc_valid`, wyjścia
  servo/ESC, GPS (`gps_fix`, `gps_sats`, `gps_lat_e7`, `gps_lon_e7`, `gps_speed_cms`),
  kompas (`imu_ok`, `imu_heading_deg10`, `imu_calib`), flagi kalibracji/NVS.
- **R3. Komendy operacyjne.** `arm` / `disarm` / `deploy` / `stow` przez
  `POST /api/command`. Z czytelnym wynikiem (envelope `{ data, error }`) i obsługą
  odrzuceń (np. 409 gdy nie DISARMED).
- **R4. Mapa offline z pozycją łodzi.** Warstwa bazowa OSM (wektorowa) + opcjonalna
  warstwa satelitarna; obie pre-cache'owane przed wypłynięciem, działają bez internetu.
  Pozycja łodzi z telemetrii (`gps_lat_e7`/`gps_lon_e7`), orientacja kursora łodzi
  z kompasu (`imu_heading_deg10`).
- **R5. Spot Lock (v2).** Użytkownik włącza „trzymaj pozycję"; aplikacja wysyła
  komendę celu do ESP32; **logika utrzymania pozycji działa na łodzi (ESP32)**.
- **R6. Go To Point (v2).** Użytkownik wskazuje punkt na mapie; aplikacja wysyła
  współrzędne celu; **ESP32 nawiguje do punktu i tam trzyma pozycję**.
- **R7. Wskaźniki bezpieczeństwa.** Wyraźna sygnalizacja stanu FAILSAFE, stanu
  uzbrojenia oraz **utraty linku** (brak świeżej telemetrii > próg). Aplikacja w żaden
  sposób nie blokuje ani nie wyłącza fizycznego toru RC.
- **R8. Konfiguracja pozostaje na web panelu (v1).** Edycja 23 parametrów, kreator
  kalibracji ESC i trim serwa zostają w istniejącym web panelu. Aplikacja może
  oferować skrót/odnośnik do panelu.

## Kryteria sukcesu
- Na wodzie, w słońcu, użytkownik widzi pozycję łodzi na mapie i telemetrię na żywo.
- Może uzbroić/rozbroić oraz deploy/stow z tabletu.
- Aplikacja działa całkowicie offline po wcześniejszym pobraniu map.
- Utrata połączenia z ESP32 jest natychmiast i jednoznacznie widoczna.
- (v2) Jednym tapnięciem aktywuje Spot Lock; może wskazać punkt i wysłać Go To Point,
  a łódź realizuje zadanie boat-side.

## Granice scope'u (non-goals)
- **Brak wirtualnego joysticka / manualnej jazdy z tabletu** — gaz/skręt zostają na
  fizycznym nadajniku RC (niski lag, niezawodny link).
- **Brak edytora 23 parametrów i kreatora kalibracji ESC w v1** — pozostają na web panelu.
- **Brak map żeglarskich / batymetrii** (głębokości) — tylko OSM + satelita.
- **Brak wersji iOS.**
- **Tablet bez własnego GPS** — pozycjonowanie opiera się o GPS łodzi z telemetrii.
- **Spot Lock / Go To Point poza v1** — wymagają osobnego firmware (patrz Założenia),
  realizowane jako v2.

## Kluczowe decyzje
- **Logika Spot Lock / Go To Point działa na łodzi (ESP32), nie na tablecie** —
  station-keeping nie zależy od stabilności WiFi ani od tego, czy tablet się uśpił;
  CH3 jest już zarezerwowany pod spot lock.
- **Manualna jazda zostaje na RC** — aplikacja = parytet operacyjny web panelu + mapa,
  nie zastępuje nadajnika.
- **Mapy offline: OSM (baza) + satelita (warstwa)** — wystarczające na śródlądzie,
  bez zależności od internetu na wodzie.
- **v1 = podzbiór operacyjny + mapa (wyświetlanie pozycji); autonomia w v2** —
  aplikacja powstaje przed firmwarem regulatora pozycji, żeby szybciej dać wartość.
- **Tablet WiFi-only bez GPS.** Rekomendacja sprzętu (priorytet: jasność → RAM →
  bateria → cena):
  - **TOP: Xiaomi Redmi Pad Pro / Pad 2 Pro 12,1" 8/256 WiFi** (~1000–1300 zł,
    ME/Euro) — ~600 nit (najjaśniejszy w budżecie), duży ekran pod mapę, Snapdragon 7s,
    ~10 000 mAh.
  - **Budżet: Lenovo Tab M11 10,95" 8/128 WiFi + rysik** (~600–700 zł) — 400 nit
    (słabiej w słońcu), Helio G88.
  - Niezależnie od modelu: **matowa folia antyodblaskowa + wodoodporny uchwyt** —
    żaden budżetowy tablet nie przekracza ~600 nit, a komfort w pełnym słońcu zaczyna
    się ~1000 nit.

## Zależności / Założenia
- **Telemetria już zawiera lat/lon/heading** — potwierdzone w obecnym firmware
  (`WS /ws`: `gps_lat_e7`, `gps_lon_e7`, `imu_heading_deg10`).
- **Spot Lock / Go To Point (R5/R6) wymagają nowego firmware na ESP32** — regulator
  pozycji (station-keeping + nawigacja do punktu) oraz endpoint przyjmujący współrzędne
  celu (rozszerzenie `/api/command` lub nowy endpoint nawigacyjny). To osobny strumień
  pracy firmware, prerequisite dla v2.
- **Tablet jeszcze nie kupiony** — wybór sprzętu wg rekomendacji powyżej.
- Istniejące API (`/api/params`, `/api/command`, `/ws`, envelope `{ data, error }`)
  jest stabilnym kontraktem, do którego aplikacja się podłącza.

## Otwarte pytania

### Do rozwiązania przed planowaniem
- (brak — zakres v1 jest domknięty)

### Odroczone do planowania
- [Dotyczy R1][Techniczne] Stack aplikacji Android (np. Kotlin + Jetpack Compose vs
  alternatywy) i biblioteka map offline (MapLibre/osmdroid + źródło kafli satelitarnych
  i jego licencja).
- [Dotyczy R4][Wymaga researchu] Sposób i format pre-cache map (rozmiar obszaru,
  źródło ortofoto/satelity dające się legalnie cache'ować offline — np. ortofoto
  geoportal vs komercyjne kafle).
- [Dotyczy R2/R7][Techniczne] Próg „utraty linku" (ile ms bez telemetrii = link down)
  i zachowanie reconnect WS.
- [Dotyczy R5/R6][Techniczne] Kontrakt komendy celu (lat/lon e7) między aplikacją
  a ESP32 — do zaprojektowania razem z firmwarem regulatora pozycji (v2).

## Następne kroki
→ `/dev-plan` — planowanie techniczne implementacji v1 (aplikacja: telemetria +
komendy operacyjne + mapa offline z pozycją łodzi). Firmware regulatora pozycji
(Spot Lock / Go To Point) planowany osobno jako prerequisite v2.
