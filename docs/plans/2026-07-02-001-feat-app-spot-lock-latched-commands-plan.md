---
title: "feat: Spot-lock z aplikacji + latchowany model komend (pilot nadrzędny)"
type: feat
status: active
date: 2026-07-02
origin: docs/dev-brainstorms/2026-07-02-app-spot-lock-requirements.md
---

# feat: Spot-lock z aplikacji + latchowany model komend (pilot nadrzędny)

## Przegląd

Mapujemy 4. przycisk iOS (Spot-lock, dziś placeholder) na firmware jako komendę `hold` =
kotwica w bieżącej pozycji łodzi. Przy okazji zmieniamy model bezpieczeństwa: komendy z app
(goto i spot-lock) stają się **latchowanymi intencjami trwałymi wobec utraty linku** —
firmware przestaje pauzować silnik przy comms-timeout. **Pilot RC pozostaje jedynym
failsafe.** Kotwica realizowana jest jako `goto(własny fix)` — jedno źródło `SRC_GOTO`, bez
nowego trybu w firmware.

## Ujęcie problemu

Operator chce jednym tapnięciem zakotwiczyć łódź „na kotwicy" oraz — kluczowy use case —
**ustawić punkt goto, wygasić ekran telefonu i odejść, a łódź ma płynąć dalej**. Dziś
firmware traktuje link z aplikacją jak warstwę failsafe (utrata keepalive ~1,5 s → PAUSED),
co blokuje ten scenariusz. Docelowo RC jest jedynym nadrzędnym failsafe, a app to kanał
zatrzaśniętych komend. (zob. źródło: `docs/dev-brainstorms/2026-07-02-app-spot-lock-requirements.md`)

## Śledzenie wymagań

- R1. Spot-lock kotwiczy w **bieżącej pozycji** w momencie tapnięcia (position-only, forward-only).
- R2. Kotwica = `goto(własny fix)` (jedno `SRC_GOTO`); **pętla** łapie fix atomowo, angażuje tylko przy fresh+fix.
- R3. Goto i spot-lock trwają wobec utraty linku app — firmware **nie pauzuje** przy comms-timeout (odwrócenie).
- R4. **RC = jedyny failsafe/override** — `rc_valid`, override drążkiem (poza martwą strefą), CH3 preempt, disarm nadal kończą/wywłaszczają tryb app; CH3 `SRC_HOLD` wygrywa. Override rzuca kotwicę trwale (bez auto-powrotu).
- R5. Jeden cel autonomiczny naraz; STOP = uniwersalny off (`goto_cancel`); STOP nie rusza CH3 pilota; Rozbrój = twardy kill.
- R6. Wejście wymaga ARMED + świeży realny fix + drążki na zerze; app **wyszarza przycisk** gdy brak fixu (pre-walidacja z telemetrii).
- R7. App etykietuje tryb **lokalnie** (wg wciśniętego przycisku); po force-quit neutralne „Trzymam pozycję".
- R8. App **resync po powrocie** z tła/wygaszenia — odczyt stanu z telemetrii, odtworzenie UI.
- R9. App nigdy nie uzbraja; spot-lock dostępny tylko w ARMED.

## Granice scope'u

- Brak trzymania kursu dzioba (position-only).
- Brak geofence dla spot-locka (kotwica = bieżąca pozycja, z definicji wewnątrz fence).
- Brak strojenia parametrów spot-locka z app; brak tras/multi-waypoint.
- **Bez nowego źródła firmware i bez nowego pola telemetrii `target_source`** (świadomie odrzucone w roaście).
- Nie dotykamy warstwy uzbrajania (arm tylko z RC) ani ścieżki failsafe (`rc_valid`/`sm_inputs`).

## Kontekst i research

### Relevantny kod i wzorce

**Firmware:**
- `components/web_panel/include/command_parse.h` + `src/command_parse.c` — tablica `COMMAND_TABLE`, mapowanie keyword→event (pure, host-testowane). Wzór dla `hold`: wiersze `{"goto",...}`, `{"goto_cancel",...}`.
- `components/web_panel/src/http_server.c` — `to_ui_events()` (mapuje `command_parse_result`→`control_loop_ui_events`), gałąź `parsed.goto_request` z `extract_goto_target` + `goto_target_valid` (400 przy błędzie).
- `components/web_panel/include/goto_target.h` — `goto_target_valid(lat_e7, lon_e7)` (pure, host-testowane) — reużyć do walidacji złapanego fixu.
- `components/control_loop/include/control_loop.h` — struct `control_loop_ui_events` (linie 83-98) — dodać `hold_request`.
- `components/control_loop/src/control_loop.c` — `apply_goto_events()` (staging latcha `s_goto_engage`, `s_goto_lat/lon_e7`, stamp `s_last_goto_ms`), `apply_goto_inputs()` (liczy `comms_fresh` przez `sensor_is_fresh`). `gps_get_state()` już używane w `apply_sensor_inputs()`.
- `components/control_loop/src/spot_lock.c` — `hold_or_pause(..., comms_gated)` (linie ~152-166: gdy `comms_gated && !comms_fresh` → PAUSED); wywołanie `SRC_GOTO` z `comms_gated=true` (linia ~217) ← **flip na false**; bramka re-latchu `if (is_entering_goto || in->comms_fresh)` (linie ~210-215) ← **zostaje nietknięta**.
- `components/gps/include/nmea_parse.h` — `gps_state {bool fix; bool fresh; int32_t lat_e7; int32_t lon_e7; ...}`.

**iOS:**
- `ios/KayakKit/Sources/KayakContract/Command.swift` — enum `Command` (`goto`/`gotoCancel`/`disarm`), `cmdName`, `httpBody()`. Dodać `case hold` (bez współrzędnych, jak `gotoCancel`).
- `ios/KayakMotor/App/AppModel.swift` — `requestSpotLock()` (linie 127-129, placeholder), `keepaliveTick()` (linie 57-66), `sendGoto/stopGoto/disarm/clearTarget`.
- `ios/KayakKit/Sources/KayakContract/Keepalive.swift` — `KeepaliveDecision.shouldResend/shouldDisableIdleTimer` (do usunięcia).
- `ios/KayakKit/Sources/KayakContract/Telemetry.swift` — `HoldState` (0/1/2), `gotoState`, `spotLockState`, `gpsFix`, `gpsLatE7/LonE7`.
- `ios/KayakMotor/Features/Goto/ControlBarView.swift` — przyciski (linia 30 Spot-lock), `iconButton(enabled:)` (wzór wyszarzenia: przycisk Goto linie 23-27).
- `ios/KayakMotor/RootView.swift` — okablowanie `onSpotLock` (linia 147), `.task { model.start() }`; brak obsługi `scenePhase`.
- `ios/KayakKit/Sources/KayakContract/GotoReadiness` (+ `Tests/.../GotoReadinessTests.swift`) — wzór pure „readiness/blockReason" do naśladowania dla `SpotLockReadiness`.
- Testy iOS: Swift Testing (`@Suite`/`@Test`/`#expect`) w `ios/KayakKit/Tests/KayakContractTests/`.

**Host-testy firmware:** Unity, `test/host/`, uruchamiane `test/host/run.sh` (cmake+ninja→`./build/host_tests`). `test_command_parse.c` (16 testów), `test_spot_lock.c` (30 testów, m.in. `test_goto_pauses_on_comms_loss_then_resumes_same_target` linie 463-491 — do przepisania).

### Wiedza instytucjonalna

- `docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md` — **ten plan częściowo odwraca P1 (PAUSE-on-link-loss)**. ZACHOWAĆ: P3 (retencja celu/null-island — `comms_fresh` zostaje w bramce re-latchu), P2 (walidacja untrusted double przed castem — `goto_target_from_double`/`goto_target_valid`).
- `docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md` — override liczony w gałęzi ARMED, poza ARMED forsuj OFF; **fresh ≠ valid fix**: `!gps_fresh||!imu_ok||!gps_has_fix` pauzuje KAŻDY cykl (nie mieszać z odwracaną bramką comms). `sensor_freshness_stamp` przesuwa się tylko przy ważnym odczycie → atomowy grab fixu.
- `docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md` — `comms_fresh` zostaje w domenie `now_ms` przez `sensor_is_fresh` (wrap-safe).
- `docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md` — test odwrócenia musi wchodzić w scenariusz różnicujący starą i nową implementację (utrata TYLKO linku, fix trzymany).
- `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md` — decyzje hold/pause/re-latch/grab-fix jako czyste `(inputs)→(outputs)`, HAL cienki.

### Referencje zewnętrzne

Pominięte — codebase ma silne lokalne wzorce (pure⊥HAL, Unity host-tests, Swift Testing); temat w pełni pokryty wiedzą instytucjonalną. Zewnętrzny research nie dodałby wartości.

## Kluczowe decyzje techniczne

- **Jedno źródło `SRC_GOTO`, `hold` = `goto(własny fix)`** — brak nowego źródła/telemetrii; cała ścieżka persist/retencja/CH3-preempt/override-clear działa za darmo, R5 gratis. (zob. źródło + roast)
- **Chirurgiczne odwrócenie pauzy** — flip TYLKO `comms_gated` na `false` dla `SRC_GOTO` w `hold_or_pause`. `comms_fresh` ZOSTAJE w bramce re-latchu (ochrona null-island + retarget-w-locie). NIE wycinać watchdogu.
- **Rozdzielone domeny degradacji** — comms-loss przestaje pauzować; GPS/IMU-loss dalej pauzuje co cykl. Predykaty fizycznie osobne, żeby jedno odwrócenie nie przeciekło na drugie.
- **Grab fixu w pętli, atomowo** — `apply_goto_events` na `hold_request` robi jeden `gps_get_state()`, bramkuje `fresh && fix && goto_target_valid(...)`, dopiero wtedy latchuje. Decyzja wyekstrahowana jako pure helper (host-test).
- **Etykieta lokalna w app** — app-hold jest telemetrycznie identyczna z app-goto (`goto_state=active`); app rozróżnia z lokalnej intencji wciśniętego przycisku. `spot_lock_state=active` przy `goto_state=off` = **pilot przejął CH3**. Bez pola `target_source`.
- **Usunięcie keepalive/idle-timer w app** — persist nie potrzebuje resendu ani „trzymaj ekran"; sprzeczne z „wygaś ekran i płyń".
- **Resync-on-resume** — app na `scenePhase==.active` reconnectuje WS i uzgadnia lokalną intencję ze stanem firmware.

## Otwarte pytania

### Rozwiązane podczas planowania

- Nowe źródło firmware vs reużycie `SRC_GOTO`? → Reużycie `SRC_GOTO` (roast).
- Jak głęboko wyciąć watchdog? → Chirurgiczny flip, `comms_fresh` zostaje jako świeżość retargetu (roast).
- Gdzie łapać fix? → W pętli, atomowo; app pre-waliduje ze swojej telemetrii (roast).
- Jak app rozróżnia hold od goto? → Lokalna intencja; telemetria ich nie rozróżnia (oba `SRC_GOTO`).

### Odroczone do implementacji

- Profil gazu kotwicy: na start reużyj cruise-decel `SRC_GOTO`; łagodniejszy powrót przy dużym dryfie to strojenie `goto_slowdown_distance_m` w terenie, nie nowa gałąź.
- Dokładny kształt pure helpera grabu fixu (nazwa/sygnatura) — po dotknięciu `apply_goto_events`.
- Szczegóły restartu WS przy resume (czy `stop()`+`start()` TelemetryStore wystarczy, czy potrzebny dedykowany `reconnect()`).
- Czy usunąć całą `Keepalive.swift` czy zostawić pusty typ — po zobaczeniu wszystkich referencji przy edycji.

## Implementation Units

### Faza 1 — Firmware (fundament: persist + komenda hold)

- [ ] **Unit 1: Odwrócenie PAUSE→CONTINUE dla `SRC_GOTO` (chirurgiczny flip)**

**Cel:** Utrata linku z aplikacją przestaje pauzować goto; latch celu i RC-abort nietknięte.

**Wymagania:** R3, R4

**Zależności:** Brak (niezależna zmiana istniejącego goto; fundament dla persist kotwicy).

**Pliki:**
- Modyfikuj: `components/control_loop/src/spot_lock.c` (flip `comms_gated` na `false` w wywołaniu `SRC_GOTO`; NIE ruszać bramki re-latchu ani gałęzi sensorycznej)
- Modyfikuj: `components/control_loop/include/spot_lock.h`, `components/control_loop/include/loop_step.h` (prze-dokumentować `comms_fresh`: „link failsafe" → „świeżość retargetu / bramka re-latchu")
- Test (unit): `test/host/test_spot_lock.c`

**Podejście:**
- Zmienić WYŁĄCZNIE flagę `comms_gated` przekazywaną do `hold_or_pause` dla gałęzi `SRC_GOTO` (`true`→`false`). Gałąź `SRC_HOLD` już `false` — bez zmian.
- Bramka re-latchu `if (is_entering_goto || in->comms_fresh)` ZOSTAJE — to chroni null-island (P3) i realizuje retarget-w-locie.
- Predykat sensoryczny `!gps_fresh || !imu_ok || !gps_has_fix` ZOSTAJE i dalej pauzuje co cykl (fresh ≠ valid fix). Fizycznie osobny od bramki comms.

**Notatka wykonawcza:** Zacznij od przepisania testu wyroczni (utrata TYLKO linku, fix trzymany → HOLDING), potem flip. Test MUSI failować przeciw staremu kodowi i przechodzić po flipie.

**Wzorce do naśladowania:** istniejący `test_comms_gate_does_not_pause_ch3_hold` (test_spot_lock.c:528-547) — analogiczna struktura, odwrócona asercja dla goto.

**Scenariusze testowe:**
- [Unit] ARMED + `SRC_GOTO` ACTIVE + `comms_fresh=false` + fix ważny (`gps_fresh&&gps_has_fix&&imu_ok`) → substate ACTIVE, `throttle_cmd>0`, `ref_*` zachowany. (NOWY oracle odwrócenia — przepisany z `test_goto_pauses_on_comms_loss_...:463-491`; usuwamy testowaną funkcjonalność PAUSE, więc przepisanie jest zgodne z regułą, NIE osłabieniem asercji.)
- [Unit] ZACHOWANY: `comms_fresh=false` + upstream zeruje `goto_lat/lon` w wejściu → `ref_lat/lon_e7` == ostatni dobry (nie 0,0). Mutacja „bezwarunkowy re-latch" MUSI failować.
- [Unit] ZACHOWANY: `SRC_GOTO` ACTIVE + `gps_has_fix=false` (fix zgubiony) → PAUSED co cykl. Mutacja „usuń re-walidację sensoryczną" MUSI failować.
- [Unit] ZACHOWANY: `!armed` → OFF (precedence). `SRC_HOLD` + `comms_fresh=false` → nadal ACTIVE (bez regresu).

**Weryfikacja:** `test/host/run.sh` zielone; nowy test odwrócenia przechodzi, a przywrócenie `comms_gated=true` czyni go czerwonym (moc wyroczni potwierdzona ręcznie/komentarzem).

---

- [ ] **Unit 2: Komenda `hold` w warstwie parsowania (pure)**

**Cel:** Rozpoznanie keywordu `hold` jako osobnej intencji, bez współrzędnych.

**Wymagania:** R1, R2

**Zależności:** Brak.

**Pliki:**
- Modyfikuj: `components/web_panel/include/command_parse.h` (dodać `bool hold_request;` do `command_parse_result`; zaktualizować docstring listy keywordów)
- Modyfikuj: `components/web_panel/src/command_parse.c` (dodać `{"hold", {.ok=true, .hold_request=true}}` do `COMMAND_TABLE`)
- Test (unit): `test/host/test_command_parse.c`

**Podejście:** Dokładnie wzór `goto`/`goto_cancel`. `hold` to keyword-only (bez payloadu), więc `command_parse` tylko ustawia flagę.

**Wzorce do naśladowania:** `test_goto_maps_to_goto_request` (test_command_parse.c:105-115).

**Scenariusze testowe:**
- [Unit] `command_parse("hold")` → `ok=true`, `hold_request=true`, `goto_request=false`, `goto_cancel_request=false`.
- [Unit] Nieznany keyword nadal `ok=false` (regresja — istniejący test bez zmian).

**Weryfikacja:** host-tests zielone; `test_command_parse` +1 test.

---

- [ ] **Unit 3: Okablowanie `hold` + atomowy grab własnego fixu w pętli**

**Cel:** `hold` z HTTP → pętla łapie bieżący fix, waliduje i latchuje jako `SRC_GOTO` (cel = własna pozycja); brak fixu → nie angażuje.

**Wymagania:** R1, R2, R6 (bramka fixu)

**Zależności:** Unit 2 (flaga `hold_request`), Unit 1 (żeby kotwica persistowała po utracie linku).

**Pliki:**
- Modyfikuj: `components/control_loop/include/control_loop.h` (dodać `bool hold_request;` do `control_loop_ui_events`)
- Modyfikuj: `components/web_panel/src/http_server.c` (`to_ui_events`: przenieść `parsed.hold_request`; gałąź `hold` NIE przechodzi przez `extract_goto_target` — brak body z coords, więc nie zwracać 400 za brak lat/lon)
- Modyfikuj: `components/control_loop/src/control_loop.c` (`apply_goto_events`: na `hold_request` jeden `gps_get_state()`, przez pure helper zdecyduj engage; przy engage ustaw `s_goto_engage=true`, `s_goto_lat/lon_e7` z fixu, stamp `s_last_goto_ms`)
- Stwórz: pure helper decyzji grabu (np. w `components/web_panel/src/goto_target.c` — reużywa `goto_target_valid`) — sygnatura do ustalenia w implementacji
- Test (unit): `test/host/test_command_parse.c` lub `test/host/test_goto_target.c` (pure helper); okablowanie `control_loop.c`/`http_server.c` jako integracja HAL — cienka, weryfikacja przez helper + review

**Podejście:**
- Pure helper: `(bool fresh, bool fix, int32 lat_e7, int32 lon_e7) → {bool engage; int32 lat; int32 lon}` = `engage = fresh && fix && goto_target_valid(lat,lon)`. Host-testowany z mocą wyroczni.
- Atomowość: jeden odczyt `gps_get_state()` — lat i lon z tego samego sample (nie mieszać świeżego lon ze starym lat).
- `hold` nadpisuje bieżący cel (R5, ten sam latch). ARMED/neutral gating dzieje się dalej w `spot_lock_step` (bez zmian) — brak fixu/nie-ARMED → `goto_substate` zostaje off (widoczne w telemetrii).

**Notatka wykonawcza:** Helper grabu test-first (pure, moc wyroczni), potem cienki adapter HAL w `apply_goto_events`.

**Wzorce do naśladowania:** `extract_goto_target` + `goto_target_valid` w `http_server.c` (walidacja przed latchem); `apply_goto_events` (staging latcha).

**Scenariusze testowe:**
- [Unit] helper: `fresh=true, fix=true, lat/lon w zakresie` → `engage=true`, cel = wejście.
- [Unit] helper: `fix=false` (seed-fresh) → `engage=false`. Mutacja „pomiń bramkę fix" MUSI failować (wejście POZA — brak fixu — daje inny wynik niż bez bramki).
- [Unit] helper: lat/lon POZA int32 / INF (gdyby fix dostarczył śmieci) → `engage=false` (reużycie `goto_target_valid`).
- [Unit] `command_parse` regresja: `hold` nie ustawia `goto_lat/lon`.

**Weryfikacja:** host-tests zielone; ręczny przegląd: `hold` bez body nie zwraca 400; przy złapanym fixie `s_goto_engage` latchuje `SRC_GOTO` z własną pozycją.

### Faza 2 — iOS (mapowanie, usunięcie keepalive, resync)

- [ ] **Unit 4: Komenda `.hold` w kontrakcie + mapowanie przycisku + lokalna intencja**

**Cel:** Przycisk Spot-lock wysyła `{"cmd":"hold"}`; app pamięta lokalnie, że to kotwica (do etykiety).

**Wymagania:** R1, R5, R7

**Zależności:** Unit 2 (firmware rozumie `hold`).

**Pliki:**
- Modyfikuj: `ios/KayakKit/Sources/KayakContract/Command.swift` (`case hold`; `cmdName` → `"hold"`; `httpBody()` bez współrzędnych, jak `gotoCancel`)
- Modyfikuj: `ios/KayakMotor/App/AppModel.swift` (`requestSpotLock()` → `commands.send(.hold)` + `target.markSending/Sent` analogicznie do `sendGoto`; dodać lokalny stan intencji trybu, np. `enum AutonomousIntent { case none, goto, hold }`)
- Test (unit): `ios/KayakKit/Tests/KayakContractTests/CommandEnvelopeTests.swift`

**Podejście:**
- `.hold` serializuje do `{"cmd":"hold"}` (bez lat/lon). STOP (`gotoCancel`) i clearTarget bez zmian — anulują też kotwicę (R5, ten sam latch firmware).
- Lokalna intencja ustawiana przy tapnięciu przycisku; używana tylko do etykiety UI (nie do logiki sterowania).

**Wzorce do naśladowania:** `sendGoto` (AppModel.swift:94-104); serializacja `gotoCancel` w `Command.httpBody()`.

**Scenariusze testowe:**
- [Unit] `Command.hold.httpBody()` → JSON `{"cmd":"hold"}` (bez pól lat/lon).
- [Unit] `Command.hold.cmdName == "hold"`.
- [Unit] tap Spot-lock → `AutonomousIntent` = `.hold`; tap Goto → `.goto`.

**Weryfikacja:** testy KayakContract zielone; tap Spot-lock realnie POST-uje `hold` (weryfikacja na urządzeniu/symulatorze w Unit 7 scenariuszach).

---

- [ ] **Unit 5: Wyszarzanie przycisku Spot-lock bez fixu GPS (pure readiness)**

**Cel:** Przycisk Spot-lock nieaktywny/ostrzega, gdy telemetria nie pokazuje świeżego fixu (R6).

**Wymagania:** R6

**Zależności:** Unit 4.

**Pliki:**
- Stwórz: `ios/KayakKit/Sources/KayakContract/SpotLockReadiness.swift` (pure: `canEngage(telemetry)` + opcjonalny `blockReason`)
- Modyfikuj: `ios/KayakMotor/Features/Goto/ControlBarView.swift` (przycisk Spot-lock `enabled:` z readiness; wzór przycisku Goto)
- Test (unit): `ios/KayakKit/Tests/KayakContractTests/SpotLockReadinessTests.swift`

**Podejście:** Readiness liczony z `telemetry.gpsFix` (i ewentualnie stanu ARMED, jeśli telemetria go wystawia). Sticks-neutral nieznane app → nie bramkujemy nim w UI (firmware to dobramkuje, telemetria odbije). Pure, host-testowane jak `GotoReadiness`.

**Wzorce do naśladowania:** `GotoReadiness`/`GotoReadinessTests`; `iconButton(enabled:)` w ControlBarView (linie 56-70).

**Scenariusze testowe:**
- [Unit] `gpsFix=false` → `canEngage=false` (+ blockReason „brak GPS").
- [Unit] `gpsFix=true` (+ ARMED jeśli dostępne) → `canEngage=true`.
- [Unit] `telemetry=nil` (brak linku) → `canEngage=false`.

**Weryfikacja:** testy zielone; w UI przycisk 0.5 opacity + `.disabled` gdy brak fixu.

---

- [ ] **Unit 6: Usunięcie keepalive / idle-timer (persist nie potrzebuje resendu)**

**Cel:** App nie trzyma ekranu włączonego i nie resenduje goto; ekran może gasnąć, łódź płynie dalej (R3 od strony app).

**Wymagania:** R3

**Zależności:** Unit 1 (firmware nie pauzuje — dopiero wtedy usunięcie resendu jest bezpieczne).

**Pliki:**
- Modyfikuj: `ios/KayakMotor/App/AppModel.swift` (usunąć `keepaliveTick`, `KeepaliveController`, ustawianie `UIApplication.isIdleTimerDisabled`)
- Modyfikuj/Usuń: `ios/KayakKit/Sources/KayakContract/Keepalive.swift` (usunąć `KeepaliveDecision`; decyzja o pełnym usunięciu pliku w implementacji)
- Modyfikuj/Usuń: `ios/KayakKit/Tests/KayakContractTests/KeepaliveTests.swift` (usuwamy testowaną funkcjonalność → usunięcie testów zgodne z regułą)

**Podejście:** Usuwamy mechanizm, bo funkcjonalność (comms-watchdog jako failsafe) znika z systemu. To NIE osłabianie testów — to usunięcie testów wraz z usuwaną funkcją. `stop()` może przestać dotykać idle-timera (nigdy go nie włączamy).

**Scenariusze testowe:**
- [Unit] Po usunięciu: brak referencji do `KeepaliveDecision`/`isIdleTimerDisabled` (kompilacja + brak martwego kodu).
- [E2E/urządzenie] Ustaw goto → wygaś ekran na 30 s → łódź kontynuuje; po odblokowaniu app pokazuje tryb aktywny (weryfikacja z Unit 7). Uwaga: `/agent-browser` N/D dla natywnego iOS — weryfikacja manualna na symulatorze/urządzeniu.

**Weryfikacja:** build iOS zielony; ekran gaśnie normalnie; brak resendu w logach sieci.

---

- [ ] **Unit 7: Resync-on-resume + uzgodnienie intencji ze stanem firmware**

**Cel:** Po powrocie z tła/wygaszenia app reconnectuje telemetrię i odtwarza UI zgodnie z prawdą firmware (R8); jeśli pilot zakończył tryb — app to pokazuje.

**Wymagania:** R7, R8

**Zależności:** Unit 4 (lokalna intencja), Unit 6 (brak keepalive — resync zastępuje jego rolę „app żyje").

**Pliki:**
- Modyfikuj: `ios/KayakMotor/RootView.swift` (dodać `@Environment(\.scenePhase)` + `.onChange(of: scenePhase)` → na `.active` reconnect WS i resync)
- Modyfikuj: `ios/KayakMotor/App/AppModel.swift` (metoda `resync()`: reconnect telemetrii; uzgodnienie `AutonomousIntent` z `telemetry.gotoState`/`spotLockState`; odtworzenie pinezki celu z telemetrii jeśli dostępna)
- Stwórz: `ios/KayakKit/Sources/KayakContract/AutonomousModeReconciler.swift` (pure: `(intent, gotoState, spotLockState) → wyświetlany stan/etykieta`)
- Modyfikuj: `ios/KayakMotor/Networking/TelemetrySocket.swift` / `TelemetryStore` (jeśli potrzebny jawny `reconnect()`)
- Test (unit): `ios/KayakKit/Tests/KayakContractTests/AutonomousModeReconcilerTests.swift`

**Podejście:**
- Uzgodnienie (pure, host-testowane):
  - `gotoState==active` + intencja `.hold` → etykieta „Kotwica".
  - `gotoState==active` + intencja `.goto` → „Jadę do celu".
  - `gotoState==active` + intencja `.none` (po force-quit) → neutralna „Trzymam pozycję".
  - `gotoState==off` + `spotLockState==active` → „Pilot przejął (CH3)".
  - `gotoState==off` + `spotLockState==off` → tryb zakończony (wyczyść intencję/pinezkę).
- Reconnect WS na `.active`, bo iOS zawiesza socket w tle.

**Wzorce do naśladowania:** `LinkStateMachine`/`LinkStateTests` (pure state reconciliation); dekodowanie `HoldState` w Telemetry.

**Scenariusze testowe:**
- [Unit] intencja `.hold` + `gotoState=active` → „Kotwica".
- [Unit] intencja `.none` + `gotoState=active` → „Trzymam pozycję" (po force-quit).
- [Unit] `gotoState=off` + `spotLockState=active` → „Pilot przejął".
- [Unit] `gotoState=off` + `spotLockState=off` → stan wyczyszczony.
- [E2E/urządzenie] goto aktywne → background 20 s → foreground → UI pokazuje wciąż-aktywny tryb + pinezkę; następnie pilot override → app pokazuje „zakończono".

**Weryfikacja:** testy reconcilera zielone; ręczny cykl background→foreground na urządzeniu odtwarza stan; brak fałszywego „anulowano".

## Wpływ systemowy

- **Graf interakcji:** `POST /api/command` → `command_parse` → `to_ui_events` → `control_loop_post_ui_events` → `apply_goto_events` (nowy grab fixu) → `spot_lock_step` (odwrócona bramka comms). Telemetria `/ws` → iOS `TelemetryStore` → `AutonomousModeReconciler`.
- **Propagacja błędów:** brak fixu/nie-ARMED/drążki nie-neutral → brak engage, sygnalizowane przez `goto_substate=off` w telemetrii (app odbija). `hold` bez body: nie 400 (w przeciwieństwie do `goto`).
- **Ryzyka cyklu życia stanu:** latch `s_goto_engage` żyje w RAM (bez NVS) — reboot kasuje (akceptowalne, sesja live). iOS: force-quit gubi lokalną intencję → neutralna etykieta (obsłużone w Unit 7).
- **Parytet surface API:** kontrakt komend firmware↔iOS musi zgadzać się na keyword `hold` (Unit 2 ↔ Unit 4).
- **Pokrycie integracyjne:** persist-przez-utratę-linku i CH3-preempt-kotwicy to zachowania device-only (host-testujemy czyste decyzje; realny link/HW → known-issues, weryfikacja terenowa).

## Ryzyka i zależności

- **Odwrócenie zweryfikowanego zachowania (Unit 1)** — najwyższe ryzyko. Mitygacja: chirurgiczny flip jednej flagi, 4 testy z mocą wyroczni, rozdzielenie domen comms/sensor, RC-abort nietknięty.
- **Regres null-island** przy pokusie usunięcia `comms_fresh` — mitygacja: jawnie zostawiamy go w bramce re-latchu + test retencji.
- **Usunięcie keepalive przed Unit 1** dałoby okno, w którym app nie resenduje, a firmware jeszcze pauzuje → łódź staje. Mitygacja: sekwencja Unit 1 przed Unit 6.
- **iOS device-only weryfikacja** (background/link) — brak automatyzacji jak agent-browser; wymaga ręcznego testu na urządzeniu z AP silnika.

## Dokumentacja / Notatki operacyjne

- Zaktualizować `docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md` — oznaczyć P1 (PAUSE-on-link-loss) jako **odwrócone** tym planem, z linkiem; zaznaczyć że P2/P3 pozostają obowiązujące.
- Zaktualizować `docs/completed/kayak-motor-firmware-v1/known-issues.md §4d` — nowe zachowanie persist + device-E2E do weryfikacji terenowej (persist po utracie linku, CH3-preempt kotwicy, grab własnego fixu).
- Po wdrożeniu rozważyć `/dev-compound` dla nowego wzorca „latched command + RC-sole-failsafe" (follow-up, poza tym planem).

## Źródła i referencje

- **Dokument źródłowy:** [docs/dev-brainstorms/2026-07-02-app-spot-lock-requirements.md](../dev-brainstorms/2026-07-02-app-spot-lock-requirements.md)
- Powiązany kod: `components/control_loop/src/spot_lock.c`, `components/control_loop/src/control_loop.c`, `components/web_panel/src/command_parse.c`, `ios/KayakMotor/App/AppModel.swift`
- Wiedza instytucjonalna: `docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md`, `2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md`
