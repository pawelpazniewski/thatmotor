# Plan: Goto — autonomiczna nawigacja do punktu z aplikacji (WiFi)

Branch: `feature/goto-waypoint-navigation`
Ostatnia aktualizacja: 2026-07-01

## Źródła
- Requirements doc: docs/dev-brainstorms/2026-06-29-spot-lock-requirements.md (upstream — non-goal „nawigacja do waypointów" realizowany tutaj)
- Plan techniczny: docs/plans/2026-07-01-001-feat-goto-waypoint-navigation-plan.md

## Cele i zakres

Aplikacja iOS wysyła przez WiFi cel `lat/lon`; firmware autonomicznie kieruje kajak do punktu i utrzymuje go tam. Feature **reużywa istniejący silnik spot-lock** (`spot_lock_step`) — jedyna różnica to źródło celu (zewnętrzne zamiast snapshotu CH3). Dokładamy: kanał celu przez `POST /api/command`, comms-watchdog na link aplikacji, arbitraż z CH3 i strukturalne pierwszeństwo failsafe. Cała nowa logika decyzyjna pozostaje czystą funkcją host-testowaną (Pure ⊥ HAL). Maszyna stanów i tor `rc_valid`/failsafe nietknięte. **Aplikacja iOS jest poza scope** (osobny plan) — tu tylko firmware'owy kontrakt API.

### Decyzje produktowe (wpięte)
- **Aktywacja z aplikacji** (ARMED + drążki neutral + komenda). **Bez fizycznego CH3.**
- **CH3 = priorytet fizyczny**: włączenie w trakcie goto przerywa nawigację → hold „tu i teraz"; poza goto CH3 działa jak dziś.
- **Utrata linku → PAUZA + wznowienie** (bramka świeżości tylko dla źródła goto).
- **Override drążkiem kasuje latch goto** (brak auto-resume po powrocie drążka do neutralu).

### Granice scope'u (non-goals)
- Brak tras / wielu waypointów; brak omijania przeszkód (jazda po linii prostej); brak ciągu wstecznego; brak trzymania kursu po dojściu; brak zmian maszyny stanów / `rc_valid` / failsafe; brak persystencji celu w NVS; aplikacja iOS poza planem.

## Śledzenie wymagań

- **R1.** Kanał celu: `POST /api/command {"cmd":"goto","lat_e7","lon_e7"}` + `{"cmd":"goto_cancel"}`; walidacja zakresu; błąd → 400 `{data,error}`.
- **R2.** Nawigacja + utrzymanie: reuse silnik spot-lock (±60°, forward-only, deadband, cap); po dojściu → hold w celu.
- **R3.** Aktywacja goto: ARMED + drążki neutral + świeży GPS/heading + komenda goto; źródło celu `SRC_GOTO`.
- **R4.** Arbitraż CH3: CH3 ON → przerwij goto + hold „tu i teraz" (snapshot); poza goto CH3 jak dziś.
- **R5.** Comms-watchdog: keepalive ~2 Hz; brak świeżości > timeout → PAUSED (neutral+center, cel zapamiętany), wznowienie po powrocie; tylko dla `SRC_GOTO`.
- **R6.** Pierwszeństwo (jedna bramka): goto tylko w ARMED (istniejąca bramka); override drążkiem → OFF + kasuje latch; `goto_cancel` → OFF.
- **R7.** Parametry: `goto_comms_timeout_ms` (SI-6); nastawy ruchu reuse spot-lock.
- **R8.** Telemetria: goto substate/cel/err_m/bearing/arrived/link — WS + panel.

## Fazy wdrożenia

### Faza 1 — Kanał celu z aplikacji (komenda + walidacja + transport)
- **Unit 1** (M): Walidacja celu (`goto_target_valid`, czysta) + rozszerzenie `command_parse` o goto/goto_cancel. Wymagania: R1, R6.
- **Unit 2** (M): HTTP handler goto (payload lat/lon, cJSON, 400 na błąd) + transport przez mailbox UI events. Wymagania: R1, R6. Zależy od Unit 1.

### Faza 2 — Rdzeń decyzyjny (rozszerzenie silnika, jeszcze nie steruje)
- **Unit 3** (L): Rozszerzenie `spot_lock_step` o źródło celu (`SRC_HOLD`/`SRC_GOTO`) + arbitraż CH3/goto + bramka linku. Wymagania: R2, R3, R4, R5, R6.

### Faza 3 — Integracja + comms-watchdog (włączenie sterowania)
- **Unit 4** (L): Integracja goto w `loop_step`/`control_loop` + watchdog linku (`sensor_is_fresh`) + cykl życia latcha. Wymagania: R2, R3, R4, R5, R6. Zależy od Unit 2, Unit 3.

### Faza 4 — Parametry + telemetria + panel
- **Unit 5** (S): Parametr `goto_comms_timeout_ms` (SI-6, bump schematu). Wymaganie: R7.
- **Unit 6** (M): Telemetria goto + panel ESP. Wymaganie: R8. Zależy od Unit 4.

## Kryteria akceptacji (globalne)
- Wszystkie host-testy zielone (nowe + zero regresji `spot_lock`/`loop_step`/`command_parse`/`settings`).
- `idf.py build` (esp32s3) zielony.
- Zero nowych `esp_*`/`driver/*` w czystych nagłówkach (`goto_target.h`, `spot_lock.h`).
- Pierwszeństwo failsafe/manualne udowodnione testami z mocą wyroczni.
- Weryfikacje hardware/na wodzie zalogowane w `known-issues.md`.

## Ocena ryzyka (skrót)
- App jako źródło sterowania bez RC-failsafe → comms-watchdog + bramka ARMED + override.
- Rozszerzenie przetestowanego `spot_lock_step` → zachowanie wszystkich istniejących asercji.
- Jazda po linii prostej ignoruje przeszkody → watchdog + łagodne defaults + dokumentacja.
- Bump `SETTINGS_SCHEMA_VERSION` → wzorzec migracji do defaults + round-trip test.

Pełne szczegóły (podejście, wzorce, scenariusze testowe, weryfikacja per Unit): patrz plan techniczny w `docs/plans/2026-07-01-001-feat-goto-waypoint-navigation-plan.md`.
