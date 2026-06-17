# Code Review — Faza 3 (Unit 6 + 7): Maszyna stanów + pętla 50 Hz + watchdog + single-writer

**Commit:** `b6232ba` · **Data review:** 2026-06-16 · **Reviewer:** dev-autopilot (5 perspektyw)
**Zakres:** `git diff b6232ba~1 b6232ba` — Unit 6 (state_machine), Unit 7 (loop_step + control_loop),
domknięcie kontraktu epoch/recency (cross-phase: rc_capture / rc_validity / signal_chain).

---

## Severity gate

**✅ CZYSTE → KONTYNUUJ** (P1=0, P2=0, P3=4).

Brak problemów blokujących i brak P2. Cztery drobne P3 (nity / luki testowe HAL) — do rozważenia,
nie blokują Fazy 4. Wszystkie testy hosta i build na żywo przechodzą.

| Severity | Liczba | Typy |
|----------|--------|------|
| 🔴 P1 [blocking] | 0 | — |
| 🟠 P2 [important] | 0 | — |
| 🟡 P3 [nit] | 4 | 1× KOD, 3× TEST |
| 🌐 E2E | N/A | brak UI w Fazie 3 |

---

## Weryfikacja na żywo

- **Testy hosta:** `./test/host/run.sh` → **103/103 PASS, 0 Failures** (83 baza + 15 state_machine + 5 loop_step). Zgodne z oczekiwaniem (103/103).
- **Build firmware:** `. idf-env.sh && idf.py set-target esp32 && idf.py build` → **EXIT 0**, „Project build complete".

---

## Werdykt o kontrakcie EPOCH/RECENCY: **POPRAWNY** (nie dziura)

Cross-phase'owa zmiana w `rc_capture` / `rc_validity` jest **spójna i bezpieczna**. Szczegółowo:

1. **Ta sama domena, ten sam wrap.** ISR stempluje
   `last_edge_ticks = (uint32_t)(esp_timer_get_time() * 80)` (`rc_capture.c:20-23,77`), a
   `rc_capture_now_ticks()` zwraca *identyczne* wyrażenie (`rc_capture.c:155-158`). Oba: monotoniczny
   `esp_timer` (µs, int64) × 80 → truncate do uint32 → wrap przy 2^32 ticków ≈ 53,687 s. Producent i
   konsument są tej samej skali i tego samego modulo — kluczowy warunek z Fazy 1 spełniony.

2. **Recency wygasa przy braku zboczy (R6 — KLUCZOWE).** Gdy RC milknie, `last_edge_ticks` zamrożone,
   a `now_ticks` postępuje w czasie rzeczywistym (esp_timer biegnie niezależnie od zboczy).
   `cap_ticks_elapsed(now, prev) = now - prev` rośnie aż przekroczy `edge_timeout_us` →
   `channel_valid` false → debounce (N=5) → `sm_step` w każdym stanie z RC invalid → **FAILSAFE**.
   Zweryfikowane testem `test_rc_invalid_failsafe_soft_stop_and_center` (stary edge 40 ms,
   `edge_timeout` 30 ms → latched FAILSAFE, ESC=neutral, serwo=center).

3. **Modularne odejmowanie poprawne wokół wrapu.** `now - prev` w uint32 daje prawdziwy elapsed przez
   jedno przepełnienie, dopóki elapsed < 2^32 (53,6 s ≫ timeout failsafe rzędu dziesiątek ms). Testy
   wrapu z Fazy 1 (`test_rc_validity`) nadal przechodzą.

4. **width_us / period_us niezależne od epoch.** Nadal liczone z `edata->cap_value` (sprzętowy licznik
   MCPWM) przez `cap_period_us` / `cap_ticks_to_us` (`rc_capture.c:66-67,82-83`) — całkowicie odcięte
   od domeny esp_timer. Recency (esp_timer) i period (licznik HW) nigdy nie są od siebie odejmowane:
   `edge_recent` porównuje recency z `edge_timeout_us`, `period_in_tolerance` porównuje period z
   `period_expected_us` — żadnego cross-domain miksu.

**Zgodność z zakazem z Fazy 1.** Faza 1 zabraniała podawać *surowy* `esp_timer_get_time()` (µs) jako
`now_ticks` — bo to inna skala i brak wrapu przy 2^32 ticków. Nowy projekt nie łamie tego: tworzy
**własną, samospójną domenę recency** (esp_timer × 80, truncate), w której są OBA znaczniki, w pełni
odseparowaną od sprzętowego licznika capture. Kontrakt w nagłówkach (`rc_sample.h:37-44`,
`rc_validity.h:53-60`) zaktualizowany spójnie. **To poprawne domknięcie findingu P2 z Fazy 1.**

---

## Regresje cross-phase: **BRAK**

Wszystkie wcześniejsze testy przechodzą po zmianach w plikach Faz 1–2:
- `rc_validity` (channel_valid/debounce/boundary) — PASS.
- `throttle_chain` / `servo_chain` / `ramp` — PASS; nowa `throttle_is_neutral` używa tych samych
  kroków (`normalize_us` → `shape_deadband`) co łańcuch gazu, więc bramka R7 i łańcuch zgadzają się
  co do „neutralu".
- Liczba testów wzrosła 81 (Faza 2) → 83 baza + 20 nowych = **103/103 PASS**. Zero osłabionych asercji,
  zero usuniętych testów.

---

## Ocena szczegółowa (zakres Fazy 3)

### Maszyna stanów (Unit 6) — ✅
- Boot → DISARMED bezwarunkowo: `loop_state_init` ustawia `SM_STATE_DISARMED`; reset reason tylko
  logowany w `app_main.c`. Pokryte `test_boot_disarmed_regardless_of_inputs`.
- Guard DISARMED→ARMED (R7): `can_arm` = `rc_valid && throttle_neutral && ui_arm_request &&
  !calib_in_progress && !settings_apply_in_progress`. Wszystkie 5 warunków pokryte testami (w tym
  blokady calib / settings-apply).
- FAILSAFE latched: `next_from_failsafe` — wyjście WYŁĄCZNIE `rc_valid → DISARMED`, nigdy wprost ARMED
  (`test_failsafe_rc_recovery_goes_disarmed_not_armed` dowodzi tego nawet przy pełnym armable input).
- Reguła serwa niezależna od arming: `servo_target_for(inputs)` patrzy tylko na `rc_valid` — pokryte
  dla DISARMED i ARMED, valid i invalid.
- Pełna tabela przejść (DISARMED/ARMED/FAILSAFE/ESC_CALIBRATION + default→FAILSAFE) pokryta; discriminated
  `enum sm_state`; każda funkcja `next_from_*` ≤ ~6 linii, early-return, nesting ≤ 1.

### Pętla / integracja (Unit 7) — ✅
- `loop_step` czysty: zero include IDF (tylko `signal_chain`, `state_machine`, `rc_validity`); składa
  `channel_valid + debounce → rc_valid → sm_step → throttle/servo chains`.
- DISARMED + drążek max → ESC neutral: `test_disarmed_full_throttle_outputs_neutral_esc` (bramka R7).
- RC invalid → FAILSAFE soft-stop + serwo center: pokryte (po debounce + rampach).

### Single-writer / TOCTOU (SI-6) — ✅
- Tylko `control_loop.c` pisze `s_params` (active). Pending przez `xQueueCreate(1, …)` +
  `xQueueOverwrite` (mailbox length-1).
- Apply tylko DISARMED z re-checkiem w momencie apply: `maybe_apply_pending` robi
  `peek → loop_should_apply_pending(state) → receive` (`control_loop.c:270-284`) — receive (konsumpcja)
  dopiero po bramce, więc zmiana stanu między peek a apply nie przepuści zapisu w ARMED/FAILSAFE.
- Watchdog: `esp_task_wdt_reset()` po `run_one_cycle()`, przed `vTaskDelayUntil` — tylko na końcu
  ukończonej iteracji (`control_loop.c:315-319`).

### Architektura — ✅
- `loop_step.c` 75 linii, `control_loop.c` 131, `state_machine.c` 98 — wszystkie < 300.
- Funkcje < 50 linii, nesting ≤ 2, named constants (`CONTROL_LOOP_PERIOD_MS`, `RC_WIDTH_*`,
  `RC_DEBOUNCE_DEFAULT_THRESHOLD`).
- Brak circular deps: `control_loop → {state_machine, signal_chain, rc_*, settings, pwm_out}`;
  `state_machine → signal_chain` (dla enum target mode); brak odwrotnych krawędzi.
- HAL cienki (control_loop), logika czysta (loop_step / state_machine) — granica warstw zachowana.

---

## Findingi (P3)

- 🟡 **TEST** — `control_loop.c:270-284` `maybe_apply_pending` (sekwencja peek→gate→receive, realny
  TOCTOU re-check) jest HAL-only i niepokryta testem hosta. Pokryta jest tylko *czysta* bramka
  `loop_should_apply_pending`. Zgodne z planem (control_loop = cienki HAL, weryfikacja na sprzęcie),
  ale sama kolejność peek/receive to logika warta wyniesienia/odtestowania (np. fake queue) — do
  rozważenia przy Unit 8/10, gdzie pending realnie płynie z panelu.
- 🟡 **KOD** — `control_loop.c:209` `RC_PERIOD_EXPECTED_US 20000` zahardkodowane mimo zasady
  „NIE zakładać 20 ms". Świadomie udokumentowane jako placeholder z hojną tolerancją 8000 µs (nigdy
  fałszywie nie odrzuci) do czasu pomiaru okresu ramki — spójne z Planem pomiarów. Dopiąć realną
  wartość po pomiarze odbiornika (nie blokuje).
- 🟡 **TEST** — brak dedykowanego host-testu „recency wygasa gdy `now_ticks` postępuje a
  `last_edge_ticks` zamrożone w domenie esp_timer×80". Pośrednio pokryte przez `stale_sample`
  (loop_step) i testy wrapu z Fazy 1, ale jawny test kontraktu epoch byłby mocniejszą wyrocznią.
- 🟡 **TEST** — `loop_step` nie ma testu ścieżki ESC_CALIBRATION (override ESC z sekwencji) — celowo,
  bo sekwencja to Unit 9 (Faza 5); maszyna stanów ma tylko przejścia. Odnotowane jako oczekiwana luka
  do domknięcia w Unit 9.

---

## Odchylenia od planu

**Brak istotnych odchyleń.** Implementacja zgodna z Implementation Unit 6 i 7
(`docs/plans/2026-06-16-001-...-plan.md:439-531`):
- Wszystkie pliki z sekcji **Pliki:** istnieją (`state_machine.{h,c}`, `loop_step.{h,c}`,
  `control_loop.{h,c}`, `test_state_machine.c`, `test_loop_step.c`) i są dopięte do host-buildu.
- Wszystkie scenariusze testowe `[Unit]` z planu obecne i z realnymi asercjami (zero assertion-free).
- Drobne rozszerzenie ponad plan (akceptowalne): dodano jawny `ui_disarm_request` (ARMED→DISARMED) —
  plan dopuszczał „ARMED + ręczny disarm (jeśli dodany)". Pokryte testem.
- `loop_inputs` niesie `now_ticks` w domenie recency — wynik domknięcia kontraktu epoch (planowane
  „do Unit 7").

---

## Podsumowanie

Faza 3 jest **gotowa do kontynuacji**. Serce bezpieczeństwa (R6/R7/SI-1/SI-4/SI-6) zaimplementowane
poprawnie i przetestowane; kontrakt epoch/recency domknięty bez dziury; brak regresji cross-phase.
Cztery P3 to nity i świadome luki HAL/measurement — do domknięcia naturalnie w Fazach 4–5.
