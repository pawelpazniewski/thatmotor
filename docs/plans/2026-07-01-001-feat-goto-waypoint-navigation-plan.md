---
title: "feat: Goto — autonomiczna nawigacja do punktu z aplikacji (WiFi)"
type: feat
status: active
date: 2026-07-01
origin: docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md  # rozszerzenie non-goala „nawigacja do waypointów"
---

# feat: Goto — autonomiczna nawigacja do punktu z aplikacji (WiFi)

## Przegląd

Dodajemy tryb **goto**: aplikacja (iOS, SwiftUI + MapLibre) wysyła przez WiFi cel
`lat/lon`, a firmware autonomicznie kieruje kajak do tego punktu i utrzymuje go tam.
Feature **reużywa istniejący silnik spot-lock** (`spot_lock_step`, point-and-shoot,
stożek ±60°, tylko przód, deadband, cap) — jedyna różnica to **źródło celu**: zamiast
snapshotu bieżącej pozycji (CH3) cel przychodzi z zewnątrz. Dokładamy trzy rzeczy:
(1) kanał celu przez `POST /api/command {"cmd":"goto",...}`, (2) **comms-watchdog** na
link aplikacji (utrata linku → pauza, jak przy utracie GPS), (3) arbitraż z CH3 i
strukturalne pierwszeństwo manualne/failsafe. Cała nowa logika decyzyjna pozostaje
czystą funkcją host-testowaną (Pure ⊥ HAL); maszyna stanów i tor `rc_valid`/failsafe
pozostają nietknięte. Sama aplikacja iOS jest poza scope tego planu (osobny artefakt).

## Ujęcie problemu

Operator chce wskazać punkt na mapie w telefonie i kazać łódce tam dopłynąć
(point-and-shoot), bez ciągłego sterowania drążkami — np. dopłynięcie do łowiska.
Spot-lock (v1) rozwiązał utrzymanie **bieżącej** pozycji na przełącznik CH3, ale w
swoich non-goalach explicite wykluczył „nawigację do waypointów" i „ustawianie celu
z panelu / ręczne lat/lon" (zob. źródło: „Granice scope'u (non-goals)"). Ten plan
realizuje dokładnie tę odroczoną funkcję.

Kluczowa różnica względem spot-locka jest **safety**: przy spot-locku źródłem
sterowania jest fizyczny nadajnik RC (CH3), a failsafe RC domyka bezpieczeństwo
czasowe. Przy goto **aplikacja staje się źródłem sterowania**, a obecny failsafe jest
wyłącznie RC-owy — dlatego autonomiczny przejazd wymaga własnego watchdoga linku i
jednoznacznego pierwszeństwa manualnego. Fundament fizyczny (nieholonomiczna łódka,
ster bez władzy w spoczynku) i strategia sterowania są identyczne jak w spot-locku i
nie są tu ponownie rozstrzygane — reużywamy sprawdzony silnik.

## Śledzenie wymagań

- **R1. Kanał celu z aplikacji.** `POST /api/command {"cmd":"goto","lat_e7":<int32>,"lon_e7":<int32>}`
  ustawia zewnętrzny cel nawigacji. Współrzędne jako `degrees × 1e7` (bez floatów na łączu,
  spójne z `gps_lat_e7`). Walidacja zakresu (±90°/±180°); niepoprawne → `400` w kopercie
  `{data,error}`. Druga komenda `{"cmd":"goto_cancel"}` kończy tryb.
- **R2. Nawigacja + utrzymanie.** Reużyj silnik spot-lock: obróć dziób do celu (servo ∝ błąd
  kierunku), ciąg do przodu ∝ odległość **tylko** w stożku ±60°, cap gazu (R7 spot-locka),
  tylko przód. Po wejściu w deadband → neutral+center; tryb pozostaje **ACTIVE** (dojście = hold
  w celu). Punkt za rufą → jeden łagodny zawrót (efekt bramki ±60°, jak w spot-locku).
- **R3. Aktywacja goto (z aplikacji).** Warunki (wszystkie): `ARMED` + **oba drążki w neutralu** +
  świeży fix GPS + świeży heading + odebrana komenda `goto`. **Bez fizycznego CH3** — goto jest
  źródłem celu `SRC_GOTO`.
- **R4. Arbitraż z CH3 (priorytet fizyczny).** CH3 zawsze wygrywa z goto:
  - CH3 ON w trakcie goto → **przerwij goto** i przejdź w **hold „tu i teraz"** (snapshot bieżącej
    pozycji, spot-lock jak dziś). Kasuje latch goto.
  - Gdy goto nieaktywne, CH3 działa jak obecnie (spot-lock hold pozycji).
- **R5. Comms-watchdog linku aplikacji.** Aplikacja podtrzymuje link keepalive'em (~2 Hz, ponowny
  `goto` lub lekki heartbeat). Brak świeżości linku > `goto_comms_timeout_ms` (~1,5 s) → **PAUSED**
  (neutral+center, cel zapamiętany); powrót linku → wznowienie do tego samego celu. Reużyj
  wrap-safe `sensor_is_fresh`. **Tylko** tryb `SRC_GOTO` jest bramkowany świeżością linku
  (spot-lock/CH3 nie — jest RC-owy).
- **R6. Pierwszeństwo manualne i failsafe (jedna bramka, bez nowego toru).** goto liczone
  **wyłącznie** gdy `sm.state == ARMED` (istniejąca bramka w `resolve_spot_lock`), więc
  bezwarunkowo ustępuje `FAILSAFE`/`DISARMED`. Ruch gazu lub steru poza martwą strefę drążka →
  natychmiast manual **i skasowanie latcha goto** (brak auto-resume po powrocie drążka do neutralu).
  `goto_cancel` → OFF. Żaden nowy tor nie dotyka `rc_valid`/`sm_inputs`.
- **R7. Parametry (SI-6).** `goto_comms_timeout_ms` konfigurowalny (apply tylko w DISARMED).
  Nastawy ruchu (deadband, max gaz, gainy) **reużyte ze spot-locka** (jeden zestaw). Ewentualny
  osobny cap goto — odroczony do strojenia.
- **R8. Telemetria goto.** WS + panel: sub-stan goto (off/active/paused), cel `lat/lon`, błąd `err_m`,
  bearing do celu `deg10`, `arrived` (błąd ≤ deadband), świeżość linku. Do diagnostyki/strojenia i
  jako kontrakt dla aplikacji iOS.

## Granice scope'u (non-goals)

- **Brak tras / wielu waypointów / sekwencji** — jeden aktywny cel; nowy `goto` zastępuje poprzedni.
- **Brak omijania przeszkód / planowania trasy** — jazda po linii prostej (point-and-shoot).
  Odpowiedzialność operatora; watchdog ogranicza „jazdę na ślepo" do timeoutu.
- **Brak ciągu wstecznego** — jak spot-lock, tylko przód.
- **Brak trzymania kursu dzioba po dojściu** — jak spot-lock, hold pozycji, nie kursu.
- **Brak zmiany maszyny stanów, toru `rc_valid` ani istniejącego failsafe RC.**
- **Brak zmian w rdzeniu regulatora spot-locka** poza dodaniem źródła celu i bramki linku —
  matematyka ruchu (geo_math, ±60°, P-control) reużyta bez modyfikacji zachowania dla CH3.
- **Aplikacja iOS (SwiftUI + MapLibre, offline-mapy, Network.framework)** — poza tym planem;
  osobny artefakt. Tu definiujemy tylko firmware'owy kontrakt API, z którego ona korzysta.
- **Brak persystencji celu w NVS** — cel żyje w RAM, znika po reboocie (goto to sesja live).

## Kontekst i research

### Relevantny kod i wzorce

- **Silnik spot-lock (rdzeń do reużycia):** `components/control_loop/include/spot_lock.h`,
  `components/control_loop/src/spot_lock.c` — `spot_lock_step()`. Stan `spot_lock_state{ substate,
  ref_lat_e7, ref_lon_e7 }` (spot_lock.h:63-68) już trzyma dowolny cel; wejście robi snapshot na
  `ch3_edge_on` (spot_lock.c: ustawia `ref_*` = bieżąca pozycja). Sub-stany `OFF/ACTIVE/PAUSED`
  (spot_lock.h:34-38). Wyjścia znormalizowane (throttle 0..cap, servo signed) + `err_m`,
  `bearing_deg10`.
- **Jedna bramka pierwszeństwa (kluczowa dla R6):** `resolve_spot_lock()` w
  `components/control_loop/src/loop_step.c:237-250` — jeśli `resolved_state != SM_STATE_ARMED`
  → `SPOT_LOCK_OFF` bezwarunkowo; `spot_lock_step` wołane tylko w ARMED. Wołane z `loop_step()`
  (loop_step.c:269). goto **rzuca się na tę samą bramkę** — zero nowego toru failsafe.
- **Geo-matematyka:** `components/control_loop/include/geo_math.h` — dystans+bearing z `lat/lon (e7)`.
  Reużyta bez zmian (goto liczy tak samo do zewnętrznego celu).
- **Signal chain (wstrzyknięcie computed command + clamp):** tryby `THROTTLE_TARGET_SPOT_LOCK` /
  `SERVO_TARGET_SPOT_LOCK` już przepuszczają computed command przez ramp/slew → `map_normalized_to_us`
  → **hard clamp SI-3**. goto reużywa te same tryby (to wciąż „silnik position-hold liczy komendę").
- **Comms-watchdog (rdzeń gotowy do reużycia):** `components/gps/include/sensor_freshness.h` —
  `sensor_is_fresh(now_ms, last_ms, threshold_ms)` (wrap-safe, kontrakt epoki w nagłówku) +
  `sensor_freshness_stamp(prev, now, valid)`. Domena `now_ms()` = `esp_timer_get_time()/1000`
  (`control_loop.c:61`). Identyczny wzorzec jak świeżość GPS/IMU.
- **Ścieżka komendy HTTP → pętla:** `POST /api/command` → `http_server.c` `post_command()`
  (~:177) → `extract_command()` (cJSON, wyjmuje `"cmd"`) → `command_parse()` (czyste, keyword→flagi,
  `components/web_panel/include/command_parse.h`, `src/command_parse.c`) → `to_ui_events()` →
  `control_loop_post_ui_events()` → mailbox `xQueueOverwrite` (length-1). Konsumpcja:
  `apply_ui_events()` w `control_loop.c:331`.
- **UI events mailbox (single-writer):** `control_loop_ui_events` (`control_loop.h:70-81`) —
  edge-semantics, konsumowane raz. Rozszerzamy o pola goto.
- **Parametry SI-6 end-to-end:** `settings_model.h` (`spot_lock_*` już istnieją, :87-93),
  `settings_ranges.h`/`settings_defaults.c`/`settings_validate.c`, `params_json.c`
  (`U16_FIELDS`), gate `params_decide` + apply `maybe_apply_pending` (tylko DISARMED). Bump
  `SETTINGS_SCHEMA_VERSION` migruje brakujące pola do defaults.
- **Telemetria:** `control_loop_snapshot` (`control_loop.h:26-63`, ma już `spot_lock_*`, `gps_*`),
  populacja w `publish_snapshot`, serializacja **ints/bools only** w `ws_telemetry.c`.
- **Host-test harness:** `test/host/CMakeLists.txt` (`PURE_SOURCES`+`TEST_SOURCES`), `run.sh`,
  `test_main.c`. Wzorce: `test_spot_lock.c`, `test_loop_step.c`, `test_sensor_freshness.c`,
  `test_command_parse.c` (jeśli jest), `test_settings_validate.c`.

### Wiedza instytucjonalna

- **Override sterowania nie może osłabić failsafe — pierwszeństwo strukturalne**
  (`docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md`).
  goto = sensor/command-driven override liczony **PO** maszynie stanów i wykonywany **wyłącznie**
  w gałęzi ARMED. Cel z aplikacji podpinamy jako wejście override'u, **nigdy** do `rc_valid`/`sm_inputs`.
  Test pierwszeństwa musi wchodzić w stan, który bez bramki by przeciekł (moc wyroczni).
- **fresh ≠ valid: jakość re-waliduj co cykl** (ten sam solution). Świeżość linku aplikacji
  bramkujemy **co cykl** podczas goto, nie tylko przy aktywacji.
- **Wrap-safe recency w jednej domenie zegara**
  (`docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md`). Watchdog linku
  liczymy przez `sensor_is_fresh` w domenie `now_ms()`; raw stamp, modular subtraction; host-test
  wokół granicy wrapu (reużycie istniejących testów `sensor_freshness` pokrywa rdzeń).
- **Pure ⊥ HAL** (`docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md`).
  Rozszerzenie `spot_lock_step`, walidacja celu i decyzja watchdoga = czyste, host-testowane; HAL
  (http_server, control_loop timestamp) cienki.
- **Oracle power testu clampu/limitu / bramki**
  (`docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md`). Priorytet CH3>goto,
  bramkę linku, walidację zakresu lat/lon i „override kasuje latch" testuj wejściem **poza** zakresem /
  w stanie, który bez bramki przecieka. Reguła: „czy test FAILuje, gdy usunę testowaną bramkę?".

### Referencje zewnętrzne

Pominięte dla firmware. Codebase ma silny lokalny wzorzec dla każdego elementu (silnik position-hold,
świeżość sensora/linku, command→mailbox, settings/telemetria). Kontekst iOS (Network.framework
`requiredInterfaceType = .wifi`, `NSLocalNetworkUsageDescription`, offline-mapy MapLibre) należy do
osobnego planu aplikacji, nie do firmware.

## Kluczowe decyzje techniczne

- **Reużycie silnika spot-lock zamiast duplikacji — źródło celu jako oś rozszerzenia.** goto i
  spot-lock to ten sam regulator position-hold; różnią się wyłącznie tym, skąd bierze się `ref_lat/lon`
  i jakie warunki go pauzują/abortują. Rozszerzamy `spot_lock_step` o **arbitraż źródła celu**
  (`SRC_HOLD` = CH3 snapshot, `SRC_GOTO` = cel zewnętrzny), nie tworzymy równoległego modułu ruchu
  (coding-rules §3: reuse shared logic). Rename modułu na „position_hold/nav" **odrzucony** —
  niepotrzebny churn w w pełni przetestowanym module (zob. Rozważane alternatywy).
- **CH3 ma priorytet fizyczny nad goto (decyzja operatora).** Kolejność źródeł w ARMED:
  (1) `!sticks_neutral` → OFF (override, kasuje latch goto); (2) `ch3_on` → `SRC_HOLD` (na zboczu:
  snapshot „tu i teraz", kasuje latch goto); (3) `goto_engage && !ch3_on` → `SRC_GOTO` (cel zewnętrzny);
  (4) inaczej OFF. Fizyczny przełącznik zawsze wygrywa z aplikacją — spójne z „CH3 jako wyłącznik goto".
- **Utrata linku aplikacji → PAUSED (nie abort).** Analogicznie do utraty GPS w spot-locku
  („nie szarpać"): neutral+center, cel zapamiętany, wznowienie po powrocie linku. Bramka świeżości
  linku dotyczy **wyłącznie** `SRC_GOTO`. Krótkie blipy WiFi nie kończą misji; trwała utrata telefonu
  = łódka stoi na neutralu (dryf bezpieczny), a granicę czasową i tak domyka failsafe RC przy utracie RC.
- **goto rzuca się na istniejącą pojedynczą bramkę failsafe.** Zero nowego toru: `spot_lock_step`
  (a więc i goto) wołane tylko w `SM_STATE_ARMED` (`resolve_spot_lock`). FAILSAFE/DISARMED → OFF
  bezwarunkowo. To realizuje R6 strukturalnie (learning: failsafe-precedence).
- **Override kasuje latch goto (brak auto-resume).** Wychylenie drążka → OFF **i** wyczyszczenie
  `goto_engage`, żeby powrót drążka do neutralu nie wznawiał autonomicznej jazdy bez świadomej
  ponownej komendy. Wznowienie po override wymaga nowego `goto` z aplikacji. (Pauza od utraty linku
  celowo latcha zachowuje — to inny, przejściowy warunek.)
- **Współrzędne jako `int32 e7` na łączu.** `lat_e7`/`lon_e7` — bez floatów w JSON i na granicy
  systemu; spójne z `gps_lat_e7`. Walidacja zakresu jako czysty predykat host-testowany; niepoprawne
  → `400` `{data,error}` (fail-fast na granicy API).
- **Keepalive przez ponowny `goto` (lub lekki heartbeat).** Firmware stempluje `s_last_goto_ms`
  przy każdej odebranej komendzie goto (`sensor_freshness_stamp`) i co cykl liczy
  `comms_fresh = sensor_is_fresh(now_ms(), s_last_goto_ms, timeout)`. Aplikacja ponawia `goto`
  ~2 Hz (idempotentnie odświeża cel + stempel). Dokładny format wire (repeat vs dedykowany endpoint)
  odroczony do implementacji.
- **Telemetria na intach.** `goto_state` (0/1/2 wspólne z `spot_lock` substate albo osobne pole),
  `goto_target_lat_e7/lon_e7`, `goto_err_m`, `goto_bearing_deg10`, `goto_arrived` (bool),
  `app_link_fresh` (bool) — spójnie z konwencją snapshotu (bez floatów).

## Otwarte pytania

### Rozwiązane podczas planowania

- **Czy goto to nowy moduł czy rozszerzenie spot-locka?** Rozszerzenie `spot_lock_step` o źródło celu —
  ten sam silnik ruchu, zero duplikacji matematyki.
- **Jak goto zachowuje pierwszeństwo manualne/failsafe?** Rzuca się na istniejącą bramkę
  `resolve_spot_lock` (tylko ARMED). Brak nowego toru; `rc_valid`/`sm_inputs` nietknięte.
- **Aktywacja: app czy CH3?** Aktywacja z aplikacji (ARMED + drążki neutral + komenda). CH3 to
  fizyczny wyłącznik/priorytet (włączenie w trakcie goto → przerwij + hold „tu i teraz").
- **Utrata linku?** PAUZA + wznowienie (jak spot-lock), bramka świeżości tylko dla `SRC_GOTO`.
- **Domena czasu watchdoga?** `now_ms()` (esp_timer/1000, uint32), `sensor_is_fresh` wrap-safe —
  ten sam wzorzec i moduł co GPS/IMU.
- **Format celu na łączu?** `lat_e7`/`lon_e7` int32, walidacja zakresu, błąd → 400 `{data,error}`.
- **Czy NVS/blob_codec wymaga zmian?** Tak, bump `SETTINGS_SCHEMA_VERSION` dla `goto_comms_timeout_ms`
  (wzorzec migracji do defaults). Cel goto NIE trafia do NVS (żyje w RAM).

### Odroczone do implementacji

- **Dokładne sygnatury** rozszerzenia `spot_lock_step` (nowe pola `spot_lock_inputs`/`spot_lock_state`:
  `goto_engage`, `goto_lat_e7/lon_e7`, `comms_fresh`, `target_source`) — ustalić przy dotknięciu kodu,
  zachowując istniejące zachowanie dla ścieżki CH3 (zero regresji `test_spot_lock`).
- **Format keepalive** (ponowny `goto` vs dedykowany `{"cmd":"goto_ping"}` vs licznik na WS) i jego
  częstotliwość — do ustalenia razem z aplikacją; wpływa tylko na warstwę HTTP, nie na rdzeń.
- **Czy goto potrzebuje osobnego cap gazu / gainów** niż spot-lock (dłuższe dystanse mogą chcieć wyższy
  ciąg marszowy) — decyzja po próbach w terenie; start na współdzielonych nastawach spot-locka.
- **Semantyka „arrived"** poza hold-w-deadbandzie (np. jawny latch/powiadomienie dla aplikacji) —
  minimalnie: flaga `goto_arrived` w telemetrii; rozbudowa odroczona.
- **Zachowanie na wodzie** (realna reakcja silnika, „jeden łagodny zawrót" do odległego celu, dryf w
  pauzie, dobór `goto_comms_timeout_ms`) — nieweryfikowalne na hoście; log w `known-issues`.

## Implementation Units

Pogrupowane w 4 fazy. Fazy 1–2 dokładają kanał celu i rdzeń decyzyjny bez zmiany zachowania
aktuatorów w istniejących ścieżkach; Faza 3 włącza sterowanie; Faza 4 wystawia parametry i telemetrię.

### Faza 1 — Kanał celu z aplikacji (komenda + walidacja + transport)

- [x] **Unit 1: Walidacja celu + rozszerzenie `command_parse` o goto/goto_cancel**

**Cel:** Czysta, host-testowana walidacja `lat_e7`/`lon_e7` oraz reprezentacja komend `goto`
(z payloadem) i `goto_cancel` w warstwie parsowania.

**Wymagania:** R1, R6

**Zależności:** Brak

**Pliki:**
- Stwórz: `components/web_panel/include/goto_target.h`, `components/web_panel/src/goto_target.c`
  (czyste: `bool goto_target_valid(int32_t lat_e7, int32_t lon_e7)` — zakres ±90°/±180° w e7;
  named constants zamiast magic numbers)
- Modyfikuj: `components/web_panel/include/command_parse.h` (`command_parse_result`: dodaj
  `bool goto_request`, `bool goto_cancel_request`, `int32_t goto_lat_e7`, `int32_t goto_lon_e7`),
  `components/web_panel/src/command_parse.c` (keyword `goto`/`goto_cancel` w tablicy komend; payload
  wypełnia warstwa HTTP w Unit 2 — `command_parse` pozostaje keyword-only, pola lat/lon nośne)
- Modyfikuj: `test/host/CMakeLists.txt` (`goto_target.c` do `PURE_SOURCES`, `test_goto_target.c`,
  `test_command_parse.c`), `test/host/test_main.c`
- Test (unit): `test/host/test_goto_target.c`; rozszerz `test/host/test_command_parse.c`

**Podejście:**
- `goto_target_valid` jako czysta wyrocznia: odrzuca `lat_e7` poza `[-900000000, 900000000]`,
  `lon_e7` poza `[-1800000000, 1800000000]`. Bez zależności HAL (grep-checkable).
- `command_parse` rozpoznaje keyword `goto`/`goto_cancel` (jak istniejące arm/disarm); faktyczny
  payload lat/lon wstrzykuje handler HTTP (cJSON) w Unit 2 — tu tylko pola i flagi.

**Notatka wykonawcza:** Test-first dla `goto_target_valid` — wartości **poza** zakresem (oracle power),
nie tożsamość na granicy; przetestuj dokładne granice ±90/±180.

**Wzorce do naśladowania:** `components/web_panel/src/command_parse.c` (tablica keyword→flagi),
`components/settings/src/settings_validate.c` (walidacja zakresów), `test/host/test_command_parse.c`.

**Scenariusze testowe:**
- [Unit] `goto_target_valid`: cel w zakresie → true; `lat_e7 = 900000001` → false; `lon_e7 = -1800000001`
  → false; dokładne granice zdefiniowane i przetestowane.
- [Unit] `command_parse("goto")` → `goto_request=true`; `command_parse("goto_cancel")` →
  `goto_cancel_request=true`; nieznany keyword → `ok=false` (bez regresji istniejących komend).

**Weryfikacja:** Host-tests zielone; grep braku `esp_*`/`driver/*` w `goto_target.h`. Zero regresji
istniejących testów `command_parse`.

- [x] **Unit 2: HTTP handler goto (payload lat/lon) + transport przez mailbox UI events**

**Cel:** `POST /api/command` obsługuje `goto` (wyjmuje i waliduje lat/lon, błąd → 400) oraz
`goto_cancel`; komenda propaguje do pętli przez rozszerzone `control_loop_ui_events`.

**Wymagania:** R1, R6

**Zależności:** Unit 1

**Pliki:**
- Modyfikuj: `components/web_panel/src/http_server.c` (`extract_command`/`post_command`: dla `cmd=="goto"`
  odczytaj `lat_e7`/`lon_e7` cJSON, `goto_target_valid` → przy błędzie `400` `{data,error:{code:"...",
  message}}`; `to_ui_events` kopiuje `goto_request`/`goto_cancel_request` + lat/lon do UI events)
- Modyfikuj: `components/control_loop/include/control_loop.h` (`control_loop_ui_events`: `bool goto_request`,
  `bool goto_cancel_request`, `int32_t goto_lat_e7`, `int32_t goto_lon_e7`)
- Modyfikuj: `components/control_loop/src/control_loop.c` (`apply_ui_events`: skonsumuj event goto →
  ustaw staged goto target + `goto_engage` latch; `goto_cancel` → wyczyść latch; stempluj
  `s_last_goto_ms = sensor_freshness_stamp(..., now_ms(), reading_valid=true)`)
- Test (unit): rozszerz `test/host/test_command_parse.c` o mapowanie payloadu (jeśli `to_ui_events`
  wydzielone jako czyste) — inaczej pokrycie w Unit 4/5 na poziomie loop

**Podejście:**
- Warstwa HTTP pozostaje cienka: cJSON + walidacja + koperta błędu; decyzja ruchu w rdzeniu (Pure ⊥ HAL).
- `goto_engage` (latch) i staged target żyją w `control_loop` (single writer = pętla). Mailbox
  `xQueueOverwrite` — najnowszy goto wygrywa (spójne z resztą UI events).
- Stempel `s_last_goto_ms` aktualizowany przy każdej odebranej komendzie goto → baza watchdoga (Unit 4).

**Notatka wykonawcza:** Najpierw test kontraktu request/response dla nieprawidłowego lat/lon → 400
w kopercie (fail-fast na granicy API).

**Wzorce do naśladowania:** `http_server.c` `post_command`/`extract_command` + `params_json.c`
(cJSON parsing), koperta `{data,error}` istniejących endpointów, `to_ui_events`.

**Scenariusze testowe:**
- [Unit] Poprawny `goto` z lat/lon w zakresie → UI event z `goto_request` i skopiowanym celem.
- [Unit] `goto` z lat/lon poza zakresem → `400` `{data:null, error:{code}}` (bez postu do mailbox).
- [Unit] `goto_cancel` → UI event `goto_cancel_request`.

**Weryfikacja:** `idf.py build` zielony; host-tests zielone; `POST /api/command` zwraca poprawną kopertę
dla obu ścieżek; UI event dociera do `apply_ui_events`.

### Faza 2 — Rdzeń decyzyjny (rozszerzenie silnika, jeszcze nie steruje przez goto)

- [x] **Unit 3: Rozszerzenie `spot_lock_step` o źródło celu + arbitraż CH3/goto + bramka linku**

**Cel:** Czysta logika arbitrażu źródła celu (`SRC_HOLD` vs `SRC_GOTO`), wejścia z zewnętrznym celem,
priorytet CH3 nad goto, pauza na utratę linku, kasowanie latcha przy override — jako rozszerzenie
istniejącej czystej funkcji, bez regresji ścieżki CH3.

**Wymagania:** R2, R3, R4, R5, R6

**Zależności:** Brak (równolegle do Fazy 1)

**Pliki:**
- Modyfikuj: `components/control_loop/include/spot_lock.h` (`spot_lock_inputs`: `bool goto_engage`,
  `int32_t goto_lat_e7`, `int32_t goto_lon_e7`, `bool comms_fresh`; `spot_lock_state`: `target_source`
  {`SRC_NONE`/`SRC_HOLD`/`SRC_GOTO`}; ewentualnie `spot_lock_outputs`: `bool arrived`)
- Modyfikuj: `components/control_loop/src/spot_lock.c` (arbitraż źródła przed/we wejściu; dla `SRC_GOTO`
  `ref_* = goto_*`; bramka `comms_fresh` w pauzie tylko dla `SRC_GOTO`; CH3 preemptuje goto)
- Test (unit): rozszerz `test/host/test_spot_lock.c`

**Podejście:**
- Priorytet w ARMED (rozszerza istniejące przejścia, nie zmienia ich dla CH3):
  1. `!armed || !sticks_neutral` → OFF (+ warstwa loop kasuje `goto_engage`).
  2. `ch3_on` → `SRC_HOLD`; na `ch3_edge_on` snapshot `ref_*` = bieżąca pozycja (jak dziś), preempt goto.
  3. `goto_engage && !ch3_on` → `SRC_GOTO`, `ref_*` = `goto_*`, ACTIVE.
  4. inaczej OFF.
- Pauza: `SRC_HOLD` → `!gps_fresh || !imu_ok` (jak dziś). `SRC_GOTO` → `!gps_fresh || !imu_ok ||
  !comms_fresh`. Wznowienie po powrocie warunków, ten sam cel.
- Ruch/utrzymanie/deadband/±60°/cap: **bez zmian** — reużyty istniejący rdzeń dla obu źródeł.
- `arrived = (err_m <= deadband_m)` w ACTIVE (telemetria).

**Notatka wykonawcza:** Test-first, moc wyroczni. Priorytet CH3>goto i bramkę linku testuj wchodząc w
stan, który bez bramki by przeciekł (np. `SRC_GOTO` aktywne + CH3 ON → MUSI przejść w `SRC_HOLD`, nie
zostać w goto). Zachowaj **wszystkie** istniejące asercje `test_spot_lock` (ścieżka CH3 bez regresji).

**Wzorce do naśladowania:** `components/control_loop/src/spot_lock.c` (istniejące przejścia),
`components/state_machine/src/state_machine.c` (przejścia jako czyste funkcje), `test/host/test_spot_lock.c`.

**Scenariusze testowe:**
- [Unit] `SRC_GOTO`: `goto_engage`+ARMED+neutral+fresh, CH3 OFF → ACTIVE, `ref_* == goto_*`; steruje do
  celu (throttle>neutral gdy poza deadbandem i w stożku ±60°).
- [Unit] Priorytet CH3: goto ACTIVE, `ch3_on`(edge) → `SRC_HOLD`, `ref_*` = bieżąca pozycja (nie cel goto).
  Test FAILuje, gdy usunę preempcję CH3.
- [Unit] Bramka linku: `SRC_GOTO` ACTIVE, `comms_fresh=false` → PAUSED (neutral+center, cel zachowany);
  powrót `comms_fresh` → ACTIVE ten sam cel. `SRC_HOLD` z `comms_fresh=false` → **nie** pauzuje (link
  nie dotyczy CH3). Test FAILuje, gdy bramka linku obejmie `SRC_HOLD`.
- [Unit] Override: `SRC_GOTO` ACTIVE + `!sticks_neutral` → OFF.
- [Unit] Regresja CH3: wszystkie istniejące scenariusze spot-lock (wejście, deadband, ±60°, cap,
  pauza GPS/IMU, abort) przechodzą bez zmian.

**Weryfikacja:** Host-tests zielone (nowe + wszystkie istniejące `test_spot_lock`); grep braku
`esp_*`/`driver/*` w `spot_lock.h`; funkcja deterministyczna.

### Faza 3 — Integracja + comms-watchdog (włączenie sterowania)

- [x] **Unit 4: Integracja goto w `loop_step`/`control_loop` + watchdog linku + cykl życia latcha**

**Cel:** Podłączenie staged goto target + `goto_engage` + `comms_fresh` do `spot_lock_step` przez
`loop_step`, z watchdogiem linku (`sensor_is_fresh`) i poprawnym cyklem życia latcha (kasowanie przy
override/cancel/CH3-preempt), z zachowaniem jednej bramki failsafe.

**Wymagania:** R2, R3, R4, R5, R6

**Zależności:** Unit 2, Unit 3

**Pliki:**
- Modyfikuj: `components/control_loop/include/loop_step.h` (`loop_inputs`: `goto_engage`,
  `goto_lat_e7/lon_e7`, `comms_fresh`)
- Modyfikuj: `components/control_loop/src/loop_step.c` (`resolve_spot_lock`: przekaż nowe wejścia do
  `spot_lock_step`; przy `!sticks_neutral`/`goto_cancel`/CH3-preempt sygnalizuj wyczyszczenie latcha
  do warstwy loop — bramka `state==ARMED` **bez zmian**)
- Modyfikuj: `components/control_loop/src/control_loop.c` (`s_last_goto_ms`, `s_goto_engage`,
  `s_goto_lat_e7/lon_e7`; `read_inputs`: `comms_fresh = sensor_is_fresh(now_ms(), s_last_goto_ms,
  s_params.goto_comms_timeout_ms)`; kasowanie latcha po override/cancel/preempt)
- Test (unit): rozszerz `test/host/test_loop_step.c` (pełna ścieżka goto→hold→pauza→abort/preempt);
  rdzeń wrap-safe pokryty istniejącym `test_sensor_freshness.c`

**Podejście:**
- Override aplikowany **wyłącznie** w `SM_STATE_ARMED` — istniejąca bramka `resolve_spot_lock` (R6).
  FAILSAFE/DISARMED → OFF; goto ustępuje bez zmian w maszynie stanów.
- Watchdog: stempel `s_last_goto_ms` odświeżany w `apply_ui_events` przy odebranej komendzie goto;
  `comms_fresh` liczone co cykl (fresh ≠ valid — re-walidacja co cykl, learning).
- Cykl życia latcha: `goto_engage` ustawiany komendą, kasowany przez (a) `goto_cancel`, (b) override
  drążkiem, (c) CH3-preempt. Pauza od utraty linku **nie** kasuje latcha (przejściowa).
- GPS/IMU/CH3 czytane raz w `read_inputs`; spójny obraz cyklu, jak w spot-locku.

**Notatka wykonawcza:** Najpierw failing test integracyjny w `test_loop_step.c` dla pełnej ścieżki
komenda→nawigacja→(pauza na utratę linku)→wznowienie→(CH3-preempt→hold), potem implementacja.

**Wzorce do naśladowania:** `components/control_loop/src/loop_step.c` (`resolve_spot_lock`, kolejność
kroków), `apply_ui_events`/`read_inputs` w `control_loop.c`, `sensor_freshness` (GPS staleness w
`gps_reader.c` jako wzorzec stempla).

**Scenariusze testowe:**
- [Unit] Komenda goto w ARMED+neutral+fresh, CH3 OFF → `loop_step` daje servo/ESC computed (≠ tor
  stickowy) i `SRC_GOTO` ACTIVE.
- [Unit] W trakcie goto: utrata RC → `sm_step`=FAILSAFE → ESC neutral+servo center (override się NIE
  wykonuje). Test FAILuje, gdyby goto działało poza ARMED.
- [Unit] W trakcie goto: `comms_fresh=false` (link timeout) → PAUSED (neutral+center), latch zachowany;
  powrót linku → ACTIVE ten sam cel.
- [Unit] W trakcie goto: `!sticks_neutral` → OFF **i** latch skasowany (powrót drążka do neutralu NIE
  wznawia goto). Test FAILuje, gdy latch przetrwa override.
- [Unit] W trakcie goto: CH3 ON → `SRC_HOLD` (hold „tu i teraz"), latch goto skasowany.
- [Unit] `goto_cancel` → OFF, latch skasowany.
- [Unit] Każde wyjście goto przechodzi przez hard clamp SI-3 (out-of-window computed → clamp).

**Weryfikacja:** Host-tests zielone; `idf.py build` zielony; zero regresji `loop_step`/state_machine/
spot_lock/chain. Grep: brak nowych `esp_*`/`driver/*` w czystych nagłówkach.

### Faza 4 — Parametry + telemetria + panel

- [x] **Unit 5: Parametr `goto_comms_timeout_ms` (SI-6)**

**Cel:** Timeout watchdoga linku jako konfigurowalny parametr z regułą SI-6 (apply tylko w DISARMED).

**Wymagania:** R7

**Zależności:** Brak (potrzebny przez Unit 4)

**Pliki:**
- Modyfikuj: `components/settings/include/settings_model.h` (`uint16_t goto_comms_timeout_ms`; **bump
  `SETTINGS_SCHEMA_VERSION`**), `settings_ranges.h` (MIN/MAX/DEFAULT ~1500 ms), `settings_defaults.c`,
  `settings_validate.c`, `components/web_panel/src/params_json.c` (`U16_FIELDS`)
- Test (unit): rozszerz `test/host/test_settings_validate.c`; `test/host/test_blob_codec.c` (round-trip
  nowej wersji schematu)

**Podejście:**
- Reużyj istniejący tor pending-apply (`params_decide` → `maybe_apply_pending`, TOCTOU, single writer) —
  bez nowej logiki apply.
- Default łagodny (~1,5 s) — kompromis między nuisance-pauzą a szybką reakcją na utratę linku;
  strojenie w terenie.

**Notatka wykonawcza:** Bez modyfikacji istniejących testów settings poza dodaniem przypadków; przy
bumpie schematu zweryfikuj migrację do defaults.

**Wzorce do naśladowania:** istniejące `spot_lock_*` i `failsafe_timeout_ms` (model→ranges→defaults→
validate→params_json), `test/host/test_settings_validate.c`.

**Scenariusze testowe:**
- [Unit] Wartość poza zakresem odrzucona/clampowana zgodnie z konwencją modułu; w zakresie akceptowana.
- [Unit] Defaults ładują się przy świeżej/skorrumpowanej NVS z sensownym timeoutem.
- [Unit] POST `goto_comms_timeout_ms` w ARMED → 409 (SI-6 niezmienione).

**Weryfikacja:** Host-tests zielone; pole serializuje się w `/api/params`; `idf.py build` zielony.

- [x] **Unit 6: Telemetria goto + panel**

**Cel:** Sub-stan goto, cel, błąd, bearing, `arrived` i świeżość linku w WS/panelu — diagnostyka,
strojenie i kontrakt dla aplikacji iOS.

**Wymagania:** R8

**Zależności:** Unit 4

**Pliki:**
- Modyfikuj: `components/control_loop/include/control_loop.h` (`control_loop_snapshot`: `uint8_t
  goto_state`, `int32_t goto_target_lat_e7/lon_e7`, `uint16_t goto_err_m`, `uint16_t goto_bearing_deg10`,
  `bool goto_arrived`, `bool app_link_fresh`), `control_loop.c::publish_snapshot` (populacja z
  `loop_outputs`/watchdog), `components/web_panel/src/ws_telemetry.c::snapshot_to_json` (nowe pola,
  **ints/bools only**)
- Modyfikuj: front-end panelu ESP (HTML/JS `web_panel`) — blok „Goto: off/active/paused, cel, błąd X m,
  bearing Y° / dziób Z°, arrived, link"
- Test (unit): jeśli istnieje host-test serializacji telemetrii — rozszerz; inaczej weryfikacja
  kontraktu JSON (pola obecne)

**Podejście:**
- Bieżący dziób już dostępny (`imu_heading_deg10`); `spot_lock_*` telemetria istnieje — dołóż pola
  specyficzne dla goto (cel + arrived + link) spójnie ze stylem `snprintf` (bez floatów).
- To ten sam kontrakt WS, z którego skorzysta aplikacja iOS — nazwy pól stabilne (parytet API).

**Wzorce do naśladowania:** istniejące `spot_lock_*`/`gps_*` w `snapshot_to_json` i ich populacja w
`publish_snapshot`.

**Scenariusze testowe:**
- [Unit] (jeśli host-testowalne) Snapshot z goto ACTIVE serializuje `goto_state=1`, cel, `err_m`,
  `bearing_deg10`, `app_link_fresh` jako int/bool.
- [E2E] (zalogowany w known-issues jako gap hardware/na wodzie): panel/aplikacja pokazuje off→active po
  komendzie goto, błąd maleje przy dopływaniu, paused przy utracie linku, hold po dojściu.

**Weryfikacja:** `idf.py build` zielony; panel renderuje nowe pola; JSON zawiera `goto_*`. Hardware/E2E
odłożone do `known-issues`.

## Wpływ systemowy

- **Graf interakcji:** Nowe wejście (cel z aplikacji) wpływa przez `POST /api/command` → mailbox UI
  events → `loop_step` → `spot_lock_step`, wyłącznie w gałęzi `state==ARMED`. `sm_step`, `rc_validity`,
  tor CH3-spot-lock nietknięte semantycznie (goto to nowe źródło celu tego samego silnika).
- **Propagacja błędów:** Niepoprawny cel → `400` na granicy API (nie dociera do pętli). Utrata linku →
  PAUSED wewnątrz goto; utrata RC → FAILSAFE (istniejący tor) wygrywa; każde wyjście kończy hard clamp SI-3.
- **Ryzyka cyklu życia stanu:** `goto_engage`/staged target/`s_last_goto_ms` żyją w `control_loop`
  (single writer = pętla). Latch kasowany deterministycznie (cancel/override/CH3-preempt); pauza od
  linku latcha nie kasuje. Cel w RAM (brak persystencji NVS). Settings pending-apply tylko w DISARMED —
  zmiana `goto_comms_timeout_ms` nie zaburza aktywnego goto (ARMED blokuje apply).
- **Parytet surface API:** `/api/command` (+`goto`,`goto_cancel`), `/api/params` (+timeout), WS
  (+`goto_*`). Koperta `{data,error}` i 409-w-ARMED bez zmian. Kontrakt WS = interfejs dla aplikacji iOS.
- **Pokrycie integracyjne:** Pełna ścieżka komenda→nawigacja→pauza(link)→wznowienie→CH3-preempt→abort
  pokryta w `test_loop_step.c` + `test_spot_lock.c`; zachowanie na wodzie = jawna luka hardware w
  `known-issues`.

## Ryzyka i zależności

- **Aplikacja jako źródło sterowania bez RC-owego failsafe.** Mitygacja: comms-watchdog (`sensor_is_fresh`)
  co cykl → PAUSED; goto liczone tylko w ARMED (ustępuje FAILSAFE RC); override drążkiem natychmiast +
  kasuje latch. Test pierwszeństwa z mocą wyroczni (wejście w stan, który bez bramki przecieka).
- **Rozszerzenie w pełni przetestowanego `spot_lock_step`.** Ryzyko regresji ścieżki CH3. Mitygacja:
  zachowanie wszystkich istniejących asercji `test_spot_lock` jako bramka; nowe źródło celu nie zmienia
  matematyki ruchu.
- **Nawigacja po linii prostej ignoruje przeszkody.** Ryzyko operacyjne. Mitygacja: watchdog ogranicza
  jazdę na ślepo do timeoutu; łagodne defaults; jasny komunikat/limit; odpowiedzialność operatora
  (dokumentacja + known-issues).
- **Bump `SETTINGS_SCHEMA_VERSION`.** Ryzyko migracji NVS. Mitygacja: wzorzec wersjonowany
  (`blob_codec`) → brakujące pola = defaults; round-trip test.
- **Format keepalive uzgadniany z aplikacją.** Ryzyko couplingu wire. Mitygacja: warstwa HTTP cienka,
  rdzeń niezależny od formatu; keepalive przez idempotentny repeat `goto` jako domyślny, zmiana = tylko HTTP.
- **Dobór `goto_comms_timeout_ms` i nastaw marszowych** = nieweryfikowalne na hoście. Mitygacja:
  łagodne defaults, strojenie w terenie, log w `known-issues`.

## Fazowe dostarczanie

### Faza 1 — Kanał celu (Unit 1–2)
Komenda + walidacja + transport. Bez zmiany zachowania aktuatorów. Odblokowuje testowanie kontraktu API
z aplikacją (nawet zanim goto steruje).

### Faza 2 — Rdzeń decyzyjny (Unit 3)
Arbitraż źródła + bramka linku jako czysta logika host-testowana. Nadal nie steruje (nie podłączone do loop).

### Faza 3 — Integracja (Unit 4)
Włączenie sterowania goto + watchdog. Tu materializuje się pełna ścieżka safety; największa gęstość testów.

### Faza 4 — Parametry + telemetria (Unit 5–6)
Strojenie (timeout) i widoczność (WS/panel) — kontrakt dla aplikacji iOS.

## Dokumentacja / Notatki operacyjne

- **`docs/completed/kayak-motor-firmware-v1/known-issues.md`:** dopisz luki hardware/na wodzie: realna
  reakcja silnika na goto, „jeden łagodny zawrót" do odległego celu, dryf w pauzie, dobór
  `goto_comms_timeout_ms`, brak omijania przeszkód, zachowanie CH3-preempt w terenie.
- **Kontrakt API dla aplikacji iOS:** udokumentuj `POST /api/command {"cmd":"goto","lat_e7","lon_e7"}`,
  `{"cmd":"goto_cancel"}`, wymóg keepalive (~2 Hz), pola telemetrii `goto_*` — to wejście do osobnego
  planu aplikacji ([[ios-app-goto-direction]]).
- **Rollout/de-risking:** start na łagodnych, współdzielonych nastawach spot-locka (duża martwa strefa,
  niski cap), najpierw krótkie dystanse i weryfikacja pauzy na utratę linku, potem zacieśnianie.
- Po wdrożeniu rozważ `/dev-compound` dla wzorca „app-driven override z comms-watchdogiem bez naruszenia
  failsafe RC" (rozszerzenie failsafe-precedence learning na źródło sieciowe).

## Źródła i referencje

- **Dokument źródłowy (upstream):** [docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md](../dev-brainstorms/2026-06-29-spot-lock-requirements.md)
  — non-goal „nawigacja do waypointów / ustawianie celu lat/lon" jest tu realizowany.
- **Plan bazowy (silnik):** [docs/plans/2026-06-29-001-feat-spot-lock-position-hold-plan.md](2026-06-29-001-feat-spot-lock-position-hold-plan.md)
- Wiedza instytucjonalna: `docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md`,
  `docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md`,
  `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md`,
  `docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md`
- Powiązany kod: `components/control_loop/src/{spot_lock,loop_step,control_loop}.c`,
  `components/control_loop/include/{spot_lock,control_loop,loop_step}.h`,
  `components/gps/include/sensor_freshness.h`, `components/web_panel/src/{http_server,command_parse,
  params_json,ws_telemetry}.c`, `components/web_panel/include/command_parse.h`, `components/settings/*`
- Reguły projektu: `.claude/rules/coding-rules.md`, `.claude/rules/learned-patterns.md`
