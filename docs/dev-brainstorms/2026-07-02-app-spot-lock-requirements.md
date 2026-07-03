---
date: 2026-07-02
topic: app-spot-lock
---

# Spot-lock z aplikacji + latchowany model komend (pilot nadrzędny)

> Zaktualizowano 2026-07-02 po sesji roast (`/zroastuj-mnie`): rozstrzygnięto
> architekturę (jedno źródło `SRC_GOTO`), sposób usunięcia pauzy (chirurgiczny flip),
> lokalizację przechwytu fixu i dwa nowe wymagania (resync-on-resume, usunięcie
> keepalive). Zdjęto z planu nowe źródło firmware i pole telemetrii `target_source`.

## Problem

Aplikacja iOS ma 4 przyciski. Trzy (Goto, STOP, Rozbrój) są zmapowane na firmware;
**Spot-lock jest martwym placeholderem** ("wkrótce"). Operator chce jednym tapnięciem
zakotwiczyć łódź w bieżącej pozycji ("na kotwicy"), analogicznie do fizycznego CH3.

Przy okazji ujawnił się głębszy problem w modelu bezpieczeństwa. Dziś firmware traktuje
**link z aplikacją jako warstwę failsafe**: utrata keepalive (~1,5 s) pauzuje goto
(silnik na luz). Operator tego nie chce. Konkretny use case: **„ustaw punkt, wygaś ekran
telefonu, schowaj go — łódź płynie dalej na zadany punkt"**. Model docelowy: **pilot RC
jako jedyny, nadrzędny failsafe**, a aplikacja jako kanał **zatrzaśniętych (latched)
komend**, których utrata linku (wygaszenie ekranu / tło / restart) nie gubi.

## Wymagania

- **R1.** Przycisk Spot-lock kotwiczy łódź w **bieżącej pozycji w momencie tapnięcia**
  (position-hold, forward-only, bez trzymania kursu dzioba — zgodnie z [[spot-lock-design]]).
- **R2.** Spot-lock to **`goto` z celem = własna pozycja łodzi** (jedno źródło `SRC_GOTO`).
  **Pętla** przechwytuje bieżący fix atomowo w momencie komendy `hold` i angażuje tylko przy
  świeżym realnym fixie; brak fixu → nie angażuje (cicho, `goto_substate` zostaje off).
- **R3.** Tryby autonomiczne (goto **i** spot-lock) to **latchowane intencje trwałe wobec
  utraty linku app** — firmware **NIE pauzuje ani nie neutralizuje** silnika przy
  comms-timeout. (Odwraca dzisiejsze PAUSE-on-link-loss dla `SRC_GOTO`.)
- **R4.** **Pilot RC pozostaje jedynym failsafe/override.** Nadal kończą/wywłaszczają tryb
  app: `rc_valid` failsafe (pilot poza zasięgiem → OFF), override drążkiem, CH3 preempt,
  rozbrojenie. **CH3 `SRC_HOLD` nadal wygrywa** z trybem app. Override drążkiem odpala
  dopiero **poza martwą strefą** (mikro-drgania tolerowane); rzuca kotwicę **trwale, bez
  auto-powrotu** — wznowienie wymaga ponownego tapnięcia w app.
- **R5.** **Jeden cel autonomiczny naraz** (ten sam latch `s_goto_engage`). Tap spot-lock
  podczas goto → nadpisuje cel bieżącą pozycją. Tap goto podczas spot-locka → nadpisuje
  cel tapniętym punktem. **STOP anuluje aktywny tryb app** (uniwersalny off = `goto_cancel`);
  STOP **nie rusza** fizycznej kotwicy pilota (CH3). **Rozbrój = twardy kill** (ubija silnik,
  wymaga ponownego zazbrojenia z pilota).
- **R6.** Wejście w spot-lock wymaga **ARMED + świeży realny fix GPS + oba drążki na zerze**.
  App **pre-waliduje fix ze swojej telemetrii WS**: przycisk Spot-lock **wyszarzony/ostrzega
  gdy brak fixu**, zanim operator tapnie. Blokady „nie zazbrojono / drążki nie na zerze"
  odbijają się przez telemetrię (spójnie z goto), nie przez synchroniczny błąd.
- **R7.** App **etykietuje tryb lokalnie** na podstawie wciśniętego przycisku („Kotwica" vs
  „Jadę do celu"). Przejęcie przez pilota widać z istniejącej telemetrii (`goto_substate=off`
  + `spot_lock_substate=active` → „pilot przejął"). **Bez nowego pola telemetrii.** Po
  force-quit (utrata lokalnej intencji) app używa neutralnej etykiety „Trzymam pozycję".
- **R8.** App **resync po powrocie** — po wygaszeniu ekranu / tle / restarcie aplikacja
  odczytuje z telemetrii, że tryb autonomiczny **wciąż trwa** (albo że pilot go zakończył),
  i **odtwarza UI zgodnie z prawdą firmware** — nigdy nie zakłada „anulowano".
- **R9.** App **nigdy nie uzbraja** (bez zmian). Spot-lock dostępny tylko gdy już ARMED.

## Kryteria sukcesu

- Tap Spot-lock przy ARMED + neutral + fix → łódź trzyma bieżący punkt w promieniu
  deadband; w martwej strefie silnik na luz.
- **Wygaszenie ekranu / tło podczas spot-locka** → łódź **dalej trzyma**; po powrocie do app
  UI pokazuje tryb wciąż aktywny (R8).
- **Wygaszenie ekranu / tło podczas goto** → łódź **dalej płynie** do celu (happy path).
- Ruch drążkiem poza martwą strefę / CH3 / rozbrojenie / wyjście pilota poza zasięg → tryb
  app kończy się natychmiast (pilot nadrzędny), bez auto-powrotu.
- **STOP anuluje aktywny spot-lock** (nie tylko goto); **Rozbrój** ubija silnik.
- Tap Spot-lock bez fixu GPS → przycisk wyszarzony/ostrzega; łódź nie kotwiczy w null-island.

## Granice scope'u

- **Brak trzymania kursu dzioba** — position-only (fundament [[spot-lock-design]]).
- **Brak geofence dla spot-locka** — kotwica = bieżąca pozycja, z definicji wewnątrz fence.
- Brak strojenia parametrów spot-locka z aplikacji (non-goal v1, jak w goto).
- Brak tras / multi-waypoint.
- Nie dotykamy warstwy uzbrajania (arm tylko z RC).
- **Bez nowego źródła firmware i bez nowego pola telemetrii** — świadomie odrzucone (R2/R7).

## Kluczowe decyzje

- **Model latchowanych komend, pilot RC = jedyny failsafe** — wybrany świadomie; **zastępuje**
  wcześniejszą własność „utrata linku → PAUSE" dla goto. Uzasadnienie: RC failsafe
  (`rc_valid` + drążki neutral + ARMED) już jest nadrzędny i wystarczający; keepalive-PAUSE
  był redundantną warstwą, która blokowała pożądany use case „wygaś ekran i płyń".
  Rezydualne ryzyko przyjęte świadomie: telefon do wody podczas dalekiego goto → łódź
  dopłynie, kill tylko z pilota.
- **Jedno źródło `SRC_GOTO`, spot-lock = `goto(własny fix)`** — brak nowego źródła w firmware.
  Cała ścieżka persist / retencja celu / CH3-preempt / override-clear działa za darmo; R5
  (jeden cel, wzajemny preempt) wychodzi gratis z tego samego latcha.
- **Chirurgiczny flip pauzy, watchdog zostaje jako świeżość retargetu** — zmieniamy tylko
  `comms_gated=false` dla goto w `hold_or_pause`. `comms_fresh` **zostaje** jako bramka
  re-latchu (ochrona null-island + zmiana celu w locie). NIE wycinamy maszynerii watchdogu
  (rip-out zepsułby retarget-w-locie i wymagałby nowego mechanizmu).
- **Fix łapie pętla, app pre-sprawdza GPS** — atomowy przechwyt bez skew i bez nowej
  zależności `web_panel→gps`; feedback „brak GPS" daje app ze swojej telemetrii (wyszarzenie).

## Zależności / Założenia

- **Wymagane zmiany firmware:**
  1. Nowa komenda `hold` w `command_parse` (`components/web_panel`) → flaga `hold_request`
     (bez współrzędnych, host-testowalna).
  2. `http_server`: gałąź dla `hold_request` (nie wchodzi ścieżką `extract_goto_target`,
     bo brak body z coords).
  3. `control_loop` (`apply_goto_events`): na `hold_request` zrób `gps_get_state()`; przy
     `fresh && fix` ustaw `s_goto_lat/lon = własna pozycja`, `s_goto_engage=true`, stampuj
     `s_last_goto_ms`; inaczej nie angażuj.
  4. `spot_lock.c`: flip `hold_or_pause(..., comms_gated=false)` dla goto. Prze-dokumentować
     `comms_fresh` z „link failsafe" na „świeżość retargetu"; `app_link_fresh` zostaje jako
     wskaźnik zdrowia linku w UI (nie bramka sterowania).
- **Wymagane zmiany iOS:**
  1. Zmapować przycisk Spot-lock na komendę `hold` (dziś placeholder `requestSpotLock`).
  2. **Usunąć logikę „trzymaj ekran włączony"** (`KeepaliveDecision.shouldDisableIdleTimer`)
     i resend ~2 Hz — zbędne i sprzeczne z „wygaś ekran i płyń".
  3. **Resync-on-resume** (R8) — po powrocie z tła odczytać stan z telemetrii.
  4. Wyszarzać/ostrzegać Spot-lock gdy brak fixu (R6); etykieta lokalna (R7).
- Licznik świeżości GPS + silnik `spot_lock_step` już dostarczone (goto, 2026-07-01).
- **Zakłada, że pilot RC jest zawsze przy operatorze** — to on jest kill-switchem gdy app
  zniknie. Gdy RC też padnie/wyjdzie z zasięgu → `rc_valid` failsafe wymusza OFF.

## Otwarte pytania

### Do rozwiązania przed planowaniem
- (brak — decyzje produktowe i architektoniczne rozstrzygnięte w roaście)

### Odroczone do planowania
- [Dotyczy R2][Techniczne] Profil gazu kotwicy: na start reużyj cruise-decel `SRC_GOTO`
  (błąd mały → ≈ P-throttle); łagodniejszy powrót przy dużym dryfie to strojenie
  `goto_slowdown_distance_m`, nie nowa gałąź.
- [Dotyczy R8][Techniczne] Szczegóły resync: iOS lifecycle (`scenePhase`), ponowne
  otwarcie WS, odtworzenie pinezki celu z ostatniej telemetrii.
- [Techniczne][Dokumentacja] Aktualizacja materiałów, które ta zmiana **zastępuje**:
  `docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md`
  (rationale PAUSE→CONTINUE), `2026-07-01-ios-app-requirements.md`, known-issues §4d.

## Następne kroki

→ `/dev-plan` do planowania technicznego implementacji (firmware + iOS).
