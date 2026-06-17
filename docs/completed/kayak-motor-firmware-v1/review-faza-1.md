# Code Review — Faza 1 (Unit 3 + 4)

**Commit:** `e36e01a` — feat(kayak-motor-firmware): Faza 1 — RC capture (MCPWM) + cap_math + RC_valid + model ustawień
**Data review:** 2026-06-16
**Zakres:** Unit 3 (MCPWM capture HAL + pure `cap_math`), Unit 4 (pure `rc_validity` + model ustawień + walidacja).
**Metodologia:** dev-docs-review (5 perspektyw: security/correctness, performance, architecture, test coverage, E2E).

---

## Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI

- **P1 (blocking):** 0
- **P2 (important):** 2
- **P3 (nit):** 5
- Typy: KOD = 6, TEST = 1, E2E = 0
- E2E: **N/A** (brak panelu WWW przed Unit 10).

## Weryfikacja na żywo

- **Testy hosta:** `./test/host/run.sh` → **42 Tests, 0 Failures, 0 Ignored — OK** (17 baza + 25 nowych: 6 cap_math, 13 rc_validity, 6 settings_validate). Potwierdzone na żywo.
- **Build:** `. "$HOME/esp/idf-env.sh" && idf.py set-target esp32 && idf.py build` → **EXIT=0**, `kayak-motor-firmware.bin` 0x30af0 B (87% partycji wolne). Potwierdzone na żywo.
- Worktree po buildzie czysty (brak nieoczekiwanych zmian śledzonych plików).

---

## Ocena wg perspektyw

### 1. Bezpieczeństwo / poprawność

**Mocne strony:**
- `cap_ticks_to_us` — czysta integer math (num/den 25/2, round-to-nearest), bez floatów. `ticks*25` (max ~1.07e11) bezpiecznie mieści się w `uint64_t`; brak UB. Komentarz poprawny.
- Przepełnienie 32-bit licznika obsłużone idiomatycznie przez unsigned modular subtraction w `cap_ticks_elapsed` (`now - prev`). Test `test_ticks_elapsed_handles_counter_overflow` + `test_period_us_across_overflow` realnie pokrywają wrap.
- `channel_valid` — fail-fast, early-return, kolejność tania→droga (edge_seen → recency → width → period). `rc_valid = CH1 && CH2`, CH4 strukturalnie poza predykatem (nie jest argumentem) — R12 spełnione na poziomie typu, nie tylko konwencji.
- Debounce: licznik resetowany przy pierwszej dobrej ramce, latch invalid dopiero po N; guard `bad_frame_count < UINT16_MAX` zapobiega przepełnieniu licznika. Test resetu sprawdza też `bad_frame_count == 1` (zachowanie, nie tylko wynik).
- `settings_validate` — per-field fallback + cross-field z przywróceniem CAŁEJ grupy (spójność wewnętrzna). Defaulty NIGDY poza oknem sanity — `test_defaults_pass_their_own_validation` dowodzi że defaulty przechodzą własną walidację jako NVS/valid (R16 / SI-4). `settings_ranges.h` współdzielone, więc defaulty prowadnie spełniają walidator.
- Discriminated `settings_source` enum zamiast luźnych boolean flag — zgodne z coding-rules §10.

**Findingi:** patrz P2-1 (epoch `last_edge_us`), P3-1 (torn read), P3-2 (`nvs_error` zawsze false).

### 2. Performance

- `on_cap` oznaczony `IRAM_ATTR`, brak alokacji/blocking/logowania w ISR, tylko integer math i zapisy do statycznego stanu per kanał (`user_ctx`). Zgodne z coding-rules §12/§13.
- Brak pętli z I/O, brak N+1. cap_math O(1).
- Brak zastrzeżeń performance.

### 3. Architektura

- **Separacja pure⊥HAL czysta:** `cap_math.{c,h}` i `rc_validity` nie linkują IDF; `rc_sample.h` celowo bez `esp_err.h` (komentarz to dokumentuje), dzięki czemu `rc_validity` testuje się na hoście bez sterownika MCPWM. MCPWM izolowany wyłącznie do `rc_capture.c`.
- **Brak circular deps:** `rc_validity` REQUIRES `rc_capture` jednokierunkowo (dla `rc_sample.h`); `rc_capture` nie zna `rc_validity`. `settings` bez zależności.
- **Rozmiary:** wszystkie pliki < 300 (max 168), nesting ≤ 2, early-return. Named constants w `settings_ranges.h` (zero magic numbers w logice).
- **Nazewnictwo:** typy PascalCase (`RcCaptureChannel`) — kontynuacja nitu z fazy 0; reszta zgodna (`UPPER_SNAKE` stałe, `snake_case` funkcje/pola, prefix `RC_CAP_`).
- **Findingi:** P3-3 (`validate_fields` ~60 linii), P3-4 (niespójny prefix mapy GPIO vs faza 0).

### 4. Pokrycie testów

Wszystkie scenariusze `[Unit]` z checklisty zadania.md (Unit 3 + 4) pokryte realnymi asercjami (brak assertion-free), testują zachowanie nie stan wewnętrzny:
- cap_math: ticki→µs (1 µs, typowy 1500, round-to-nearest), okres z 2 zboczy, **2× overflow** ✓
- channel_valid: brak edge, never-seen, width 700/2300, period poza tolerancją, valid ✓
- rc_valid: CH1 zły / CH2 zły / oba dobre / CH4 bez wpływu ✓
- debounce: 1 zła→valid, N→invalid, dobra resetuje licznik ✓
- settings: empty→DEFAULTS, valid→NVS, single OOR→MIXED_RECOVERED+fallback, cross-field forward inverted + RC non-monotonic ✓

Progi udokumentowane (`RC_DEBOUNCE_DEFAULT_THRESHOLD=5` z uzasadnieniem; okres ramki konfigurowalny, NIE hardcode 20 ms — placeholder tylko w teście). **Finding:** P2-2 (brakujący test happy-path `cap_period_us` bez overflow przy nie-zerowym `prev`… patrz niżej — w istocie pokryty; właściwy P2 to brak testu boundary width/period inclusive).

### 5. E2E

**N/A** — faza 1 nie wprowadza UI. Agent 5 pominięty zgodnie z kontekstem.

---

## Findingi (posortowane wg severity)

### 🟠 P2 — important

- **P2-1 [KOD]** `components/rc_capture/src/rc_capture.c:56` + `components/rc_validity/src/rc_validity.c:29` — **epoch/wrap `last_edge_us`.** `last_edge_us = cap_ticks_to_us(ticks)` to konwersja *absolutnego* 32-bit licznika capture; przy 80 MHz licznik (a więc i `last_edge_us`) zawija co ~53,6 s. `channel_valid`/`edge_recent` liczy `abs_diff_u32(now_us, last_edge_us)` i wymaga, by `now_us` był w TYM SAMYM epoku ticków. Nagłówek to dokumentuje ("same epoch as last_edge_us"), ale żaden producent `now_us` jeszcze nie istnieje, a naiwne podpięcie `esp_timer_get_time()` (inny epoch, brak wrapu) da błędną decyzję recency tuż po starcie i wokół każdego zawinięcia. To kontrakt cross-unit do domknięcia w Unit 7 (integracja pętli): albo `now_us` musi pochodzić z tego samego licznika capture i porównanie ma być modularne (`abs_diff_u32` na 32-bit nie wykrywa wrapu jako "recent" poprawnie dla dużych różnic), albo recency liczyć po stronie ISR jako licznik ramek/wieku. **Działanie:** udokumentować i rozwiązać w Unit 7 zanim `channel_valid` zostanie wpięty do realnej pętli; nie blokuje Fazy 1 (logika pure poprawna dla zadanego kontraktu).

- **P2-2 [TEST]** `test/host/test_rc_validity.c` — **brak testu granicy inclusive** dla `width`/`period`. Predykat używa `>=`/`<=` (granice włącznie), ale testy sprawdzają tylko wartości WYRAŹNIE poza/wewnątrz (700/2300/30 ms). Brak przypadku dokładnie na `width_min_us`/`width_max_us` i na `period_expected ± period_tol` (czy granica jest akceptowana). Granice inclusive to klasyczne miejsce off-by-one; faza 0 explicite testowała clamp "na granicy → granica (inclusive)" — tu analogiczny przypadek pominięto. **Działanie:** dodać 2–3 asercje boundary (width == 800/2200 → true; period == expected±tol → true; expected±(tol+1) → false).

### 🟡 P3 — nit

- **P3-1 [KOD]** `components/rc_capture/src/rc_capture.c:130` / `include/rc_capture.h:24-26` — **torn read** w `rc_capture_read`: kopia 16-bajtowej struktury (`*out = s_state[ch].sample`) współbieżnie z zapisem pól w `on_cap` może zmieszać pola sprzed i po zboczu (np. nowa `width_us` ze starym `period_us`). Na 32-bit każde słowo atomowe, ale nie cała struktura. Nagłówek to świadomie dokumentuje ("best-effort snapshot"), a downstream jest debounce'owany — akceptowalne w Fazie 1, ale do potwierdzenia/utwardzenia przy integracji pętli (np. seqlock lekki albo kopia pod krytycznie krótkim disable IRQ).

- **P3-2 [KOD]** `components/settings/src/settings_validate.c:122-166` — flaga `nvs_error` jest ZAWSZE `false` (ścieżka defaults ustawia `false`, ścieżka stored też). Pole istnieje w wyniku, ale walidator nie ma jak go ustawić — własność rozróżnienia "blob obecny lecz nieczytelny" należy do warstwy NVS (Unit 8). Komentarz to wyjaśnia; zostawić jako pole kontraktu, ale brak realnego użycia/testu do Unit 8 (świadomy dług, nie defekt).

- **P3-3 [KOD]** `components/settings/src/settings_validate.c:24-85` — `validate_fields` ~60 linii (> 50 z coding-rules §1). To płaska lista przypisań `field_or_default` na jednym poziomie abstrakcji (czytelna), więc pragmatycznie OK, ale formalnie przekracza próg — rozważyć podział na grupy (rc/servo/esc/safety) jeśli przyrośnie.

- **P3-4 [KOD]** `components/rc_capture/src/rc_capture.c:19` — prefix `RC_CAP_GPIO_MAP` spójny, ale typy nadal PascalCase (`RcCaptureChannel`) vs konwencja ESP-IDF `snake_case_t` — kontynuacja nitu z fazy 0 (zgodne z literą reguł §7, niespójne z frameworkiem). Potwierdzić jednolitą konwencję projektu.

- **P3-5 [KOD]** `components/rc_capture/src/rc_capture.c:118-122` — handle timera/kanałów (`timer`, `handle`) nie są przechowywane ani zwalniane (init jednorazowy, fail-fast przez return). Akceptowalne dla zasobu żyjącego do końca działania urządzenia, ale brak ścieżki deinit/rollback przy częściowym sukcesie (np. timer ok, kanał 2 fail → timer wisi). Dla firmware bez restartu komponentu — nit.

---

## Odchylenia od planu

- **Ścieżki testów:** plan (Unit 3/4, sekcja **Pliki: Test:**) definiuje `components/rc_capture/test/test_cap_math.c`, `components/rc_validity/test/test_rc_validity.c`, `components/settings/test/test_settings_validate.c`. Faktycznie testy w `test/host/`. To **świadoma, udokumentowana** decyzja standalone host-harness (kontekst.md:14, kontynuacja fazy 0) — NIE naruszenie. Wszystkie pliki testowe z planu istnieją funkcjonalnie (inna lokalizacja, pełne pokrycie scenariuszy). Nie liczę jako P2.
- **`cap_math` API:** plan przewidywał logikę pomiaru w `cap_math`; implementacja rozbiła ją na 3 czyste funkcje (`cap_ticks_to_us`, `cap_ticks_elapsed`, `cap_period_us`) — czytelniejsze, "single place the overflow rule lives". Ulepszenie zgodne z kontraktem.
- **Brak odchyleń funkcjonalnych** od scenariuszy [Unit] Unit 3/4.

## [HW] odroczone (nie findingi — decyzja użytkownika)

- [HW] Unit 3: oscyloskop równolegle z odczytem, zmierzony okres ramki → Plan pomiarów.
- [HW] Unit 4 weryfikacja sprzętowa utraty CH1/CH2 → RC_valid=false po N ramkach.
- Trafiają do known-issues, nie do severity gate.

---

## Wniosek

Logika pure (cap_math, rc_validity, settings) jest poprawna, dobrze przetestowana zachowaniowo i czysto odseparowana od HAL. Oba gate'y (host 42/42, build EXIT=0) zielone na żywo. Brak P1. Dwa P2 to dług do domknięcia przy integracji pętli (Unit 7) i jeden brak testu granicznego — żaden nie blokuje startu Fazy 2. Kontynuacja z zastrzeżeniami.

---

## Re-review po cyklu 1

**Commit naprawczy:** `cbc259d` — fix(kayak-motor-firmware): poprawki po review fazy 1 (cykl 1)
**Diff weryfikowany:** `git diff e36e01a cbc259d` (rc_sample.h, rc_capture.c, rc_validity.{c,h}, test_rc_validity.c, zadania.md).
**Data:** 2026-06-16.

### Severity gate: ✅ CZYSTE

- **P1:** 0 · **P2:** 0 (oba rozwiązane) · **P3:** 5 (bez zmian — świadomy dług/nity, poza zakresem cyklu).
- Typy pozostałych findingów: KOD = 5, TEST = 0, E2E = 0 (E2E **N/A**).

### Weryfikacja na żywo (po naprawie)

- **Testy hosta:** `./test/host/run.sh` → **49 Tests, 0 Failures, 0 Ignored — OK** (42 → 49: +7 nowych w rc_validity: 2 wrap-recency + 5 boundary inclusive). Potwierdzone na żywo.
- **Build:** `. "$HOME/esp/idf-env.sh" && idf.py set-target esp32 && idf.py build` → **EXIT=0**, `kayak-motor-firmware.bin` 0x30af0 B (87% partycji wolne). Potwierdzone na żywo.

### P2-1 [KOD] — epoch/wrap recency `last_edge`: ✅ ROZWIĄZANE

- **Domena ticków, nie µs:** pole `rc_channel_sample.last_edge_us` → `last_edge_ticks` (surowy 32-bit tick licznika capture, BEZ konwersji na µs). `on_cap` zapisuje teraz `state->sample.last_edge_ticks = ticks` (rc_capture.c:56), nie `cap_ticks_to_us(ticks)`.
- **Wrap-safe modular subtraction:** `edge_recent` liczy `cap_ticks_elapsed(now_ticks, last_edge_ticks)` (unsigned `now - prev` mod 2^32 w `cap_math`), dopiero potem `cap_ticks_to_us(elapsed)` → porównanie z `edge_timeout_us`. To jest odejmowanie w domenie ticków capture, NIE odejmowanie µs ani dwóch epok. Poprzedni `abs_diff_u32(now_us, last_edge_us)` usunięty z recency.
- **Kontrakt epoch jawny w nagłówku:** `rc_validity.h` ma blok CONTRACT (epoch): `now_ticks` i `last_edge_ticks` w tej samej domenie MCPWM 32-bit (12,5 ns/tick), poprawność po starcie i przez każdy wrap ~53,6 s, **jawny zakaz `esp_timer_get_time()`** (inny epoch, nie zawija na 2^32). `rc_sample.h` lustrzanie dokumentuje pole jako raw tick z domeny MCPWM, "now" z tej samej domeny.
- **Spójność sygnatury:** `channel_valid(sample, now_ticks, cfg)` zmieniony jednolicie — definicja, deklaracja, wewnętrzny `edge_recent`, oraz WSZYSTKIE wywołania w testach (`grep` potwierdza brak pozostałości `now_us`/`last_edge_us` w `components/` i `main/`). Brak innych konsumentów `last_edge_*` w drzewie.
- **Testy wrapu sensowne (nie assertion-free):** `test_recency_correct_across_counter_wrap` — `last_edge_ticks` tuż pod 2^32, `now_ticks` tuż po wrapie, elapsed = 1000 µs ≤ 30 ms → `TEST_ASSERT_TRUE`. `test_stale_edge_across_wrap_is_invalid` — ta sama geometria, edge 100 ms stary → `TEST_ASSERT_FALSE`. Geometria realnie przechodzi przez `prev > now` (modular), naiwne odejmowanie w µs dałoby zły wynik — test faktycznie różnicuje naprawę od regresji.

### P2-2 [TEST] — granice inclusive width/period: ✅ ROZWIĄZANE

- `test_width_at_min_boundary_is_valid` (== `width_min_us`) → true; `test_width_at_max_boundary_is_valid` (== `width_max_us`) → true.
- `test_period_at_upper_tolerance_boundary_is_valid` (== `expected + tol`) → true; `test_period_at_lower_tolerance_boundary_is_valid` (== `expected - tol`) → true.
- `test_period_one_us_past_tolerance_is_invalid` (== `expected + tol + 1`) → false. Przypadek "o 1 poza granicą → invalid" pokryty. Wszystkie z realnymi asercjami; używają `CFG.*` (nie magic numbers).

### Regresje: BRAK

- **Konsumenci `last_edge_ticks`:** zmiana nazwy pola nie zepsuła `rc_capture.c` (period liczony osobno z `rising_edge_ticks` przez `cap_ticks_elapsed`+`cap_ticks_to_us`, niezależnie od pola sample) ani `cap_math` (API bez zmian). Brak innych odczytów pól sample poza rc_capture/rc_validity.
- **Separacja pure⊥HAL / circular:** `rc_validity` REQUIRES `rc_capture` jednokierunkowo (CMake potwierdza; dla `rc_sample.h` + teraz `cap_math.h`); `rc_capture` REQUIRES tylko `esp_driver_mcpwm`, nie zna `rc_validity` → brak circular. `rc_validity` nadal pure (host-link), `cap_math` to pure integer math bez IDF.
- **Limity:** rc_validity.c 81 linii, rc_validity.h 100, rc_sample.h 53, test 261 — wszystkie < 300; funkcje < 50; nesting ≤ 2.
- **Asercje:** żadna istniejąca asercja nie osłabiona; tylko `NOW_US`→`NOW_TICKS` (+ przeliczenie przez `TICKS_PER_US=80`) i nowe testy dodane.

### P3 — bez zmian

P3-1..P3-5 świadomie poza zakresem cyklu (torn read / nvs_error / długość `validate_fields` / konwencja nazw / deinit). Pozostają jako dług do odpowiednich Unitów. Nie blokują.

### Wniosek re-review

Oba P2 rozwiązane realnie (nie kosmetycznie): P2-1 to poprawna naprawa korzenia (domena ticków + modular subtraction + jawny kontrakt epoch), P2-2 domyka lukę off-by-one z testami granic włącznie i przypadkiem o 1 poza. Zero regresji, separacja warstw i brak circular utrzymane, gate'y zielone na żywo (49/49, build EXIT=0). **Gate: CZYSTE — Faza 1 zamknięta, gotowość do Fazy 2.**
