---
title: "Bezpieczne odwrócenie dostarczonego failsafe + przepisanie testu wyroczni przy zmianie specyfikacji (nie osłabienie)"
date: 2026-07-03
category: testing-issues
severity: high
stack:
  - ESP32
  - ESP-IDF
  - C
tags:
  - failsafe
  - spec-change
  - oracle-power
  - test-weakening
  - behavior-inversion
  - spot-lock
  - goto
  - circular-dependency
  - pure-core
status: verified
last_verified: 2026-07-03
---

# Bezpieczne odwrócenie failsafe + oracle-rewrite przy zmianie specyfikacji

Feature `app-spot-lock` (branch `feature/app-spot-lock`, plan
`docs/plans/2026-07-02-001-feat-app-spot-lock-latched-commands-plan.md`) odwraca
model bezpieczeństwa dostarczony w `goto-waypoint-navigation`: z „utrata linku z
aplikacją PAUZUJE goto" na „pilot RC = jedyny failsafe, aplikacja = kanał
latchowanych komend; łódź płynie dalej po wygaśnięciu telefonu". Ten dokument
opisuje **dyscyplinę bezpiecznej zmiany dostarczonego zachowania** — kąt
komplementarny do
`docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md`
(który dokumentuje pierwotne P1/P2/P3). Tu chodzi o to, jak odwrócić failsafe i
przepisać jego testy, NIE regresując ortogonalnych inwariantów ani nie wpadając w
anty-pattern „test weakening".

## Symptomy

- **Wymaganie odwraca dostarczone zachowanie**: „PAUSE na utratę linku" ma się stać
  „CONTINUE". Naiwna zmiana kusi, by pousuwać całą maszynerię `comms_*`, co
  przypadkiem zabiera ORTOGONALNE inwarianty, które akurat współdzieliły ten sam
  predykat (`comms_fresh`).
- **Istniejące testy asertują STARE zachowanie** (`PAUSED`, `throttle==0`). Pokusa:
  osłabić asercję do `toBeDefined`-owego odpowiednika albo skasować test — to
  wyglądałoby jak „test weakening" (anty-pattern #2 z coding-rules), nawet gdy
  intencją jest legalna zmiana specyfikacji.
- **Nowy sub-feature (kotwica „Spot-lock" z aplikacji)** kusi, by dodać nowe źródło
  celu w firmware + nowe pole telemetrii, choć wystarczy reużyć istniejące
  `SRC_GOTO`.

## Root Cause

- Predykat `comms_fresh` pełnił W JEDNYM MIEJSCU dwie role: (a) link-failsafe
  (pauza) oraz (b) bramka re-latchu celu (ochrona null-island). Odwrócenie (a) bez
  jawnego zachowania (b) cicho regresuje ochronę przed (0,0).
- Test asertujący stare zachowanie nie jest „zły" — jest po prostu związany ze
  starą specyfikacją. Zmiana spec wymaga PRZEPISANIA wyroczni na nowe zachowanie z
  zachowaniem mocy wyroczni, nie osłabienia.
- Nowe źródło firmware = niepotrzebny coupling i drugi wymiar telemetrii do
  utrzymania; kotwicę da się wyrazić jako `goto(własny fix)` — jedno `SRC_GOTO`.

## Rozwiązanie

### 1. Chirurgiczny flip JEDNEJ flagi, ortogonalne inwarianty jawnie zachowane

Odwrócenie sprowadzone do zmiany jednego argumentu (`comms_gated`) w wywołaniu
`hold_or_pause` dla gałęzi `SRC_GOTO` — z `true` na `false`. Parametr NIE jest
usuwany: zostaje, by dwie domeny degradacji (link vs sensor) pozostały
strukturalnie rozdzielone w miejscach wywołania.

```c
/* spot_lock.c — SRC_GOTO: latchowana intencja, RC jedynym failsafe.
 * comms_fresh NIE jest już link-failsafe — to bramka retargetu-w-locie /
 * anty-null-island. Re-latch ref_* TYLKO na wejściu albo gdy link świeży. */
if (in->goto_engage) {
    bool is_entering_goto = st->target_source != SPOT_LOCK_SRC_GOTO;
    if (is_entering_goto || in->comms_fresh) {   /* (b) bramka re-latchu — ZOSTAJE */
        st->ref_lat_e7 = in->goto_lat_e7;
        st->ref_lon_e7 = in->goto_lon_e7;
    }
    st->target_source = SPOT_LOCK_SRC_GOTO;
    return hold_or_pause(in, p, st, false); /* (a) comms_gated: true -> false */
}
```

Trzy ortogonalne inwarianty jawnie chronione podczas flipa:
- **(b)** `comms_fresh` zostaje w bramce re-latchu → cel nie jest nadpisywany
  zerami, gdy pętla wyzeruje `goto_*` na utracie linku (ochrona null-island).
- **Predykat sensoryczny** (`fresh ≠ fix`, GPS/IMU) pauzuje goto co cykl — osobna
  domena degradacji, nietknięta.
- **Ścieżka failsafe RC** (stick override / CH3 preempt / disarm) — nietknięta.

### 2. Przepisanie testu wyroczni (nie osłabienie) — moc wyroczni potwierdzona empirycznie

Stary test asertujący `PAUSED` przepisany na nowe zachowanie `ACTIVE + throttle>0 +
ref zachowany`. Kluczowe: przywrócenie starego kodu (`comms_gated=true`) MUSI
czynić nowy test CZERWONYM. Empirycznie potwierdzone: 2 testy czerwone przed
flipem (test-first).

```c
/* test_spot_lock.c — ORACLE flipa comms-gate. Restore comms_gated=true dla
 * SRC_GOTO -> pauza tutaj -> ta asercja ACTIVE+thrust idzie na czerwono.
 * To ZMIANA SPEC (usunęliśmy funkcjonalność PAUSE-on-link-loss per R3),
 * NIE osłabiona asercja. */
in.comms_fresh = false;             /* link stracony; fix GPS/IMU ważny */
spot_lock_outputs out = spot_lock_step(&in, &p, &st);
TEST_ASSERT_EQUAL_INT(SPOT_LOCK_ACTIVE, out.substate);
TEST_ASSERT_TRUE(out.throttle_cmd > 0);
TEST_ASSERT_EQUAL_INT32(GOTO_LAT_E7, st.ref_lat_e7);  /* ref zachowany */
```

Odróżnienie od anty-patternu „test weakening": osłabienie ZDEJMUJE wyrocznię
(asercja przechodzi z transformacją LUB bez niej). Oracle-rewrite PRZENOSI
wyrocznię na nowe zachowanie — test dalej failuje, gdy zachowanie jest błędne
(tu: gdy przywrócić stary flag). Reguła kciuka: „czy test failuje, gdy wrócę do
starego kodu?" — jeśli tak, to rewrite, nie weakening.

Osobny test null-island też przepisany: pinuje teraz RETENCJĘ (`ref` == ostatni
dobry, nie 0,0), a nie pauzę — bo pauza zniknęła, a retencja została. Wyrocznia
(naiwny „ref_* = goto_* co cykl" pisze (0,0) → czerwony) zachowana.

### 3. Kolaps sub-feature na istniejące źródło zamiast nowego

Kotwica „Spot-lock" z aplikacji = `goto(własny bieżący fix)`, nie nowe źródło
firmware ani nowe pole telemetrii. Komenda `hold` próbkuje własny GPS raz i
angażuje `SRC_GOTO` tą samą pozycją. Rozróżnienie „goto punkt vs kotwica" jest
etykietą LOKALNĄ w aplikacji, nie wymiarem w telemetrii.

### 4. Czysty helper w komponencie, który NIE tworzy cyklu

`goto_grab_decide` (czysta decyzja: angażuj kotwicę tylko przy fresh + realny fix +
w zakresie geo) umieszczony w `control_loop`, NIE w `web_panel` — bo `web_panel
REQUIRES control_loop`, więc helper w `web_panel` dałby cykl. Świadoma drobna
duplikacja stałych zakresu (mirror `goto_target_valid`) zaakceptowana, by uniknąć
circular dependency (Duplication > Complexity).

## Komendy diagnostyczne

```bash
# host-testy logiki (bez sprzętu) — 442 zielone po zmianie
grep -rn "comms_gated\|comms_fresh\|goto_grab\|hold_or_pause" components/
# potwierdź moc wyroczni: przywróć comms_gated=true dla SRC_GOTO i uruchom testy
#  -> test_goto_persists_through_comms_loss_* MUSI być czerwony
```

## Zapobieganie

- **Odwracasz dostarczony failsafe?** Znajdź WSZYSTKIE role współdzielonego
  predykatu; flipnij TYLKO tę jedną (pauza), a ortogonalne (re-latch/null-island,
  sensor-pause, RC-failsafe) jawnie zachowaj i pokryj osobnymi testami. Preferuj
  chirurgiczny flip jednej flagi nad usuwaniem maszynerii.
- **Zmiana spec unieważnia test?** Przepisz wyrocznię na nowe zachowanie, potwierdź
  empirycznie że stary kod czyni go czerwonym (test-first). To NIE jest „test
  weakening". Weakening = zdjęcie wyroczni; rewrite = przeniesienie wyroczni.
- **Nowy sub-feature?** Zapytaj, czy da się zwinąć na istniejące źródło/kanał zamiast
  dodawać nowe źródło + nowy wymiar telemetrii. Rozróżnienia rób lokalnie u
  konsumenta, nie w kontrakcie.
- Czysty helper umieszczaj w komponencie, który nie utworzy cyklu importów; drobna
  duplikacja stałych < circular dependency.

## Powiązane

- docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md
  (pierwotne P1/P2/P3 — ten dokument odwraca P1 i opisuje DYSCYPLINĘ odwracania;
  tamten zaktualizowany banerem 2026-07-03)
- docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md
  (moc wyroczni — reguła kciuka „czy test failuje, gdy usunę transformację")
- docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md

## Kontekst

Branch `feature/app-spot-lock`, commity `eccc76d` (flip Unit 1), `4f4d3ef` (parse
`hold`, Unit 2), `bae631e` (grab własnego fixu + helper `goto_grab`, Unit 3). 442
host-testy zielone. Model bezpieczeństwa: pilot RC pozostaje jedynym failsafe;
aplikacja to kanał latchowanych komend (goto/hold). Use case: „ustaw punkt, wygaś
ekran, łódź płynie dalej".
