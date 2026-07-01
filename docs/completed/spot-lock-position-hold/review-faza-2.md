# Code Review — Faza 2 (Czysta logika spot-lock: Unit 3 + Unit 4)

**Zadanie:** spot-lock-position-hold
**Branch:** `feature/spot-lock-position-hold`
**Commit recenzowany (re-review cykl 1):** `f612827` (fix P2 nad bazą `ccbe986`)
**Data review:** 2026-06-29 (RE-REVIEW po naprawie cyklu 1)

---

## Werdykt severity gate

✅ **CZYSTE — GOTOWE DO KONTYNUACJI.** 0× P1, 0× P2. Jedyny P2 z poprzedniego review
(re-walidacja `gps_has_fix` w trakcie hold) został **rozwiązany** commitem `f612827`.
Pozostają wyłącznie nity P3 (opcjonalne, nieblokujące). Bramki zielone.

## Liczniki (re-review)

| Severity | KOD | TEST | E2E | Razem |
|----------|-----|------|-----|-------|
| 🔴 P1 (blocking)   | 0 | 0 | 0 | **0** |
| 🟠 P2 (important)  | 0 | 0 | 0 | **0** |
| 🟡 P3 (nit)        | 5 | 3 | 0 | **8** (carry-over, niezmienione) |

**Czy P2 z poprzedniego review jest rozwiązany: TAK.**
**Nowe findingi w kodzie: 0 (KOD/TEST/E2E).**

---

## Bramki jakości (zweryfikowane empirycznie w tym re-review)

- ✅ Host-tests: `./test/host/run.sh` → **328 Tests, 0 Failures, 0 Ignored**
  (+1 vs poprzedni review 327: nowy `test_pause_on_fix_loss_then_resume_keeps_target`).
- ✅ `idf.py build` (esp32s3, N16R8): **zielony** — „Project build complete",
  `kayak-motor-firmware.bin` 0xe9b70 B, 39% partycji wolne.
- ✅ Diagnostyki LSP (`-mlongcalls`, „unknown type name 'driver'") — szum toolchainu
  xtensa, nie findingi (zgodnie z notą kontekstu). Autorytatywne są run.sh + idf.py build.

---

## Weryfikacja naprawy P2 (priorytet re-review)

### 1. Warunek pauzy — POPRAWNY

`components/control_loop/src/spot_lock.c:137`:
```c
if (!in->gps_fresh || !in->imu_ok || !in->gps_has_fix) {
    st->substate = SPOT_LOCK_PAUSED;
    return make_idle_output(SPOT_LOCK_PAUSED);
}
```
Term `!in->gps_has_fix` dodany. Gate cyklu hold jest teraz spójny z gate'em wejścia
(linia 126: `!ch3_edge_on || !gps_fresh || !gps_has_fix`) — seed-fresh window (świeży
timestamp bez realnego fixu) nie utrzyma już regulacji na niewiarygodnych współrzędnych.
Komentarz (134-136) wprost dokumentuje powód re-walidacji.

### 2. Semantyka PAUSED → ACTIVE z tym samym celem — ZACHOWANA

`st->ref_lat_e7/ref_lon_e7` ustawiane WYŁĄCZNIE przy OFF→ACTIVE (step 2, linia 129-130,
strzeżone `st->substate == SPOT_LOCK_OFF`). Pauza nie czyści celu (`make_idle_output` nie
dotyka `st->ref_*`). Po powrocie fixu substate==PAUSED (≠ OFF) → step 2 pominięty →
step 4 (`compute_active_output`) liczy na zachowanym celu. Potwierdzone asercją
`st.ref_lat_e7 == REF_LAT_E7` przed i po wznowieniu.

### 3. Moc wyroczni testu — POTWIERDZONA EMPIRYCZNIE

Test `test_pause_on_fix_loss_then_resume_keeps_target` (test_spot_lock.c:280-313):
ustawia ACTIVE, cel ~10 m (poza deadbandem 3 m), dziób wyrównany (gaz BYŁBY niezerowy),
`gps_fresh=true` + `gps_has_fix=false` + `imu_ok=true`.

Empiryczny dowód oracle power — usunięto `|| !in->gps_has_fix` z gate'a, przebudowano,
uruchomiono suite:
```
test_pause_on_fix_loss_then_resume_keeps_target:FAIL: Expected 2 Was 1
328 Tests 1 Failures 0 Ignored
```
(`Expected 2` = SPOT_LOCK_PAUSED, `Was 1` = SPOT_LOCK_ACTIVE.) Test **FAILuje** po
usunięciu transformacji → spełnia regułę „test clampu/limitu z mocą wyroczni".
Źródło przywrócone (`git checkout`), 328/328 ponownie zielone.

### 4. Brak osłabienia asercji

Test trzyma 5 twardych asercji (`TEST_ASSERT_EQUAL_INT(SPOT_LOCK_PAUSED…)`,
`throttle_cmd==0`, `servo_cmd==0`, `ref_lat_e7==REF_LAT_E7` ×2). Brak `assertTrue`,
brak rozluźnień. Asercja `paused.throttle_cmd==0` jest load-bearing (bez fixu w gate
byłby `350`).

---

## Regresja w pozostałej logice — BRAK

- Diff `f612827` w kodzie produkcyjnym to **jedna linia** (term gate'a). Cała reszta
  `spot_lock.c` i całe `geo_math.c` nietknięte.
- Pozostałe 327 testów dalej zielone → brak regresji w wejściu, deadbandzie, bramce ±60°,
  P-control, cap gazu, abortach.
- Zmiana nie wprowadza over-pause: pauza jest stanem bezpiecznym, a wejście i tak już
  wymagało `gps_has_fix` — fix jedynie domyka asymetrię wejście/hold.
- Fix minimalny, zero defensive over-engineering, zero nowych stałych/abstrakcji.

---

## Nity P3 (carry-over z poprzedniego review — niezmienione, opcjonalne)

Bez zmian względem pierwszego review; żaden nie blokuje. Skrótowo:

1. 🟡 KOD `spot_lock.c:73-77` — `throttle_command` cast-before-clamp; dla `dist>~32 km`
   iloczyn > INT32_MAX (cast impl-defined). Cap wymusza bezpieczny zakres; takie dystanse
   nie wystąpią w spot-locku.
2. 🟡 KOD `spot_lock.c:113` — brak NULL-guardów (kontrakt non-NULL, czysta funkcja wewn.).
3. 🟡 KOD `geo_math.c:31` — equirectangular degeneruje przy biegunach; bez znaczenia dla
   szerokości kajakowych.
4. 🟡 KOD — duplikacja `DEG10_*`; `SPOT_LOCK_CMD_FULL_SCALE` vs `SIGNAL_NORMALIZED_FULL_SCALE`
   — Unit 6 musi utrzymać synchronizację.
5. 🟡 KOD `spot_lock.c:41-49` — `heading_error_deg10(1800)` zwraca +1800 (asymetria na
   granicy); bramka ±600 odrzuca → nieszkodliwe.
6. 🟡 TEST — `gps_fresh` na OFF→ACTIVE nie testowany osobno (logika dzielona z pauzą).
7. 🟡 TEST — brak dedykowanego testu przejścia z sub-stanu PAUSED (PAUSED→OFF/PAUSED).
8. 🟡 TEST `test_spot_lock.c:157` — asercja `throttle_cmd==0` redundantna wobec wyroczni
   deadbandu; zostawić.

---

## E2E / Weryfikacja w przeglądarce

**N/A dla Fazy 2.** Czysta logika decyzyjna jeszcze niepodłączona do pętli (integracja
Unit 6, telemetria/panel Unit 7). Kryteria ukończenia Fazy 2 są host-testowe/grep/build —
potwierdzone powyżej.

---

## Podsumowanie

Naprawa P2 jest prawidłowa, kompletna i bezpieczna: warunek pauzy domyka re-walidację
fixu, semantyka wznowienia z tym samym celem zachowana, test ma udowodnioną empirycznie
moc wyroczni, brak osłabienia asercji, brak regresji (328/328 + build green). Faza 2
gotowa do kontynuacji (Unit 5/6 — integracja).
