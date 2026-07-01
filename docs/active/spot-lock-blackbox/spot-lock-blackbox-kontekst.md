# Kontekst: Spot-lock blackbox

**Branch:** `feature/spot-lock-blackbox`
**Ostatnia aktualizacja:** 2026-07-01

## Postęp

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
