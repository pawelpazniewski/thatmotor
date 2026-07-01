# Podsumowanie ukończenia: Goto — autonomiczna nawigacja do punktu z aplikacji (WiFi)

**Zadanie:** goto-waypoint-navigation
**Branch:** `feature/goto-waypoint-navigation`
**Data ukończenia:** 2026-07-01

## Status końcowy

- **429/429 testów hosta (Unity) PASS** — standalone CMake+Unity (`test/host/run.sh`), +29 vs 400 baseline.
- **`idf.py build` (target esp32s3 N16R8) PASS** — bin 0xf1ce0, 37% wolne w partycji app.
- Wszystkie 4 fazy (Unit 1–6) zaimplementowane i zreviewowane (multi-agent review każdej fazy).
- **Review:** Faza 1 ⛔ 1× P1 (UB cast przed walidacją) naprawiony w 1 cyklu + ✅ CZYSTE re-review;
  Faza 2 ⚠️ 1× P2 (null-island/retencja celu w PAUSED) naprawiony w 1 cyklu + ✅ CZYSTE re-review;
  Faza 3 ✅ CZYSTE (0× P1/P2); Faza 4 ✅ CZYSTE (0× P1/P2). Pozostają tylko nity P3 (opcjonalne)
  opisane w `review-faza-{1,2,3,4}.md` + `review-faza-2-rereview.md`.
- Luki `[HW]`/`[E2E]` (realna nawigacja na wodzie, pauza/wznowienie na utratę linku, CH3-preempt
  w terenie, dryf, panel/aplikacja off→active→paused→arrived) **świadomie odłożone** do
  `docs/completed/kayak-motor-firmware-v1/known-issues.md` §4d — luki weryfikacji zależne od
  środowiska (brak przeglądarki / hardware / aplikacji iOS), NIE braki implementacji.

## Co zostało dostarczone

Autonomiczne **goto** — aplikacja iOS wysyła przez WiFi cel `lat/lon` (`POST /api/command`),
firmware kieruje kajak do punktu i utrzymuje go tam. Feature **reużywa istniejący silnik
spot-lock** (`spot_lock_step`): jedyna różnica to źródło celu (zewnętrzne `SRC_GOTO` zamiast
snapshotu bieżącej pozycji `SRC_HOLD` z CH3). Dołożono: kanał celu przez komendę HTTP, comms-watchdog
na link aplikacji (bramka świeżości TYLKO dla goto), arbitraż CH3↔goto i strukturalne pierwszeństwo
failsafe. Cała nowa logika decyzyjna pozostaje czystą funkcją host-testowaną (Pure ⊥ HAL). Maszyna
stanów i tor `rc_valid`/failsafe **nietknięte** — goto liczone WYŁĄCZNIE w gałęzi ARMED
(`resolve_spot_lock`), zero nowego toru failsafe. Aplikacja iOS poza scope (osobny plan).

- **Faza 1 — Kanał celu z aplikacji (Unit 1–2):**
  - Unit 1: czysty moduł `goto_target.*` — `goto_target_valid(lat_e7, lon_e7)` (±90°/±180° e7,
    named constants) + `goto_target_from_double` (walidacja `isfinite`+zakres w domenie double
    PRZED castem na int32, wynik fixu P1). `command_parse` rozpoznaje `goto`/`goto_cancel`.
  - Unit 2: `http_server.c::post_command` wyjmuje lat/lon (cJSON), waliduje; malformed/out-of-range
    → `400 {data:null,error:{code:"VALIDATION_FAILED"}}` bez postu do mailbox. `apply_goto_events`:
    latch `s_goto_engage` + staged cel + stempel `s_last_goto_ms` (`sensor_freshness_stamp`).
- **Faza 2 — Rdzeń decyzyjny (Unit 3):**
  - `spot_lock_step` rozszerzone o arbitraż źródła (`SPOT_LOCK_SRC_NONE/HOLD/GOTO`), pola wejść
    `goto_engage`/`goto_lat/lon_e7`/`comms_fresh`, `target_source` w stanie, `arrived` w wyjściu.
    Kolejność w ARMED: override→OFF; CH3→SRC_HOLD (snapshot, preempt goto); goto&&!CH3→SRC_GOTO;
    else OFF. Bramka `comms_fresh` TYLKO dla SRC_GOTO (SRC_HOLD nigdy nie pauzuje na link).
    Fix P2: `ref_*` (re)latchowane TYLKO gdy `is_entering_goto || comms_fresh` — retencja celu
    w PAUSED to własność czystego rdzenia, nie niejawny kontrakt latcha.
- **Faza 3 — Integracja + comms-watchdog (Unit 4, + przeniesiony plumbing Unit 5):**
  - `loop_step.c::build_spot_lock_inputs` przekazuje nowe pola do `spot_lock_step`;
    `goto_latch_should_clear` = `ARMED && goto_engage && (!sticks_neutral || ch3_on)` →
    `out.goto_latch_clear`. `control_loop.c::apply_goto_inputs` liczy
    `comms_fresh = sensor_is_fresh(now_ms(), s_last_goto_ms, goto_comms_timeout_ms)` KAŻDY cykl.
  - Cykl życia latcha (3 drogi kasowania): `goto_cancel`, override drążkiem, CH3-preempt.
    Pauza od linku/GPS = przejściowa (latch przetrwa, auto-resume tego samego celu).
  - Parametr `goto_comms_timeout_ms` dostarczony end-to-end (przeniesiony z Unit 5, bo watchdog
    potrzebował realnego timeoutu): model+bump `SETTINGS_SCHEMA_VERSION` v6→v7, ranges 200/5000/1500 ms,
    defaults, validate, `params_json` (`PARAMS_JSON_FIELD_COUNT` 31→32), `blob_codec` (63/67 B).
- **Faza 4 — Parametry (domknięcie) + telemetria + panel (Unit 5–6):**
  - Unit 5: dedykowany `test_armed_rejects_goto_comms_timeout_write` (409-w-ARMED, SI-6 niezmienione).
  - Unit 6: kontrakt WS dla iOS — pola snapshotu `goto_state` (≠0 tylko gdy SRC_GOTO — CH3 hold
    czyta off), `goto_target_lat/lon_e7`, `goto_err_m`, `goto_bearing_deg10`, `goto_arrived`,
    `app_link_fresh`. Populacja goto-specyficzna w `loop_telemetry.goto_*` (liczona z `sl` TYLKO
    gdy `target_source==SRC_GOTO`). `snapshot_to_json` serializuje ints/bools (`%u/%d/%s`), bufor
    512→640. Front-end: karta „Goto (app nav)" w `web/index.html` + `web/app.js`.

## Podjęte kluczowe decyzje

| Decyzja | Wybór |
|---|---|
| Reuse silnika spot-lock, oś rozszerzenia = źródło celu | goto NIE jest nowym regulatorem, tylko inne `ref_*` (zewnętrzne) + warstwa bezpieczeństwa sieciowego; rename modułu odrzucony (churn w przetestowanym kodzie) |
| CH3 = priorytet fizyczny nad goto | CH3 ON w trakcie goto → przerwij + hold „tu i teraz" (snapshot); detekcja preempcji keyed na `target_source != SRC_HOLD`, nie na `substate==OFF` |
| Utrata linku → PAUSED (nie abort), bramka świeżości TYLKO dla SRC_GOTO | SRC_HOLD jest RC-owy, nigdy nie pauzuje na utratę linku app; cel zapamiętany, auto-resume po powrocie |
| goto na istniejącej JEDNEJ bramce failsafe | liczone wyłącznie w ARMED (`resolve_spot_lock`), zero nowego toru failsafe; wejścia goto NIGDY do `rc_valid`/`sm_inputs` |
| Override drążkiem kasuje latch goto | brak auto-resume po powrocie drążka; `goto_latch_clear` rozróżnia pauzę (false) od abortu (true) |
| Współrzędne `int32 e7` na łączu, walidacja w domenie double przed castem | JSON nie zna int32 — walidacja `isfinite`+zakres na oryginalnej wartości z łącza PRZED castem (fix P1: naive cast dawał UB + wrap-into-range bypass) |
| Retencja celu SRC_GOTO w PAUSED jako własność czystego rdzenia | `ref_*` pisane TYLKO gdy `is_entering_goto || comms_fresh` (fix P2: eliminuje hazard null-island niezależnie od latcha upstream) |
| `goto_comms_timeout_ms` przeniesiony z Fazy 4 do Fazy 3 | watchdog potrzebował realnego timeoutu zamiast magic number; bump schematu v6→v7 z round-trip testem |
| `goto_state` pole GOTO-SPECYFICZNE (nie współdzielone ze `spot_lock_state`) | app dostaje jednoznaczny sygnał „czy goto aktywne", odseparowany od CH3-holdu (parytet API iOS) |
| Telemetria na intach/boolach | zero floatów na WS; podziały display-side w panelu; panel bez XSS (`textContent`) |

## Główne utworzone/zmodyfikowane pliki

Utworzone (czysty moduł, host-testowany):
- `components/web_panel/{include,src}/goto_target.{h,c}` — walidacja celu (pure, grep-clean)

Modyfikacje — rdzeń decyzyjny:
- `components/control_loop/include/spot_lock.h` + `src/spot_lock.c` — arbitraż źródła, bramka linku, `arrived`
- `components/control_loop/include/loop_step.h` + `src/loop_step.c` — plumbing goto, `goto_latch_should_clear`, `loop_telemetry.goto_*`
- `components/control_loop/include/control_loop.h` + `src/control_loop.c` — UI events goto, `apply_goto_inputs` (watchdog), `apply_goto_events` (latch), `publish_snapshot`

Modyfikacje — kanał komendy + telemetria + parametry:
- `components/web_panel/include/command_parse.h` + `src/command_parse.c` — keywordy goto/goto_cancel
- `components/web_panel/src/http_server.c` — handler goto (cJSON, 400 na błąd)
- `components/web_panel/src/params_json.c` — `goto_comms_timeout_ms` w `/api/params`
- `components/web_panel/src/ws_telemetry.c` — serializacja pól goto (bufor 512→640)
- `components/settings/include/settings_model.h` — `goto_comms_timeout_ms` + bump `SETTINGS_SCHEMA_VERSION` v6→v7
- `components/settings/{settings_ranges.h, src/settings_defaults.c, src/settings_validate.c, src/blob_codec.{c,h}}`
- `web/index.html` + `web/app.js` — karta „Goto (app nav)"

Testy hosta (utworzone/rozszerzone):
- `test/host/test_goto_target.c` (nowy) — walidacja + double-domain oracle (INF/NaN/wrap)
- `test/host/{test_spot_lock.c, test_loop_step.c, test_command_parse.c, test_settings_validate.c, test_blob_codec.c, test_params_decide.c}` — rozszerzone

Dokumentacja:
- `docs/completed/kayak-motor-firmware-v1/known-issues.md` — sekcja §4d (luki HW/E2E goto)

## Wyciągnięte wnioski

1. **Walidacja MUSI działać na oryginalnej wartości z łącza, nie na już-zawężonej przez cast**
   (finding P1 Fazy 1) — `(int32_t)valuedouble` przed walidacją to UB dla INF/NaN + wrap-into-range
   bypass (`4.39e9` → `1e8` przechodzi). JSON nie zna int32; walidacja `isfinite`+zakres w domenie
   double PRZED castem. Oracle-testy: mutacja naive-cast FAILuje.
2. **Retencja stanu przez lukę linku to własność czystego rdzenia, nie niejawny kontrakt latcha
   upstream** (finding P2 Fazy 2) — śledzenie `ref_*` na żywo co cykl nadpisywało cel także w PAUSED
   przed bramką → hazard null-island jeśli warstwa loop wyzeruje cel. Fix: jeden predykat
   `is_entering_goto || comms_fresh` — dokładnie jedna gałąź pisze (świeży link / wejście), druga
   zachowuje (pauza). Test: naive overwrite → ref=(0,0) FAILuje.
3. **App-driven override z comms-watchdogiem bez naruszenia failsafe RC** — sensor sieciowy (link app)
   podpięty jako wejście override'u liczonego PO maszynie stanów, WYŁĄCZNIE w gałęzi ARMED; nigdy do
   `rc_valid`/`sm_inputs`. `comms_fresh = sensor_is_fresh(...)` co cykl (fresh≠valid), wrap-safe
   w jednej domenie zegara `now_ms()`. Bramka linku TYLKO dla SRC_GOTO — SRC_HOLD (RC-owy) nigdy nie
   pauzuje na link. Test failsafe-precedence (`test_goto_rc_loss_failsafe_wins`) wchodzi w stan
   przeciekający (drifted+aligned=WOULD thrust) i asertuje ESC=1500+servo=center+OFF.
4. **Pauza vs abort rozróżnione strukturalnie sygnałem latcha** — `goto_latch_clear==false` (pauza:
   link/GPS-loss, przejściowa, auto-resume) vs `==true` (abort: override/CH3-preempt, latch skasowany,
   brak resume). Cykl życia latcha = jeden predykat `goto_latch_should_clear`, nie warunki rozsiane.
5. **Rozszerzenie przetestowanego silnika bez regresji przez korektę modelu fixture, nie osłabienie
   asercji** — `make_active_state` (+`target_source=SRC_HOLD`) to KONIECZNA korekta modelu pod nowe
   keyowanie `run_ch3_hold` (bez niej carry-over testy failują na OFF), nie test-weakening. Wszystkie
   16 oryginalnych asercji spot-lock nietknięte; diff testów wyłącznie addytywny (+29 przez 4 fazy).
6. **Telemetria goto gated na źródle, nie na substate silnika** — `goto_state` liczone z
   `spot_lock_outputs` TYLKO gdy `target_source==SRC_GOTO`, inaczej off/0. Ten sam silnik spot-lock nie
   myli CH3-holdu z goto → app dostaje jednoznaczny kontrakt. Test oracle: CH3-hold ⟹ spot_lock ACTIVE
   ORAZ goto off/0; naiwny mirror bez bramki źródła FAILuje.
7. **Bump `SETTINGS_SCHEMA_VERSION` = wzorzec migracji do defaults + round-trip + anchor rozmiaru**
   — v6→v7 z round-trip testem non-default i anchorem rozmiaru blobu (63/67 B); update anchora to nie
   test-weakening, tylko odzwierciedlenie legalnej zmiany schematu. Nowa partycja/pole nie przesuwa
   istniejących offsetów.

## Powiązane dokumenty

- Plan: `goto-waypoint-navigation-plan.md`
- Kontekst: `goto-waypoint-navigation-kontekst.md`
- Zadania (pełna lista z findingami review): `goto-waypoint-navigation-zadania.md`
- Raporty review: `review-faza-{1,2,3,4}.md`, `review-faza-2-rereview.md`
- Known issues (odroczone `[HW]`/`[E2E]`): `docs/completed/kayak-motor-firmware-v1/known-issues.md` §4d
- Requirements: `docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md`
- Plan techniczny: `docs/plans/2026-07-01-001-feat-goto-waypoint-navigation-plan.md`
- Wiedza instytucjonalna: `docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md`

## Otwarte domknięcia zadaniowe (poza scope implementacji)

- Dokumentacja kontraktu API dla aplikacji iOS (goto/goto_cancel, keepalive ~2 Hz, pola `goto_*`) — do osobnego dokumentu / planu iOS.
- Rozważ `/dev-compound` dla wzorca „app-driven override z comms-watchdogiem bez naruszenia failsafe RC" (uruchamia orkiestrator osobno).

## Commity feature

- `9c6f103` docs: inicjalizacja planu
- `4e0b4c0` Faza 1 — kanał celu (walidacja + transport)
- `ffbc43a` fix P1 Fazy 1 — walidacja w domenie double przed castem
- `d3b1d9c` Faza 2 — rozszerzenie spot_lock (arbitraż CH3/goto + bramka linku)
- `28d3c7c` fix P2 Fazy 2 — retencja celu SRC_GOTO w PAUSED
- `f019b33` Faza 3 — integracja w pętli + comms-watchdog + cykl życia latcha
- `fcc8743` Faza 4 — telemetria goto + panel + test 409-w-ARMED
