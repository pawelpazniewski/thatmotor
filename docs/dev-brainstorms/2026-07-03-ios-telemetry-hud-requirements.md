---
date: 2026-07-03
topic: ios-telemetry-hud
---

# Telemetria w aplikacji iOS — HUD na wodzie

## Problem
Telemetria jest dziś tylko w panelu WWW — pulpit diagnostyczny „przy biurku". Na wodzie
sterujemy łódką z telefonu (aplikacja iOS już odbiera ten sam strumień WebSocket ~10 Hz i
używa części pól), ale nie widać na nim najważniejszych rzeczy w locie: czy GPS trzyma, czy
silnik jest uzbrojony, w jakim trybie jest łódka, ile do celu, jaka prędkość/kurs. Chcemy
te dane w aplikacji — nowocześnie, czytelnie pod słońcem, **bez zasłaniania mapy**. Dane już
płyną; brakuje prezentacji (i jednej nowej akcji — patrz R5).

## Wymagania

- **R1. Kompaktowy HUD „rzut oka", zawsze widoczny**, w wolnym **lewym górnym rogu**. Pokazuje:
  - jakość GPS: fix + liczba satelitów,
  - stan systemu: DISARMED / ARMED / FAILSAFE,
  - aktywny tryb (spot-lock / goto: off / aktywny / pauza) + dystans (m) + namiar (°) do celu,
  - prędkość nad dnem (m/s) + kurs dziobu (°) z kompasu.
  Mały pionowy „ploter", cyfry monospace, wysoki kontrast.

- **R2. HUD nie może zasłaniać środka mapy.** Siedzi tylko w lewym górnym rogu i nie nachodzi
  na istniejące nakładki: chip połączenia (górny środek), waypointy (góra-prawo), zoom (prawa
  krawędź), pasek sterowania (dół). Środek mapy zostaje czysty.

- **R3. Tapnięcie HUD otwiera natywny dolny arkusz** (uchwyt + detenty pół/pełna wysokość) ze
  szczegółami. Zamknięty — mapa czysta; otwarty — przykrywa tylko na chwilę, chowany gestem.

- **R4. Zawartość arkusza (kurowany zestaw)** — to, co tłumaczy zachowanie łódki w terenie:
  - pełny GPS: fix, satelity, prędkość, pozycja (lat/lon),
  - kompas: kalibracja IMU (0–3) + OK,
  - pełny spot-lock: stan (off/aktywny/pauza), błąd (m), namiar (°),
  - pełne goto: stan, błąd (m), namiar (°), cel (lat/lon), „dotarto",
  - RC valid, świeżość linku aplikacji.
  **Wykluczone** (zostają w panelu WWW): surowe impulsy RC w µs, okresy ramek, servo/esc µs,
  źródło ustawień NVS i flagi ustawień.

- **R5. Regulacja neutrala serwa (trim) z arkusza.** Przyciski −/+ zmieniające trim małymi
  krokami (µs), **efekt na żywo** (można wyzerować ściąganie w trakcie jazdy na wprost),
  bieżący odczyt wartości, zapis do pamięci urządzenia. Działa niezależnie od stanu uzbrojenia,
  ale przy ARMED pokazuje **ostrzeżenie** (bez blokady). Serwo = sterowanie kierunkiem silnika.

- **R6. Obsługa nieświeżości danych.** Gdy link jest stale/rozłączony, HUD i arkusz to
  odzwierciedlają (myślniki / wyszarzenie), nigdy nie pokazują zamrożonych starych liczb jak
  gdyby były aktualne. (Aplikacja ma już `LinkState` z progiem stale ~1 s.)

- **R7. Reużycie istniejącego design systemu** (SunlightTheme): mrożone panele, SF Pro rounded,
  cyfry monospace, kontrast pod słońce, touch-targety ≥44 pt, poszanowanie reduced-motion.

## Kryteria sukcesu
- Na wodzie, jednym spojrzeniem (bez otwierania czegokolwiek) widać: połączenie, kondycję GPS,
  czy uzbrojony, aktywny tryb + dystans do celu, prędkość/kurs.
- Środek mapy nigdy nie jest zasłonięty przez zawsze-widoczny HUD.
- Pełny kurowany zestaw diagnostyki jest o ≤1 tapnięcie i chowa się jednym gestem.
- Neutral serwa da się wyregulować z telefonu, a wartość przeżywa restart urządzenia.
- Przy utracie świeżości HUD widocznie degraduje (myślniki) — żadnych mylących zamrożonych liczb.

## Granice scope'u (non-goals)
- Brak pełnego parytetu z panelem WWW — surowe µs RC, okresy, flagi NVS i źródło ustawień
  zostają wyłącznie w panelu WWW (warsztat/kalibracja przy biurku).
- Brak komendy „arm" z aplikacji — aplikacja pozostaje cienkim klientem (bez zmian).
- Brak nowych pól telemetrii w firmware — konsumujemy istniejący strumień WS (poza potwierdzeniem
  endpointu do ustawiania trimu serwa dla R5).
- Brak wykresów/historii/logów — tylko wartości chwilowe.

## Kluczowe decyzje
- **Dwupoziomowo (HUD + arkusz)**: rzut oka na wierzchu, szczegóły na żądanie. Panel WWW ~38 pól
  to za dużo na telefon na wodzie.
- **Lewy górny róg**: jedyna wolna strefa, idiom morskiego plotera; nie rusza dolnych/bocznych nakładek.
- **Dolny arkusz zamiast pełnego ekranu**: zachowuje kontekst mapy, natywne chowanie gestem.
- **Kurowany zestaw zamiast parytetu**: liczy się istotność w terenie; panel WWW zostaje narzędziem warsztatowym.
- **Trim serwa: na żywo + krokowo (±)**: precyzyjnie, można niwelować ściąganie w jeździe;
  ostrzega, nie blokuje przy ARMED.
- **Chip połączenia (górny środek) zostaje osobny** od nowego HUD — to stan linku, HUD to telemetria.

## Zależności / Założenia
- Strumień WS dostarcza już wszystkie potrzebne pola (potwierdzone inwentaryzacją: `gps_fix`,
  `gps_sats`, `gps_speed_cms`, `imu_heading_deg10`, `imu_calib`, `imu_ok`, `state`, `arm_reason`,
  `spot_lock_state/err_m/bearing_deg10`, `goto_*`, `rc_valid`, `app_link_fresh`, `servo_trim_us`).
- Istnieje firmware'owy endpoint/komenda do ustawiania neutrala serwa (panel WWW już to robi) —
  do zlokalizowania w planowaniu.
- Swiftowy `Telemetry` dekoduje selektywnie — część pól HUD/arkusza może wymagać dołożenia do
  struktury (np. `gps_sats`, `gps_speed_cms`, `imu_calib`, `spot_lock_*`).

## Otwarte pytania

### Do rozwiązania przed planowaniem
- (brak — decyzje produktowe rozstrzygnięte)

### Odroczone do planowania
- [Dotyczy R5][Techniczne] Który endpoint/komenda w firmware ustawia neutral serwa, jaki payload,
  jednostki i limity (panel WWW już to robi — zlokalizować i reużyć).
- [Dotyczy R1/R4][Techniczne] Rozszerzyć Swiftowy `Telemetry` o brakujące pola do dekodowania
  (`gps_sats`, `gps_speed_cms`, `imu_calib`, `imu_ok`, `spot_lock_state/err/bearing`, `arm_reason`).
- [Dotyczy R1][Design] Uniknąć dublowania „tryb + dystans + namiar" między HUD a istniejącą linią
  statusu w pasku goto — zdecydować jedno źródło albo świadomy dublet.
- [Dotyczy R1][Design] Jednostka prędkości (m/s vs km/h) i dokładny układ wierszy/typografia HUD.

## Następne kroki
→ `/dev-plan` do planowania technicznego implementacji
