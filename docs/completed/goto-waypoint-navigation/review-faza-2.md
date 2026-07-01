# Review Fazy 2 — Rdzeń decyzyjny goto (Unit 3)

Data: 2026-07-01
Commit: `d3b1d9c` feat(goto): rozszerzenie spot_lock o źródło celu + arbitraż CH3/goto + bramka linku
Zakres: `components/control_loop/{include/spot_lock.h, src/spot_lock.c}`, `test/host/test_spot_lock.c`
Metoda: analiza ręczna + 2 dedykowane subagenty (safety-arbitration, test-oracle-power) + walidacja.

## Severity gate: ⚠️ ZASTRZEŻENIA

- **P1 (blocking): 0**
- **P2 (important): 1**
- **P3 (nit): 4**
- E2E: N/A (firmware ESP-IDF, brak przeglądarki — poza scope)

Brak defektów blokujących. Wszystkie 5 kluczowych inwariantów bezpieczeństwa są **strukturalnie szczelne** (wymuszone kolejnością bramek + parametrem `comms_gated`, nie rozproszonymi warunkami). Jedno zastrzeżenie P2 to niejawny kontrakt integracyjny (retencja celu / null-island), do domknięcia w Unit 4 — nie blokuje kontynuacji.

## Wynik walidacji

- Host-tests: **411/411 PASS** (405→411, +6 Unit 3). Zero regresji.
- `idf.py build` (esp32s3): **PASS** — `0xf12f0` B, 37% partycji app wolne.
- Grep Pure ⊥ HAL: `spot_lock.h` zawiera tylko `<stdbool.h>`/`<stdint.h>`; brak `esp_*`/`driver/*`. Czysty.
- Determinizm: funkcja czysta, brak globali/statyków mutowalnych; `test_step_is_deterministic` obecny.

## Weryfikacja kluczowych inwariantów (moc wyroczni)

### A. Priorytet CH3 nad goto — ✅ SZCZELNY
`target_source` ustawiane na `SRC_GOTO` wyłącznie w kroku 3 (`spot_lock.c:186`), osiągalnym tylko gdy `ch3_on == false` (krok 2 short-circuituje pierwszy, `spot_lock.c:178-180`). Każdy cykl z `ch3_on == true` trafia do `run_ch3_hold`, którego bramka wejścia `st->target_source != SPOT_LOCK_SRC_HOLD` (`spot_lock.c:157`) dla przychodzącego `SRC_GOTO` rozstrzyga w `SRC_HOLD` (edge+fix, :161-163) albo `make_off`→`SRC_NONE` (:159). **Nie istnieje kombinacja wejść pozostawiająca stan w `SRC_GOTO` przy CH3 ON.**
Test `test_ch3_preempts_active_goto_and_snapshots_here_and_now` — najsilniejsza wyrocznia: mutacja klucza wejścia z `target_source!=SRC_HOLD` na `substate==OFF` FAILuje (`ref` zostałby na celu goto zamiast snapshotu „tu i teraz").

### B. Bramka linku NIE dotyka SRC_HOLD — ✅ SZCZELNY
`run_ch3_hold` woła `hold_or_pause(..., comms_gated=false)` (`spot_lock.c:165`). Człon comms to `if (comms_gated && !in->comms_fresh)` (`:136`) — z `comms_gated==false` nigdy nie podnosi `sensors_lost`. `comms_fresh=false` **nie może** spauzować SRC_HOLD.
Test `test_comms_gate_does_not_pause_ch3_hold` — mutacja rozszerzająca bramkę na SRC_HOLD (przekazanie `true` z :165) FAILuje (PAUSED zamiast ACTIVE+throttle>0).

### C. Failsafe-precedence (!armed / !sticks_neutral) — ✅ SZCZELNY
`if (!in->armed || !in->sticks_neutral) return make_off(st);` jest **pierwszą instrukcją** `spot_lock_step` (`spot_lock.c:173-175`), bezwarunkową i przed gałęziami CH3/goto. `make_off` forsuje `SPOT_LOCK_OFF` + czyści źródło (`:119-120`). To pojedyncza bramka strukturalna przepisana z learningu 2026-06-29; w połączeniu z bramką `resolve_spot_lock` (`spot_lock_step` wołane tylko w `SM_STATE_ARMED`, `loop_step.c:245-249`) pierwszeństwo failsafe zachowane. Zamierzona zmiana vs poprzednia wersja: `!ch3_on` NIE forsuje już OFF na górze (goto działa bez CH3) — nie osłabia failsafe.
Testy: `test_goto_override_on_stick_deflection` (SRC_GOTO+!neutral→OFF/SRC_NONE), `test_entry_blocked_when_disarmed`, `test_abort_on_stick_out_of_deadband` — wszystkie z mocą wyroczni.

### D. SRC_GOTO nie liczy aktuatorów przy utracie linku — ✅ SZCZELNY
Krok 3 używa `comms_gated=true` (`:187`); `comms_fresh=false` → `sensors_lost=true` (:137) → `make_idle_output(SPOT_LOCK_PAUSED)` neutral+center (:141). `compute_active_output` nieosiągnięte — żadna komenda aktuatora.

### E. Pure ⊥ HAL + determinizm — ✅
Nagłówek grep-clean; funkcja deterministyczna (pokryta testem).

### Test-weakening (anti-pattern P1/P2) — ✅ BRAK
`git show d3b1d9c -- test_spot_lock.c` jest **wyłącznie addytywny**. Zero usuniętych/osłabionych asercji; 16 oryginalnych testów + 6 nowych = 22 zarejestrowane w `run_spot_lock_tests`.
Zmiana fixture `make_active_state()` (+`target_source = SPOT_LOCK_SRC_HOLD`) jest **konieczną korektą modelu** (nie osłabieniem): `run_ch3_hold` keyuje wejście na `target_source != SRC_HOLD`; bez pola `SRC_NONE(0) != SRC_HOLD(1)` → gałąź wejścia → `!ch3_edge_on` → `make_off` → OFF, co złamałoby KAŻDY carry-over test asertujący ACTIVE/PAUSED (deadband, heading-gate, cap, pauzy, determinizm). Asercje pozostają nietknięte — poprawiono tylko stan modelu pod nowe keyowanie. Wszystkie 6 nowych testów ma realną moc wyroczni (żaden nie jest tożsamością input==output).

## Findings

### 🟠 P2 (important) — KOD

- **`spot_lock.c:184-185` + kontrakt nagłówka** — cel `SRC_GOTO` jest **śledzony na żywo z wejścia co cykl**, nie snapshotowany, i `st->ref_*` jest nadpisywane z `in->goto_*` PRZED bramką świeżości w `hold_or_pause` (także podczas PAUSED). Konsekwencje: (1) w odróżnieniu od `SRC_HOLD` (snapshot raz, :161-163) „retencja celu" goto podczas pauzy realnie zależy od stabilności wejściowego latcha, co przeczy sformułowaniu „target retained" w nagłówku i komentarzu `hold_or_pause` (:124-129); (2) niepoprawny/zerowy `goto_*` zostałby skopiowany do trwałego stanu nawet przy `comms_fresh=false` — jeśli warstwa integracyjna (Unit 4) wyzeruje cel przy utracie linku, `ref` staje się `(0,0)` (**hazard null-island**) trzymanym w stanie do nadejścia świeżego celu.
  **Ocena:** dla czystego rdzenia (Faza 2, jeszcze niepodłączony) to nie jest funkcjonalny defekt — cel jest zewnętrznie posiadany, więc re-read co cykl jest zgodny z projektem, a Unit 2 waliduje cel `goto_target_valid` na granicy HTTP i pauza (nie cancel) nie kasuje latcha. To **niejawny kontrakt integracyjny** do domknięcia w Unit 4.
  **Rekomendacja:** albo snapshotować goto na przejściu `→ SRC_GOTO` (symetrycznie do CH3) / zapisywać `ref` tylko gdy `comms_fresh`, albo jawnie udokumentować w nagłówku wymóg upstreamu: „warstwa loop MUSI utrzymać `goto_lat/lon` jako stabilny, zwalidowany latch przez luki linku i NIE zerować go na utratę linku". Do rozstrzygnięcia w Unit 4.

### 🟡 P3 (nit)

- **`spot_lock.c:158-159` (`run_ch3_hold`)** — wejście z `ch3_on` true ale `!ch3_edge_on || !gps_fresh || !gps_has_fix` przy preempcji goto → `make_off` (przerywa goto do neutralu). Yield do OFF jest poprawnym fail-safe (operator zażądał CH3; brak ważnego snapshotu → neutral). Caveat: po zgubieniu edge bez fixu `SRC_NONE` utrzymuje się, a `ch3_on` bez `ch3_edge_on` daje powtarzalne `make_off` — CH3-hold nie zaskoczy do przetoglowania CH3 (pre-existing zachowanie toru CH3, nie regresja). Wart jednolinijkowego komentarza.
- **`spot_lock.c` (goto target)** — brak walidacji współrzędnych CELU goto w rdzeniu (`hold_or_pause` bramkuje tylko jakość własnej pozycji `gps_has_fix`). Zmitygowane upstreamem: Unit 2 `goto_target_valid` odrzuca poza-zakres na granicy HTTP (fail-fast). Do potwierdzenia przy integracji Unit 4, że żaden inny tor nie wstrzyknie niezwalidowanego celu.
- **`test_spot_lock.c`** — brak dedykowanego testu pauzy `SRC_GOTO` na utratę GPS/IMU/fix (pokryty tylko comms-loss; tor `!gps_fresh||!imu_ok||!gps_has_fix` dla goto ćwiczony pośrednio przez wspólny `hold_or_pause`, który dla SRC_HOLD ma własne testy). Symetryczny test goto-fix-loss byłby czystszy.
- **`test_goto_arrived_flag_tracks_deadband`** — gałąź on-target asertuje `arrived`, ale nie `throttle_cmd==0` w deadbandzie dla źródła goto (relax-in-deadband). Pokryte dla SRC_HOLD przez `test_deadband_inside`; wspólny rdzeń, minor.

## Odchylenia od planu

- Brak istotnych. Pliki zmienione = dokładnie te z planu Unit 3 (`spot_lock.h`, `spot_lock.c`, `test_spot_lock.c`). Wszystkie scenariusze testowe Unit 3 z planu pokryte (SRC_GOTO drive, CH3 preempt, bramka linku w obie strony, override, regresja CH3) + `arrived`.
- **Niepodłączenie do pętli jest zgodne z planem** — `build_spot_lock_inputs` (`loop_step.c:199`) nie ustawia `goto_engage/goto_*/comms_fresh` (designated-init → 0/false), więc gałąź goto jest uśpiona, tor CH3 niezmieniony. Integracja = Unit 4 (Faza 3). NIE brak.

## Wniosek

Rdzeń decyzyjny goto jest bezpieczny i dobrze przetestowany: priorytet CH3, izolacja bramki linku i failsafe-precedence są strukturalnie szczelne z pełną mocą wyroczni testów; zero test-weakeningu. Jedno zastrzeżenie P2 (kontrakt retencji celu / null-island) do jawnego domknięcia w Unit 4. Faza 2 gotowa do kontynuacji z zastrzeżeniami.
