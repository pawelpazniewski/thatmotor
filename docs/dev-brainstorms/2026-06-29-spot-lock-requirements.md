---
date: 2026-06-29
topic: spot-lock
---

# Spot-Lock — automatyczne utrzymywanie pozycji

## Problem
Operator kajaka chce zatrzymać się w jednym miejscu na wodzie (np. do wędkowania,
robienia zdjęć, odpoczynku) bez ciągłego korygowania drążkami i bez kotwicy.
Wiatr i prąd spychają kajak z miejsca. Spot-lock ma autonomicznie sterować silnikiem,
żeby utrzymać zapamiętany punkt GPS, na żądanie operatora (przełącznik CH3),
z zachowaniem pełnej kontroli nad bezpieczeństwem.

Sprzęt jest już przygotowany: GPS NEO-M9N i kompas BNO085 działają jako źródła danych
(diagnostyka, poza failsafe), a kanał **CH3 (GPIO8) jest zarezerwowany** jako przełącznik
spot-lock poza torem RC_valid. Brakuje warstwy logiki utrzymania pozycji.

## Kluczowe ograniczenie (fundament całej logiki)
Kajak jest **nieholonomiczny**: nie potrafi przesuwać się w bok. Porusza się tylko
tam, gdzie patrzy dziób (servo kierunku + ESC ciąg), a ster ma władzę **wyłącznie gdy
łódka płynie** (jest przepływ wody po piórze) — stojąc w miejscu, ster nie działa.

Z tego wynikają dwie konsekwencje, które definiują całą logikę:

1. **Korekta pozycji wymaga wycelowania dzioba w punkt i dopłynięcia do niego** —
   nie da się wrócić „w bok" bez obrócenia dzioba. Dlatego spot-lock to **jedna pętla**:
   celuj dziobem w punkt → jedź przodem proporcjonalnie do odległości.
2. **Niezależne trzymanie kursu dzioba jest na tym sprzęcie niewykonalne.** W spoczynku
   (gdy pozycja jest OK) silnik stoi → ster nie ma władzy → nie da się ustawić dzioba.
   Dlatego **spot-lock NIE trzyma kursu** — kierunek dzioba jest efektem ubocznym
   dopływania do punktu. Decyzja operatora: ważne, żeby łódka nie wirowała, a nie żeby
   dziób patrzył w konkretną stronę.

## Strategia sterowania
- **Tylko przód.** Spot-lock nigdy nie używa ciągu wstecznego (przy jednym pchającym
  silniku i słabym sterze wsteczny jest niesterowalny; anti-plugging dwell i tak wprowadza
  opóźnienia). Gdy punkt znajdzie się za rufą — łódka wykonuje **jeden łagodny zawrót**
  i wraca przodem.
- **Gaz tylko gdy dziób wycelowany.** Ciąg do przodu podawany jest wyłącznie gdy dziób
  jest w stożku **~±60°** względem kierunku do punktu. Gdy punkt jest z boku/z tyłu —
  łódka najpierw łagodnie obraca się minimalnym pełzaniem, potem dodaje ciągu. Chroni
  przed jazdą bokiem na pełnej mocy i przed donutami.
- **Łagodnie + wygaszanie w strefie.** Niski limit gazu i martwa strefa 2–3 m sprawiają,
  że łódka wytraca pęd w strefie zamiast przez nią przelatywać — to zabija pętlę
  przestrzeliwania.

## Wymagania

- **R1. Aktywacja przez CH3 z zapisem celu „tu i teraz".**
  Przełączenie CH3 w ON (próg pulse, z debounce analogicznym do CH4) włącza spot-lock.
  W momencie włączenia system robi snapshot: **bieżąca pozycja GPS** (lat/lon) jako punkt
  docelowy. Kursu nie zapamiętujemy — spot-lock go nie trzyma.

- **R2. Utrzymanie pozycji (jedna pętla).**
  - Gdy błąd pozycji jest **w martwej strefie** (R6): **silnik na luz** (ESC neutral),
    łódka dryfuje spokojnie. Nie próbujemy trzymać kierunku dzioba w spoczynku.
  - Gdy błąd pozycji **przekracza martwą strefę**: kontroler liczy kierunek do punktu,
    obraca dziób w jego stronę (servo proporcjonalnie do błędu kierunku) i — gdy dziób
    jest w stożku ±60° — daje ciąg do przodu proporcjonalny do odległości.
  - Kierunek dzioba **nie jest osobnym celem** — ustawia się sam podczas dopływania.
    Cel projektowy: łódka utrzymuje się w okolicy punktu i nie wiruje.

- **R3. Warunki wejścia (wszystkie muszą być spełnione).**
  - Spot-lock włącza się **tylko gdy system jest ARMED**. CH3 ON w stanie DISARMED nie
    uruchamia trybu.
  - Wymagany **ważny i świeży fix GPS** w momencie aktywacji (patrz R5: świeżość ≤ ~1,5 s).
    Bez fixu lub przy zamarłym sygnale — brak wejścia.
  - **Oba drążki (gaz CH2 i ster CH1) muszą być w neutralu** (w martwej strefie drążka).
    Wejście z wychylonym drążkiem jest blokowane — inaczej spot-lock zostałby natychmiast
    przerwany przez własny mechanizm override (R4).

- **R4. Odzyskanie kontroli ręcznej (dwa niezależne mechanizmy).**
  - **CH3 OFF → natychmiast manual:** pełna kontrola wraca do drążków RC (CH1/CH2).
    Główny „abort".
  - **Ruch drążkiem przerywa:** wychylenie gazu lub steru **poza martwą strefę drążka**
    (tę samą, której używa tor manualny) natychmiast przerywa tryb (override) i oddaje
    kontrolę manualną. Bez osobnego progu override.

- **R5. Świeżość sensorów i degradacja przy utracie GPS.**
  - GPS dostaje **licznik świeżości** (analogicznie do kompasu BNO085, który już go ma):
    „utrata GPS" = `fix` zniknął **lub** brak nowej pozycji przez ~1,5 s.
  - Przy utracie GPS w trakcie trzymania: natychmiast **ESC → neutral (stop),
    servo → center**. Tryb przechodzi w stan **„paused"**, pozostając aktywny. Gdy dane
    wracają (świeży fix) — wznawia trzymanie tego samego celu.
  - Brak timeoutu wyjścia (kajak driftuje bezpiecznie, ale nie szarpie). Granicą czasową
    pozostaje istniejący failsafe RC (utrata RC → FAILSAFE → silnik neutral).
  - Kompas: spot-lock potrzebuje bieżącego heading do liczenia, w którą stronę kręcić
    dziób. Sprawdzono w terenie, że silnik **nie zakłóca** kompasu, więc heading jest
    wiarygodnym wejściem sterowania. Utrata świeżego heading (`imu_ok=false`) traktowana
    jak utrata GPS → neutral + paused.

- **R6. Martwa strefa pozycji (deadband).**
  Konfigurowalny promień wokół punktu (rząd 2–3 m), w którym silnik nie koryguje pozycji
  (ESC neutral). Chroni przed ciągłym szarpaniem przy szumie GPS NEO-M9N (~1–2 m).
  Default startowy: **~3 m**.

- **R7. Limit maksymalnego gazu (authority cap).**
  Spot-lock nigdy nie przekracza konfigurowalnego maksymalnego ciągu ESC. Autonomiczny
  tryb nie może pojechać na pełnej mocy. Default startowy: **~35%** zakresu (do strojenia
  w terenie — wyżej, jeśli nie pokonuje wiatru).

- **R8. Parametry konfigurowalne w panelu web.**
  Promień martwej strefy, max gaz, agresywność (gainy regulatora) edytowalne w panelu jak
  reszta settings — z zachowaniem reguły SI-6 (pending params stosowane tylko gdy DISARMED).
  Wbudowane sensowne wartości domyślne.

- **R9. Status spot-lock w telemetrii.**
  Panel pokazuje: stan (off / aktywny / paused), błąd pozycji [m], kierunek do punktu [°]
  vs bieżący dziób [°]. Do diagnostyki i strojenia na wodzie.

## Kryteria sukcesu
- Po włączeniu CH3 przy ARMED + świeży fix GPS + drążki na zerze, w warunkach spokojnych
  kajak **utrzymuje się w okolicy zapamiętanego punktu** (rząd kilku metrów) bez interwencji
  operatora.
- Przy zepchnięciu poza martwą strefę kajak wraca w jej okolice; gdy punkt jest za rufą,
  wykonuje **jeden łagodny zawrót**, nie wiruje w kółko.
- CH3 OFF oraz ruch drążkiem natychmiast (≤ jeden cykl pętli) przywracają sterowanie manualne.
- Utrata świeżego fixu GPS (lub heading) skutkuje stopem silnika (neutral), bez szarpania;
  powrót danych wznawia tryb.
- Spot-lock nigdy nie przekracza ustawionego limitu gazu i nigdy nie obchodzi failsafe RC.
- Logika decyzyjna pokryta host-testami (Unity) bez zależności od sprzętu.

## Granice scope'u (non-goals)
- **Brak nawigacji do waypointów / cruise / podążania trasą** — tylko trzymanie jednego
  zapamiętanego punktu.
- **Brak ustawiania celu z panelu** — cel zawsze „tu i teraz" w momencie CH3 ON (nie ręczne
  lat/lon w v1).
- **Brak jog/nudge** (przesuwania punktu o krok) w v1.
- **Brak trzymania kursu dzioba** — fizycznie niewykonalne na tym sprzęcie w spoczynku;
  kierunek dzioba jest efektem ubocznym (patrz Kluczowe ograniczenie).
- **Brak ciągu wstecznego** — spot-lock działa tylko przodem.
- **Brak zmiany istniejącego failsafe i toru RC_valid** — spot-lock jest sub-trybem w obrębie
  ARMED (flaga, nie nowy stan maszyny) i ustępuje failsafe.

## Kluczowe decyzje
- **Cel utrzymania = wyłącznie pozycja, jedna pętla „celuj dziobem w punkt + jedź przodem":**
  jedyny stabilny sposób przy jednym aktuatorze i nieholonomicznej łódce. Trzymanie kursu
  odrzucone jako fizycznie niewykonalne (ster bez władzy w spoczynku).
- **Tylko przód, point-and-shoot, gaz w stożku ±60°:** chroni przed jazdą bokiem i donutami;
  przy punkcie za rufą jeden łagodny zawrót.
- **Cel „tu i teraz" (snapshot pozycji przy CH3 ON):** najprostszy i najbardziej naturalny UX,
  zero interakcji z telefonem na wodzie.
- **Maksymalnie restrykcyjne wejście (ARMED + świeży fix GPS + drążki na zerze) i dwa
  mechanizmy abort (CH3 OFF + stick override przez martwą strefę drążka):** autonomiczne
  sterowanie silnikiem traktowane jak funkcja safety-critical.
- **Utrata sensora = neutral + paused, bez timeoutu:** preferencja „nie szarpać" nad
  „nie driftować"; bezpieczeństwo czasowe domyka failsafe RC.
- **Spot-lock jako flaga w ARMED, nie nowy sm_state:** nie ruszamy maszyny stanów ani jej
  testów; spot-lock dokłada nowe tryby celu do signal_chain (computed throttle/servo).
- **Strojenie i limity w panelu (SI-6):** spot-lock wymaga strojenia w terenie, ale zmiana
  parametrów tylko w DISARMED — spójne z resztą systemu.

## Zależności / Założenia
- Reużycie istniejących komponentów: `gps` (gps_state: lat/lon/fix — **do dorobienia licznik
  świeżości**), `imu` (heading + `imu_ok`), `rc_capture` (CH3 — już zarezerwowany na GPIO8),
  `rc_validity`/`ch4_switch` (wzorzec debounce do **wspólnego modułu** dla CH3+CH4),
  `state_machine` (ARMED/FAILSAFE), `signal_chain` (nowe tryby celu), `pwm_out` (hard clamp
  SI-3), `control_loop` (50 Hz, loop_step pure logic), `settings` (SI-6 pending params),
  telemetria (control_loop_snapshot).
- Założenie (zweryfikowane): BNO085 daje wiarygodny heading — silnik nie zakłóca kompasu.
- Założenie: dokładność i częstotliwość GPS NEO-M9N wystarcza dla martwej strefy ~2–3 m.
- Spot-lock działa wyłącznie wewnątrz stanu ARMED i nigdy nie ma priorytetu nad FAILSAFE.

## Otwarte pytania

### Do rozwiązania przed planowaniem
- (brak — decyzje produktowe rozstrzygnięte)

### Odroczone do planowania
- [Dotyczy R2][Techniczne] Struktura pętli: PID/P-regulator błędu pozycji → bearing+throttle,
  z bramką stożka ±60° na throttle. Czysta funkcja `spot_lock_step()` do host-testów (Pure ⊥ HAL).
- [Dotyczy R2][Techniczne] Reprezentacja stanu: flaga spot-lock w obrębie ARMED + nowe tryby
  celu servo/throttle w signal_chain (computed value).
- [Dotyczy R1,R2][Techniczne] Liczenie błędu pozycji i kierunku do celu: konwersja lat/lon
  (scaled 1e7) na metry i bearing (płaska aproksymacja na małych dystansach). Float dozwolony
  wewnątrz czystej funkcji (ESP32-S3 ma FPU); telemetria zostaje na intach.
- [Dotyczy R5][Techniczne] Licznik świeżości GPS — dorobić do `gps` analogicznie do
  `IMU_STALE_AFTER_MS`; próg ~1,5 s.
- [Dotyczy R4][Techniczne] Debounce CH3 — wyciągnąć wzorzec `ch4_switch` do wspólnego modułu
  switch-debounce używanego przez CH3 i CH4.
- [Dotyczy R6,R7,R8][Wymaga researchu] Domyślne wartości startowe (deadband ~3 m, max gaz ~35%,
  gainy) — do strojenia w terenie.

## Rekomendacja realizacji (de-risking)
Po uproszczeniu scope'u (tylko pozycja) etapowanie jest mniej krytyczne, ale wciąż zalecane:
najpierw stabilna pętla position-hold na łagodnych nastawach (duża martwa strefa, niski gaz),
weryfikacja braku donutów i zachowania przy punkcie za rufą, potem zacieśnianie nastaw.
Bramka „silnik nie zakłóca kompasu" — **zaliczona** (zweryfikowane w terenie).

## Następne kroki
→ /dev-plan do planowania technicznego implementacji
