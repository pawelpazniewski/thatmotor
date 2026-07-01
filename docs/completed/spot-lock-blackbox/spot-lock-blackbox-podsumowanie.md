# Podsumowanie ukończenia: Spot-lock blackbox — nagrywanie sesji na flash + auto-kalibracja przez USB

**Zadanie:** spot-lock-blackbox
**Branch:** `feature/spot-lock-blackbox`
**Data ukończenia:** 2026-07-01

## Status końcowy

- **391/391 testów hosta (Unity) PASS** — standalone CMake+Unity (`test/host/run.sh`).
- **`idf.py build` (target esp32s3 N16R8) PASS** — bin 0xf0f70, 37% wolne w partycji app.
- Wszystkie 3 fazy (Unit 1–6) zaimplementowane i zreviewowane (multi-agent review każdej fazy).
- **Review:** Faza 1 ✅ CZYSTE (0× P1/P2); Faza 2 ⚠️ 1× P2 (data-integrity: kolizja
  `session_id`/`session_seq` po reboocie między wypłynięciami) naprawiony w 1 cyklu + ✅ CZYSTE
  re-review; Faza 3 ✅ CZYSTE (0× P1/P2). Pozostają tylko nity P3 (opcjonalne) opisane w
  `review-faza-{1,2,3}.md`.
- Luki `[HW]`/`[E2E]` (realny zapis na flash, jitter 50 Hz przy erase, parsowalność CSV na żywo,
  wpływ nastaw na wodzie, koegzystencja REPL/ESP_LOG, wznowienie po pełnym zawinięciu ringu)
  **świadomie odłożone** do `docs/completed/kayak-motor-firmware-v1/known-issues.md` §4b/§4c —
  luki weryfikacji zależne od środowiska, NIE braki implementacji.

## Co zostało dostarczone

Standalone **blackbox** dla spot-locka: ESP samo nagrywa na wodzie przebieg każdej sesji
(błąd pozycji, bearing, heading, komendy serwa/ESC, drążki, GPS, flagi) do dedykowanej partycji
flash `spotlog` (surowy ring po `esp_partition`, bez łączności). Po podłączeniu USB komenda
`spotlog dump` zrzuca region jako zdenormalizowany, płaski CSV; Claude analizuje offline i jedną
komendą `params set` zapisuje dostrojone nastawy regulatora przez istniejący tor SI-6. Firmware
jest „głupi" — tylko nagrywa, zrzuca i stosuje przekazane wartości. Nagrywanie/dump/zapis nie
dotykają pętli 50 Hz ani toru failsafe (recorder = task tła prio 2; blackbox to obserwator).

- **Faza 1 — Fundamenty persystencji (Unit 1–2):**
  - Unit 1: partycja `spotlog` (@0x196000, 1 MiB, subtype 0x40) dopisana NA KOŃCU
    `partitions.csv` — offsety nvs/phy_init/factory/appcfg niezmienione. Geometria jako named
    constants w `blackbox_region.h` (sektor 4 KB, rekord 64 B, pojemność 16384; rekord dzieli
    sektor równo, brak straddle 64|4096).
  - Unit 2: czysty rdzeń host-testowany — `blackbox_record.*` (kodek próbki/nagłówka sesji,
    magic 0xB10C + schema + CRC32, taksonomia empty/magic/crc/schema/type) i `blackbox_ring.*`
    (seq→slot modulo, decyzja erase, wrap-safe `seq_after` signed-modular, `oldest_seq`).
- **Faza 2 — Nagrywanie (Unit 3):**
  - Cienki HAL `blackbox.*` (adapter `esp_partition`: init/erase/append/read-all,
    `esp_err_t`→`blackbox_status`, cursor `seq` napędza pure ring). Czysta decyzja sesji/próbki
    `blackbox_sampler.*` (OFF→non-OFF=START, non-OFF→non-OFF=SAMPLE, non-OFF→OFF=CLOSE). Recorder
    `blackbox_recorder.c` — task tła prio 2, stack 3 KB, tick ~500 ms (2 Hz, wzorzec gps_reader),
    peek `control_loop_get_snapshot()`. Naprawa P2: nowy czysty `blackbox_resume.*` (streaming
    fold), `blackbox_init` skanuje region i zasiewa `s_seq`/`session_seq` = max+1.
- **Faza 3 — Konsola, kalibracja, dokumentacja (Unit 4–6):**
  - Unit 4: nowy komponent `usb_console` (`esp_console` REPL na USB Serial/JTAG); `spotlog dump`
    = `blackbox_read_all` → dekod (rdzeń) → CSV przez czystą `blackbox_csv.*` (nagłówek == kolejność
    kolumn = kontrakt parsera, 22 płaskie kolumny).
  - Unit 5: `params get`/`params set` — czysta `params_cmd.*` (parsowanie/walidacja, mapowanie na
    `spot_lock_*`) komponuje ten SAM `settings_validate` co HTTP; HAL woła `control_loop_post_pending`
    (SI-6: apply tylko w DISARMED, single-writer, out-of-range odrzucone).
  - Unit 6: `docs/blackbox-calibration.md` (pętla dump→analiza→set→reflash off, kolumny CSV →
    metryki polowania/przeregulowania/donuta → kierunek zmiany, priorytet stabilności R8);
    known-issues §4c; README sekcja „Blackbox spot-lock".

## Podjęte kluczowe decyzje

| Decyzja | Wybór |
|---|---|
| Surowy ring po `esp_partition` (nie SPIFFS/FAT) | deterministyczny, bez narzutu FS; sektorowy erase 4 KB z wyprzedzeniem; rekord z monotonicznym wrap-safe `seq` do uporządkowania przy odczycie |
| Partycja `spotlog` na końcu `partitions.csv` | offsety nvs/appcfg/factory bez zmian; kalibracja przeżywa reflash |
| Recorder = task tła prio 2 (wzorzec GPS), peek 2 Hz | nigdy w pętli 50 Hz; utrata próbki akceptowalna, jitter pętli nie; blackbox = obserwator poza failsafe |
| Recorder w komponencie, nie inline w app_main | `blackbox_recorder_start()` (wzorzec gps_start: opcjonalny, log-and-continue); app_main cienki (SRP) |
| Primary console = USB Serial/JTAG | warunek dostępności `esp_console_new_repl_usb_serial_jtag`; port operatora (ten sam co flash/monitor); logi i REPL dzielą port, dump = surowy printf filtrowalny po schemacie CSV |
| `params set` bez duplikacji progów | `params_cmd_decide_set` komponuje `settings_validate` (jedno źródło prawdy); out-of-range → walidator naprawia do defaultu i zwraca `!valid` → odrzucenie (moc wyroczni) |
| `params get` bez nowego gettera | istniejący `control_loop_get_active_params` wystarcza; druk przez czystą `params_cmd_format_get` |
| Zdenormalizowany płaski CSV | każdy wiersz niesie `session_id` + aktywne nastawy sesji; trywialny do parsowania/grupowania offline |
| Firmware „głupi" | brak logiki strojenia on-device; Claude liczy nastawy offline |
| P2 z review Fazy 2 | `blackbox_init` skanuje region i zasiewa `s_seq`/`session_seq` = max+1 (czysta `blackbox_resume_decide`) → globalnie unikatowe id po reboocie |

## Główne utworzone/zmodyfikowane pliki

Utworzone (komponent `blackbox`):
- `components/blackbox/include/blackbox_region.h` — geometria (named constants, pure)
- `components/blackbox/{include,src}/blackbox_record.{h,c}` — kodek próbki/nagłówka (pure)
- `components/blackbox/{include,src}/blackbox_ring.{h,c}` — wrap-safe indeks ringu (pure)
- `components/blackbox/{include,src}/blackbox_sampler.{h,c}` — decyzja sesji/próbki (pure)
- `components/blackbox/{include,src}/blackbox_resume.{h,c}` — seed cursora po reboocie (pure)
- `components/blackbox/{include,src}/blackbox_csv.{h,c}` — dekod rekord → wiersz CSV (pure)
- `components/blackbox/{include,src}/params_cmd.{h,c}` — parsowanie/walidacja args (pure)
- `components/blackbox/{include,src}/blackbox.{h,c}` — HAL `esp_partition` (init/erase/append/read-all)
- `components/blackbox/src/blackbox_recorder.c` — task tła prio 2
- `components/blackbox/CMakeLists.txt`

Utworzone (komponent `usb_console`):
- `components/usb_console/{include,src}/usb_console.{h,c}` — REPL + rejestracja komend
- `components/usb_console/CMakeLists.txt`

Utworzone (testy hosta):
- `test/host/test_blackbox_record.c`, `test_blackbox_ring.c`, `test_blackbox_sampler.c`,
  `test_blackbox_resume.c`, `test_blackbox_csv.c`, `test_params_cmd.c`

Utworzone (dokumentacja):
- `docs/blackbox-calibration.md`

Modyfikacje:
- `partitions.csv` — partycja `spotlog` na końcu
- `main/app_main.c` — start recordera + init konsoli
- `test/host/CMakeLists.txt`, `test/host/test_main.c` — rejestracja nowych testów
- `sdkconfig.defaults` — `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`
- `docs/completed/kayak-motor-firmware-v1/known-issues.md` — luki HW/E2E §4b/§4c
- README/pinout — sekcja „Blackbox spot-lock"

## Wyciągnięte wnioski

1. **Pure ⊥ HAL utrzymane przez cały komponent** — kodek, ring index, decyzja sesji/próbki, seed
   cursora, formatowanie CSV, parsowanie argumentów = czyste i host-testowane (bez `esp_*`/`driver/*`);
   `esp_partition`/`esp_console`/task = cienkie adaptery. Cała nietrywialna logika blackboxa
   weryfikowalna na hoście bez sprzętu.
2. **Cursor w RAM ≠ persystencja porządku** (finding P2 Fazy 2) — zerowanie `s_seq`/`session_seq`
   na init dawało kolizję `session_id` + przeplot dwóch wypłynięć po reboocie bez dumpu. Fix:
   skan regionu i zasianie cursora z najwyższego `session_seq` +1 — globalnie unikatowe id i
   porządkowanie po zapisanym seq. Test wznowienia ma moc wyroczni (mutacja `decide→{0,0}` = 5 FAIL).
3. **Kontrakt CSV = full-string oracle nagłówek==kolumny** — test porównujący cały string nagłówka
   z kolejnością emisji kolumn chroni parser Claude przed cichym dryfem schematu; 22 płaskie
   kolumny, każdy wiersz samowystarczalny (session_id + cel + 4 nastawy).
4. **Reużycie walidatora zamiast duplikacji tabeli progów** — `settings_ranges.h` jest prywatne
   w `settings/src`; kompozycja `settings_validate` (a nie kopia zakresów) eliminuje ryzyko dryfu.
   Semantyka „naprawia do defaultu + `!valid`" daje wprost sygnał odrzucenia dla `params set`.
5. **Blackbox jako obserwator poza torem RT** — cały I/O flash (append/erase/read-all) w tasku
   prio 2 co 2 Hz; snapshot czytany best-effort; błąd flash = log; start opcjonalny. Zero wpływu
   na pętlę 50 Hz i failsafe (analogia do gps/imu).
6. **Primary console na USB Serial/JTAG to warunek API, nie wybór stylu** —
   `esp_console_new_repl_usb_serial_jtag` istnieje tylko pod `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`,
   nie SECONDARY. Współdzielenie portu z ESP_LOG akceptowalne: dump = surowy printf, log wpada
   MIĘDZY wiersze (zapisy VFS liniowo-atomowe), parser filtruje po schemacie CSV.

## Powiązane dokumenty

- Plan: `spot-lock-blackbox-plan.md`
- Kontekst: `spot-lock-blackbox-kontekst.md`
- Zadania (pełna lista z findingami review): `spot-lock-blackbox-zadania.md`
- Raporty review faz 1–3: `review-faza-{1,2,3}.md`
- Procedura kalibracji: `docs/blackbox-calibration.md`
- Known issues (odroczone `[HW]`/`[E2E]`): `docs/completed/kayak-motor-firmware-v1/known-issues.md` §4b/§4c
- Requirements: `docs/dev-brainstorms/2026-06-29-spot-lock-blackbox-requirements.md`
- Plan techniczny: `docs/plans/2026-06-29-002-feat-spot-lock-blackbox-logging-plan.md`

## Commity feature

Faza 1: `08af88a`, `9c7e7eb`, `fd35386`
Faza 2: `6f6cb62`, `746b227` (naprawa P2 — resume/seed cursora)
Faza 3: `ed615ff`, `312685d`, `bc38aa9`
