# Plan: Aplikacja iOS — mapa offline + tap-to-goto (goto)

**Branch:** `feature/ios-app-goto-navigation`
**Ostatnia aktualizacja:** 2026-07-01

## Cele i zakres

Natywna aplikacja iOS (SwiftUI, iPhone, iOS 17+) będąca **cienkim klientem** gotowego
firmware'u ESP32 (`kayak-motor`). Operator dotyka punkt na mapie offline i każe łódce
tam dopłynąć (point-and-shoot), widząc na żywo pozycję i kurs łodzi z telemetrii WS
~10 Hz. Cała logika ruchu i bezpieczeństwa zostaje w firmware; aplikacja wysyła jeden
cel `lat/lon`, utrzymuje keepalive ~2 Hz i pokazuje stan.

**MVP brutalnie chudy** — tylko rdzeń nawigacji + waypointy. Projekt startuje od
**bramki de-risk** (Unit 0): zielone → reszta planu; czerwone → stop i rozmowa o BLE.

### Kluczowe decyzje (z /dev-plan)
- **Lokalizacja projektu:** podkatalog `ios/KayakMotor/` w tym repo.
- **Bramka de-risk jako Unit 0** — nic dalej zanim łączność nie jest zielona na realnym iPhonie.
- **Testy: Swift Testing** (`@Test`/`#expect`) dla czystej logiki.
- **Transport:** HTTP przez `URLSession` (fallback `NWConnection`/`.wifi`); WS przez
  **Network.framework pinowany do `.wifi`**.
- **Konwersja:** `e7 = Int32((deg*1e7).rounded())`, `deg = Double(e7)/1e7`; wysyłamy int32.
- **Cienki klient** — zero duplikacji regulatora/bramek (są w firmware).

### Kontrakt firmware (zweryfikowany w kodzie)
- AP `kayak-motor` (WPA2, `CHANGE-ME-kayak`), **max 1 klient**, `http://192.168.4.1:80`, brak CORS/auth.
- WS `GET /ws` ~100 ms (~10 Hz): `state`(0=DISARMED,1=ARMED,2=FAILSAFE,3=ESC_CALIBRATION,4=DEPLOY),
  `arm_reason`(0-4), `rc_valid`,`gps_fix`,`gps_sats`,`gps_lat_e7`,`gps_lon_e7`,`gps_speed_cms`,
  `imu_ok`,`imu_heading_deg10`(deg×10), `goto_state`(0=OFF,1=ACTIVE,2=PAUSED),
  `goto_target_lat_e7`,`goto_target_lon_e7`,`goto_err_m`,`goto_bearing_deg10`,`goto_arrived`,
  `app_link_fresh`,`spot_lock_state`.
- `POST /api/command` (body ≤256 B, JSON): `{"cmd":"goto","lat_e7":<i32>,"lon_e7":<i32>}`
  (walidacja double PRZED castem; ±90/±180 e7; poza → 400), `{"cmd":"goto_cancel"}`,
  `{"cmd":"disarm"}`. Koperta: 200 `{"data":null,"error":null}`; 400 `{"data":null,"error":{"code","message"}}`.
- Comms-watchdog `GOTO_COMMS_TIMEOUT_MS_DEFAULT=1500` → app ponawia `goto` co ~1000 ms (≈2 Hz);
  utrata linku → goto PAUSED (retencja celu), nigdy disarm.

## Fazy i zadania

### Faza 0 — Bramka de-risk
- **Unit 0:** Bramka łączności (throwaway app) — dowód HTTP+WS do `192.168.4.1` po Wi‑Fi
  przy aktywnym LTE. Gate go/no-go. **(R1)**

### Faza 1 — Fundament klienta
- **Unit 1:** Szkielet projektu Xcode, zależności (MapLibre SwiftPM), uprawnienia. **(R1, R2, R10)**
- **Unit 2:** Modele kontraktu + konwersja współrzędnych (pure, tested). **(R2, R3, R8)**
- **Unit 3:** Warstwa sieciowa — join AP, wymuszenie Wi‑Fi, HTTP, WS pinowany. **(R1, R9)**
- **Unit 4:** Store telemetrii + bramkowanie świeżości + status/HUD (stan i przyczyny). **(R2, R8, R9)**

### Faza 2 — Mapa i nawigacja
- **Unit 5:** Mapa offline (kontur + marker łodzi + jakość GPS). **(R2, R10)**
- **Unit 6:** Tap-to-goto (pin, linia, wysłanie celu, err/bearing). **(R3)**
- **Unit 7:** Keepalive + STOP/Rozbrój + idle-timer + arrived→CH3. **(R5, R6)**

### Faza 3 — Waypointy i barierki
- **Unit 8:** Waypointy (zapis realnej pozycji, trwała lista, re-send). **(R4)**
- **Unit 9:** Miękkie ostrzeżenie geofence (R7) + polish słoneczny (R10). **(R7, R10)**

## Kryteria akceptacji (MVP — ze źródła)
- Bramka de-risk zaliczona: natywna apka niezawodnie gada z `192.168.4.1` po Wi‑Fi przy LTE.
- Bez internetu kontur akwenu renderuje się; pozycja/kurs łodzi żyją ~10 Hz.
- Tap + „Płyń do punktu" startuje goto (ARMED+fix+neutral); pin, linia, malejąca
  odległość; „Anuluj"/STOP natychmiast przerywają.
- Zapis pozycji łódki jako waypoint + re-send działa i przeżywa restart.
- Utrata linku → apka pokazuje pauzę (`goto_state=2`, `app_link_fresh=false`), po powrocie
  wznawia; nic nie „udaje" żywego.
- Gdy goto nie rusza — apka jasno mówi dlaczego (stan/fix/drążki/link).
- STOP zawsze dostępny i natychmiastowy; osobny Rozbrój dostępny.

## Ryzyka i mitygacje
- **Ryzyko binarne (Unit 0):** brak niezawodnego dostępu do `192.168.4.1` → Plan B = BLE
  (dotyka firmware, POZA MVP). Cały plan zależny od zielonej bramki.
- **Bug DHCP iOS (~130 s):** duże timeouty + fallback manual-join.
- **Deprioryzacja bez-internetowego Wi‑Fi:** pin `.wifi` + `prohibitExpensivePaths`, foreground.
- **MapLibre/hotspot/WS tylko na realnym urządzeniu** — E2E wymaga iPhone'a.
- **Max 1 klient AP** — nie trzymać panelu WWW i apki połączonych jednocześnie.

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-07-01-ios-app-requirements.md
- Plan techniczny: docs/plans/2026-07-01-002-feat-ios-app-goto-navigation-plan.md
