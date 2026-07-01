# Re-review Fazy 2 (cykl 1) — po naprawie P2 wznowienia cursora/session_seq

**Data re-review:** 2026-07-01
**Commit naprawy:** 746b227 `fix(blackbox): wznowienie cursora/session_seq po reboocie (poprawki po review fazy 2, cykl 1)`
**Zakres naprawy:** `blackbox_resume.{h,c}` (nowy czysty moduł Pure ⊥ HAL), `blackbox.c`
(skan regionu na init + getter), `blackbox_recorder.c` (zasiew `s_session_seq`),
`blackbox.h` (getter + doc), `test_blackbox_resume.c` (7 testów), `known-issues.md §4b`,
`CMakeLists.txt` (host + komponent), `test_main.c`.

## Severity gate (re-review cykl 1): ✅ CZYSTE (GOTOWE DO KONTYNUACJI)

- **P1 (blocking): 0**
- **P2 (important): 0** — poprzedni P2 rozwiązany
- **P3 (nit): 0 nowych** (4 P3 z cyklu 0 świadomie odłożone „pomiń P3", niezregresowane)

**Czy P2 z poprzedniego review rozwiązany: TAK.** Zweryfikowane w kodzie, empirycznie
(oracle power) i przez twarde bramki.

### Weryfikacja P2 — faktyczna naprawa

| Kryterium | Wynik |
|---|---|
| init NIE zeruje cursora/session gdy region ma dane | ✅ `blackbox.c:resume_from_flash` skanuje 0..CAPACITY, folduje przez `blackbox_resume_decide`, ustawia `s_seq=seed.cursor`, `s_resume_session_seq=seed.session_seq`. Zerowanie `s_seq=0` usunięte. |
| brak kolizji session_id po reboocie | ✅ `blackbox_recorder_start` zasiewa `s_session_seq = blackbox_resume_session_seq()` (= max seen); następny START = `++s_session_seq` = max+1. Nigdy reużyte id. |
| brak nadpisania poprzedniego wypłynięcia od slotu 0 | ✅ cursor = `last_valid_slot+1`; nowe rekordy lądują za ostatnim ważnym → `read_all` fizycznie rosnąco daje [A][B] chronologicznie (przeplot z cyklu 0 zniknął). |
| test wznowienia ma REALNĄ moc wyroczni | ✅ **empirycznie potwierdzone**: zmutowałem `blackbox_resume_decide` na `{0,0}` → **5 testów FAIL** (`test_resume_after_reboot…` Expected 100 Was 0, `…highest_session` Expected 3 Was 0, `…gap_does_not_shorten` Expected 11 Was 0, `…out_of_order` Expected 5 Was 0, `…trailing_empty` Expected 3 Was 0). Plik przywrócony, `git status` czysty. |
| brak osłabienia asercji | ✅ Wszystkie 7 testów mają `TEST_ASSERT_EQUAL_UINT32` na konkretne wartości; zero assertion-free; test luki/korupcji broni „highest slot ≠ count valid". |

### Kontrakty Fazy 2 — nie złamane naprawą

- **Obserwator / zero wpływu na 50 Hz / failsafe** — ✅ cały skan to `esp_partition_read`
  na init, PRZED `xTaskCreate(recorder_task)`, na tasku wołającym `blackbox_recorder_start`
  (log-and-continue w `app_main`). Nie dotyka pętli sterującej ani toru failsafe. Recorder
  bez zmian (peek snapshotu best-effort).
- **HAL cienki** — ✅ decyzja w czystym rdzeniu `blackbox_resume_decide`/`_scan_slot`
  (streaming fold, nie tablica). HAL (`resume_from_flash`, `scan_slot`) robi tylko raw read
  + klasyfikację przez `blackbox_record_classify`/`_decode_header` i deleguje seed.
- **Pure ⊥ HAL** — ✅ `grep` po `esp_`/`driver/` w `blackbox_resume.{h,c}` → NONE. Tylko
  `<stdbool.h>`/`<stdint.h>`. Host-testowany standalone.
- **Brak pułapki `*/` w komentarzach** — ✅ balans `/* … */` zgodny (resume.c 2/2,
  resume.h 12/12, blackbox.c 6/6); host + idf build to potwierdzają.
- **Rozmiary/SRP** — ✅ `blackbox_resume.c` 39 linii, `blackbox.c` 151 (< 300), funkcje < 50.
  Nowy moduł zamiast utuczenia HAL — zgodne z regułą „ekstrakcja > komplikacja".

### Brak regresji (twarde bramki)

| Bramka | Wynik |
|---|---|
| `test/host/run.sh` | ✅ **379 Tests, 0 Failures, 0 Ignored** (+7 blackbox_resume vs 372) |
| `idf.py build` (esp32s3 N16R8) | ✅ **zielony** — bin `0xECDF0`, app partition `0x180000`, `0x93210` (38%) free; `blackbox_resume.c` zlinkowany do `libblackbox.a` |

Diagnostyki clangd/xtensa (`-mlongcalls`, `unknown type name`) potraktowane jako szum
toolchainu — nie findingi (zgodnie z memory toolchain: autorytatywne to `idf.py build` + host).

### Uczciwość residualu w known-issues §4b

✅ **Uczciwy — nie ukrywa problemu programowego jako sprzętowy.** Tekst §4b jawnie stwierdza,
że część data-integrity (kolizja `session_id`, kontynuacja kursora) jest **naprawiona i
host-testowana**, a resztka dotyczy WYŁĄCZNIE wznowienia po pełnym ZAWINIĘCIU ringu (>16384
rekordów w jednym wypłynięciu) — gdzie bez per-record monotonicznego seq skan nie odróżni
głowicy w środku ringu od granicy oldest/newest. Podana realna przyczyna (brak per-record
seq), ścieżka domknięcia (per-record ring-seq + `oldest_seq`/`seq_after`) i założenie
odsuwające ryzyko (16384 sloty ≫ jedno wypłynięcie; „dump po każdym wypłynięciu").
Uwaga stylistyczna (nie finding): tag `[HW]` + „czysto sprzętowa" jest lekko nieprecyzyjny —
to raczej ograniczenie modelu danych — ale treść w pełni ujawnia software'owy charakter, więc
brak dismissalu.

### Nowe findingi

**Brak nowych P1/P2/P3.** Jedyna obserwacja (poniżej progu findingu): `resume_from_flash`
robi 16384 osobne `esp_partition_read` po 64 B na każdym boocie — to jednorazowy koszt
startu, PRZED taskiem, poza torem 50 Hz; ta sama natura co już zalogowany P3 „read_all
sektorami" (Unit 4). Nie eskaluję — off-path, jednorazowe, ~1 MB odczytu mmap.

### Werdykt re-review

P2 z cyklu 0 **faktycznie naprawiony** (kod + empiryczna moc wyroczni + zielone bramki),
kontrakty Fazy 2 nienaruszone, Pure ⊥ HAL utrzymane, residual w known-issues uczciwy.
**Gate: ✅ CZYSTE — 0×P1, 0×P2, 0 nowych P3. Faza 2 domknięta, wolna droga do Fazy 3 (Unit 4).**

---

## Historia — Review Fazy 2 (cykl 0) — Unit 3 (HAL flash esp_partition + recorder task tła)

**Data:** 2026-07-01
**Commit:** 6f6cb62 `feat(blackbox): adapter flash HAL esp_partition + recorder task tła (Unit 3)`
**Zakres:** `blackbox.c` (HAL), `blackbox_sampler.*` (czysta decyzja), `blackbox_recorder.*`
(task tła), `CMakeLists.txt` (rejestracja komponentu w buildzie IDF), `main/app_main.c`
(+`main/CMakeLists.txt`), `test/host/test_blackbox_sampler.c`.

---

## Severity gate: ⚠️ ZASTRZEŻENIA (KONTYNUUJ Z ZASTRZEŻENIAMI)

- **P1 (blocking): 0**
- **P2 (important): 1** — cursor/session_seq reset na init (integralność kolejności przy dumpie)
- **P3 (nit): 4**

Typy findingów per severity:
- **P2:** KOD / E2E (data-integrity, dotyka kontraktu Unit 4 dump)
- **P3:** KOD (nity, głównie noty pod Unit 4)
- **TEST:** brak findingów — testy sampler mają realną moc wyroczni.

---

## Wynik weryfikacji (twarde bramki)

| Bramka | Wynik |
|---|---|
| `test/host/run.sh` | ✅ **372 Tests, 0 Failures, 0 Ignored** (+6 sampler) |
| `idf.py build` (esp32s3 N16R8) | ✅ **zielony** — bin `0xECAD0`, app partition `0x180000`, `0x93530` (38%) free |

Obie zgodne z deklaracją w commicie/kontekście. Diagnostyki clangd/xtensa
(`-mlongcalls`, `unknown type name bool`) potraktowane jako szum toolchainu — nie findingi.

---

## Werdykt na priorytety review

### 1. OBSERWATOR / ZERO WPŁYWU NA STEROWANIE — ✅ OK

- `recorder_tick` czyta wyłącznie `control_loop_get_snapshot()` (plain `*out = s_snapshot`,
  nieblokujący, bez locka — najnowszy slot best-effort) i `control_loop_get_active_params()`
  (plain struct copy). Nie zapisuje `rc_valid`/`sm_inputs`, nie woła `sm_step`,
  nie dotyka toru failsafe.
- Cały I/O flash (`esp_partition_erase_range`/`_write`/`_read`) żyje w `blackbox.c`
  wołanym z taska `blackbox` (prio 2, ~2 Hz, tick 500 ms). NIGDY w pętli 50 Hz.
- Błąd flash = best-effort: `append_or_warn` → `ESP_LOGW`, brak crasha/stalla; encode
  fail → `ESP_LOGW` + return. Start w `app_main` opcjonalny (log-and-continue, nie
  przerywa boota — dokładny wzorzec `gps_start`/`imu_start`).
- **Ocena residualnego ryzyka (jitter 50 Hz przy erase):** task tła jest POPRAWNĄ i
  jedyną firmware'ową mitygacją — gwarantuje, że erase nie jest NIGDY wywoływany
  z taska sterującego (brak preempcji schedulera). Erase 4 KB pada raz na 64 rekordy
  (≈ raz na 32 s przy 2 Hz). Residualne ryzyko cache-stall SPI-flash (erase potrafi
  wstrzymać kod z flasha na obu rdzeniach na czas operacji) jest **weryfikacją
  SPRZĘTOWĄ → known-issues**, nie findingiem hosta. Architektura robi maksimum, co
  można po stronie firmware.

### 2. HAL CIENKI — ✅ OK

- `blackbox.c` deleguje matematykę ringu do czystego rdzenia: `blackbox_ring_offset`
  (offset slotu) + `blackbox_ring_needs_erase` (kiedy erase). Kodek rekordu w
  `blackbox_record` (encode w recorderze). HAL robi tylko erase/write/read — **zero
  duplikacji** logiki ringu/kodeka.
- `map_err`: `ESP_OK→OK`, `INVALID_ARG`/`INVALID_SIZE→ERR_ARG`, reszta→`ERR_IO`.
  Poprawne mapowanie. Stan przed init → `ERR_STATE`, brak partycji → `ERR_NOT_FOUND`.
- Uwaga (nie duplikacja): `blackbox_read_all` skanuje sloty fizycznie rosnąco
  (`offset = slot * RECORD_SIZE`), nie używa `oldest_seq`/`seq_after`. To świadome —
  uporządkowanie logiczne to sprawa dekodera (Unit 4). Patrz P2 poniżej.

### 3. SAMPLER ORACLE POWER — ✅ OK

- `blackbox_sampler_decide` mapuje przejścia dokładnie: `OFF→non-OFF=START`,
  `non-OFF→non-OFF=SAMPLE` (w tym `ACTIVE→PAUSED`, `PAUSED→PAUSED`, `PAUSED→ACTIVE`),
  `non-OFF→OFF=CLOSE`, `OFF→OFF=IDLE`. Enkoding OFF/ACTIVE/PAUSED=0/1/2 zgodny z
  `spot_lock.h` (`spot_lock_substate`).
- **Moc wyroczni potwierdzona:** `test_off_to_active_starts_session` z jawnym komentarzem
  „naive 'sample whenever non-OFF' would return SAMPLE here" — test odróżnia START od
  SAMPLE. Naiwna impl. (record gdy non-OFF) FAILuje ten test. `test_active_to_paused`
  broni R2 (PAUSED w sesji). `test_nonoff_to_off_closes_session` odróżnia CLOSE od IDLE.
  Nie ma testów assertion-free, każdy z asercją.

### 4. CURSOR SEQ ZEROWANY NA INIT (RAM) — 🟠 **P2 [important]**

**Klasyfikacja: P2 (important), nie-blokujące, do rozwiązania w/przed Unit 4 (dump).**

Fakty ustalone z kodu:
- `blackbox_init` ustawia `s_seq = 0`; `s_session_seq` (static w recorderze) też startuje
  od 0 po reboocie → pierwsza sesja po reboocie dostaje `session_seq = 1` **ponownie**.
- Rekord **nie przechowuje** per-record ring-seq. Nagłówek trzyma tylko `session_seq`,
  próbka tylko `t_ms` (względem startu sesji). Uporządkowanie przy odczycie opiera się
  WYŁĄCZNIE o fizyczną kolejność slotów (`read_all` skanuje 0..N liniowo).

Scenariusz kolizji (reboot MIĘDZY wypłynięciami bez dumpu — realny brownout baterii
kajaka mid-outing):
1. Sesja A (power-cycle 1) zapisuje sloty 0..N, `session_seq=1`.
2. Reboot → `s_seq=0`, `s_session_seq=0`.
3. Sesja B (power-cycle 2) zapisuje sloty 0..M (M<N), `session_seq=1` (**ta sama!**).
   Erase-ahead czyści sektory B, ale resztki A w sektorach > M dekodują się jako
   ważne rekordy „sesji 1".
4. Dump (`read_all` fizycznie rosnąco) daje: [B session 1 …] potem [A session 1 …] —
   **dwie różne sesje pod tym samym `session_id`, przeplecione w złej kolejności**.

Skutek: zdenormalizowany CSV kluczowany po `session_id` **scali dwa wypłynięcia** pod
jednym id i przeplecie wiersze → analiza offline (rdzeń wartości: „Claude odtwarza przebieg
z CSV") dostaje mylące dane. Dodatkowo: mechanizm wrap-safe ringu (`oldest_seq`/`seq_after`,
zaprojektowany dokładnie do porządkowania po zawinięciu) jest przez `read_all` obchodzony —
nawet BEZ reboota zawinięty ring czytany jest od slotu 0 (środek ringu), nie od najstarszego
logicznie rekordu.

Dla celu happy-path (**dump po każdym wypłynięciu, w tym samym power-cycle**) problem NIE
występuje — w jednym cyklu zasilania `s_seq`/`session_seq` są monotoniczne i spójne. Ryzyko
materializuje się tylko gdy reboot wpadnie między wypłynięcie a dump (pominięty dump albo
brownout mid-outing — na wodzie realne). Świadomie odłożone w planie (resume cursora przez
skan regionu — poza Unit 3) i udokumentowane w kontekście.

**Dlaczego P2 (nie P3):** propozycja wartości fazy 3 (parsowalny, jednoznaczny CSV do
strojenia) łamie się cicho — kolizja `session_id` + przeplot aktywnie wprowadzają w błąd,
a naprawa naturalnie należy do NASTĘPNEGO unitu (Unit 4 dump/CSV). To więcej niż nit.
**Nie blokuje** kontynuacji: obserwator, brak wpływu na sterowanie/failsafe, happy-path OK.

**Rekomendacja (Unit 4, do wyboru):**
- (preferowane) na `blackbox_init` zeskanuj region i zasiej `s_seq`/`session_seq` z
  najwyższego istniejącego seq +1 (globalnie unikatowe id sesji + `read_all` może
  porządkować po zapisanym seq), LUB
- minimum: zapisz per-record monotoniczny ring-seq i uporządkuj dump przez `oldest_seq`/
  `seq_after`, oraz udokumentuj w known-issues + procedurze polowej „dump PRZED
  odłączeniem/rebootem", a `spotlog dump` niech odrzuca/oznacza rekordy sprzed reboota.

### 5. Pure ⊥ HAL — ✅ OK

- `blackbox_sampler.h` czysty: tylko `<stdbool.h>`/`<stdint.h>`; słowa „esp_" występują
  wyłącznie w tekście komentarza (dokumentacja „no esp_ or driver includes"), nie jako
  include. `blackbox_region.h` analogicznie czysty. Grep potwierdza brak realnych
  include `esp_*`/`driver/*` w czystych nagłówkach.
- Brak pułapki `*/` — wszystkie `*/` to poprawne domknięcia komentarzy (komentarze
  zbalansowane, build/host to potwierdza).

### 6. Reguły rozmiaru / SRP — ✅ OK

- Rozmiary: `blackbox_recorder.c` 166, `blackbox.c` 89, `blackbox_sampler.c` 26 —
  wszystkie < 300. Funkcje wszystkie < 50 linii, jeden poziom abstrakcji.
- `app_main.c` NIE utuczony logiką tła: dodane 11 linii = wyłącznie wołanie
  `blackbox_recorder_start()` + log-and-continue. Logika taska w komponencie
  (`blackbox_recorder_start()`), zgodnie ze świadomą decyzją (SRP, wzorzec gps/imu).
  To pozytywne odchylenie od dosłownego brzmienia zadania („modyfikuj app_main — start
  taska"), utrzymuje app_main cienki. Kontrakt „start po `control_loop_init`, prio 2,
  ~2 Hz" zachowany.

---

## Findingi P3 (nity — noty, głównie pod Unit 4)

- 🟡 **blackbox.c:69-89** — `blackbox_read_all` robi 16384 osobnych `esp_partition_read`
  po 64 B. Dla jednorazowego dumpu offline akceptowalne; przy wpięciu do `spotlog dump`
  (Unit 4) rozważ czytanie sektorami. Nie dotyczy tej fazy (read_all jeszcze niepodpięty).
- 🟡 **blackbox.c (współbieżność) / Unit 4** — `s_part`/`s_seq` bez locka; `read_all`
  (task konsoli, Unit 4) i `append` (recorder) współdzielą stan. Dump równoległy do
  nagrywania może się przeplatać. Dziś nieaktywne (read_all niepodpięty); w Unit 4 zrób
  dump w DISARMED/off-water albo dodaj synchronizację.
- 🟡 **blackbox_recorder.c:82-100** — `write_header` inkrementuje `s_session_seq` i ustawia
  `s_session_start_ms` PRZED encode; przy encode-fail id sesji jest „skonsumowane", a na
  flash nie ma nagłówka. Best-effort, akceptowalne; kosmetyczne.
- 🟡 **blackbox_recorder.c:61-64** — cast `u32→u16` na `servo_us/esc_us/ch1_us/ch2_us`
  (komentarz to potwierdza: zawsze w paśmie serwa). Glitch >65535 by uciął, ale RC/clamp
  to ogranicza; dane obserwatora, nie safety. Akceptowalne.

---

## Podsumowanie

Solidny, cienki adapter HAL nad czystym rdzeniem z Fazy 1; recorder to prawdziwy
obserwator (best-effort, zero wpływu na sterowanie/failsafe, cały I/O poza pętlą 50 Hz).
Sampler poprawny z realną mocą wyroczni. Testy i build zielone i zgodne z deklaracją.

Jeden istotny finding (**P2**): zerowanie cursora/`session_seq` w RAM na init prowadzi do
kolizji `session_id` i przeplotu danych sprzed/po reboocie przy dumpie — happy-path (dump
w tym samym power-cycle) OK, ale reboot/brownout między wypłynięciami daje mylący CSV.
Świadomie odłożone; **musi zostać zaadresowane w Unit 4 (dump/CSV)** — dodane do „Do
poprawy po review fazy 2".

**Gate: ⚠️ ZASTRZEŻENIA — 0×P1, 1×P2, 4×P3. Kontynuacja dozwolona; P2 domknąć w Unit 4.**
