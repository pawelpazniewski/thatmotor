# Review Fazy 3 — Konsola USB + spotlog dump/CSV + params get/set (Unit 4–6)

**Data:** 2026-07-01
**Branch:** `feature/spot-lock-blackbox`
**Commity:** ed615ff (Unit 4), 312685d (Unit 5), bc38aa9 (Unit 6)
**Zakres:** `usb_console.*`, `blackbox_csv.*`, `params_cmd.*`, `app_main.c`, `sdkconfig.defaults`,
`CMakeLists` (usb_console/blackbox/main/test), testy host `test_blackbox_csv.c` / `test_params_cmd.c`,
docs (`blackbox-calibration.md`, known-issues §4c, README).

---

## Severity gate: ✅ CZYSTE — GOTOWE DO KONTYNUACJI

- **P1 (blocking): 0**
- **P2 (important): 0**
- **P3 (nit): 4**

Typy findingów per severity: P3 = 3× KOD + 1× E2E (weryfikacja sprzętowa). Brak TEST-findingów
(pokrycie kompletne, testy mają moc wyroczni). Brak blokerów kontynuacji.

## Weryfikacja empiryczna

- `test/host/run.sh`: **391 Tests, 0 Failures, 0 Ignored — OK** (zgodne z deklaracją).
- `idf.py build` (esp32s3): **zielony**, bin `0xf0f70`, `0x8f090` (**37%**) wolne w partycji app
  (zgodne z deklaracją „37% free").
- `settings_valid = !repaired` w `settings_validate.c:238` — **potwierdza moc wyroczni** testu
  out-of-range: wartość poza zakresem → walidator „naprawia" pole → `settings_valid=false` →
  `params_cmd_decide_set` zwraca `PARAMS_CMD_SET_ERR_OUT_OF_RANGE`, HAL nie stage'uje. Test
  `test_set_out_of_range_rejected_no_stage` FAILuje, gdyby gate pominąć.

---

## Werdykt priorytetów review

### 1. Odchylenie — primary console na USB Serial/JTAG → **P3 (akceptowalne z uwagą)**

`sdkconfig.defaults: CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (wcześniej UART0 primary). Ocena wpływu:

- **Flash/monitor:** USB Serial/JTAG na ESP32-S3 to osobny peryferyk wspierany przez esptool —
  flash i monitor działają po tym samym porcie (`/dev/ttyACM*`), który wpina operator. Bez regresji.
- **Bootlog:** log 2. stopnia bootloadera i ROM idą na skonfigurowaną konsolę; operator widzi je na
  USB-C. OK.
- **Interleaving ESP_LOG ↔ REPL:** dzielą port (standardowy wzorzec esp_console). `spotlog dump` = surowy
  `printf("%s\n", row)` bez prefiksu; parser offline filtruje po schemacie CSV (22 kolumny, wiodący
  numeryczny `session_id`). Zapisy przez VFS USB-Serial/JTAG są liniowo-atomowe pod lockiem, więc log
  wpada MIĘDZY wiersze CSV (nie w środek wiersza) — filtrowalny. Dodatkowo `written_or_zero`/`blackbox_csv_row`
  zwraca 0 przy truncacji (brak częściowego, nieparsowalnego wiersza).
- **Klasyfikacja:** **P3**, nie P2 — realnie nie psuje parsowalności (schema-filter) ani flashowania
  (USB-Serial/JTAG wspiera esptool). Decyzja świadomie udokumentowana (komentarz w `sdkconfig.defaults`,
  known-issues §4c, kontekst). Residual (hałaśliwy dump przy gęstym logowaniu; parsowalność „na żywo")
  = **weryfikacja sprzętowa → known-issues** (poprawnie odłożone).

### 2. SI-6 / zapis nastaw silnika → **RESPEKTOWANE ✅**

- `params_cmd_decide_set` **nie obchodzi SI-6**: komponuje **ten sam** `settings_validate` co HTTP
  params API (`params_cmd.c:108`), zamiast własnej tabeli zakresów (`settings_ranges.h` prywatne —
  brak dryfu). Jedno źródło prawdy dla zakresów.
- HAL (`usb_console.c:params_set`) na ACCEPT woła `control_loop_post_pending` →
  `maybe_apply_pending` (`control_loop.c:267`) **aplikuje tylko w DISARMED** z re-checkiem TOCTOU
  (`loop_should_apply_pending`) i konsumpcją z kolejki dopiero po zobowiązaniu — **single-writer**
  (pętla jest jedynym pisarzem `s_params`).
- **ARMED → staged:** post_pending wpisuje do mailbox (`xQueueOverwrite`), apply czeka do DISARMED;
  zmiana nigdy nie gubiona (tylko odroczona). `params_cmd_apply_when` raportuje applied/staged
  operatorowi (tylko RAPORT, nie bramka).
- **Odrzucenie poza zakresem:** działa z mocą wyroczni (potwierdzone `settings_valid=!repaired`).
- **Zwraca zastosowane:** echo `field=value applied/staged` (pełny stan przez `params get`).

### 3. Kontrakt CSV → **JEDNOZNACZNY ✅**

- Nagłówek `CSV_HEADER` (`blackbox_csv.c:8`) == kolejność kolumn w `blackbox_csv_row` — **full-string
  oracle** w `test_header_matches_field_order` + `test_row_exact_column_order_and_values` (wartości
  wszystkich pól różne → zła kolejność ujawnia się jako mismatch, nie przypadkowy pass).
- Każdy wiersz: `session_id` + cel (`target_lat/lon_e7`) + 4 aktywne nastawy sesji + telemetria =
  **22 kolumny, zdenormalizowany płaski** (test `test_session_rows_share_id_and_settings`,
  `test_header_column_count_matches_row_commas`). Parser offline grupuje bez joinu.
- Wszystkie pola całkowite (bez przecinków/cudzysłowów w danych) → brak potrzeby escapowania CSV.

### 4. Obserwator → **POZA TOREM FAILSAFE ✅**

- `usb_console_start` opcjonalny: błąd startu = `ESP_LOGW` + continue (`app_main.c`), nie przerywa
  bootu; REPL prio 2 (nie preemptuje pętli 50 Hz).
- `spotlog dump` = read-only; `params set` tylko stage'uje przez SI-6 (apply gated DISARMED).
- Konsola nie zasila `rc_valid`/`sm_inputs`, nie blokuje `sm_step`.

### 5. Pure ⊥ HAL → **CZYSTE ✅**

- `blackbox_csv.h/.c`, `params_cmd.h/.c`: **zero** `#include esp_*/driver/*` (grep czysty). HAL
  (`usb_console.c`) cienki — deleguje do czystego rdzenia. Brak pułapki `*/` (wszystkie `*/` to
  legalne domknięcia bloków komentarzy).

### 6. Reguły → **SPEŁNIONE ✅**

- Rozmiary: `usb_console.c` 214, `params_cmd.c` 139, `blackbox_csv.c` 54 — wszystkie **<300**.
  `blackbox_record.c` **302** (znany nit z Fazy 1) — **niepogorszony** (bez zmian w Fazie 3).
- Funkcje <50 linii; `app_main` cienki (tylko `usb_console_start()` + log-and-continue).
- Named constants: `USB_CONSOLE_TASK_PRIO/STACK`, `BLACKBOX_CSV_LINE_MAX/COLUMN_COUNT`,
  `PARAMS_CMD_GET_MAX`. Brak osłabiania asercji; każda nowa funkcja ma happy + error test.

---

## Findingi P3 (nity — nie blokują)

- 🟡 [nit] **sdkconfig.defaults / usb_console.c (KOD/E2E)** — primary console na USB Serial/JTAG:
  ESP_LOG dzieli port z REPL. Dump filtrowalny po schemacie CSV; parsowalność „na żywo" przy gęstym
  logowaniu = weryfikacja sprzętowa (known-issues §4c). Akceptowalne z uwagą. Ewentualnie: podnieść
  log level lub wyciszyć logi na czas dumpu (gold-plating — nie wymagane).
- 🟡 [nit] **usb_console.c:135-159 `params_set` (KOD)** — baza to `control_loop_get_active_params`
  + mailbox `xQueueOverwrite` (głębokość 1). Dwa kolejne `params set` na RÓŻNE pola **w ARMED**
  (obie staged, nie zaaplikowane) → drugi nadpisuje pending zbudowany z aktywnych params → zmiana
  pierwszego pola gubiona do rozbrojenia. W DISARMED bez problemu (każdy apply w ciągu jednego cyklu
  20 ms zanim padnie następna komenda). Zgodne z zachowaniem mailbox HTTP (nie regresja). Zalecenie:
  udokumentować „w ARMED ustawiaj jedno pole albo rozbrój między setami", lub set-then-get.
- 🟡 [nit] **blackbox.c read_all ↔ recorder append (KOD)** — carry z Fazy 2 (nit), teraz realnie
  wpięty: `spotlog dump` (task konsoli prio 2) czyta region równolegle do `append` recordera bez
  locka; możliwy przeplot. Mitygacja: dump w DISARMED/off-water (udokumentowane known-issues §4c).
- 🟡 [nit] **usb_console.c:107 `printf("%s", buf)` (KOD)** — `params get` drukuje bufor bez
  końcowego `\n` poza treścią (każda linia ma własny `\n` z formattera) — kosmetyczne, prompt REPL
  wraca w nowej linii i tak.

## Odchylenia od planu

Brak istotnych odchyleń. Wszystkie pliki i testy zdefiniowane w Unit 4–6 istnieją i zawierają
asercje (`blackbox_csv.*`, `params_cmd.*`, `usb_console.*`, `test_blackbox_csv.c`,
`test_params_cmd.c`, `docs/blackbox-calibration.md`, known-issues §4c, README). Odchylenie
świadome i zgodne z planem: brak własnej tabeli zakresów w `params_cmd` (kompozycja
`settings_validate`) — właściwa decyzja (jedno źródło prawdy). E2E na wodzie (dump→CSV→set→zachowanie)
poprawnie odłożone do known-issues (sprzętowe).

## E2E / weryfikacja sprzętowa

Faza firmware ESP-IDF (C), brak UI przeglądarkowego — Agent 5 (browser) nie dotyczy. Host-weryfikowalne
`Weryfikacja:` **potwierdzone zielone** (`run.sh` 391/391, `idf.py build`). Sprzętowe (realny dump po
USB, parsowalność na żywo, jitter 50 Hz przy erase, `params set` na wodzie, koegzystencja REPL↔log)
odłożone do known-issues §4c — architektura je gwarantuje (obserwator poza torem RT, apply gated SI-6),
lecz wymagają fizycznej weryfikacji.
