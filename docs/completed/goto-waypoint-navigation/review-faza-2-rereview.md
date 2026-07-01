# Re-Review Fazy 2 (cykl fix 1) — Rdzeń decyzyjny goto (Unit 3)

Data: 2026-07-01
Commit fix: `28d3c7c` fix(goto): retencja celu SRC_GOTO w PAUSED w czystym rdzeniu — poprawki po review fazy 2 (cykl 1)
Poprzedni review: `review-faza-2.md` (commit `d3b1d9c`, gate ⚠️ ZASTRZEŻENIA, P2=1)
Zakres re-review: weryfikacja czy P2 (null-island / retencja celu w PAUSED) rzeczywiście rozwiązany + kontrola regresji.
Metoda: analiza diff-a fixu + odczyt pełnego stanu `spot_lock.c`/`spot_lock.h`/`test_spot_lock.c` + walidacja (host-tests, idf.py build).

## Severity gate: ✅ CZYSTE

- **P1 (blocking): 0**
- **P2 (important): 0** (poprzedni P2 potwierdzony ROZWIĄZANY)
- **P3 (nit): 3** (rezydualne, dziedziczone z poprzedniego review; nie blokują)
- E2E: N/A (firmware ESP-IDF, brak przeglądarki — poza scope, nie zgłaszane)

## Wynik walidacji

- Host-tests: **413/413 PASS** (411→413, +2 Unit 3). Zero regresji, zero ignored.
- `idf.py build` (esp32s3): **PASS** — `0xf1300` B, 37% partycji app wolne.
- Zmiana zamknięta w bloku `if (in->goto_engage)` (osiągalnym tylko przy `!ch3_on`); tory CH3/SRC_HOLD/failsafe nietknięte.

## Weryfikacja rozwiązania P2 (moc wyroczni)

Poprzedni P2 (`spot_lock.c:184-185`): `st->ref_*` nadpisywane z `in->goto_*` KAŻDY cykl PRZED bramką świeżości, także w PAUSED → jeśli Unit 4 wyzeruje cel na utratę linku, `ref` staje się `(0,0)` (hazard null-island) trzymany w stanie.

Fix (`spot_lock.c:189-196`):
```c
if (in->goto_engage) {
    bool is_entering_goto = st->target_source != SPOT_LOCK_SRC_GOTO;
    if (is_entering_goto || in->comms_fresh) {
        st->ref_lat_e7 = in->goto_lat_e7;
        st->ref_lon_e7 = in->goto_lon_e7;
    }
    st->target_source = SPOT_LOCK_SRC_GOTO;
    return hold_or_pause(in, p, st, true);
}
```

Analiza szczelności:
- **PAUSED (link stracony, cel już zaangażowany):** `is_entering_goto == false` (bo `target_source == SRC_GOTO`) ∧ `comms_fresh == false` → warunek `false || false` → `ref_*` NIE zapisywane → dotychczasowy cel ZACHOWANY. Zerowany `in->goto_*` z Unit 4 nie wpływa na stan. **Hazard null-island domknięty w czystym rdzeniu** — retencja to teraz własność rdzenia, nie niejawny kontrakt latcha upstream. ✅
- **R1 (świeży link, nowy cel):** `comms_fresh == true` → re-latch na nowy cel. Śledzenie na żywo zachowane. ✅ Współistnienie R1 ↔ retencja rozstrzygane jednym predykatem `is_entering_goto || comms_fresh` — brak dziury: dokładnie jedna gałąź pisze, druga zachowuje.
- **Wejście przy nieświeżym linku (`is_entering_goto == true`, `comms_fresh == false`):** `ref_*` zapisany z (potencjalnie zerowego) wejścia, ale `hold_or_pause(..., comms_gated=true)` z `comms_fresh=false` → `sensors_lost=true` → **PAUSED, zero komend aktuatora**. Ewentualny zły `ref` nigdy nie jest wykonany i zostaje nadpisany poprawnym przy pierwszym świeżym cyklu. Benign. ✅

Testy z mocą wyroczni (dodane w fixie, obie realnie failują pod naiwną implementacją):
- `test_goto_retains_target_during_pause_ignoring_input` — link stracony + wejście `goto_*=(0,0)`: asertuje PAUSED oraz `ref_*` == oryginalny cel (nie 0,0, nie wejście), następnie powrót linku → ACTIVE na TYM SAMYM celu. Naiwne „`ref_*=goto_*` co cykl" zapisuje (0,0) → FAIL. Wyrocznia mocna.
- `test_goto_fresh_link_tracks_new_target` — świeży link + nowy cel: asertuje re-latch na nowy punkt. Latch „tylko na wejściu" zostawiłby stary punkt → FAIL. Wyrocznia dla R1 mocna.

## Kontrola regresji inwariantów safety (potwierdzone szczelne)

Kod bramek A–E względem poprzedniego review NIEZMIENIONY (failsafe-precedence `:173-175`, arbitraż CH3 `:178-180`, `run_ch3_hold` `:153-166`, `hold_or_pause` z `comms_gated` `:131-145`). Fix dotyka wyłącznie logiki latchowania `ref_*` w kroku 3.

- **A. Priorytet CH3 nad goto** — SZCZELNY (blok goto osiągalny tylko przy `!ch3_on`; fix nie zmienia kolejności bramek). ✅
- **B. Bramka linku nie dotyka SRC_HOLD** — SZCZELNY (`run_ch3_hold` woła `comms_gated=false`, niezmienione). ✅
- **C. Failsafe-precedence (`!armed || !sticks_neutral`)** — SZCZELNY (pierwsza, bezwarunkowa instrukcja `spot_lock_step`, niezmieniona). ✅
- **D. SRC_GOTO nie liczy aktuatorów przy utracie linku** — SZCZELNY i WZMOCNIONY: `hold_or_pause(..., true)` na `:196` niezmienione; dodatkowo fix gwarantuje, że nawet transientnie zły `ref` na wejściu przy stale-comms nigdy nie jest wykonany (PAUSED). ✅
- **E. Pure ⊥ HAL + determinizm** — nagłówek nadal grep-clean (`<stdbool.h>`/`<stdint.h>`), funkcja czysta, brak nowych globali/statyków. ✅

## Test-weakening (anti-pattern) — ✅ BRAK

`git show 28d3c7c -- test_spot_lock.c` jest wyłącznie addytywny: +2 nowe testy zarejestrowane w `run_spot_lock_tests`, +1 asercja WZMACNIAJĄCA (`throttle_cmd==0` w deadbandzie on-target w `test_goto_arrived_flag_tracks_deadband`, moc wyroczni: relax-in-deadband). Zero usuniętych/osłabionych asercji, zero zmienionych progów, fixtures nietknięte.

## Findings

### Nowe: brak (P1=0, P2=0)

Fix nie wprowadza nowych defektów. `is_entering_goto`-write przy stale-comms przeanalizowany jako benign (PAUSED zanim zły `ref` mógłby zostać wykonany).

### 🟡 P3 (rezydualne, dziedziczone — nie blokują, do domknięcia w Unit 4)

- **Fresh-link + zerowany cel z Unit 4** — jeśli upstream poda `comms_fresh=true` przy `goto_*=(0,0)`, rdzeń re-latchuje `ref` na null-island podczas ACTIVE. To LEGALNY, zawężony kontrakt Unit 4: świeży link ⟹ zwalidowany latch (Unit 2 `goto_target_valid` na granicy HTTP). Nagłówek NIE over-claimuje (gwarantuje retencję tylko dla luki linku, co jest prawdą). Do potwierdzenia przy integracji Unit 4, że żaden tor nie wstawia niezwalidowanego celu przy świeżym linku.
- **`spot_lock.c:158-159` (`run_ch3_hold`)** — powtarzalne `make_off` po zgubieniu edge bez fixu (pre-existing tor CH3, nie regresja). Wart jednolinijkowego komentarza.
- **`test_spot_lock.c`** — brak dedykowanego testu pauzy `SRC_GOTO` na utratę GPS/IMU/fix (pokryty tylko comms-loss; tor GPS ćwiczony pośrednio przez wspólny `hold_or_pause`). Symetryczny test byłby czystszy.

(P3 „throttle==0 w deadbandzie dla goto" z poprzedniego review — ROZWIĄZANY w fixie, patrz sekcja test-weakening.)

## Odchylenia od planu

Brak. Pliki zmienione = dokładnie Unit 3 (`spot_lock.h`, `spot_lock.c`, `test_spot_lock.c`). Niepodłączenie do pętli zgodne z planem (integracja = Unit 4 / Faza 3).

## Wniosek

Poprzedni P2 (retencja celu / null-island w PAUSED) **potwierdzony ROZWIĄZANY** w czystym rdzeniu z pełną mocą wyroczni testów; retencja jest teraz strukturalną własnością rdzenia, nie niejawnym kontraktem upstream. Fix chirurgiczny, zamknięty w bloku goto — zero regresji torów CH3/SRC_HOLD/failsafe, wszystkie inwarianty A–E nadal szczelne, zero test-weakeningu. Walidacja zielona (413/413 host-tests, idf.py build PASS). **Faza 2 gotowa do kontynuacji (Unit 4).** Rezydualne P3 to jawne kontrakty integracyjne / czystościowe do domknięcia w Fazie 3.
