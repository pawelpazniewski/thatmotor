# Review Faza 5 — Unit 9: ESC_RANGE_CALIBRATION (tryb serwisowy)

**Commit:** `de3447e` — feat(kayak-motor-firmware): Faza 5 — ESC_RANGE_CALIBRATION (krokowy, z abortem, przez clamp)
**Data review:** 2026-06-17
**Metodologia:** dev-docs-review, 5 perspektyw (Security, Performance, Architecture+Quality, Test Coverage, E2E).
**Cross-reference:** Plan techniczny Unit 9 (`docs/plans/2026-06-16-001-feat-kayak-motor-firmware-v1-plan.md:586-628`).

---

## Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI

**P1 (blocking): 0 · P2 (important): 4 · P3 (nit): 5**
Typy findingów: **KOD: 1** (P2 SRP) · **TEST: 6** (1×P2 oracle, 2×P2 luka pokrycia, 3×P3) · **E2E: N/A**

---

## Weryfikacja na żywo

- **Testy hosta:** `./test/host/run.sh` → **150/150 PASS, 0 Failures, 0 Ignored** (131→150, +19). Zweryfikowane na żywo.
- **Build:** `. "$HOME/esp/idf-env.sh" && idf.py set-target esp32 && idf.py build` → **EXIT=0**. Zweryfikowane na żywo.
- **E2E:** **N/A** — brak UI w fazie 5 (panel = Unit 10). Sekwencja sterowana zdarzeniami `calib_event` z warstwy, której producent powstaje w Unit 10.

---

## Werdykty inwariantów bezpieczeństwa

### SI-5 (wejście niemożliwe bez jawnego potwierdzenia): **TAK** ✅

`can_enter_calibration` (`state_machine.c:13-18`) wymaga KONIUNKCJI wszystkich warunków:
`rc_valid && throttle_neutral && ui_calib_request && ui_calib_confirm && !settings_apply_in_progress`.
Brak któregokolwiek → `false` → brak wejścia. Guard wołany WYŁĄCZNIE z `next_from_disarmed`, gdzie poprzedza go dominujący `if (!rc_valid) return FAILSAFE` — warunek DISARMED egzekwowany topologicznie. Wymaga DWÓCH odrębnych sygnałów (`ui_calib_request` ORAZ `ui_calib_confirm`) — nigdy nie startuje automatycznie.

Dowód testowy MOCNY: `test_state_machine.c` testuje każdy warunek osobno metodą drop-one z baseline `calib_entry_inputs()`:
- brak confirm → DISARMED · brak request → DISARMED ("nigdy automatycznie") · brak neutral → DISARMED · rc_invalid → **FAILSAFE (nie calib)** · wszystkie → ESC_CALIBRATION.
Dodatkowo `test_calibration_entry_requires_confirmation` (poziom loop).

### SI-3 (clamp nieomijalny w trybie serwisowym): **TAK strukturalnie / dowód testowy SŁABY** ⚠️

Inwariant SPEŁNIONY: `run_calibration` (`loop_step.c:82-99`) ma JEDYNY `return clamp_esc(co.esc_us)` — każda stała kalibracji routowana przez `clamp_esc`→`clamp_pwm_us` z oknem `[1000,2000]`. Nie istnieje gałąź w `resolve_esc`/`run_calibration` zwracająca ESC z pominięciem clampa. Sam `clamp_pwm_us` jest fail-safe (normalizuje odwrócone okno, działa w release/NDEBUG) i osobno udowodniony behawioralnie (`test_safety_clamp.c`: 500→1000, 3000→2000, inverted-window).

**ALE** dedykowany test ścieżki serwisowej (`test_calibration_output_stays_within_hard_clamp`) NIE ma mocy wyroczni — patrz P2-1. SI-3 jest dowiedzione przez kompozycję (clamp osobno + strukturalny single-return), nie behawioralnie na samej ścieżce calib.

### Bypass łańcucha gazu (CH2 nie wpływa na wyjście calib): **TAK** ✅ (dowód MOCNY)

`resolve_esc` gdy `state==ESC_CALIBRATION` zwraca `run_calibration`, całkowicie omijając `throttle_chain_step`. `test_calibration_steps_emit_constants_bypassing_throttle` trzyma CH2=2000 (full throttle) przez całą sekwencję i asertuje ESC=1500/2000/1000 wg KROKU, nie wg sticka — gdyby chain nie był bypassowany, krok NEUTRAL przy full throttle dałby ~2000, nie 1500.

### Precedencja abortów (RC loss dominuje): **POPRAWNA** ✅

`calib_step_next` (`esc_calibration.c:83-92`): `!rc_valid`→FAILSAFE PIERWSZE, potem timeout/cancel→DISARMED, na końcu advance. Pokryte `test_rc_loss_takes_precedence_over_cancel_and_timeout` (rc+timeout+cancel jednocześnie→FAILSAFE) oraz `test_calibration_rc_loss_aborts_to_failsafe` (pełna ścieżka przez debounce). Dwupoziomowa obrona: `next_from_calibration` niezależnie wymusza FAILSAFE przy `!rc_valid`.

---

## Regresje: BRAK ✅ (150/150)

Zmiana `build_sm_inputs` (`loop_step.c:52`: `calib_in_progress = state==ESC_CALIBRATION`) nie łamie istniejących ścieżek:
- `can_arm` używa `!calib_in_progress` — w DISARMED state≠CALIBRATION → =false, arm guard jak wcześniej.
- `throttle_target_for` wciąż NEUTRAL poza ARMED; calib dostaje neutral target, ale i tak bypassowany.
- Testy FSM (boot/DISARMED/ARMED/FAILSAFE) i loop_step niezmienione, dalej sensowne; `armable_inputs` rozszerzone o 2 pola=false (neutralnie).
Wszystkie wcześniejsze testy PASS (131 sprzed fazy + 19 nowych = 150).

---

## Architektura / czystość: PASS

- **PURE potwierdzone:** `esc_calibration.c/.h` — jedyne includy `esc_calibration.h`, `<stdbool.h>`, `<stdint.h>`. Zero ESP-IDF/FreeRTOS/driver. Host-testowalne.
- **Discriminated enums:** `calib_step` / `calib_event` / `calib_exit` rozdzielone; typed exit zamiast magic int.
- **Zero circular deps:** `state_machine` nie referuje `control_loop`. Kierunek `control_loop → state_machine(esc_calibration) → signal_chain` zachowany.
- **Layer boundaries:** `safety_clamp` jawnie dodany do REQUIRES `control_loop` — poprawne (`clamp_esc` używa `clamp_pwm_us` + typu `PwmWindow`; jawna zależność zamiast tranzytywnej).
- **Rozmiary:** wszystkie pliki <300 (max `loop_step.c`=147), funkcje <50, nesting ≤2 (early-return). PASS.
- **Named constants:** `CALIB_NEUTRAL/FORWARD/REVERSE_US`, `ESC_WINDOW_MIN/MAX_US`, sufiks `U`. Zero magic µs.

## Performance: PASS (real-time 50 Hz)

Cała ścieżka calib O(1), zero alokacji, zero blokad. `switch` po enumach stało-czasowy; struktury `calib_inputs`/`calib_outputs` małe POD-y (~12-16 B) przez wartość/const-ptr. Branże `resolve_esc` mutually exclusive (calib XOR throttle) — brak kosztu na ścieżce ARMED. Stack płytki, bez rekurencji/VLA/alloca. Brak findingów.

---

## Odchylenia od planu

- Plan (`:601`) podał ścieżkę testu `components/state_machine/test/test_esc_calibration.c`; faktycznie `test/host/test_esc_calibration.c`. **NIE jest to odchylenie** — projekt konsekwentnie centralizuje testy hosta w `test/host/` (wszystkie fazy 0-4), spójna konwencja harnessu Unity. Plik istnieje, zarejestrowany w `test/host/CMakeLists.txt` i `test_main.c`.
- Reszta zgodna z planem: pure sub-machine, mapowanie krok→stałe, guard wejścia, bypass z clampem, reguły abortu. Sekwencja sterowana zdarzeniami (brak automatu na timerze) — zgodnie z `:609`.

---

## Findingi

### 🟠 [P2-important] — TEST: dowód SI-3 na ścieżce serwisowej bez mocy wyroczni
**`test/host/test_loop_step.c:236-254`** (`test_calibration_output_stays_within_hard_clamp`)
Stałe kalibracji 1000/1500/2000 == DOKŁADNIE granice okna `[1000,2000]`. Asercje `GREATER_OR_EQUAL 1000`/`LESS_OR_EQUAL 2000` przejdą IDENTYCZNIE czy `clamp_esc()` jest na ścieżce, czy nie — usunięcie `clamp_esc` z `run_calibration:98` (zwrot surowego `co.esc_us`) NIE wywróciłoby testu. Test dowodzi "stałe są legalne", nie "clamp jest na ścieżce". Plan explicite żądał "dowodu SI-3 w trybie serwisowym" (`:624`); reguła §2 zakazuje słabych wyroczni. Inwariant fizycznie SPEŁNIONY (clamp osobno udowodniony + single-return strukturalnie), więc NIE blocker, ale test wymaga wzmocnienia. Rekomendacja: przemianować obecny test na "wartości stałych w oknie" + dodać behawioralny dowód clampa na ścieżce (np. parametryzowalny test `clamp_esc`/`clamp_pwm_us` z wartością poza oknem powiązany z dowodem strukturalnym).

### 🟠 [P2-important] — KOD: `resolve_esc` miesza odpowiedzialności (SRP)
**`components/control_loop/src/loop_step.c:105-119`**
`resolve_esc` robi trzy rzeczy: (1) wykrywa wejście calib i MUTUJE `state->calib_step` (`:109-113`), (2) wybiera gałąź calib-vs-throttle, (3) zwraca wyjście ESC. Detekcja przejścia + reset stanu to osobna odpowiedzialność od routingu wyjścia (reguła §14/SRP). Działa poprawnie i <50 linii — nie blokuje. Rekomendacja: wydzielić `reset_calib_on_entry(state, sm)` wołany przed `resolve_esc`.

### 🟠 [P2-important] — TEST: abort przez timeout niepokryty integracyjnie (loop)
**`test/host/test_loop_step.c`** (brak)
Pole `calib_timeout` (`loop_step.h:41`) przekazywane w `run_calibration:89`, ale ŻADEN test integracyjny go nie ustawia. Ścieżka `in->calib_timeout → co.exit → calib_exit_state → DISARMED` niezweryfikowana behawioralnie na poziomie loop (tylko sub-machine: `test_timeout_aborts_to_disarmed_neutral`). Dodać `test_calibration_timeout_returns_to_disarmed_neutral`.

### 🟠 [P2-important] — TEST: ramka wejścia z `calib_event` niepokryta
**`test/host/test_loop_step.c`** (brak), kontrakt **`components/control_loop/src/loop_step.c:109-116`**
W ramce wejścia `entering_calib` resetuje step→NEUTRAL, ale ta sama ramka woła `run_calibration` z `in->calib_event`. Jeśli warstwa UI (Unit 10) wyśle `ui_calib_request+confirm` ORAZ `calib_event=NEXT/CANCEL` w jednej ramce, sekwencja przeskoczy NEUTRAL→FORWARD (2000 zamiast 1500 na pierwszej ramce) lub natychmiast wyjdzie. `enter_calibration()` helper zawsze wchodzi z `CALIB_EVENT_NONE`, więc interakcja poza pokryciem. Bezpieczeństwo nienaruszone (wyjście dalej przez clamp), ale dla trybu safety-krytycznego to realny edge case + kontrakt do domknięcia w Unit 10. Rekomendacja: na ramce `entering_calib` wymusić `CALIB_EVENT_NONE` (lub udokumentować kontrakt entry-frame) + dodać test.

### 🟡 [P3-nit] — TEST: re-entry po DONE/cancel nieprzetestowane
**`test/host/test_loop_step.c`** (brak), `loop_step.c:111-113`. Brak testu że po wyjściu calib→DISARMED ponowne wejście re-inicjalizuje `calib_step`. Tryb wielokrotnego użytku; logika prosta.

### 🟡 [P3-nit] — TEST: `settings_apply_in_progress` w entry guard bez drop-one
**`components/state_machine/src/state_machine.c:17`**. 5. warunek `can_enter_calibration` nie ma osobnego drop-one testu; martwy na ścieżce loop (`build_sm_inputs:54` hardcoduje `false`). Domknąć gdy Unit 10/8 podłączy realny producent.

### 🟡 [P3-nit] — KOD: `calib_in_progress` poza guardem wejścia (asymetria z `can_arm`)
**`components/state_machine/src/state_machine.c:13-18`**. `can_arm` blokuje przy `calib_in_progress`, `can_enter_calibration` nie. Nieosiągalne (guard tylko z DISARMED, tam calib_in_progress=false). Defensywna spójność, nie luka — zostawić jak jest (vs. defensive over-engineering).

### 🟡 [P3-nit] — KOD: `calib_exit_state` bez jawnego switch po wszystkich enumach
**`components/control_loop/src/loop_step.c:69-76`**. Wołane tylko gdy `exit != NONE`, ale dla discriminated enum czytelniejszy byłby jawny `switch` po `calib_exit` (spójność z `calib_us_for_step`/`step_after`). Stylistyczne.

### 🟡 [P3-nit] — KOD: `step_after(CALIB_STEP_DONE)` przez wspólny `default`
**`components/state_machine/src/esc_calibration.c:31-43`**. `DONE` i `REVERSE` dzielą gałąź `default→DONE`. Działa (REVERSE→DONE zamierzone), `DONE` jako wejście nieosiągalne. Rozważyć komentarz; bez zmiany.

---

## Top findingi

1. **🟠 P2 (TEST):** SI-3 dowiedzione strukturalnie, ale dedykowany test clampu na ścieżce calib jest bez mocy wyroczni (stałe == granice okna) — wzmocnić, plan tego żądał.
2. **🟠 P2 (KOD):** `resolve_esc` łączy reset stanu wejścia z routingiem wyjścia (SRP).
3. **🟠 P2 (TEST):** timeout-abort i entry-frame-z-eventem niepokryte integracyjnie na poziomie loop.

Brak P1. Oba inwarianty bezpieczeństwa spełnione (SI-5 mocno, SI-3 strukturalnie). Faza nadaje się do kontynuacji po uzupełnieniu testów (P2) — najlepiej domknąć równolegle przy Unit 10 (producent `calib_event`/timeout/UI).

---

## Re-review po cyklu 1 (commit 861c878)

**Severity gate: ✅ CZYSTE** (P1=0, P2=0, P3=5 — wszystkie 5 P3 to przeniesione, świadomie odroczone nity z poprzedniego cyklu; zero nowych findingów).

**Weryfikacja na żywo:**
- Testy hosta: `./test/host/run.sh` → **154/154 PASS** (150→154, +4 nowe testy calib). 0 Failures, 0 Ignored.
- Build: `. idf-env.sh && idf.py set-target esp32 && idf.py build` → **EXIT=0**, `kayak-motor-firmware.bin` 0x375d0 B (86% partycji wolne).

**Liczniki:** P1=0 / P2=0 / P3=5 (przeniesione, bez zmian).

### Status 4 P2 — wszystkie ROZWIĄZANE

- [x] **P2-1 (SI-3 oracle) — ROZWIĄZANE, REALNA WYROCZNIA.** Dodano `test_calib_clamp_esc_snaps_out_of_window_value_to_boundary` (`test_loop_step.c:259-277`). Test woła nowo wyeksponowaną `calib_clamp_esc(esc_us, window)` — JEDYNY clamp na ścieżce `run_calibration` (przez `compute_calib_esc:123`). Wyrocznia: stała FORWARD=2000 z ZAWĘŻONYM oknem `{1000,1500}` → wg `clamp_pwm_us` (2000 > hi=1500) MUSI dać 1500. Gdyby clamp usunąć z ścieżki (zwrot surowego `co.esc_us`), wartość przeszłaby jako 2000 i asercja `EQUAL_UINT32(1500U, ...)` PADŁABY. To NIE tautologia — okno celowo wykracza poza stałą, czego domyślne `[1000,2000]` (gdzie stałe == granice) nie potrafiło wykazać. Stary test przemianowany na `test_calibration_constants_lie_within_clamp_window` z jawnym komentarzem że sam nie dowodzi clampu (asercje window-bound zachowane, nie osłabione). Potwierdzam logicznie: usunięcie clampu łamie nowy test.
- [x] **P2-2 (SRP resolve_esc) — ROZWIĄZANE.** `resolve_esc` (`loop_step.c:147-158`, 12 linii) rozbite na czyste funkcje jednej odpowiedzialności: `esc_clamp_window()` (źródło okna), `is_calib_entry_frame()` (czysty predykat), `reset_calib_on_entry()` (mutacja stanu, void), `compute_calib_esc()` (obliczenie cyklu+clamp), `run_calibration()` (routing exit→state). Każda <50 linii, czytelna w 5 s. Zachowanie niezmienione — wszystkie testy DISARMED/ARMED/FAILSAFE/calib dalej PASS. Drobna duplikacja warunku `entering_calib` między `is_calib_entry_frame` i `reset_calib_on_entry` (DRY) — akceptowalna per §11 (Duplication > Complexity), nie podnoszę jako finding.
- [x] **P2-3 (timeout) — ROZWIĄZANE.** `test_calibration_timeout_returns_to_disarmed_neutral` (`test_loop_step.c:279-297`) ustawia `calib_timeout=true` na ścieżce loop i asertuje `state==DISARMED` + `esc_us==ESC_NEUTRAL_US`. Pokrywa łańcuch `in->calib_timeout → co.exit → calib_exit_state → DISARMED+neutral`.
- [x] **P2-4 (entry-frame) — ROZWIĄZANE.** Kontrakt entry-frame domknięty w kodzie: `compute_calib_esc` na ramce wejścia wymusza `event=CALIB_EVENT_NONE` i `timeout=false` (`loop_step.c:115-117`). Test `test_calibration_entry_frame_ignores_event_starts_at_neutral` (`:299-322`): co-arriving `calib_event=NEXT` na ramce wejścia → wyjście 1500 (NEUTRAL), nie 2000 (FORWARD). Bonus: `test_calibration_reentry_reinitialises_step_to_neutral` (`:324-347`) pokrywa też P3 re-entry. Granica Unit 10 udokumentowana w `zadania.md` ("Do poprawy po review fazy 5"): producent UI nadal nie istnieje, ale kontrakt entry-frame jest domknięty po stronie loop_step — event na ramce wejścia nie może przeskoczyć kroku.

### Regresje: BRAK

- Testy: 150→**154**, wszystkie PASS. Zero osłabionych asercji (renamed test zachował `GREATER_OR_EQUAL`/`LESS_OR_EQUAL` window-bound + dodano equality oracle).
- Refaktor `resolve_esc` nie złamał ścieżek: `test_disarmed_full_throttle_outputs_neutral_esc`, `test_armed_throttle_tracks_with_ramp`, `test_rc_invalid_failsafe_soft_stop_and_center`, wszystkie testy calib — PASS.
- SI-5 nadal egzekwowany: drop-one testy `test_calib_entry_without_confirmation/request/neutral_refused` + `test_calib_entry_rc_invalid_goes_failsafe_not_calib` obecne i PASS.
- Precedencja RC-loss: `test_rc_loss_takes_precedence_over_cancel_and_timeout` PASS.
- Limity §1: `loop_step.c` 186 linii (<300), wszystkie funkcje <50 linii, nesting ≤2. (`test_loop_step.c` 428 linii — przekracza wytyczną 300, ale to plik testowy z kolokowanymi testami loop, pre-existing wzorzec, nie regresja tej naprawy.)

**Werdykt re-review:** wszystkie 4 P2 rozwiązane realnie (nie kosmetycznie); SI-3 test to prawdziwa wyrocznia; zero regresji. Pozostałe 5 P3 świadomie odroczone do Unit 8/10. Faza 5 zamknięta — gotowa do kontynuacji.
