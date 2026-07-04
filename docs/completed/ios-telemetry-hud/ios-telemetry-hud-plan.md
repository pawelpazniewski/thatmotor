# Plan: Telemetria w aplikacji iOS — HUD + arkusz szczegółów + trim serwa

Branch: `feature/ios-telemetry-hud`
Ostatnia aktualizacja: 2026-07-03

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-07-03-ios-telemetry-hud-requirements.md
- Plan techniczny: docs/plans/2026-07-03-002-feat-ios-telemetry-hud-plan.md

## Podsumowanie wykonawcze

Aplikacja iOS już odbiera strumień telemetrii WebSocket (~10 Hz) i używa części pól, ale nie
prezentuje najważniejszych danych na wodzie. Dodajemy **dwupoziomowy** widok telemetrii, w całości
reużywając istniejącego strumienia i design systemu (SunlightTheme):

1. **Kompaktowy HUD „rzut oka"** w wolnym lewym górnym rogu mapy — jakość GPS, stan systemu,
   aktywny tryb + dystans/namiar do celu, prędkość + kurs.
2. **Dolny arkusz** (`presentationDetents`) otwierany tapnięciem HUD — kurowany zestaw szczegółów
   + regulacja neutrala serwa (trim), aktywna wyłącznie po rozbrojeniu.

Środek mapy pozostaje czysty. Panel WWW zostaje narzędziem warsztatowym (surowe µs / NVS).

## Cele i zakres

**Cele:**
- Rzut oka na wodzie: połączenie, kondycja GPS, stan uzbrojenia, tryb + dystans do celu, prędkość/kurs — bez otwierania czegokolwiek.
- Szczegóły diagnostyczne o ≤1 tapnięcie, chowane gestem.
- Regulacja neutrala serwa z telefonu (po rozbrojeniu), wartość przeżywa restart.
- Widoczna degradacja przy utracie świeżości danych (myślniki, nie zamrożone liczby).

**Poza zakresem:**
- Pełny parytet z panelem WWW (surowe µs RC, okresy, servo/esc µs, NVS/źródło ustawień zostają w panelu WWW).
- Komenda „arm" z aplikacji (cienki klient bez zmian).
- Nowe pola telemetrii / zmiany w firmware (poza konsumpcją istniejących pól).
- Zmiany w gate'cie DISARMED trimu (świadome).
- Wykresy/historia/logi.

## Analiza obecnego stanu

- **Strumień telemetrii** działa: `TelemetrySocket` → `TelemetryStore.latest: Telemetry?` +
  `linkState`. Konsumowane m.in. pozycja, kurs, `gotoState`, dystans/namiar goto.
- **Kontrakt** `Telemetry` NIE dekoduje: `imu_calib`, `spot_lock_err_m`,
  `spot_lock_bearing_deg10`, `servo_trim_us` (`app_link_fresh`, `spot_lock_state` już są).
- **Komendy** `Command`: `goto/gotoCancel/disarm/hold` — brak trimu.
- **Layout** `RootView` ZStack: chip połączenia (górny środek), waypointy (góra-prawo), zoom
  (prawa krawędź), console (dół). **Lewy górny róg wolny.**
- **Firmware trim:** `trim_left/right/save` na `POST /api/command`, krok ±7 µs, zakres ±300 µs,
  **stosowany tylko gdy DISARMED** (przy ARMED 200 OK, ale cicho ignorowane).

## Proponowany stan docelowy

- Nowa nakładka HUD (lewy górny róg), 4 wiersze z żywej telemetrii, myślniki przy stale, tap → arkusz.
- Dolny arkusz z kurowanymi grupami (GPS / kompas / spot-lock / goto / link-RC) + sekcja trimu.
- Czysta logika prezentacji i bramki w `KayakContract` (host-testowalna); widoki cienkie.

## Fazy wdrożenia

Sekwencja: Unit 1 i 2 równolegle → Unit 3 (po 1) → Unit 4 (po 1+3) → Unit 5 (po 1+2+3+4).

### Faza / Unit 1: Rozszerz kontrakt telemetrii (S)
Dołóż `imu_calib`, `spot_lock_err_m`, `spot_lock_bearing_deg10`, `servo_trim_us` z tolerancyjnym
dekodowaniem (`decodeIfPresent` + 0). Computed helpery do prezentacji.
Kryteria akceptacji: nowe pola zdekodowane; brak pól nie wywala dekodera; znak int16 zachowany.

### Faza / Unit 2: Rozszerz słownik komend + AppModel (S)
`Command.trimLeft/.trimRight/.trimSave` (bezparametrowe) → `trim_left/right/save`; metody
`trimLeft()/trimRight()/saveTrim()` w AppModel wzorcem `disarm()`.
Kryteria akceptacji: `httpBody()` produkuje poprawne `{"cmd":...}`; brak `lat/lon` w trimie.

### Faza / Unit 3: Czysty moduł prezentacji `TelemetryDisplay` (M)
Format pól + bramka „wartość vs myślnik" wg świeżości; `isTrimEnabled(state:)`. Test-first.
Kryteria akceptacji: etykiety stanów/trybu poprawne; stale → „—"; testy z mocą wyroczni.

### Faza / Unit 4: Kompaktowy HUD (M)
`HudView` czyta store, składa 4 wiersze przez `TelemetryDisplay`; osobna nakładka ZStack
`.topLeading`; tap → `showTelemetryDetail`. Nie zasłania środka mapy.
Kryteria akceptacji: HUD w rogu, myślniki przy stale, tap otwiera arkusz.

### Faza / Unit 5: Dolny arkusz + sekcja trimu (L)
`TelemetryDetailView` w stylu `WaypointListView`; `.presentationDetents([.medium,.large])`;
kurowane grupy; sekcja trimu `.disabled(!isTrimEnabled(state:))` + notka.
Kryteria akceptacji: grupy widoczne; trim tylko DISARMED z żywym odczytem; myślniki przy stale.

## Ocena ryzyka i mitygacje
- Rozbieżność kluczy JSON firmware → test dekodowania na realnej ramce (Unit 1).
- Target app bez testów → logika w `KayakContract`, widoki cienkie; UI jako [E2E].
- Arkusz `.medium` nie może chować krytycznych akcji na stałe → chowany gestem, sterowanie wraca.

## Mierniki sukcesu
- Na wodzie jednym spojrzeniem: połączenie, GPS, uzbrojenie, tryb+dystans, prędkość/kurs.
- Środek mapy nigdy zasłonięty przez HUD.
- Trim regulowalny z telefonu (DISARMED), wartość przeżywa restart.
- Stale → myślniki, żadnych zamrożonych liczb.

## Zależności
- Istniejący strumień WS i endpoint `/api/command` (firmware bez zmian).
- Target testowy `KayakContractTests` (istnieje).
