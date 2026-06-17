# Code Review — Faza 4 (Unit 8: Persystencja NVS)

**Commit:** `1fd5bf9` — feat(kayak-motor-firmware): Faza 4 — persystencja NVS (blob+CRC32, debounce, pending/apply)
**Data review:** 2026-06-16
**Zakres:** Unit 8 — versioned blob + CRC32, delayed commit (debounce), load-at-boot, commit gated DISARMED.

## Severity gate

⚠️ **KONTYNUUJ Z ZASTRZEŻENIAMI** — P1=0, P2=1, P3=5.

| Severity | Liczba |
|----------|--------|
| 🔴 P1 blocking | 0 |
| 🟠 P2 important | 1 |
| 🟡 P3 nit | 5 |

Typy: **KOD**: 6 (5×P3 + 1×P2). **TEST**: 1 (P2, współdzielony — luka pokrycia HAL). **E2E**: N/A (Unit 8 to pure logic + HAL flash, brak UI; weryfikacja persystencji to [HW], odroczone — brak sprzętu).

## Werdykt: bezpieczeństwo deserializacji (rule 9)

**BEZPIECZNE. Brak odczytu poza buforem. CRC sprawdzany PRZED użyciem zawartości.**

Ścieżka `blob_codec_decode` (blob_codec.c:136–163) jest twarda na wrogokształtny NVS:

1. **Length-before-read:** `len != BLOB_CODEC_SIZE` (46 B) → `BLOB_CODEC_ERR_LENGTH` natychmiast, przed jakimkolwiek odczytem pola (linia 142). Akceptowana jest WYŁĄCZNIE dokładna długość — nie `>=`, nie `<=`. To eliminuje zarówno under-read (za krótki bufor) jak i tolerowanie nadmiarowych bajtów.
2. **CRC-before-use:** CRC liczony nad pierwszymi `BLOB_CODEC_FIELD_BYTES` (42 B) i porównany ze stored trailer PRZED `deserialize_fields` i przed `*out` (linie 146–152). Zawartość nie jest używana dopóki CRC się nie zgadza.
3. **Schema-after-CRC:** `schema_version` sprawdzany po CRC, na zdekodowanej (zweryfikowanej) kopii lokalnej `decoded`; `*out` zapisywany TYLKO przy pełnym sukcesie (linia 161). Odrzucenie nie mutuje wyjścia wołającego.
4. **Brak `memcpy` struktury:** serializacja/deserializacja pole-po-polu, little-endian jawny (`put_u16`/`get_u16`), niezależny od ABI/paddingu. Round-trip porównywany pole-po-polu w teście (nie `memcmp`).
5. **Granica bufora w HAL:** `read_blob` (nvs_store.c:37–53) woła `nvs_get_blob` z capem `len=BLOB_CODEC_SIZE` nad `uint8_t buf[BLOB_CODEC_SIZE]`. Blob większy → IDF zwraca `ESP_ERR_NVS_INVALID_LENGTH` (≠ OK) → brak decode. Blob mniejszy → `len` ustawione na realny rozmiar → decode odrzuca na `len != SIZE`. Nigdy nie czyta poza `buf`.

Kontrola kolejności (length → CRC → schema) jest pokryta testami: `test_wrong_length_too_short_is_rejected` (length przed CRC), `test_bad_crc_is_rejected`, `test_corrupt_crc_trailer_is_rejected`, `test_other_schema_version_is_rejected` (re-stamp CRC żeby izolować schema). Skorumpowany blob nigdy nie daje wartości — wraca do `settings_validate(NULL, false)` → defaulty w oknie sanity (R16).

## CRC32 — known-answer zweryfikowany na żywo

Niezależna kompilacja implementacji (`blob_codec.c:14–25`, poly `0xEDB88320`, init/final `0xFFFFFFFF`, reflected):
- `CRC32("123456789") == 0xCBF43926` ✅ (kanoniczny IEEE 802.3 / zlib check value)
- `CRC32(empty) == 0x00000000` ✅

Implementacja poprawna. `mask = -(crc & 1)` branchless reflected — standardowy idiom, bezpieczny (unsigned wrap intencjonalny). Test `test_crc32_check_value_is_standard` zakotwicza wybór parametrów polinomu.

## Testy hosta + build (na żywo)

- **Testy hosta:** `./test/host/run.sh` → **124 Tests, 0 Failures, 0 Ignored — OK**. Oczekiwane 124/124 ✅.
- **Build:** `. idf-env.sh && idf.py set-target esp32 && idf.py build` → **EXIT=0**, `kayak-motor-firmware.bin` 0x372a0 B (86% wolnego w partycji app). ✅
- Partycja `appcfg` zdefiniowana w `partitions.csv` (data/nvs, 0x6000), `CONFIG_PARTITION_TABLE_CUSTOM=y` w sdkconfig — osobna od systemowego `nvs` (poprawna izolacja).

## Regresje

**BRAK.** 103 → 124 (+21: 11 blob_codec, 10 commit_debounce). Wszystkie wcześniejsze testy nadal PASS (cały zestaw 124/124, w tym Faza 0–3). Cross-phase: brak naruszeń.

## Checklist scenariuszy (plan vs implementacja)

| Scenariusz (plan) | Pokrycie | Status |
|---|---|---|
| round-trip serialize↔deserialize równe | `test_round_trip_preserves_every_field` (pole-po-polu) | ✅ |
| zła CRC → odrzucone | `test_bad_crc_is_rejected`, `test_corrupt_crc_trailer_is_rejected` | ✅ |
| zła długość → odrzucone | `test_wrong_length_too_short/too_long` | ✅ |
| inny schema_version → odrzucone | `test_other_schema_version_is_rejected` | ✅ |
| encode stampuje bieżący schema | `test_encode_stamps_current_schema_version` | ✅ |
| debounce: zmiana → brak commitu przed oknem | `test_change_does_not_commit_before_window` | ✅ |
| kolejna zmiana resetuje timer | `test_second_change_resets_the_timer` | ✅ |
| force → natychmiast (gdy dirty) | `test_force_commits_immediately_when_dirty` | ✅ |
| brak zmian → brak commitu (też force) | `test_force_does_nothing_when_clean`, `test_init_is_clean_no_commit` | ✅ |
| granica inclusive (elapsed >= window) | `test_commit_exactly_at_window_boundary_is_inclusive` | ✅ |
| clamp okna (min/max) | `test_window_clamped_below_minimum/above_maximum` | ✅ |
| mark_committed czyści dirty | `test_mark_committed_clears_dirty` | ✅ |
| [HW] przeżycie restartu / power-cut / pusty NVS | odroczone — brak sprzętu | [HW] N/A |

Zero testów bez asercji. `now_ms` wstrzykiwany (czysty), brak realnego zegara w pure modułach.

## Architektura

- **pure ⊥ HAL:** `blob_codec.{c,h}` i `commit_debounce.{c,h}` mają ZERO include IDF (zweryfikowane grepem `esp_/nvs/freertos/driver`). Oba w `PURE_SOURCES` host-harness. `nvs_store.c` to jedyny plik dotykający flasha. ✅
- **Bramka DISARMED (R17/SI-6):** `maybe_commit_params` (control_loop.c) wczesny return przy `!loop_should_apply_pending(state)` — ta sama bramka co apply. Commit tylko DISARMED + debounce. Pętla pozostaje jedynym pisarzem active params (NVS store nie mutuje active w locie). ✅
- **Failed write → retry:** `nvs_store_commit` błąd → `maybe_commit_params` loguje i wraca BEZ `mark_committed`, więc `dirty` zostaje → następny cykl retry. Poprawnie. ✅
- **Load-at-boot:** `app_main` woła `nvs_store_load` PRZED `control_loop_init`/`control_loop_run`, na zwalidowanych params (defaulty w oknie sanity przy braku/zepsuciu). ✅
- **NEW_VERSION_FOUND vs korupcja:** `init_partition` rozdziela `NEW_VERSION_FOUND` (log "alert, not corruption") od `NO_FREE_PAGES`; oba → erase + re-init. ✅
- **Typed errors / named constants:** `blob_codec_result` enum, `esp_err_t`, stałe `BLOB_CODEC_*`, `COMMIT_DEBOUNCE_*` UPPER_SNAKE. Zero magic numbers w hot path. ✅
- **Rozmiary:** największy plik blob_codec.c = 163 linii (<300). Wszystkie funkcje <50 linii. Zero circular deps. ✅

## Odchylenia od planu

Plan (Unit 8) lokalizował testy w `components/settings/test/test_blob_codec.c` i `components/settings/test/test_commit_debounce.c`. Faktycznie znajdują się w `test/host/test_blob_codec.c` i `test/host/test_commit_debounce.c` (centralny host-harness). To **spójna, świadoma konwencja projektu** ustalona w Fazach 0–3 (wszystkie pure-testy w `test/host/`, jeden `test_main.c`), a NIE pominięcie — testy istnieją, są zarejestrowane (`run_blob_codec_tests`/`run_commit_debounce_tests` w `test_main.c`) i przechodzą. Brak realnego odchylenia funkcjonalnego. Nie traktuję jako P2 (plik testowy istnieje i działa; rozbieżność to tylko ścieżka katalogu, zgodna z istniejącym wzorcem repo).

## Findingi

### 🟠 P2 — important

- **[TEST/KOD] components/settings/src/nvs_store.c (cały HAL)** — `nvs_store_load`, `read_blob`, `resolve_params`, `nvs_store_commit`, `init_partition` nie mają żadnego host-testu (są HAL, niedostępne w pure harness). Niepokryta jest realna logika decyzyjna `resolve_params`: mapowanie `read_err`/`decode` → `has_stored` (NOT_FOUND → defaulty bez alertu; INVALID_LENGTH/inny błąd → alert+defaulty; decode-fail → alert+defaulty). To jedyna warstwa, która decyduje czy skorumpowany blob trafi do walidacji jako "brak". `blob_codec` i `settings_validate` są dobrze pokryte osobno, ale ich **złożenie** w `resolve_params` (gałąź `read_err != OK && != NOT_FOUND`, gałąź decode != OK) nie ma żadnej wyroczni. Rozważyć ekstrakcję czystej funkcji decyzyjnej (np. `resolve_provenance(read_err, decode_result) -> {has_stored, log_reason}`) testowalnej na hoście, albo fakeowany `nvs_*` w teście integracyjnym. Bezpieczeństwo zachowane (każda gałąź kończy się defaultami), więc nie blokujące — ale to logika rozróżniająca alert/korupcję/empty z planu (R16), warta wyroczni.

### 🟡 P3 — nit

- **[KOD] components/settings/src/nvs_store.c:73 + settings_validate.c (nvs_error)** — przy blob obecnym ale nieczytelnym (CRC/length/schema fail = realna korupcja) `resolve_params` woła `settings_validate(NULL, false, out)`, co ustawia `nvs_error=false`. Rozróżnienie "pusty NVS" vs "obecny-ale-skorumpowany blob" jest tylko logowane (`ESP_LOGW "stored blob rejected"`), NIE wystawione w `settings_validation_result.nvs_error` do telemetrii R16. Pole `nvs_error` (dodane w Fazie 1 jako kontrakt do domknięcia w Unit 8) nadal zawsze `false`. Domknąć: przekazać do `settings_validate` informację o korupcji blobu albo ustawić `nvs_error` na warstwie NVS przy `decode != OK`.

- **[KOD] components/settings/src/nvs_store.c:31** — `ESP_ERROR_CHECK(nvs_flash_erase_partition(...))` w `init_partition` abortuje proces przy błędzie erase, podczas gdy reszta funkcji zwraca `esp_err_t`. Niespójna obsługa błędu (abort vs return). Akceptowalne dla nieodwracalnego błędu flasha przy boocie, ale rozważyć return błędu dla spójności kontraktu (wołający `nvs_store_load` i tak zwraca esp_err do `ESP_ERROR_CHECK` w app_main).

- **[KOD] components/control_loop/src/control_loop.c:34 (`now_ms`)** — `esp_timer_get_time()/1000` rzutowane do `uint32_t` zawija co ~49.7 dnia. Debounce używa wrap-safe modular subtraction (poprawnie), więc bez błędu funkcjonalnego — ale truncation int64→uint32 warta komentarza, że wrap jest świadomy i bezpieczny (analogicznie do udokumentowanego epoch w rc_capture).

- **[TEST/KOD] components/control_loop/src/control_loop.c (`maybe_commit_params`)** — orkiestracja commit (gate DISARMED + debounce + retry-on-fail) jest HAL-only, niepokryta host-testem. Czyste składniki (`loop_should_apply_pending`, `commit_debounce_should_commit`) pokryte osobno, ale ich złożenie + ścieżka retry (dirty zostaje) nie ma wyroczni. Kontynuacja nitu z Fazy 3 (`maybe_apply_pending` też HAL-only). Domknąć przy Unit 10 (realny przepływ pending z panelu).

- **[KOD] components/settings/include/commit_debounce.h:27 + control_loop.c** — `COMMIT_DEBOUNCE_DEFAULT_MS 3000` używane na sztywno w `control_loop_init`; okno debounce nie jest konfigurowalne z `settings_params` (nie ma pola). Plan dopuszcza 2–5 s — bieżące 3 s OK, ale gdyby miało być tunowalne z panelu, brak ścieżki. Świadoma decyzja (stała w oknie); odnotować, nie zmieniać bez wymagania.

## Re-review po cyklu 1 (commit `d69722c`)

**Severity gate:** ✅ **CZYSTE** — P1=0, P2=0, P3=5 (P3 bez zmian, niewymagane do domknięcia w tej fazie).

**P2 ROZWIĄZANY: TAK** (realna logika, nie kosmetyka). Wprowadzono czystą funkcję decyzyjną `resolve_provenance(read_status, decode, decoded, out)` w nowym module `components/settings/{include/nvs_provenance.h, src/nvs_provenance.c}` (52 linie, zero include IDF — zweryfikowane grepem `esp_/nvs/freertos/driver/esp_log`; w `PURE_SOURCES` host-harness). Logika jest realnie rozgałęziona, nie tożsamościowa:

- `NVS_READ_NOT_FOUND` → DEFAULTS, `nvs_error=false` (pusty store — cichy).
- `NVS_READ_OK` + `BLOB_CODEC_ERR_CRC`/`ERR_LENGTH` → korupcja → DEFAULTS + `nvs_error=true`.
- `NVS_READ_OK` + `BLOB_CODEC_ERR_SCHEMA` (nowsza wersja) → alert → DEFAULTS, `nvs_error=false` (rozróżnione od korupcji — to sedno findingu).
- `NVS_READ_OK` + `BLOB_CODEC_OK` → delegacja `settings_validate(decoded, true, out)` (możliwy `MIXED_RECOVERED`).
- `NVS_READ_ERROR` → DEFAULTS + `nvs_error=true`.

Każda gałąź ≠ OK przechodzi przez `settings_validate(NULL,...)` → `settings_load_defaults` → skorumpowany blob NIGDY nie wychodzi poza okno sanity.

**`nvs_error` nie jest już zawsze false:** `defaults_with_error` jawnie nadpisuje `result.nvs_error = nvs_error` (P3 z poprzedniego raportu — R16 — domknięty przy okazji dla gałęzi korupcja/read-error). Pozostałe P3 (init_partition abort, now_ms wrap komentarz, maybe_commit_params HAL-only, debounce non-config) bez zmian.

**HAL cienki i deleguje:** `nvs_store.c::resolve_params` ekstrahowane do `map_read_status` (esp_err_t → nvs_read_status) + jednego wywołania `resolve_provenance`. Zero logiki decyzyjnej zostało w HAL.

**Pokrycie testowe (test/host/test_nvs_provenance.c, 7 testów, w `test_main.c` jako `run_nvs_provenance_tests`):** wszystkie gałęzie z konkretnymi asercjami (zero assertion-free): not_found, bad CRC, bad length, schema-alert, valid→NVS (params zachowane), out-of-range→MIXED_RECOVERED, read error. Każdy test asertuje `source`, `nvs_error` i okno sanity (`assert_within_sanity_window`). Brak osłabionych asercji.

**Regresje: BRAK.** 124 → 131 (+7 nvs_provenance). Wszystkie 131/131 PASS na żywo (blob_codec, commit_debounce, integracja, Fazy 0–3 nienaruszone). Separacja pure⊥HAL zachowana. Limity: nvs_provenance.c 52 linii, najdłuższa funkcja `resolve_provenance` ~17 linii — w normach. Zero circular deps (nvs_store → nvs_provenance, nie odwrotnie).

**Testy hosta + build (na żywo):**
- `./test/host/run.sh` → **131 Tests, 0 Failures, 0 Ignored — OK** ✅
- `idf.py set-target esp32 && idf.py build` → **EXIT=0**, build complete ✅

**Checkboxy:**
- [x] `resolve_provenance` PURE (zero IDF) z realną logiką decyzyjną (nie tożsamościowe przeniesienie)
- [x] Skorumpowany NIGDY poza oknem sanity
- [x] nvs_store.c (HAL) cienki, deleguje do resolve_provenance
- [x] Testy pokrywają wszystkie gałęzie z konkretnymi asercjami + okno sanity
- [x] `nvs_error` nie jest już zawsze false
- [x] Brak regresji (131/131 PASS)
- [x] Separacja pure⊥HAL, limity plików/funkcji, brak osłabionych asercji

**Werdykt re-review:** P2 domknięty realnie. Faza 4 CZYSTA — przejście dalej.

## Wnioski

Solidna, bezpieczna implementacja Unit 8. Deserializacja niezaufanego NVS jest twarda (length→CRC→schema, brak OOB, brak memcpy, fail→defaulty w oknie sanity). CRC zweryfikowany na żywo (known-answer + empty). Pure⊥HAL czyste, bramka DISARMED i retry poprawne, 124/124 testy + build EXIT=0 na żywo, zero regresji. Główny dług: brak wyroczni dla logiki decyzyjnej w warstwie HAL (`resolve_params` / `maybe_commit_params`) — P2/P3, do domknięcia przy Unit 10 lub przez ekstrakcję czystej funkcji decyzyjnej. R16 `nvs_error` (rozróżnienie korupcja vs empty w telemetrii) nadal niedomknięte — P3.
