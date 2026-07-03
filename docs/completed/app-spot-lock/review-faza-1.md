# Review Fazy 1 (Firmware, Unit 1-3) — app-spot-lock

Data: 2026-07-02
Zakres: `git diff c164cb5..8081191` (commity eccc76d, 4f4d3ef, bae631e)
Walidacja: `bash test/host/run.sh` → **442 testy, 0 failures** (było 437; +1 hold parse, +4 grab, 2 przepisane oracle).

## Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI

- 🔴 P1 (blocking): **0**
- 🟠 P2 (important): **1**
- 🟡 P3 (nit): **4**

Typy findingów: P2 → 1 KOD. P3 → 1 KOD, 3 TEST. E2E: **N/D** (firmware ESP-IDF C — brak warstwy przeglądarki; weryfikacja = host-testy Unity).

---

## Weryfikacja zmiany krytycznej dla bezpieczeństwa (odwrócenie failsafe PAUSE→CONTINUE)

Wszystkie cztery wymagane inwarianty potwierdzone niezależnie (architecture + test-coverage):

1. **Flip chirurgiczny** — jedyna zmiana logiki w `spot_lock.c` to `hold_or_pause(..., comms_gated=true)` → `false` dla gałęzi SRC_GOTO (spot_lock.c:224). Blast radius = jeden literał boolowski.
2. **Bramka re-latchu NIETKNIĘTA** — `if (is_entering_goto || in->comms_fresh)` (spot_lock.c:219) bajtowo identyczna (tylko komentarz nad nią zmieniony). Ochrona null-island zachowana.
3. **Predykat sensoryczny NIETKNIĘTY** — `!gps_fresh || !imu_ok || !gps_has_fix` (spot_lock.c:159) niezmieniony, re-walidowany co cykl (guard seed-fresh). Pozostaje JEDYNĄ ścieżką pauzy dla goto.
4. **Ścieżka failsafe RC NIETKNIĘTA** — `!armed || !sticks_neutral` (:197) i preempt `ch3_on` (:202) bez zmian. `comms_fresh` nigdzie nie trafia do rc_valid / channel_valid / sm_inputs — potwierdzone grepem: płynie tylko do `spot_lock_inputs` i telemetrii `app_link_fresh`. Flip tylko ZAWĘŻA rolę comms_fresh (pause input → re-latch gate), nie może przeciec do failsafe.

**Moc wyroczni testów odwrócenia — POTWIERDZONA.** Przywrócenie `comms_gated=true` dla SRC_GOTO uczyniłoby czerwonymi:
- `test_goto_persists_through_comms_loss_with_valid_fix` (test_spot_lock.c:463) — asertuje ACTIVE + `throttle_cmd>0`; stara impl → PAUSED + 0.
- `test_goto_persists_on_comms_loss_latch_retained` (test_loop_step.c:759) — asertuje ARMED + ACTIVE + `esc_us>ESC_NEUTRAL_US`; stara impl → PAUSED + neutral.

To są legalne przepisania spec-change (usunięto funkcjonalność PAUSE-on-link-loss per R3), **NIE osłabienie asercji**. Retencja null-island (`test_goto_retains_target_ignoring_zeroed_input_on_stale_link`) nadal pinuje `ref_* == GOTO_LAT_E7` z mocą wyroczni (mutacja „ref_*=goto_* co cykl" → RED). Pauza fresh≠fix (`test_goto_pauses_on_gps_loss_keeps_target`) nietknięta.

**Grab fixu (`goto_grab_decide`)** — bramkuje atomowo `fresh && fix && in-range` z jednego `gps_get_state()` (control_loop.c:355-366). Pokrycie: happy + fix=false (seed-fresh) + stale + out-of-range, każdy z mocą wyroczni. `hold` bypassuje `extract_goto_target` → brak spurious 400 (http_server.c:221, tylko `goto_request` triggeruje ekstrakcję).

**Odstępstwo od planu (goto_grab w control_loop, nie web_panel/goto_target.c) — DECYZJA POPRAWNA.** `web_panel` REQUIRES `control_loop`; odwrotna zależność = circular (§14). Umieszczenie w `control_loop` dodaje zero nowych krawędzi. Duplikacja 4 stałych zakresu geo (zweryfikowana: mirror ±900000000/±1800000000 exact) akceptowalna per §11 „Duplication > Complexity" — to fizyczne granice Ziemi, drift ~0.

---

## Findingi

### 🟠 P2 — Important (1)

- [ ] 🟠 **components/control_loop/src/spot_lock.c:157,160-162,189,224** [KOD] — **Martwy kod po flipie: parametr `comms_gated` i jego gałąź.** Po odwróceniu `comms_gated` == `false` w OBU call-site'ach (SRC_HOLD :189, SRC_GOTO :224), więc `if (comms_gated && !in->comms_fresh) sensors_lost = true;` (:160-162) jest statycznie nieosiągalny, a parametr jest stałym-`false` martwym argumentem. Autor zostawił go świadomie jako „structural marker dwóch domen degradacji". Rekomendacja: **usunąć** parametr i gałąź, przenieść prozę do komentarza (już istnieje na :148-153). Uzasadnienie surowsze niż zwykle, bo to moduł failsafe: dziś inwariant „app link nigdy nie pauzuje goto" (R3/R4) jest egzekwowany WYŁĄCZNIE konwencją call-site (jeden literał `false`), nie strukturalnie. Martwa gałąź to *ścieżka odwracająca failsafe* re-armowalna zmianą jednego boola. Usunięcie parametru czyni ciche re-odwrócenie niemożliwym bez edycji ciała funkcji. Narusza §6 (usuwaj dead code), §5 anty-pattern #10 (defensive over-engineering), §11 (istniejący kod — bądź surowy). Non-blocking (dzisiejsze zachowanie poprawne) → P2 nie P1.

### 🟡 P3 — Nit (4)

- [ ] 🟡 **components/control_loop/include/control_loop.h:95, components/web_panel/include/command_parse.h:40** [KOD] — Kolizja nazewnicza: pole `hold_request` (komenda app) engażuje **SRC_GOTO**, podczas gdy `SRC_HOLD` to hold pilota RC (CH3). Dwa różne „hold" współistnieją. Komentarze disambiguują, ale czytelnik skanujący `hold_request` → `SRC_HOLD` się pomyli. Rozważ `anchor_request`/`spot_lock_request` lub notkę przy polu, że mapuje na SRC_GOTO, nie SRC_HOLD.
- [ ] 🟡 **test/host/test_goto_target.c:122** [TEST] — Testy `goto_grab_decide` mieszkają w `test_goto_target.c` zamiast dedykowanego `test_goto_grab.c`. Mąci ownership suite (§3 kolokacja). Nieszkodliwe, niski priorytet.
- [ ] 🟡 **test/host/test_goto_target.c (suite goto_grab)** [TEST] — Inkluzywna granica geo (dokładnie ±900000000 / ±1800000000) nie jest pinowana. Bo `goto_grab.h` DUPLIKUJE stałe (unik circular dep), granica NIE jest pokryta tranzytywnie przez boundary-testy `goto_target_valid` — mutacja `>=`→`>` w `grab_in_range` (goto_grab.c:6-7) przeszłaby niewykryta. Niski ryzyk (realny fix zawsze głęboko w zakresie). Fix: 1 test at-boundary engage.
- [ ] 🟡 **components/control_loop/include/goto_grab.h:28-31 vs goto_target.h:22-26** [TEST] — Brak compile-time linku pinującego `GOTO_GRAB_*_E7_* == GOTO_*_E7_*`. Jeden host-test `TEST_ASSERT_EQUAL(GOTO_GRAB_LAT_E7_MIN, GOTO_LAT_E7_MIN)` (oba nagłówki includowalne z `test_goto_target.c`) tanio zabezpieczyłby drift mirrora. Opcjonalne.

---

## Perspektywy N/D dla tego diffu

- **Security (web)**: N/D — brak auth/RLS/XSS/SQL. Failsafe inversion to świadoma decyzja projektowa, zweryfikowana że nie dotyka ścieżki RC. `hold` keyword-only bez payloadu → brak nowej powierzchni untrusted input; grab re-waliduje zakres na sampled fix. Wzorzec P2 „waliduj double przed castem" (goto_target HTTP) nietknięty — ta ścieżka nie jest zmieniana.
- **Performance**: CLEAN — jeden `gps_get_state()` na event `hold` (event-driven, nie per-cykl), helper O(1) pure, zero I/O flash w pętli RT.
- **E2E browser**: N/D — natywny firmware C; weryfikacja przez host-testy Unity (442 zielone).

## Wniosek

Odwrócenie failsafe wykonane chirurgicznie i poprawnie izolowane. Zero P1. Jedyny substantywny item to martwa gałąź comms-gate (P2) — warta usunięcia właśnie dlatego, że to moduł failsafe i martwa ścieżka może po cichu re-odwrócić failsafe. Pozostałe to kosmetyczne nity. Faza gotowa do kontynuacji po adresowaniu P2.
