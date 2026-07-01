---
title: "feat: Spot-lock blackbox — nagrywanie sesji na flash + auto-kalibracja przez USB"
type: feat
status: active
date: 2026-06-29
origin: docs/dev-brainstorms/2026-06-29-spot-lock-blackbox-requirements.md
---

# feat: Spot-lock blackbox — nagrywanie sesji na flash + auto-kalibracja przez USB

## Przegląd

Dodajemy „blackbox" dla spot-locka: ESP samo nagrywa na wodzie przebieg każdej
sesji utrzymywania pozycji do dedykowanej partycji flash (standalone, bez
łączności). Po powrocie i podłączeniu ESP do komputera (port COM) logi są
zrzucane przez konsolę USB jako CSV, Claude analizuje je offline i jedną komendą
zapisuje dostrojone nastawy regulatora do NVS. Cała inteligencja strojenia jest
po stronie Claude; firmware tylko nagrywa, zrzuca i stosuje przekazane wartości.

## Ujęcie problemu

Nastawy regulatora spot-locka (`spot_lock_deadband_m`,
`spot_lock_max_throttle_pct`, `spot_lock_throttle_gain`, `spot_lock_servo_gain`)
są dziś łagodnymi placeholderami strojonymi w terenie metodą prób i błędów.
Operator zza drążków nie widzi błędu pozycji, bearingu do celu ani komend
regulatora, więc nie potrafi zdiagnozować dryfu, oscylacji ani donutów.
Blackbox łapie ground truth w trakcie ACTIVE hold i przenosi strojenie z
subiektywnego zgadywania na analizę danych offline (zob. źródło:
docs/dev-brainstorms/2026-06-29-spot-lock-blackbox-requirements.md).

## Śledzenie wymagań

- R1. Nagrywanie standalone na wodzie do wewnętrznego flasha, bez łączności.
- R2. Trigger nagrywania = aktywny spot-lock (`spot_lock_state != OFF`).
- R3. Zawartość: znacznik startu sesji (cel + aktywne nastawy) + szereg czasowy
  zachowania (błąd, bearing vs heading, komendy serwa/ESC, jakość GPS, drążki).
- R4. Zero wpływu na pętlę 50 Hz i tor failsafe (bufor/odczyt poza torem sterowania).
- R5. Retencja pierścieniowa — nadpisywanie najstarszych sesji.
- R6. Odczyt przez konsolę USB/COM jako CSV (bez WiFi, bez MSC).
- R7. Auto-zapis nastaw przez komendę USB, w pełni automatyczny, z firmware (SI-6:
  DISARMED + walidacja + single-writer) jako siatką bezpieczeństwa; zwrot
  zastosowanych wartości.
- R8. Cel strojenia: stabilność (brak „polowania") > dopływ bez przeregulowania >
  brak donutów; energia poza scope.

## Granice scope'u

- Brak odczytu przez WiFi/panel ani USB Mass Storage — tylko konsola USB/COM.
- Brak kalibracji/strojenia na wodzie — cała kalibracja offline przy USB.
- Brak logowania trybów innych niż spot-lock (manual/DEPLOY/sam failsafe).
- Brak optymalizacji pod energię/baterię/hałas.
- Nie budujemy ogólnego rejestratora lotu — blackbox celowo wąski.
- Firmware nie liczy nastaw — heurystyki strojenia są po stronie Claude offline.

## Kontekst i research

### Relevantny kod i wzorce

- **Settings SI-6 (do recyklingu w R7):** `components/settings/include/settings_model.h`
  (pola `spot_lock_*`, u16, `schema_version` v6); pending mailbox i bramka apply w
  `components/control_loop/src/control_loop.c` (`s_pending_queue`,
  `maybe_apply_pending` → apply tylko `SM_STATE_DISARMED`, `maybe_commit_params` →
  `nvs_store_commit` + debounce). Funkcja wejścia: `control_loop_post_pending()`.
  Walidacja field-by-field: `components/settings/include/settings_validate.h`
  (używana w `components/web_panel/src/params_api.c::params_api_handle_post`).
- **Snapshot 50 Hz (do R2/R3/R4):** `components/control_loop/include/loop_step.h`
  (`CONTROL_LOOP_PERIOD_MS=20`); `control_loop_get_snapshot()` bezpieczny best-effort
  copy z innego taska; `publish_snapshot()` populuje pola spot-lock (substate,
  err_m, bearing_deg10) i GPS/IMU/komendy.
- **Wzorzec taska tła (do R4):** `components/gps/src/gps_reader.c` —
  `xTaskCreate(..., stack ~3072, prio 2, ...)`, niski priorytet, nie preemptuje
  pętli sterującej.
- **Persystencja blob (do R3 record + integralność):**
  `components/settings/src/blob_codec.c` (wersjonowany blob + CRC32),
  `nvs_store.c::resolve_provenance` (stany empty/corrupt/schema/valid),
  `params_decide_write` (decyzja 409/400/200), `command_parse` (exact-match parser).
- **Partycje:** `partitions.csv` (nvs 0x6000, phy_init, factory 0x180000, appcfg
  0x6000 @0x190000); flash 16 MB; brak użycia `esp_partition` w projekcie dotąd.
- **Host-test harness:** `test/host/CMakeLists.txt` (PURE_SOURCES + TEST_SOURCES),
  `test/host/test_main.c` (`run_*` rejestracja); precedensy `test_spot_lock.c`,
  `test_loop_step.c`.

### Wiedza instytucjonalna

- `docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md` —
  ring seq/slot przez unsigned modular subtraction w jednej domenie; host-test na
  granicy wrapu (2^32 i zawinięcie pojemności).
- `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md`
  — Pure ⊥ HAL: czysty rdzeń bez `esp_*`/`driver/*`, cienki HAL; audyt grepem
  pure-headerów; wprost wskazuje `blob_codec`/`resolve_provenance`/`params_decide_write`
  jako szablony.
- `docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md` —
  testy overflow/limitu wejściem POZA granicą (reguła: „czy FAILuje po usunięciu
  transformacji?").
- `docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md`
  — blackbox to obserwator: nagrywanie/flush/dump/zapis NVS poza ścieżką decyzyjną
  failsafe, nie zasila `rc_valid`/`sm_inputs`, nie blokuje `sm_step`.
- **Luka wiedzy:** brak udokumentowanego wzorca „flush na flash bez jittera pętli
  50 Hz" — do udokumentowania przez `/dev-compound` po wdrożeniu.

### Referencje zewnętrzne

- Nie zbierano — codebase ma silne lokalne wzorce (blob/NVS/task tła), a
  `esp_console` i `esp_partition` to standardowe API ESP-IDF v5.x używane wg docs
  na etapie implementacji. (Decyzja 1.2: pomijamy research zewnętrzny.)

## Kluczowe decyzje techniczne

- **Surowy ring po `esp_partition` (nie SPIFFS/FAT):** deterministyczny, bez
  narzutu FS, spójny z dyscypliną blob. Sektorowy erase 4 KB z wyprzedzeniem przed
  głowicą zapisu; każdy rekord niesie monotoniczny `seq` (wrap-safe) do
  uporządkowania przy odczycie.
- **Nowa partycja `spotlog` dopisana NA KOŃCU `partitions.csv`** (po appcfg), aby
  nie przesunąć offsetów nvs/appcfg/factory — istniejąca kalibracja w appcfg
  przeżywa reflash z nowym layoutem. Rozmiar startowy 1 MB (~5 h ciągłego ACTIVE
  przy ~2 Hz; regulowalny).
- **Recorder jako task tła prio 2 (wzorzec GPS), peek `control_loop_get_snapshot()`
  co ~2 Hz** — nigdy w pętli 50 Hz, utrata próbki akceptowalna, jitter pętli nie.
- **Minimalny `esp_console` REPL na USB Serial/JTAG** (frameworku komend nie ma) —
  rejestruje `spotlog dump`, `params get`, `params set`.
- **Komenda zapisu reużywa `settings_validate` + `control_loop_post_pending`** →
  SI-6 (DISARMED-only apply, walidacja, single-writer) wymuszone „za darmo".
- **Zdenormalizowany płaski CSV** — każdy wiersz niesie `session_id` + aktywne
  nastawy tej sesji; trywialny do parsowania i grupowania offline przez Claude.
- **Firmware „głupi":** brak logiki strojenia on-device; Claude liczy nastawy
  offline i wysyła gotowe wartości.
- **Czysty rdzeń ⊥ HAL:** kodek rekordu, indeks ringu, decyzja „nowa sesja/próbka",
  formatowanie wiersza CSV — czyste i host-testowane; `esp_partition`/`esp_console`/
  task — cienkie adaptery.

## Otwarte pytania

### Rozwiązane podczas planowania

- Czy istnieje framework komend konsolowych? → Nie; budujemy minimalny `esp_console`
  REPL na USB Serial/JTAG.
- Medium logów? → Surowa partycja `spotlog` + `esp_partition` ring (nie FS).
- Czy dodanie partycji skasuje kalibrację? → Nie, jeśli dopisana na końcu (offsety
  nvs/appcfg/factory bez zmian).
- Kanał odczytu i zapisu nastaw? → Konsola USB/COM; zapis przez istniejący tor SI-6.
- Częstotliwość? → ~2 Hz (decymacja z 50 Hz), named constant.

### Odroczone do implementacji

- Dokładny układ bajtów rekordu i nagłówka sesji (kolejność pól, padding, rozmiar)
  — ustalić przy pisaniu kodeka; plan ustala zbiór pól, nie layout.
- Dokładne stringi formatujące CSV i finalna lista kolumn — przy implementacji dumpu.
- Współistnienie `ESP_LOG` i REPL na USB Serial/JTAG (ewentualne przekierowanie
  logów na UART0, REPL na USB) — rozstrzygnąć przy wpięciu konsoli na sprzęcie.
- Finalny offset/rozmiar partycji `spotlog` (start 1 MB) — potwierdzić po pomiarze
  wolnego miejsca i realnej długości sesji.
- Progi/heurystyki strojenia z CSV (wykrycie polowania/przeregulowania/donuta) —
  procedura po stronie Claude, dostrajana w terenie; nie kod firmware (Unit 6).

## Implementation Units

- [ ] **Unit 1: Partycja `spotlog` + stałe geometrii regionu**

**Cel:** Zarezerwować na flashu dedykowany region logów i opisać jego geometrię
(offset/rozmiar/sektor/pojemność rekordów) jako named constants, bez ruszania
istniejących partycji.

**Wymagania:** R1, R5

**Zależności:** Brak.

**Pliki:**
- Modyfikuj: `partitions.csv` (dopisz na końcu `spotlog, data, <subtype>, , 1M`)
- Stwórz: `components/blackbox/include/blackbox_region.h` (czysty: stałe geometrii —
  rozmiar sektora 4 KB, rozmiar rekordu, pojemność w rekordach, nazwa partycji)
- Test (unit): n/d (sam nagłówek stałych; pokrycie geometrii w Unit 3)

**Podejście:**
- Partycja typu `data` z własnym subtype; dopisana po `appcfg`, więc offsety
  nvs/appcfg/factory bez zmian (kalibracja przeżywa reflash).
- `blackbox_region.h` czysty (bez `esp_*`) — używany i przez rdzeń pure, i przez HAL.

**Wzorce do naśladowania:**
- `partitions.csv` (komentarze opisujące każdą partycję), `nvs_store.h` (stałe
  `NVS_STORE_PARTITION/NAMESPACE/KEY`).

**Scenariusze testowe:**
- [Unit] (w Unit 3) pojemność = rozmiar_regionu / rozmiar_rekordu spójna ze stałymi.

**Weryfikacja:**
- `idf.py build` zielony; `idf.py partition-table` pokazuje `spotlog`; offsety
  nvs/appcfg/factory niezmienione względem poprzedniego layoutu.

---

- [ ] **Unit 2: Czysty rdzeń blackbox — kodek rekordu + indeks ringu (wrap-safe)**

**Cel:** Host-testowana logika serializacji rekordu próbki i nagłówka sesji oraz
wrap-safe mapowania `seq → slot/sektor` z decyzją o nadpisaniu najstarszego.

**Wymagania:** R3, R5

**Zależności:** Unit 1 (stałe geometrii).

**Pliki:**
- Stwórz: `components/blackbox/include/blackbox_record.h` + `src/blackbox_record.c`
  (encode/decode rekordu próbki i nagłówka sesji; magic/schema + CRC; bez `esp_*`)
- Stwórz: `components/blackbox/include/blackbox_ring.h` + `src/blackbox_ring.c`
  (z `seq`, pojemności → offset slotu, czy potrzebny erase sektora, kolejność
  odczytu; unsigned modular arithmetic)
- Test (unit): `test/host/test_blackbox_record.c`, `test/host/test_blackbox_ring.c`
- Modyfikuj: `test/host/CMakeLists.txt` (PURE_SOURCES + include), `test/host/test_main.c`

**Podejście:**
- Rekord próbki — pola ze snapshotu: `t_ms`, `substate`, `err_m`, `bearing_deg10`,
  `heading_deg10`, `servo_us`, `esc_us`, `ch1_us`, `ch2_us`, `lat_e7`, `lon_e7`,
  `sats`, `speed_cms`, flagi (`gps_fix`/`imu_ok`).
- Nagłówek sesji — `session_seq`, cel `target_lat_e7`/`target_lon_e7`, aktywne
  nastawy `deadband_m`/`max_throttle_pct`/`throttle_gain`/`servo_gain`, znacznik czasu.
- `seq` monotoniczny u32; `slot = seq % capacity`; erase sektora gdy głowica
  wchodzi w nowy sektor; przy odczycie rekordy sortowane po `seq` (wrap-safe).
- CRC/magic na rekord (i/lub nagłówek) by odróżnić ważne rekordy od pustego flasha
  (0xFF) i od nadpisanych — wzorzec z `resolve_provenance`.

**Notatka wykonawcza:** Implementuj test-first; dla overflow i wrapu podawaj
wejścia POZA granicą (oracle power — test musi FAILować po usunięciu wrap/overflow).

**Wzorce do naśladowania:**
- `components/settings/src/blob_codec.c` (wersjonowany blob + CRC), reguła
  wrap-safe recency, `nvs_store.c::resolve_provenance` (empty/corrupt/schema/valid).

**Scenariusze testowe:**
- [Unit] round-trip encode→decode rekordu i nagłówka: wszystkie pola wierne.
- [Unit] decode pustego sektora (0xFF) → rekord nieważny (nie mylony z danymi).
- [Unit] decode rekordu z zepsutym CRC → nieważny.
- [Unit] `slot` poprawny tuż przed i po zawinięciu pojemności.
- [Unit] granica wrapu `seq` u32 (`seq` tuż przed `UINT32_MAX`, kolejny po
  przewinięciu) → poprawne uporządkowanie; FAILuje przy naiwnym porównaniu.
- [Unit] overflow: zapis ponad pojemność nadpisuje najstarszy (wejście POZA
  pojemnością — FAILuje bez logiki nadpisania).

**Weryfikacja:**
- `test/host/run.sh` zielony z nowymi testami; `grep` braku `esp_*`/`driver/*` w
  `blackbox_record.h`/`blackbox_ring.h`.

---

- [ ] **Unit 3: Adapter flash (HAL) + recorder task tła**

**Cel:** Cienki adapter `esp_partition` (erase/write/read regionu) oraz task tła
prio 2, który co ~2 Hz peekuje snapshot, wykrywa start/koniec sesji i dopisuje
rekordy przez czysty rdzeń — nigdy nie dotykając pętli 50 Hz ani failsafe.

**Wymagania:** R1, R2, R3, R4, R5

**Zależności:** Unit 1, Unit 2.

**Pliki:**
- Stwórz: `components/blackbox/include/blackbox.h` + `src/blackbox.c` (HAL: mapuje
  `esp_err_t`→enum domenowy; erase sektora, append, read-all; init partycji)
- Stwórz: `components/blackbox/include/blackbox_sampler.h` + `src/blackbox_sampler.c`
  ALBO czysta funkcja w rdzeniu: decyzja „czy próbkować / czy nowa sesja" z
  `(prev_substate, cur_substate)` — czysta, host-testowana
- Stwórz: `components/blackbox/CMakeLists.txt` (REQUIRES `spi_flash`/`esp_partition`,
  `control_loop`, `freertos`, `esp_timer`)
- Modyfikuj: `main/app_main.c` (start taska recordera po `control_loop_init`,
  przed/obok bring-upu łączności)
- Test (unit): `test/host/test_blackbox_sampler.c` (decyzja sesji/próbki — czysta)
- Modyfikuj: `test/host/CMakeLists.txt`, `test/host/test_main.c`

**Podejście:**
- Task tła wzorowany na `gps_reader` (prio 2, stack ~3 KB, `vTaskDelay` ~500 ms).
- Każdy tick: `control_loop_get_snapshot()` (best-effort, nieblokujący); gdy
  `substate != OFF` — przy zboczu OFF→non-OFF zapisz nagłówek sesji (cel = bieżąca
  pozycja, nastawy = aktywne), potem rekordy próbek; przy non-OFF→OFF zamknij sesję.
- Bufor pojedynczego rekordu w RAM; write/erase na flash w tasku tła — pętla 50 Hz
  nieblokowana (R4). Erase sektora wykonywany tylko na granicy sektora.
- Decyzja „nowa sesja/próbkuj" wyekstrahowana jako czysta funkcja (Pure ⊥ HAL),
  HAL tylko wykonuje I/O.
- Obserwator: task czyta snapshot, NIE zapisuje wejść maszyny stanów ani failsafe.

**Notatka wykonawcza:** Czysta decyzja sesji/próbki test-first; HAL trzymaj cienki.

**Wzorce do naśladowania:**
- `components/gps/src/gps_reader.c` (task tła), `nvs_store.c` (init partycji,
  mapowanie `esp_err_t`→enum), reguła Pure ⊥ HAL.

**Scenariusze testowe:**
- [Unit] zbocze OFF→ACTIVE → decyzja „rozpocznij sesję" (emit nagłówka).
- [Unit] ACTIVE→ACTIVE → „próbkuj", bez nowego nagłówka.
- [Unit] ACTIVE→PAUSED → nadal „próbkuj" (ta sama sesja, R2 obejmuje PAUSED).
- [Unit] non-OFF→OFF → „zamknij sesję", brak dalszych próbek.
- [Unit] (geometria z Unit 1) pojemność spójna z rozmiarem regionu/rekordu.

**Weryfikacja:**
- `test/host/run.sh` zielony; `idf.py build` zielony; na sprzęcie task startuje,
  pętla 50 Hz bez regresji (brak wpływu na czas cyklu/telemetrię — log w
  known-issues, weryfikacja sprzętowa).

---

- [ ] **Unit 4: Konsola USB (esp_console REPL) + komenda `spotlog dump` (CSV)**

**Cel:** Postawić minimalny REPL na USB Serial/JTAG i komendę, która czyta region
logów, dekoduje rekordy i strumieniuje je jako zdenormalizowany CSV po porcie COM.

**Wymagania:** R6

**Zależności:** Unit 2 (dekoder), Unit 3 (read-all z HAL).

**Pliki:**
- Stwórz: `components/usb_console/include/usb_console.h` + `src/usb_console.c`
  (init `esp_console` REPL na USB Serial/JTAG, rejestracja komend)
- Stwórz: `components/blackbox/include/blackbox_csv.h` + `src/blackbox_csv.c`
  (czysta: dekodowany rekord+nagłówek → wiersz CSV w buforze; host-testowana)
- Stwórz: `components/usb_console/CMakeLists.txt` (REQUIRES `console`, `blackbox`)
- Modyfikuj: `main/app_main.c` (init konsoli; ustalić koegzystencję z `ESP_LOG`)
- Test (unit): `test/host/test_blackbox_csv.c` (kolejność kolumn, wartości, nagłówek)
- Modyfikuj: `test/host/CMakeLists.txt`, `test/host/test_main.c`

**Podejście:**
- `spotlog dump`: nagłówek CSV + po jednym wierszu na próbkę; każdy wiersz niesie
  `session_id` i aktywne nastawy sesji (denormalizacja) → trywialne grupowanie
  offline. Kolumny m.in.: `session_id, t_ms, substate, err_m, bearing_deg10,
  heading_deg10, servo_us, esc_us, ch1_us, ch2_us, lat_e7, lon_e7, sats, speed_cms,
  gps_fix, imu_ok, deadband_m, max_throttle_pct, throttle_gain, servo_gain`.
- Formatowanie wiersza = czysta funkcja (host-test); HAL tylko czyta region i
  `printf` po REPL.
- Rekordy w kolejności `seq` (najstarsze→najnowsze) lub odwrotnie — udokumentować
  w nagłówku CSV.

**Wzorce do naśladowania:**
- `components/web_panel/src/command_parse.c` (parser komend), `ws_telemetry.c`
  (serializacja pól jako ints), `esp_console` przykłady z ESP-IDF.

**Scenariusze testowe:**
- [Unit] dekodowany rekord → wiersz CSV: poprawna kolejność i wartości kolumn.
- [Unit] nagłówek CSV zgodny z kolejnością pól (kontrakt dla parsera Claude).
- [Unit] sesja z nagłówkiem + N próbek → N wierszy z tym samym `session_id` i
  nastawami sesji.

**Weryfikacja:**
- `test/host/run.sh` zielony; `idf.py build` zielony; na sprzęcie: po podłączeniu
  USB `spotlog dump` wypisuje parsowalny CSV (weryfikacja sprzętowa → known-issues).

---

- [ ] **Unit 5: Komendy `params get` / `params set` — auto-zapis nastaw przez SI-6**

**Cel:** Umożliwić odczyt bieżących nastaw i automatyczny zapis dostrojonych
wartości spot-locka z konsoli USB, wymuszając istniejące reguły SI-6.

**Wymagania:** R7

**Zależności:** Unit 4 (framework konsoli), istniejące settings/SI-6.

**Pliki:**
- Modyfikuj: `components/usb_console/src/usb_console.c` (rejestracja `params get`,
  `params set`)
- Stwórz: `components/blackbox/include/params_cmd.h` + `src/params_cmd.c` ALBO
  czysta funkcja decyzji parsowania/walidacji argumentów (host-testowana), HAL
  woła `settings_validate` + `control_loop_post_pending`
- Modyfikuj (jeśli brak gettera): `components/control_loop` lub `settings` — ekspozycja
  bieżących `settings_params` do druku (`params get`)
- Test (unit): `test/host/test_params_cmd.c` (parsowanie argumentów, odrzucenie poza
  zakresem, mapowanie na pola spot_lock_*)
- Modyfikuj: `test/host/CMakeLists.txt`, `test/host/test_main.c`

**Podejście:**
- `params get` drukuje bieżące `spot_lock_*` (+ ewentualnie pełny zestaw) jako
  stan „przed" do analizy.
- `params set` przyjmuje pola spot_lock_* (np. `deadband_m`, `max_throttle_pct`,
  `throttle_gain`, `servo_gain`), waliduje przez `settings_validate`, stage'uje
  przez `control_loop_post_pending`. Firmware aplikuje wyłącznie w DISARMED i
  commituje (debounce) — TA SAMA ścieżka co HTTP POST.
- Komenda zwraca: wynik walidacji (odrzucenie poza zakresem), oraz informację
  „staged → zastosuje się w DISARMED" lub „applied" + faktyczne wartości (zwrot
  zastosowanych nastaw, R7).
- Parsowanie/walidacja argumentów = czyste, host-testowane; I/O cienkie.

**Notatka wykonawcza:** Decyzję parsowania/walidacji argumentów implementuj
test-first (happy path + poza zakresem).

**Wzorce do naśladowania:**
- `components/web_panel/src/params_api.c::params_api_handle_post` (validate→stage),
  `params_decide_write` (decyzja 409/400/200), `command_parse` (exact-match).

**Scenariusze testowe:**
- [Unit] `params set` z wartością w zakresie → staged, zwraca przyjęte pola.
- [Unit] `params set` z wartością poza zakresem (np. `max_throttle_pct` 200) →
  odrzucone, brak stage'owania (FAILuje gdyby walidacja była pominięta).
- [Unit] `params get` zwraca bieżące spot_lock_* zgodne z aktywnymi nastawami.
- [Unit] (kontrakt SI-6) stage w ARMED nie aplikuje się dopóki nie DISARMED —
  potwierdzone przez reużycie istniejącej bramki (test na poziomie decyzji).

**Weryfikacja:**
- `test/host/run.sh` zielony; `idf.py build` zielony; na sprzęcie: `params set` w
  DISARMED zmienia aktywne nastawy (widoczne w `params get` i telemetrii); w ARMED
  → staged, brak zmiany do rozbrojenia (weryfikacja sprzętowa → known-issues).

---

- [ ] **Unit 6: Procedura kalibracji + dokumentacja i known-issues**

**Cel:** Udokumentować jak Claude czyta CSV i mapuje obserwacje na zmianę nastaw
(priorytet stabilności), oraz zalogować luki sprzętowe/E2E i nowy wzorzec.

**Wymagania:** R8

**Zależności:** Unit 1–5.

**Pliki:**
- Stwórz: `docs/blackbox-calibration.md` (procedura: kolumny CSV → metryki
  „polowania"/przeregulowania/donuta → kierunek zmiany deadband/gaz/gainów, z
  priorytetem stabilności R8; sekwencja `dump → analiza → params set → reflash off`)
- Modyfikuj: `docs/completed/kayak-motor-firmware-v1/known-issues.md` (sprzętowe/E2E:
  realny zapis na flash w sesji, parsowalność CSV na żywo, brak jittera 50 Hz przy
  erase, faktyczny wpływ nastaw na zachowanie na wodzie)
- Modyfikuj: `README`/pinout (sekcja: blackbox spot-locka, komendy USB `spotlog
  dump`/`params get`/`params set`, partycja `spotlog`)

**Podejście:**
- Procedura kalibracji to wiedza dla Claude, nie kod firmware: metryki liczone
  offline z CSV; progi dostrajane w terenie (odroczone do iteracji).
- Po wdrożeniu rozważyć `/dev-compound` dla wzorca „flush na flash bez jittera
  pętli RT" (udokumentowana luka wiedzy) — poza tym Unitem.

**Scenariusze testowe:**
- [E2E] (sprzętowe, do known-issues) sesja na wodzie → `spotlog dump` po USB →
  CSV zawiera nagłówek sesji + malejący `err_m`; `params set` zmienia zachowanie
  w kolejnej sesji.

**Weryfikacja:**
- Dokumenty obecne i spójne; known-issues zawiera luki sprzętowe/E2E; brak zmian
  w kodzie wymagających testów host.

## Wpływ systemowy

- **Graf interakcji:** nowy task tła czyta `control_loop_get_snapshot()` (tylko
  odczyt); nowa konsola USB rejestruje komendy; `params set` wchodzi w istniejący
  tor pending/apply (`control_loop_post_pending` → `maybe_apply_pending` →
  `nvs_store_commit`). Pętla 50 Hz i `sm_step` nietknięte.
- **Propagacja błędów:** HAL flash/konsola mapują `esp_err_t`→enum domenowy;
  błędy nagrywania są best-effort (log statusu, nie crash — blackbox to wskaźnik,
  nie wyjście bezpieczeństwa); błędy `params set` zwracane do operatora (odrzucenie
  walidacji), bez zmiany stanu.
- **Ryzyka cyklu życia stanu:** ring nadpisuje najstarsze (świadome); częściowy
  rekord na granicy erase → magic/CRC odrzuca niekompletne; reflash z nowym
  layoutem partycji nie kasuje appcfg (partycja dopisana na końcu).
- **Parytet surface API:** `params set` musi dawać te same wyniki walidacji co
  HTTP POST `/api/params` (ta sama `settings_validate`); rozbieżność = bug.
- **Pokrycie integracyjne:** brak jittera 50 Hz przy erase flash i faktyczna
  parsowalność CSV są sprzętowe — poza host-testami (known-issues).

## Ryzyka i zależności

- **Brak frameworku konsoli** — `esp_console` na USB Serial/JTAG to nowy element;
  ryzyko kolizji z `ESP_LOG` na tym samym interfejsie. Mitygacja: rozważyć logi na
  UART0, REPL na USB (odroczone do implementacji na sprzęcie).
- **Erase flash a pętla 50 Hz** — erase sektora może blokować ms. Mitygacja: cały
  I/O w tasku tła prio 2, nigdy w pętli sterującej; weryfikacja jittera na sprzęcie.
- **Zmiana `partitions.csv`** wymaga reflashu layoutu. Mitygacja: dopisanie na
  końcu (offsety bez zmian), więc appcfg/kalibracja przeżywa.
- **Auto-zapis bez potwierdzenia** (decyzja produktowa) — ryzyko ograniczone przez
  SI-6 (DISARMED + walidacja + single-writer) i zwrot zastosowanych wartości.
- **Strojenie na sprzęcie nieweryfikowalne na hoście** — heurystyki R8 dostrajane w
  terenie; host-testy pokrywają tylko czystą logikę (kodek/ring/sampler/CSV/params).

## Dokumentacja / Notatki operacyjne

- `docs/blackbox-calibration.md` (procedura odczytu i strojenia).
- Aktualizacja README/pinout o komendy USB i partycję `spotlog`.
- Po wdrożeniu: `/dev-compound` dla wzorca „flush bez jittera RT" (luka wiedzy).
- Reflash z nowym `partitions.csv` przy pierwszym wdrożeniu (komunikat dla operatora).

## Źródła i referencje

- **Dokument źródłowy:** docs/dev-brainstorms/2026-06-29-spot-lock-blackbox-requirements.md
- Powiązany kod: `components/control_loop/src/control_loop.c` (snapshot, pending/apply),
  `components/settings/*` (SI-6, blob_codec, nvs_store), `components/gps/src/gps_reader.c`
  (task tła), `partitions.csv`.
- Wiedza: `docs/solutions/2026-06-17-*` (wrap-safe, Pure⊥HAL, oracle power),
  `docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md`.
- Plan poprzedni: `docs/plans/2026-06-29-001-feat-spot-lock-position-hold-plan.md`.
