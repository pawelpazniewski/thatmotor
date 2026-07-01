# Kontekst: Spot-lock blackbox

**Branch:** `feature/spot-lock-blackbox`
**Ostatnia aktualizacja:** 2026-07-01 (Faza 3 / Unit 4–6)

## Postęp

- **Faza 3 ukończona (Unit 4 konsola+dump, Unit 5 params, Unit 6 docs).** Nowy komponent
  `usb_console` (`esp_console` REPL na USB Serial/JTAG): komendy `spotlog dump`, `params get`,
  `params set`. `spotlog dump` = `blackbox_read_all` → klasyfikacja/dekod (rdzeń) → zdenormalizowany
  płaski CSV; formatowanie = czysta `blackbox_csv.*` (nagłówek == kolejność kolumn = kontrakt
  parsera, host-testowana, +5 testów). `params set` = czysta `params_cmd.*`: parsowanie/walidacja
  argumentów + mapowanie na `spot_lock_*`, zakres egzekwowany przez ten SAM `settings_validate` co
  HTTP (jedno źródło prawdy), HAL woła `control_loop_post_pending` (SI-6, apply tylko w DISARMED);
  `params_cmd_apply_when` raportuje applied/staged (+7 testów). **391 host-testów zielone**;
  `idf.py build` (esp32s3) zielony, 37% free.
- **Decyzja (koegzystencja ESP_LOG/REPL):** primary console przełączony na **USB Serial/JTAG**
  (`sdkconfig.defaults: CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`) — to warunek dostępności
  `esp_console_new_repl_usb_serial_jtag` (funkcja jest pod `#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`,
  nie SECONDARY) i zarazem port, który operator wpina (lewy USB-C, `/dev/ttyACM*`, ten sam co
  flash/monitor). Logi i REPL dzielą port (standardowy wzorzec esp_console); dump = surowy `printf`
  bez prefiksu → parser filtruje po schemacie CSV. Alternatywa (UART0=logi, ręczny REPL na USB) —
  odrzucona: więcej kodu, sięga w internale, a port operatora to i tak USB Serial/JTAG.
- **Decyzja (params_cmd bez duplikacji progów):** `params_cmd_decide_set` komponuje
  `settings_validate` zamiast trzymać własną tabelę zakresów (`settings_ranges.h` jest prywatne w
  `settings/src`, nieeksportowane; duplikacja groziłaby dryfem). Wartość poza zakresem → walidator
  „naprawia" pole do defaultu i zwraca `!settings_valid` → `PARAMS_CMD_SET_ERR_OUT_OF_RANGE`, HAL nie
  stage'uje. Test out-of-range ma moc wyroczni (FAILuje gdyby pominąć gate).
- **Decyzja (`params get`):** istniejący `control_loop_get_active_params` wystarcza — nowy getter
  niepotrzebny; druk przez czystą `params_cmd_format_get` (host-testowana).
- **Unit 6 docs:** `docs/blackbox-calibration.md` (pętla dump→analiza→set→reflash off, kolumny CSV →
  metryki polowania/przeregulowania/donuta → kierunek zmiany, priorytet stabilności R8);
  known-issues §4c (luki HW/E2E: realny zapis na flash, jitter przy erase, parsowalność CSV na żywo,
  wpływ nastaw na wodzie, koegzystencja REPL↔log); README sekcja „Blackbox spot-lock".
- **Odłożone do known-issues (E2E/sprzętowe, NIE wykonane):** sesja na wodzie → dump → CSV z malejącym
  `err_m` → `params set` zmienia zachowanie (§4c). Weryfikacja: `run.sh`/`idf.py build` pozostają dla
  review.

- **Review Fazy 3 (2026-07-01): ✅ CZYSTE (P1=0, P2=0, P3=4).** Raport: `review-faza-3.md`.
  Zweryfikowane empirycznie: `run.sh` 391/391 zielone, `idf.py build` zielony (bin 0xf0f70, 37% free
  — zgodne z deklaracją). **Console-primary-switch (USB Serial/JTAG) = P3 akceptowalny z uwagą** —
  flash/monitor działają na USB-Serial/JTAG (esptool), dump = surowy printf filtrowalny po schemacie
  CSV, zapisy VFS liniowo-atomowe (log wpada MIĘDZY wiersze, nie w środek); parsowalność „na żywo" →
  known-issues §4c. **SI-6 respektowane** — `params_cmd_decide_set` komponuje ten SAM `settings_validate`
  co HTTP (`settings_valid=!repaired` potwierdza moc wyroczni out-of-range); `control_loop_post_pending`
  → `maybe_apply_pending` aplikuje tylko w DISARMED z TOCTOU re-checkiem, single-writer; ARMED→staged
  (nie gubione). **CSV jednoznaczny** — full-string oracle nagłówek==kolejność kolumn, 22 płaskie
  kolumny, każdy wiersz session_id+cel+4 nastawy. Obserwator ✅ (opcjonalny start, prio 2, poza
  failsafe). Pure ⊥ HAL ✅ (csv/params_cmd bez esp_/driver). Rozmiary ✅ (nowe <300, record.c 302
  niepogorszony). P3 nity: ESP_LOG↔REPL współdzielenie portu, mailbox depth-1 przy multi-set w ARMED,
  read_all↔append bez locka (dump off-water), `params get` brak trailing \n. Wolna droga do zamknięcia
  zadania (Faza 3 = ostatnia).

- **Faza 2 ukończona (Unit 3 — HAL flash + recorder task).** Komponent `blackbox`
  wchodzi teraz do buildu IDF: `components/blackbox/CMakeLists.txt` rejestruje
  wszystkie 5 plików .c (record, ring, sampler, blackbox HAL, recorder), REQUIRES
  esp_partition/spi_flash/control_loop/freertos/esp_timer. Czysta decyzja sesji/
  próbki w `blackbox_sampler.*` (OFF→non-OFF=START, non-OFF→non-OFF w tym
  ACTIVE→PAUSED=SAMPLE, non-OFF→OFF=CLOSE, OFF→OFF=IDLE) — host-testowana (6 testów).
  HAL `blackbox.*` (adapter esp_partition: init/erase/append/read-all, esp_err_t→
  `blackbox_status`, cursor `seq` napędza pure ring). Recorder task w
  `blackbox_recorder.c` (prio 2, stack 3 KB, tick ~500 ms = 2 Hz; wzorzec gps_reader).
  372 host-testów zielone (+6); `idf.py build` zielony z komponentem w buildzie
  (bin 0xECAD0, 38% wolne w partycji app).
- **Decyzja (odchylenie od dosłownego brzmienia zadania):** task recordera NIE jest
  inline w `app_main.c` — jest w komponencie (`blackbox_recorder_start()`), a app_main
  tylko go woła (dokładny wzorzec gps_start/imu_start: opcjonalny, log-and-continue,
  poza failsafe). Trzyma app_main cienki (SRP, reguła „plik komponentu nie zawiera
  logiki tła"). Kontrakt „start po control_loop_init, prio 2, 2 Hz" zachowany.
- **Decyzja (cursor seq):** `blackbox_init` zeruje cursor `seq` w RAM — po reboocie
  ring startuje od slotu 0. Dane sprzed reboota są czytelne przez read-all dopóki nie
  nadpisane. Wystarczające dla strojenia polowego (dump po każdym wypłynięciu). Odczyt
  z wznowieniem cursora (skan regionu) odłożony — nie w scope Unit 3.

- **Review Fazy 2 (2026-07-01): ⚠️ ZASTRZEŻENIA (P1=0, P2=1, P3=4).** Raport:
  `review-faza-2.md`. Zweryfikowane empirycznie: `run.sh` 372/372 zielone, `idf.py build`
  zielony (bin 0xECAD0, 38% free — zgodne z deklaracją). Obserwator/zero-wpływu ✅ (tylko
  `get_snapshot`/`get_active_params` best-effort, cały I/O flash w tasku prio 2 poza pętlą
  50 Hz, błąd flash = log, start opcjonalny). HAL cienki ✅ (deleguje do `blackbox_ring`/
  `blackbox_record`, zero duplikacji, `map_err` poprawny). Sampler ✅ z realną mocą wyroczni
  (naiwne „record gdy non-OFF" FAILuje `off_to_active`). Pure ⊥ HAL ✅ (esp_ tylko w
  komentarzach). Rozmiary/SRP ✅ (app_main cienki, task w komponencie). **P2:** cursor
  `s_seq`/`session_seq` zerowane w RAM na init → reboot/brownout MIĘDZY wypłynięciami bez
  dumpu = kolizja `session_id` + przeplot danych sprzed/po reboocie przy dumpie (`read_all`
  fizycznie rosnąco, obchodzi `oldest_seq`). Happy-path (dump w tym samym power-cycle) OK;
  domknąć w Unit 4 (skan+seed seq lub per-record ring-seq + porządkowanie dumpu) +
  known-issues. Nie-blokujące (świadomie odłożone).
- **Faza 1 ukończona (Unit 1 + Unit 2).** Partycja `spotlog` (0x196000, 1 MiB,
  subtype 0x40) dopisana na końcu `partitions.csv` — offsety nvs/phy_init/factory/
  appcfg niezmienione (partition-table potwierdza). Geometria jako named constants
  w `blackbox_region.h` (sektor 4 KB, rekord 64 B, pojemność 16384). Czysty rdzeń:
  `blackbox_record.*` (kodek próbki/nagłówka, magic+schema+CRC32, taksonomia
  empty/magic/crc/schema/type) i `blackbox_ring.*` (seq→slot modulo, offset, decyzja
  erase, wrap-safe `seq_after` signed-modular, `oldest_seq`). 23 nowe host-testy
  (366 łącznie, zielone); `idf.py build` zielony. Rekord 64 B dzieli 4 KB sektor
  równo (64/sektor) — rekord nie przechodzi przez granicę sektora.
- **Uwaga wdrożeniowa:** `components/blackbox/` nie ma jeszcze `CMakeLists.txt`, więc
  IDF ignoruje katalog (pliki .c walidowane tylko host-testami w Fazie 1). Rejestracja
  komponentu do buildu IDF następuje w Unit 3.
- **Review Fazy 1 (2026-07-01): ✅ CZYSTE.** P1=0, P2=0, P3=1. Raport:
  `review-faza-1.md`. Wrap-safety potwierdzona empirycznie (3/3 naiwne impl. łamią
  asercje → realna moc wyroczni). Integralność kodeka OK (magic 0xB10C ⊥ 0xFF/0x0000,
  CRC32 known-answer 0xCBF43926, taksonomia empty/magic/crc/schema/type kompletna,
  round-trip field-by-field, brak straddle 64|4096). Offsety partycji potwierdzone
  `gen_esp32part.py` (nvs/phy_init/factory/appcfg niezmienione, spotlog @0x196000).
  Pure ⊥ HAL OK (esp_ tylko w komentarzach). Jedyny nit: `blackbox_record.c` = 302
  linie (2 ponad próg), kohezyjny — do rozważenia przy dodaniu typów rekordu w Fazie 2.

## Źródła
- Requirements doc: `docs/dev-brainstorms/2026-06-29-spot-lock-blackbox-requirements.md`
- Plan techniczny: `docs/plans/2026-06-29-002-feat-spot-lock-blackbox-logging-plan.md`

---

## Powiązane pliki

### Reużywane (czytane / wzorce)
- `components/control_loop/src/control_loop.c` — snapshot 50 Hz (`publish_snapshot`,
  `control_loop_get_snapshot`), pending/apply (`s_pending_queue`, `maybe_apply_pending`
  → DISARMED, `maybe_commit_params` → `nvs_store_commit`), `control_loop_post_pending`.
- `components/control_loop/include/loop_step.h` — `CONTROL_LOOP_PERIOD_MS=20`.
- `components/control_loop/include/control_loop.h` — pola snapshotu (spot_lock_state/
  err_m/bearing_deg10, GPS, heading, servo_us/esc_us, ch1/ch2_us).
- `components/settings/include/settings_model.h` — `spot_lock_deadband_m`,
  `spot_lock_max_throttle_pct`, `spot_lock_throttle_gain`, `spot_lock_servo_gain`
  (u16), `schema_version` v6.
- `components/settings/include/settings_validate.h` — walidacja field-by-field.
- `components/settings/include/nvs_store.h` — `nvs_store_commit`, stałe partycji appcfg.
- `components/settings/src/blob_codec.c` — wersjonowany blob + CRC32 (wzorzec kodeka).
- `components/settings/src/nvs_store.c` — `resolve_provenance` (empty/corrupt/schema/valid).
- `components/web_panel/src/params_api.c` — `params_api_handle_post` (validate→stage).
- `components/web_panel/src/params_decide.c` — `params_decide_write` (409/400/200).
- `components/web_panel/src/command_parse.c` — exact-match parser komend.
- `components/gps/src/gps_reader.c` — wzorzec taska tła (prio 2, stack ~3 KB).
- `partitions.csv` — layout flash (nvs/phy_init/factory/appcfg).
- `test/host/CMakeLists.txt`, `test/host/test_main.c` — rejestracja host-testów.

### Nowe komponenty / pliki
- `components/blackbox/` — `blackbox_region.h` (geometria), `blackbox_record.*`
  (kodek), `blackbox_ring.*` (indeks), `blackbox.*` (HAL esp_partition),
  `blackbox_sampler.*` (decyzja sesji/próbki, czysta), `blackbox_csv.*` (wiersz CSV),
  `params_cmd.*` (parsowanie/walidacja argumentów), `CMakeLists.txt`.
- `components/usb_console/` — `usb_console.*` (REPL + rejestracja komend), `CMakeLists.txt`.
- `main/app_main.c` — start taska recordera + init konsoli.
- `test/host/test_blackbox_record.c`, `test_blackbox_ring.c`, `test_blackbox_sampler.c`,
  `test_blackbox_csv.c`, `test_params_cmd.c`.
- `docs/blackbox-calibration.md`, `docs/completed/kayak-motor-firmware-v1/known-issues.md`,
  README/pinout.

## Decyzje techniczne

- **Surowy ring po `esp_partition` (nie SPIFFS/FAT)** — deterministyczny, bez narzutu
  FS; sektorowy erase 4 KB z wyprzedzeniem; każdy rekord z monotonicznym `seq`
  (wrap-safe) do uporządkowania przy odczycie.
- **Partycja `spotlog` dopisana NA KOŃCU `partitions.csv`** (po appcfg) — offsety
  nvs/appcfg/factory bez zmian, kalibracja przeżywa reflash. Start 1 MB.
- **Recorder = task tła prio 2 (wzorzec GPS), peek snapshotu co ~2 Hz** — nigdy w
  pętli 50 Hz; utrata próbki akceptowalna, jitter pętli nie.
- **Minimalny `esp_console` REPL na USB Serial/JTAG** — frameworku nie ma; rejestruje
  `spotlog dump`, `params get`, `params set`.
- **`params set` reużywa `settings_validate` + `control_loop_post_pending`** → SI-6
  (DISARMED-only apply, walidacja, single-writer) za darmo.
- **Zdenormalizowany płaski CSV** — każdy wiersz niesie `session_id` + aktywne nastawy
  sesji; trywialny do parsowania/grupowania offline.
- **Firmware „głupi"** — brak logiki strojenia on-device; Claude liczy nastawy offline.
- **Pure ⊥ HAL** — kodek, ring index, decyzja sesji/próbki, formatowanie CSV, parsowanie
  argumentów = czyste i host-testowane; `esp_partition`/`esp_console`/task = cienkie adaptery.
- **Blackbox to obserwator** — nagrywanie/dump/zapis NVS poza ścieżką failsafe; nie
  zasila `rc_valid`/`sm_inputs`, nie blokuje `sm_step`.

## Zależności

- Branch odgałęziony od `feature/spot-lock-position-hold` (ukończona; pola `spot_lock_*`
  i telemetria spot-locka już istnieją, schema v6).
- ESP-IDF v5.x (`esp_console`, `esp_partition` — standardowe API).
- Walidacja: `test/host/run.sh` (Unity) + `idf.py build` (esp32s3 N16R8).
- Diagnostyki LSP `-mlongcalls`/`-fno-shrink-wrap`/`unknown type name` = szum toolchainu
  xtensa, NIE findingi — liczy się `run.sh` i `idf.py build`.

## Odroczone do implementacji
- Dokładny układ bajtów rekordu/nagłówka, stringi CSV, finalna lista kolumn.
- Koegzystencja `ESP_LOG` i REPL na USB Serial/JTAG (ewentualnie logi na UART0).
- Finalny offset/rozmiar partycji `spotlog` (po pomiarze wolnego miejsca).
- Progi/heurystyki strojenia z CSV (procedura Claude, dostrajana w terenie — Unit 6).

## Luka wiedzy (do `/dev-compound` po wdrożeniu)
- Brak udokumentowanego wzorca „flush na flash bez jittera pętli RT 50 Hz" —
  udokumentować po implementacji Unit 3.

## Re-review Fazy 2 (cykl 1) — 2026-07-01
- P2 z cyklu 0 (zerowanie cursora/session_seq na init → kolizja session_id + przeplot
  po reboocie) **rozwiązany** commitem 746b227: nowy czysty moduł `blackbox_resume.{h,c}`
  (Pure ⊥ HAL, streaming fold), `blackbox_init` skanuje region i zasiewa `s_seq`/`session_seq`
  z `blackbox_resume_decide`; recorder kontynuuje id = max+1.
- Moc wyroczni testu wznowienia potwierdzona **empirycznie**: mutacja `decide→{0,0}` daje
  5 FAIL, po przywróceniu 379/379 zielone. `idf.py build` zielony.
- Gate re-review: **CZYSTE** (0×P1, 0×P2, 0 nowych P3). Residual po zawinięciu ringu uczciwie
  w known-issues §4b. Wolna droga do Fazy 3 (Unit 4 dump/CSV).
