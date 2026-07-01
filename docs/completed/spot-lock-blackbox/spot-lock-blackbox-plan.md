# Plan: Spot-lock blackbox — nagrywanie sesji na flash + auto-kalibracja przez USB

**Branch:** `feature/spot-lock-blackbox`
**Ostatnia aktualizacja:** 2026-06-30

## Źródła
- Requirements doc: `docs/dev-brainstorms/2026-06-29-spot-lock-blackbox-requirements.md`
- Plan techniczny: `docs/plans/2026-06-29-002-feat-spot-lock-blackbox-logging-plan.md`

---

## Podsumowanie wykonawcze

ESP samo nagrywa na wodzie przebieg każdej sesji spot-locka do dedykowanej
partycji flash (standalone, bez łączności). Po podłączeniu ESP do komputera
(port COM) logi są zrzucane przez konsolę USB jako CSV; Claude analizuje je
offline i jedną komendą zapisuje dostrojone nastawy regulatora do NVS. Firmware
tylko nagrywa, zrzuca i stosuje przekazane wartości — cała inteligencja strojenia
jest po stronie Claude. Nagrywanie i odczyt nie dotykają pętli sterującej 50 Hz
ani toru failsafe (recorder jako task tła; zapis nastaw przez istniejący tor SI-6).

**Wartość biznesowa:** strojenie spot-locka przechodzi z subiektywnego zgadywania
zza drążków na analizę realnego przebiegu (błąd pozycji, bearing, komendy) — szybciej
i celniej, z priorytetem stabilności (brak „polowania").

## Analiza obecnego stanu

- Telemetria `control_loop_snapshot` zawiera już materiał do logu (stan spot-locka,
  err_m, bearing, GPS lat/lon/fix/sats/speed, heading, komendy serwa/ESC, drążki);
  `control_loop_get_snapshot()` jest bezpieczny do odczytu z innego taska.
- Pętla 50 Hz (`CONTROL_LOOP_PERIOD_MS=20`) publikuje snapshot co cykl.
- Settings SI-6: pending mailbox + apply tylko w DISARMED (`maybe_apply_pending`),
  commit `nvs_store_commit` + debounce; pola `spot_lock_*` (u16, schema v6);
  walidacja `settings_validate`; wejście `control_loop_post_pending`.
- **Brak frameworku komend konsolowych** (`esp_console`) — UART0/USB tylko `ESP_LOG`.
- **Brak partycji data i użycia `esp_partition`** — flash 16 MB, partycje zajmują ~1,6 MB.
- Host-test harness (Unity, `test/host/run.sh`) gotowy; wzorce do recyklingu:
  `blob_codec` (wersjonowany blob + CRC), `resolve_provenance`, `command_parse`,
  task tła `gps_reader`, reguły wrap-safe ring + Pure ⊥ HAL + oracle power.

## Proponowany stan docelowy

- Dedykowana partycja `spotlog` (dopisana na końcu `partitions.csv`, bez ruszania
  offsetów) z surowym ringiem po `esp_partition`.
- Czysty, host-testowany rdzeń: kodek rekordu/nagłówka sesji + wrap-safe indeks ringu.
- Recorder jako task tła prio 2 (peek snapshotu co ~2 Hz przy `spot_lock_state != OFF`).
- Minimalny `esp_console` REPL na USB Serial/JTAG: `spotlog dump` (CSV),
  `params get`, `params set` (auto-zapis przez SI-6).
- Procedura kalibracji (offline po stronie Claude) udokumentowana; luki sprzętowe/E2E
  w known-issues.

## Fazy wdrożenia

### Faza 1 — Fundamenty persystencji (partycja + czysty rdzeń)

**Unit 1: Partycja `spotlog` + stałe geometrii regionu** (R1, R5) — nakład: **S**
- Cel: zarezerwować region logów na flashu i opisać geometrię jako named constants,
  bez ruszania istniejących partycji.
- Zależności: brak.
- Kryteria akceptacji: `idf.py build` zielony; partycja `spotlog` widoczna; offsety
  nvs/appcfg/factory niezmienione.

**Unit 2: Czysty rdzeń — kodek rekordu + indeks ringu (wrap-safe)** (R3, R5) — nakład: **M**
- Cel: host-testowana serializacja rekordu próbki i nagłówka sesji oraz wrap-safe
  mapowanie `seq → slot/sektor` z decyzją o nadpisaniu najstarszego.
- Zależności: Unit 1.
- Kryteria akceptacji: round-trip pól wierny; wrap seq u32 i pojemności poprawny
  (oracle power na overflow); brak `esp_*`/`driver/*` w czystych nagłówkach.

### Faza 2 — Nagrywanie (HAL + recorder)

**Unit 3: Adapter flash (HAL) + recorder task tła** (R1, R2, R3, R4, R5) — nakład: **L**
- Cel: cienki adapter `esp_partition` (erase/write/read) + task tła prio 2 peekujący
  snapshot co ~2 Hz, wykrywający start/koniec sesji, dopisujący przez czysty rdzeń.
- Zależności: Unit 1, Unit 2.
- Kryteria akceptacji: decyzja sesji/próbki host-testowana; `idf.py build` zielony;
  zero wpływu na pętlę 50 Hz/failsafe (weryfikacja sprzętowa → known-issues).

### Faza 3 — Konsola, kalibracja, dokumentacja

**Unit 4: Konsola USB (esp_console REPL) + `spotlog dump` (CSV)** (R6) — nakład: **L**
- Cel: minimalny REPL na USB Serial/JTAG i komenda zrzutu regionu jako
  zdenormalizowany CSV po porcie COM.
- Zależności: Unit 2, Unit 3.
- Kryteria akceptacji: formatowanie wiersza CSV host-testowane; `idf.py build`
  zielony; na sprzęcie `spotlog dump` daje parsowalny CSV (→ known-issues).

**Unit 5: `params get` / `params set` — auto-zapis przez SI-6** (R7) — nakład: **M**
- Cel: odczyt bieżących nastaw i automatyczny zapis dostrojonych wartości z konsoli
  USB, wymuszający SI-6 (DISARMED + walidacja + single-writer).
- Zależności: Unit 4, istniejące settings/SI-6.
- Kryteria akceptacji: parsowanie/walidacja argumentów host-testowane; wartość poza
  zakresem odrzucona; `params set` reużywa `settings_validate` + `control_loop_post_pending`.

**Unit 6: Procedura kalibracji + dokumentacja i known-issues** (R8) — nakład: **S**
- Cel: udokumentować jak Claude czyta CSV → metryki → zmiana nastaw (priorytet
  stabilności); zalogować luki sprzętowe/E2E; README/pinout.
- Zależności: Unit 1–5.
- Kryteria akceptacji: dokumenty spójne; known-issues zawiera luki sprzętowe/E2E.

## Ocena ryzyka i mitygacje

- **Brak frameworku konsoli** — nowy `esp_console` na USB Serial/JTAG; ryzyko kolizji
  z `ESP_LOG`. Mitygacja: rozważyć logi na UART0, REPL na USB (decyzja na sprzęcie).
- **Erase flash a pętla 50 Hz** — erase blokuje ms. Mitygacja: cały I/O w tasku tła
  prio 2, nigdy w pętli sterującej; weryfikacja jittera na sprzęcie.
- **Zmiana `partitions.csv`** wymaga reflashu layoutu. Mitygacja: dopisanie na końcu
  (offsety bez zmian) → appcfg/kalibracja przeżywa.
- **Auto-zapis bez potwierdzenia** (decyzja produktowa) — ryzyko ograniczone przez
  SI-6 i zwrot zastosowanych wartości.
- **Strojenie na sprzęcie nieweryfikowalne na hoście** — heurystyki R8 dostrajane w
  terenie; host-testy pokrywają tylko czystą logikę.

## Mierniki sukcesu

- Po sesji na wodzie i podłączeniu USB Claude odtwarza z CSV przebieg błędu, komendy
  i aktywne nastawy, i zapisuje poprawione nastawy jedną komendą.
- Nagrywanie nie wpływa na timing pętli/failsafe (host-testy + brak regresji 50 Hz).
- Iterowanie pętli (wypłyń → wróć → strojenie → powtórz) redukuje polowanie/
  przeregulowanie/donuty.

## Wymagane zasoby i zależności

- Reużycie: `control_loop` (snapshot, pending/apply), `settings` (SI-6, blob_codec,
  nvs_store, settings_validate), `gps` (wzorzec taska tła), `partitions.csv`.
- Nowe komponenty: `blackbox` (rdzeń+HAL+recorder+CSV), `usb_console` (REPL+komendy).
- Toolchain ESP-IDF (`idf.py build`, esp32s3 N16R8) + host-test harness (Unity).
- Założenie: wolne miejsce na flashu na partycję `spotlog` (start 1 MB); reflash
  layoutu przy pierwszym wdrożeniu.

## Szacunki czasowe (orientacyjne, sekwencja zależności)

- Faza 1 (Unit 1–2): S + M; Unit 2 zależy od 1.
- Faza 2 (Unit 3): L; zależy od 1, 2.
- Faza 3 (Unit 4–6): L + M + S; Unit 5 po 4, Unit 6 po 1–5.
- De-risking: najpierw stabilny zapis/odczyt na łagodnych założeniach, potem
  strojenie heurystyk w terenie.
