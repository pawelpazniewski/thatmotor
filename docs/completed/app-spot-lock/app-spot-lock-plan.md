# Plan: Spot-lock z aplikacji + latchowany model komend

Branch: `feature/app-spot-lock`
Ostatnia aktualizacja: 2026-07-02

## Podsumowanie wykonawcze

Mapujemy 4. przycisk iOS (Spot-lock, dziś placeholder) na firmware jako komendę `hold` =
kotwica w bieżącej pozycji łodzi. Przy okazji zmieniamy model bezpieczeństwa: komendy z app
(goto i spot-lock) stają się **latchowanymi intencjami trwałymi wobec utraty linku** —
firmware przestaje pauzować silnik przy comms-timeout. **Pilot RC pozostaje jedynym
failsafe.** Kotwica = `goto(własny fix)` (jedno źródło `SRC_GOTO`), bez nowego trybu firmware.

Kluczowy use case: ustaw punkt goto, wygaś ekran telefonu, odejdź — łódź płynie dalej.

## Cele i zakres

**Cele:**
- Przycisk Spot-lock realnie kotwiczy łódź w bieżącej pozycji (R1, R2).
- Goto i spot-lock trwają wobec utraty linku z app; RC = jedyny failsafe (R3, R4).
- Jeden tryb autonomiczny naraz; STOP = uniwersalny off; Rozbrój = kill (R5).
- App wyszarza Spot-lock bez fixu; etykietuje tryb lokalnie; resync po powrocie z tła (R6–R8).

**Poza zakresem:**
- Trzymanie kursu dzioba (position-only), geofence dla kotwicy, strojenie params z app, trasy/multi-waypoint.
- Nowe źródło firmware, nowe pole telemetrii `target_source`, warstwa uzbrajania, ścieżka failsafe RC.

## Analiza obecnego stanu

- **Firmware:** spot-lock istnieje wyłącznie jako CH3 (`SRC_HOLD`, RC-owned). App-goto (`SRC_GOTO`)
  **pauzuje** przy utracie linku (`comms_fresh` bramkuje `hold_or_pause`). Brak komendy `hold`.
- **iOS:** przycisk Spot-lock = placeholder (`requestSpotLock` tylko ustawia komunikat).
  `KeepaliveDecision` trzyma ekran włączony i resenduje goto ~2 Hz. Brak obsługi `scenePhase`.

## Stan docelowy

- Komenda `hold` rozpoznana w firmware → pętla łapie własny fix atomowo → latch `SRC_GOTO`.
- `SRC_GOTO` nie pauzuje przy utracie linku (chirurgiczny flip); `comms_fresh` zostaje jako bramka re-latchu.
- iOS: przycisk wysyła `.hold`, wyszarzony bez fixu, brak keepalive/idle-timer, resync-on-resume.

## Fazy wdrożenia

### Faza 1 — Firmware (fundament: persist + komenda hold)
- **Unit 1** — Odwrócenie PAUSE→CONTINUE dla `SRC_GOTO` (chirurgiczny flip + 4 testy z mocą wyroczni). Ryzyko: najwyższe.
- **Unit 2** — Komenda `hold` w `command_parse` (pure).
- **Unit 3** — Okablowanie `hold` + atomowy grab własnego fixu w pętli.

### Faza 2 — iOS (mapowanie, usunięcie keepalive, resync)
- **Unit 4** — `.hold` w kontrakcie + mapowanie przycisku + lokalna intencja.
- **Unit 5** — Wyszarzanie Spot-lock bez fixu (pure readiness).
- **Unit 6** — Usunięcie keepalive/idle-timer.
- **Unit 7** — Resync-on-resume + reconciler intencji.

**Sekwencja krytyczna:** Unit 1 przed Unit 6 (inaczej okno „app nie resenduje, firmware jeszcze pauzuje" → łódź staje).

## Kryteria akceptacji (zbiorcze)

- Tap Spot-lock (ARMED + fix + neutral) → łódź trzyma bieżący punkt; w martwej strefie luz.
- Wygaszenie ekranu podczas goto/kotwicy → łódź kontynuuje; po powrocie app pokazuje tryb aktywny.
- Ruch drążkiem poza martwą strefę / CH3 / disarm → tryb app kończy się natychmiast, bez auto-powrotu.
- STOP anuluje aktywny spot-lock; Rozbrój ubija silnik.
- Tap bez fixu → przycisk wyszarzony; brak kotwicy w null-island.
- Host-tests firmware zielone (m.in. nowy test odwrócenia z mocą wyroczni); build iOS + testy KayakContract zielone.

## Ocena ryzyka i mitygacje

- **Odwrócenie zweryfikowanego zachowania (Unit 1)** → chirurgiczny flip 1 flagi, 4 testy z mocą wyroczni, rozdzielenie domen comms/sensor, RC-abort nietknięty.
- **Regres null-island** → `comms_fresh` zostaje w bramce re-latchu + test retencji.
- **Usunięcie keepalive przed Unit 1** → sekwencja Unit 1 → Unit 6.
- **iOS device-only weryfikacja** (background/link) → ręczny test na urządzeniu z AP silnika (agent-browser N/D dla natywnego iOS).

## Mierniki sukcesu

- 4 testy z mocą wyroczni w `test_spot_lock.c` (odwrócenie + 3 zachowane) przechodzą; przywrócenie `comms_gated=true` czyni test odwrócenia czerwonym.
- Field-test: goto z wygaszonym ekranem kontynuuje; kotwica trzyma po rozłączeniu app; CH3 wywłaszcza kotwicę.

## Zależności

- Unit 3 zależy od Unit 2 (flaga) i Unit 1 (persist). Unit 6 zależy od Unit 1. Unit 7 zależy od Unit 4 i Unit 6. Unit 4 zależy od Unit 2 (parytet kontraktu firmware↔iOS).

## Dokumentacja do aktualizacji

- `docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md` — oznaczyć P1 jako odwrócone, P2/P3 obowiązują.
- `docs/completed/kayak-motor-firmware-v1/known-issues.md §4d` — nowe zachowanie persist + device-E2E.

## Źródła

- Requirements doc: docs/dev-brainstorms/2026-07-02-app-spot-lock-requirements.md
- Plan techniczny: docs/plans/2026-07-02-001-feat-app-spot-lock-latched-commands-plan.md
