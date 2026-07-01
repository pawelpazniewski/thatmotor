# Zadania: Spot-lock blackbox

**Branch:** `feature/spot-lock-blackbox`
**Ostatnia aktualizacja:** 2026-06-30

## Źródła
- Requirements doc: `docs/dev-brainstorms/2026-06-29-spot-lock-blackbox-requirements.md`
- Plan techniczny: `docs/plans/2026-06-29-002-feat-spot-lock-blackbox-logging-plan.md`

Legenda: `Test:` = scenariusz testowy (host/Unity), `Weryfikacja:` = kryterium ukończenia.

---

## Faza 1 — Fundamenty persystencji

### Unit 1: Partycja `spotlog` + stałe geometrii regionu (R1, R5)

Implementacja:
- [x] Modyfikuj `partitions.csv` — dopisz NA KOŃCU partycję `spotlog` (data, własny subtype, ~1 MB), bez zmiany offsetów nvs/phy_init/factory/appcfg
- [x] Stwórz `components/blackbox/include/blackbox_region.h` — czyste named constants: rozmiar sektora 4 KB, rozmiar rekordu, pojemność w rekordach, nazwa partycji (bez `esp_*`)

Weryfikacja:
- [ ] Weryfikacja: `idf.py build` zielony
- [ ] Weryfikacja: `idf.py partition-table` pokazuje `spotlog`; offsety nvs/appcfg/factory niezmienione względem poprzedniego layoutu

### Unit 2: Czysty rdzeń — kodek rekordu + indeks ringu (wrap-safe) (R3, R5)

Implementacja:
- [x] Stwórz `components/blackbox/include/blackbox_record.h` + `src/blackbox_record.c` — encode/decode rekordu próbki (t_ms, substate, err_m, bearing_deg10, heading_deg10, servo_us, esc_us, ch1_us, ch2_us, lat_e7, lon_e7, sats, speed_cms, flagi gps_fix/imu_ok) i nagłówka sesji (session_seq, target_lat/lon_e7, deadband_m, max_throttle_pct, throttle_gain, servo_gain, czas); magic/schema + CRC; bez `esp_*`
- [x] Stwórz `components/blackbox/include/blackbox_ring.h` + `src/blackbox_ring.c` — z `seq`+pojemności: offset slotu, czy potrzebny erase sektora, kolejność odczytu (unsigned modular arithmetic)
- [x] Stwórz `test/host/test_blackbox_record.c`, `test/host/test_blackbox_ring.c`; zarejestruj w `test/host/CMakeLists.txt` (PURE_SOURCES + include) i `test/host/test_main.c`

Testy (test-first; overflow/wrap wejściem POZA granicą — oracle power):
- [x] Test: round-trip encode→decode rekordu i nagłówka — wszystkie pola wierne
- [x] Test: decode pustego sektora (0xFF) → rekord nieważny (nie mylony z danymi)
- [x] Test: decode rekordu z zepsutym CRC → nieważny
- [x] Test: `slot` poprawny tuż przed i po zawinięciu pojemności
- [x] Test: granica wrapu `seq` u32 (tuż przed `UINT32_MAX`, kolejny po przewinięciu) → poprawne uporządkowanie; FAILuje przy naiwnym porównaniu
- [x] Test: overflow — zapis ponad pojemność nadpisuje najstarszy (wejście POZA pojemnością; FAILuje bez logiki nadpisania)

Weryfikacja:
- [ ] Weryfikacja: `test/host/run.sh` zielony z nowymi testami
- [ ] Weryfikacja: grep brak `esp_*`/`driver/*` w `blackbox_record.h`/`blackbox_ring.h`

---

## Faza 2 — Nagrywanie

### Unit 3: Adapter flash (HAL) + recorder task tła (R1, R2, R3, R4, R5)

Implementacja:
- [x] Stwórz `components/blackbox/include/blackbox.h` + `src/blackbox.c` — HAL: init partycji, erase sektora, append, read-all; mapuje `esp_err_t`→enum domenowy
- [x] Stwórz `components/blackbox/include/blackbox_sampler.h` + `src/blackbox_sampler.c` (lub w rdzeniu) — czysta decyzja „nowa sesja / próbkuj / zamknij" z `(prev_substate, cur_substate)`
- [x] Stwórz `components/blackbox/CMakeLists.txt` — REQUIRES (esp_partition/spi_flash, control_loop, freertos, esp_timer)
- [x] Modyfikuj `main/app_main.c` — start taska recordera prio 2 (wzorzec gps_reader) po `control_loop_init`; peek `control_loop_get_snapshot()` co ~2 Hz, zapis nagłówka na zboczu OFF→non-OFF, próbki w trakcie, zamknięcie na non-OFF→OFF
- [x] Stwórz `test/host/test_blackbox_sampler.c`; zarejestruj w CMake + `test_main.c`

Testy (test-first dla czystej decyzji sesji/próbki):
- [x] Test: zbocze OFF→ACTIVE → „rozpocznij sesję" (emit nagłówka)
- [x] Test: ACTIVE→ACTIVE → „próbkuj", bez nowego nagłówka
- [x] Test: ACTIVE→PAUSED → nadal „próbkuj" (ta sama sesja; R2 obejmuje PAUSED)
- [x] Test: non-OFF→OFF → „zamknij sesję", brak dalszych próbek
- [x] Test: pojemność spójna z rozmiarem regionu/rekordu (geometria z Unit 1)

Weryfikacja:
- [ ] Weryfikacja: `test/host/run.sh` zielony
- [ ] Weryfikacja: `idf.py build` zielony
- [ ] Weryfikacja: na sprzęcie task startuje, pętla 50 Hz bez regresji czasu cyklu (→ known-issues, weryfikacja sprzętowa)

---

## Faza 3 — Konsola, kalibracja, dokumentacja

### Unit 4: Konsola USB (esp_console REPL) + `spotlog dump` (CSV) (R6)

Implementacja:
- [x] Stwórz `components/usb_console/include/usb_console.h` + `src/usb_console.c` — init `esp_console` REPL na USB Serial/JTAG, rejestracja komend
- [x] Stwórz `components/blackbox/include/blackbox_csv.h` + `src/blackbox_csv.c` — czysta: dekodowany rekord+nagłówek → wiersz CSV w buforze
- [x] Stwórz `components/usb_console/CMakeLists.txt` — REQUIRES (console, blackbox)
- [x] Modyfikuj `main/app_main.c` — init konsoli; ustal koegzystencję z `ESP_LOG`
- [x] Komenda `spotlog dump` — read-all regionu (HAL) → dekod (rdzeń) → zdenormalizowany CSV po REPL (nagłówek + wiersz/próbkę, każdy wiersz z session_id + aktywne nastawy sesji)
- [x] Stwórz `test/host/test_blackbox_csv.c`; zarejestruj w CMake + `test_main.c`

Testy:
- [x] Test: dekodowany rekord → wiersz CSV: poprawna kolejność i wartości kolumn
- [x] Test: nagłówek CSV zgodny z kolejnością pól (kontrakt dla parsera Claude)
- [x] Test: sesja z nagłówkiem + N próbek → N wierszy z tym samym session_id i nastawami sesji

Weryfikacja:
- [ ] Weryfikacja: `test/host/run.sh` zielony
- [ ] Weryfikacja: `idf.py build` zielony
- [ ] Weryfikacja: na sprzęcie `spotlog dump` wypisuje parsowalny CSV (→ known-issues)

### Unit 5: `params get` / `params set` — auto-zapis przez SI-6 (R7)

Implementacja:
- [x] Modyfikuj `components/usb_console/src/usb_console.c` — rejestracja `params get`, `params set`
- [x] Stwórz `components/blackbox/include/params_cmd.h` + `src/params_cmd.c` — czysta decyzja parsowania/walidacji argumentów (mapowanie na pola spot_lock_*); HAL woła `settings_validate` + `control_loop_post_pending`
- [x] Modyfikuj (jeśli brak gettera) — ekspozycja bieżących `settings_params` do druku (`params get`) — istniejący `control_loop_get_active_params` wystarcza, nowy getter niepotrzebny
- [x] Stwórz `test/host/test_params_cmd.c`; zarejestruj w CMake + `test_main.c`

Testy (test-first; happy path + poza zakresem):
- [x] Test: `params set` z wartością w zakresie → staged, zwraca przyjęte pola
- [x] Test: `params set` z wartością poza zakresem (np. max_throttle_pct 200) → odrzucone, brak stage'owania (FAILuje gdyby walidacja była pominięta)
- [x] Test: `params get` zwraca bieżące spot_lock_* zgodne z aktywnymi nastawami
- [x] Test: (kontrakt SI-6) stage w ARMED nie aplikuje się dopóki nie DISARMED (reużycie istniejącej bramki)

Weryfikacja:
- [ ] Weryfikacja: `test/host/run.sh` zielony
- [ ] Weryfikacja: `idf.py build` zielony
- [ ] Weryfikacja: na sprzęcie `params set` w DISARMED zmienia aktywne nastawy (widoczne w `params get`/telemetrii); w ARMED → staged, brak zmiany do rozbrojenia (→ known-issues)

### Unit 6: Procedura kalibracji + dokumentacja i known-issues (R8)

Implementacja:
- [x] Stwórz `docs/blackbox-calibration.md` — procedura: kolumny CSV → metryki polowania/przeregulowania/donuta → kierunek zmiany deadband/gaz/gainów (priorytet stabilności R8); sekwencja `dump → analiza → params set → reflash off`
- [x] Modyfikuj `docs/completed/kayak-motor-firmware-v1/known-issues.md` — luki sprzętowe/E2E (realny zapis na flash, parsowalność CSV na żywo, brak jittera 50 Hz przy erase, wpływ nastaw na zachowanie na wodzie)
- [x] Modyfikuj README/pinout — sekcja blackbox: komendy USB `spotlog dump`/`params get`/`params set`, partycja `spotlog`

Testy:
- [ ] Test: [E2E] (sprzętowe → known-issues) sesja na wodzie → `spotlog dump` po USB → CSV z nagłówkiem sesji + malejący err_m; `params set` zmienia zachowanie w kolejnej sesji

Weryfikacja:
- [ ] Weryfikacja: dokumenty obecne i spójne; known-issues zawiera luki sprzętowe/E2E
- [ ] Weryfikacja: brak zmian w kodzie wymagających testów host

---

## Do poprawy po review fazy 1

Review 2026-07-01 — severity gate ✅ CZYSTE (P1=0, P2=0). Brak blokerów. Raport: `review-faza-1.md`.

- [ ] 🟡 [nit] **components/blackbox/src/blackbox_record.c:1-302** — 302 linie (2 ponad próg 300); plik kohezyjny, split do rozważenia dopiero przy dodaniu kolejnych typów rekordu w Fazie 2+. Nie blokuje.

---

## Do poprawy po review fazy 2

Review 2026-07-01 — severity gate ⚠️ ZASTRZEŻENIA (P1=0, P2=1, P3=4). Brak blokerów.
Raport: `review-faza-2.md`. Werdykt: obserwator/zero-wpływu ✅ OK, HAL cienki ✅ OK,
sampler oracle power ✅ OK, Pure ⊥ HAL ✅ OK, rozmiary/SRP ✅ OK.

- [x] 🟠 [important] **components/blackbox/src/blackbox.c:33 + blackbox_recorder.c:87 (→ Unit 4)** — cursor `s_seq` i `session_seq` zerowane w RAM na init: po reboocie/brownoucie MIĘDZY wypłynięciami (bez dumpu) sesja post-reboot reużywa `session_id` sprzed reboota i nadpisuje sloty od 0 → dump (`read_all` fizycznie rosnąco, bez `oldest_seq`) daje kolizję `session_id` + przeplot dwóch wypłynięć w złej kolejności → mylący CSV. Happy-path (dump w tym samym power-cycle) OK. Domknąć w Unit 4: skan regionu i zasianie `s_seq`/`session_seq` z najwyższego seq +1 (globalnie unikatowe id + porządkowanie po zapisanym seq), LUB per-record ring-seq + `spotlog dump` porządkuje przez `oldest_seq`/`seq_after`; udokumentować w known-issues + procedurze polowej „dump przed rebootem".
  **NAPRAWIONE (cykl 1):** `blackbox_init` skanuje region i zasiewa `s_seq`/`session_seq` przez czystą,
  host-testowaną funkcję `blackbox_resume_decide` (najwyższy `session_seq` → następna sesja +1;
  kursor za ostatnim ważnym rekordem). Nowy `test_blackbox_resume.c` (moc wyroczni: FAILuje przy
  zerowaniu kursora). Resztka po zawinięciu ringu (bez per-record seq) → known-issues §4b.
- [ ] 🟡 [nit] **components/blackbox/src/blackbox.c:69-89 (→ Unit 4)** — `read_all` robi 16384 osobnych `esp_partition_read` po 64 B; przy wpięciu do `spotlog dump` rozważ czytanie sektorami.
- [ ] 🟡 [nit] **components/blackbox/src/blackbox.c (→ Unit 4)** — `s_part`/`s_seq` bez locka; dump (task konsoli) równoległy do `append` (recorder) może się przeplatać. Rób dump w DISARMED/off-water lub dodaj synchronizację.
- [ ] 🟡 [nit] **components/blackbox/src/blackbox_recorder.c:82-100** — `write_header` konsumuje `session_seq`/`start_ms` przed encode; przy encode-fail id sesji przepada bez nagłówka na flash (best-effort, kosmetyczne).
- [ ] 🟡 [nit] **components/blackbox/src/blackbox_recorder.c:61-64** — cast `u32→u16` na pulse-width; glitch >65535 by uciął (RC/clamp ogranicza, dane obserwatora — akceptowalne).

## Do poprawy po review fazy 3

Review 2026-07-01 — severity gate ✅ CZYSTE (P1=0, P2=0, P3=4). Brak blokerów.
Raport: `review-faza-3.md`. Werdykt: console-primary-switch (USB Serial/JTAG) = P3 akceptowalny
z uwagą (dump filtrowalny po schemacie CSV, flash/monitor działają); SI-6 respektowane ✅
(ten sam `settings_validate` co HTTP, apply gated DISARMED, single-writer, out-of-range odrzucone
z mocą wyroczni); CSV jednoznaczny ✅ (full-string oracle nagłówek==kolumny, 22 płaskie kolumny).
Empirycznie: `run.sh` 391/391 zielone, `idf.py build` zielony (37% free).

- [ ] 🟡 [nit] **sdkconfig.defaults / usb_console.c** — primary console na USB Serial/JTAG: ESP_LOG dzieli port z REPL; parsowalność `spotlog dump` „na żywo" przy gęstym logowaniu = weryfikacja sprzętowa (known-issues §4c). Opcjonalnie wyciszyć logi na czas dumpu (nie wymagane).
- [ ] 🟡 [nit] **components/usb_console/src/usb_console.c:135-159** — dwa kolejne `params set` na różne pola W ARMED (obie staged) → mailbox depth-1 nadpisuje, zmiana pierwszego pola gubiona do rozbrojenia (baza = aktywne params). W DISARMED bez problemu. Zgodne z zachowaniem mailbox HTTP. Udokumentować „w ARMED jedno pole albo rozbrój między setami".
- [ ] 🟡 [nit] **components/blackbox/src/blackbox.c (read_all ↔ append)** — carry z Fazy 2, teraz wpięty: `spotlog dump` czyta region równolegle do `append` recordera bez locka. Mitygacja: dump w DISARMED/off-water (known-issues §4c).
- [ ] 🟡 [nit] **components/usb_console/src/usb_console.c:107** — `params get` `printf("%s", buf)` bez końcowego `\n` poza treścią (kosmetyczne; każda linia ma własny `\n`).

## Zamknięcie

- [ ] Finalny self-check: `test/host/run.sh` zielony + `idf.py build` zielony
- [ ] Rozważ `/dev-compound` dla wzorca „flush na flash bez jittera pętli RT 50 Hz" (udokumentowana luka wiedzy)
