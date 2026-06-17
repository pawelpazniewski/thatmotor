# Code Review — Faza 0 (Fundament i bezpieczne wyjście)

**Commit:** `866555e` — feat(kayak-motor-firmware): Faza 0 — szkielet ESP-IDF + harness hosta + LEDC out + hard clamp
**Data review:** 2026-06-16
**Zakres:** Unit 1 (szkielet ESP-IDF + harness hosta + sdkconfig) + Unit 2 (LEDC out + hard clamp SI-3 + bezpieczny boot SI-1)
**Metodologia:** 4 agenty review równolegle (security, architecture/quality, test coverage, performance). Agent E2E = N/A (brak UI w Fazie 0).

---

## Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI

- 🔴 **P1 (blocking): 0**
- 🟠 **P2 (important): 2**
- 🟡 **P3 (nit): 5**

**Typy findingów:** KOD = 1 · TEST = 1 · E2E = 0
**Wynik E2E:** N/A (brak UI/panelu WWW w Fazie 0 — pierwszy UI dopiero w Unit 10, Faza 6)

## Walidacja środowiska (autorytatywna, nie LSP)

- Testy hosta (`./test/host/run.sh`): **10 Tests / 0 Failures / 0 Ignored — OK** (zweryfikowane w tym review).
- Build firmware (`idf.py build`) + testy hosta: przeszły w fazie execute (potwierdzone w briefie).
- Diagnostyka LSP clang (esp_err.h not found, uint32_t unknown itd.) = false positives ze standalone LSP — pominięte zgodnie z briefem.
- Checkboxy [HW] (oscyloskop/ESP32/WP880) i [E2E] (przeglądarka) = ODROCZONE do known-issues (brak sprzętu — decyzja użytkownika). NIE liczone jako findingi.

---

## Findingi

### 🟠 [P2-important] — KOD: `safety_clamp.c:7` — `assert(min<=max)` jako jedyna ochrona inwariantu, no-op w release

`clamp_pwm_us` chroni inwariant `window.min_us <= window.max_us` wyłącznie przez `assert()`. W buildzie release ESP-IDF (`NDEBUG`) `assert` kompiluje się do no-op. Gdyby okno z odwróconymi granicami (`min > max`) trafiło do clampa, funkcja zwróci wartość POZA zamierzonym zakresem bezpieczeństwa — czyli hard clamp (SI-3, „last line of defence") cicho zawiedzie.

- Header `safety_clamp.h:14` wprost obiecuje „validated by clamp_pwm_us via fail-fast assert" — w release to nieprawda.
- W Fazie 0 realnego naruszenia NIE ma: jedyne okno (`PWM_OUT_WINDOW`) to kompilowana stała `900 <= 2100`.
- Ryzyko staje się realne w **Unit 8 (NVS)** i **Unit 9 (ESC calibration)**, gdzie okna mogą pochodzić z danych niezaufanych (NVS/kalibracja) — reguła bezpieczeństwa nr 9 (waliduj każdy input na granicy).

**Rekomendacja:** zamienić assert na bezwarunkowy fail-safe niezależny od `NDEBUG` (np. deterministyczna normalizacja okna w samej funkcji albo własny `SAFETY_REQUIRE`/`abort()` aktywny w release). Minimalnie — przed wprowadzeniem okien z NVS/kalibracji. Do czasu naprawy: zaktualizować komentarz headera, by nie obiecywał ochrony, której w release nie ma.

### 🟠 [P2-important] — TEST: `pwm_out.c:68-82` — realny code-path `pwm_out_write_us` (walidacja kanału + clamp-before-convert) nieprzetestowany

`pwm_out.c` linkuje `driver/ledc.h`, więc nie jest w `PURE_SOURCES` (`test/host/CMakeLists.txt:17-20`) i nie ma żadnego testu hosta. Nieprzetestowane:

- Guard walidacji kanału: `channel < 0 || channel >= PWM_OUT_CHANNEL_COUNT → ESP_ERR_INVALID_ARG` (czysta logika, error case bez pokrycia).
- Kluczowy wymóg SI-3 „brak code-path do LEDC z pominięciem clamp" jest weryfikowany tylko POŚREDNIO — testy `*_clamped_first_then_converted` komponują `clamp` + `pwm_us_to_duty` ręcznie, nie dowodzą że produkcyjna `pwm_out_write_us` faktycznie woła clamp przed konwersją.

Reguła coding-rules §2: każda nowa funkcja publiczna = min. 1 happy path + 1 error case. `pwm_out_write_us` ma 0.

**Rekomendacja:** wyekstrahować czystą logikę do funkcji pure (np. `pwm_out_resolve(channel, value_us, *out_duty) -> esp_err_t`), dodać do `PURE_SOURCES` i pokryć: happy path (poprawny kanał → clamp+convert → spodziewane duty, w tym dowód clamp-before-convert na realnej funkcji) + error case (kanał poza zakresem → `ESP_ERR_INVALID_ARG`). Jeśli ekstrakcja świadomie odkładana — zapisać w known-issues z uzasadnieniem.

### 🟡 [P3-nit] — `pwm_out.c:70` — tautologiczne `channel < 0` dla typu enum

`PwmOutChannel` to enum bez wartości ujemnych; przy unsigned underlying type `channel < 0` może być zawsze-fałsz (potencjalny `-Wtype-limits`/`-Wtautological-compare`). Walidacja górnej granicy jest wystarczająca. Lekko ociera się o anty-pattern #10 (defensive code na scenariusz który nie może wystąpić). Rekomendacja: usunąć dolne porównanie albo rzutować na `unsigned`. (Zgłoszone przez security + architecture.)

### 🟡 [P3-nit] — `pwm_us_to_duty.c:8` — komentarz „fits in uint64_t (max ~ 20000 * 65536)" niedoszacowany

Liczba `20000` w uzasadnieniu braku overflow jest arbitralna — funkcja przyjmuje dowolny `uint32_t` i jawnie nie limituje zakresu. Faktyczny worst-case `UINT32_MAX * 65536 ≈ 2.8e14` wciąż mieści się w uint64_t (brak realnego overflow), ale komentarz powinien odwoływać się do `UINT32_MAX`, nie do okresu PWM. Kwestia poprawności komentarza, nie wydajności.

### 🟡 [P3-nit] — `pwm_out.c:20,25` — niespójny prefix `CHANNEL_MAP`/`GPIO_MAP` vs `PWM_OUT_*`

Statyczne mapy bez prefixu komponentu, podczas gdy reszta stałych pliku ma `PWM_OUT_`. UPPER_SNAKE_CASE poprawny — czysto kosmetyczna niespójność. Rekomendacja: `PWM_OUT_CHANNEL_MAP`/`PWM_OUT_GPIO_MAP`.

### 🟡 [P3-nit] — `sdkconfig.defaults:18` — `CONFIG_HTTPD_WS_SUPPORT=y` przedwczesny w Fazie 0

WS support włączony „dla Unit 10" — w Fazie 0 nie ma WiFi/HTTP, więc opcja jest martwa (anty-pattern: config dla nieistniejącej funkcjonalności). Nie jest luką teraz. Uwaga: zadania.md:18 jawnie wymaga tego flagu w Unit 1, więc to świadoma decyzja zgodna z planem — pozostawione jako nit do rozważenia (minimalizacja attack surface buildu).

### 🟡 [P3-nit] — Typy PascalCase (`PwmWindow`, `PwmOutChannel`) w bazie snake_case C

Reguła §7 mówi „typy PascalCase", ale konwencja ESP-IDF/C dla `typedef struct` to zwykle `snake_case_t`. To świadomy wybór zgodny z literą reguł projektu — do potwierdzenia spójności w kolejnych fazach (rc_capture, state_machine trzymają tę samą konwencję).

---

## Weryfikacja kryteriów bezpieczeństwa fazy (PASS)

- **SI-3 (hard clamp bezwarunkowy):** ✅ `pwm_out_write_us` jest jedyną drogą do `ledc_set_duty`/`ledc_update_duty` i ZAWSZE woła `clamp_pwm_us` przed konwersją. Grep całego repo: żaden inny komponent nie linkuje/woła LEDC; tylko `pwm_out` ma `REQUIRES esp_driver_ledc`. Brak gałęzi bypassującej clamp. (Zastrzeżenie do twardości inwariantu okna — P2 wyżej.)
- **SI-1 (boot do bezpiecznego stanu):** ✅ `app_main` woła `enter_safe_outputs()` jako pierwszą akcję funkcjonalną (przed RC/state-machine/web), `ESP_ERROR_CHECK` = fail-fast na błędzie LEDC. Neutral 1500 µs na ESC i serwo.
- **sdkconfig (TWDT panic + brownout):** ✅ `ESP_TASK_WDT_EN=y` + `ESP_TASK_WDT_PANIC=y` (timeout 5s) + `ESP_BROWNOUT_DET=y` (LVL_SEL_7). Zawieszona pętla → reset → ponowny boot-to-safe.
- **Brak hardcoded secrets:** ✅ (WiFi nieobecne w tej fazie).
- **Izolacja partycji:** ✅ `appcfg` (NVS aplikacji) rozdzielona od systemowego `nvs`.

## Weryfikacja architektury (PASS)

- **Separacja pure ⊥ HAL:** ✅ `safety_clamp.c` + `pwm_us_to_duty.c` to pure (tylko `<stdint.h>`/`<assert.h>` — zero nagłówków IDF), budowalne na hoście. `pwm_out.c` to jedyny plik dotykający `driver/ledc.h`.
- **Tylko `pwm_out` linkuje LEDC:** ✅ audyt wszystkich 10 `components/*/CMakeLists.txt` — `esp_driver_ledc` tylko w `pwm_out`.
- **Zero circular deps:** ✅ graf `app_main → pwm_out → safety_clamp` + `pwm_out → pwm_us_to_duty`, `safety_clamp` jest liściem.
- **Rozmiary:** ✅ największy plik `pwm_out.c` = 82 linie (<300); najdłuższa funkcja `pwm_out_init` ~22 linie (<50); max 2 argumenty; nesting ≤1 (early return).
- **Magic numbers:** ✅ GPIO/kanały/timer wyciągnięte do named constants; `PWM_PERIOD_US`/`PWM_DUTY_MAX` derived (jeden punkt prawdy).

## Test coverage (PASS dla scenariuszy planu)

Wszystkie 10 wymaganych scenariuszy Unit 2 pokryte testem z mocną asercją (`TEST_ASSERT_EQUAL_UINT32` z konkretną wartością, AAA, zero assertion-free):

- `clamp_pwm_us`: <min→min, >max→max, w oknie→bez zmian, granica min inclusive, granica max inclusive.
- `pwm_us_to_duty`: 1000→3277, 1500→4915, 2000→6554.
- clamp-przed-konwersją: >max (5000→6554) i <min (200→3277).

Gap: realny code-path `pwm_out_write_us` (P2 wyżej).

## Performance (PASS)

Brak realnych problemów. Hot-path (`pwm_out_write_us`/`pwm_us_to_duty`/`clamp_pwm_us`) jest alokacja-free, bez O(n²), bez blocking. 64-bit dzielenie przez compile-time stałą (`PWM_PERIOD_US`) zostanie zoptymalizowane przez kompilator; przy 100 wywołań/s (~50 Hz × 2 kanały) i tak nieistotne. Krytyczne dla wydajności elementy (pętla 50 Hz, jitter, watchdog) przyjdą w Unit 7.

---

## Odchylenia od planu technicznego

- **Lokalizacja testów hosta:** plan techniczny (`docs/plans/...plan.md:260,265`) wymienia `components/safety_clamp/test/test_safety_clamp.c` oraz `components/pwm_out/test/test_pwm_us_to_duty.c` (kolokacja). Implementacja umieściła testy w `test/host/`. **NIE jest to naruszenie:** `kontekst.md:14` i `zadania.md:21` jawnie definiują harness jako `test/host/CMakeLists.txt` (standalone Unity / target linux, budowalny bez IDF). To świadoma, udokumentowana decyzja architektoniczna — pure sources są budowane przez ten jeden harness. Katalogi `components/*/test/` nie istnieją i nie są wymagane przez tę realizację.
- **Matematyka konwersji µs→duty:** plan sugerował `duty = (us << 16) / 20000` (floor). Implementacja używa round-to-nearest `(value_us·65536 + 10000)/20000`. To ulepszenie (dokładniejsze), nie regresja — wartości brzegowe 1000/1500/2000 → 3277/4915/6554 zgodne z kontraktem; overflow bezpieczny.

---

## Podsumowanie

Faza 0 jest solidnym fundamentem bezpieczeństwa. Kluczowe inwarianty bezpieczeństwa (SI-1 boot-to-safe, SI-3 niebypassowalny hard clamp, izolacja LEDC do jednego komponentu) są spełnione na poziomie source. Separacja pure ⊥ HAL czysta, pokrycie testowe wymaganych scenariuszy 100%, build + testy hosta przechodzą.

Dwa P2 do domknięcia (najpóźniej przed Unit 8/9 i Unit 7 odpowiednio): (1) twardość egzekwowania inwariantu okna w `clamp_pwm_us` zanim wejdą okna z NVS/kalibracji; (2) test realnego code-path `pwm_out_write_us` (walidacja kanału + dowód clamp-before-convert), najlepiej przez ekstrakcję czystej logiki do `PURE_SOURCES`. Żaden P2 nie blokuje rozpoczęcia Fazy 1 (Unit 3/4 są niezależne od tych ścieżek), ale powinny zostać naprawione w obrębie zadania.

---

## Re-review po cyklu 1

**Commit fix:** `c0cbd54` — fix(kayak-motor-firmware): poprawki po review fazy 0 (cykl 1)
**Data re-review:** 2026-06-16
**Bazowy diff:** `git diff 866555e c0cbd54`

### Severity gate: ✅ CZYSTE — P1=0, P2=0, P3=5 (pozostałe nity bez zmian, niewymagane do zamknięcia fazy)

### Weryfikacja na żywo (autorytatywna)

- Testy hosta (`./test/host/run.sh`): **17 Tests / 0 Failures / 0 Ignored — OK** (było 10 → +7 nowych testów: 3× odwrócone okno, 4× `pwm_out_resolve_duty`).
- Build firmware (`idf.py set-target esp32 && idf.py build`): **EXIT=0**, obraz `kayak-motor-firmware.bin` wygenerowany (0x2e790 B, 88% wolnej partycji). `_Static_assert` synchronizujący kanały przeszedł kompilację.

### P2 #1 — `safety_clamp.c` assert no-op → ROZWIĄZANY ✅

- `#include <assert.h>` usunięty, `assert(min<=max)` zastąpiony deterministyczną normalizacją okna (swap `lo/hi` gdy `min_us > max_us`) — fail-safe niezależny od `NDEBUG`. Brak `abort()`/paniki, funkcja zawsze zwraca wartość w znormalizowanym zakresie.
- Header `safety_clamp.h` zaktualizowany: usunięta nieprawdziwa obietnica „fail-fast assert"; kontrakt opisuje normalizację w każdym buildzie (w tym release). Zgodny z zachowaniem.
- Testy odwróconego okna: 3 sensowne, niepustych asercji (`{min:2000,max:200}` → clamp high do 2000, clamp low do 200, wartość w środku 1000 bez zmian). Mocne `TEST_ASSERT_EQUAL_UINT32` z konkretnymi wartościami — nie assertion-free, nie osłabione. Pokrywają wszystkie 3 gałęzie po normalizacji.

### P2 #2 — `pwm_out_write_us` nieprzetestowany → ROZWIĄZANY ✅

- Czysta logika wyekstrahowana do `pwm_out_resolve_duty(channel, value_us, window, *out_duty)` w nowym pure module `pwm_out_logic.c/.h` (zero nagłówków IDF — zweryfikowane grepem), dodanym do `PURE_SOURCES`.
- Produkcyjne `pwm_out_write_us` realnie deleguje do `pwm_out_resolve_duty` (walidacja kanału + clamp przed konwersją) i tylko forwarduje wynikowe `duty` do LEDC — brak gałęzi bypassującej clamp na produkcyjnej ścieżce. Dowód SI-3 „clamp przed konwersją" jest teraz na realnej funkcji, nie ręcznie komponowany.
- Pokrycie: happy path (kanał 0, 1500µs → 4915) + 2× error case (kanał == COUNT i kanał -1 → `PWM_OUT_LOGIC_INVALID_CHANNEL`) + dowód clamp-before-convert (kanał 1, 9000µs → clamp do 2100µs → 6881, nie wyższe duty). Stałe duty zweryfikowane matematycznie (round-to-nearest). Spełnia coding-rules §2 (happy + error case).
- `_Static_assert(PWM_OUT_LOGIC_CHANNEL_COUNT == PWM_OUT_CHANNEL_COUNT)` w `pwm_out.c` zabezpiecza zduplikowaną stałą przed dryfem (uzasadniona duplikacja — pure module nie może linkować LEDC-zależnego `pwm_out.h`).

### Regresje: BRAK

- SI-3 (niebypassowalny clamp): utrzymany — jedyna droga do LEDC nadal przez `clamp_pwm_us` przed konwersją, teraz dodatkowo dowiedziony testem produkcyjnej ścieżki.
- SI-1 (boot-to-safe): nietknięty (`app_main`/`enter_safe_outputs` bez zmian w diffie).
- Izolacja LEDC: tylko `pwm_out` linkuje `esp_driver_ledc`/`driver/ledc.h` (zweryfikowane grepem po fixie).
- Separacja pure ⊥ HAL: `safety_clamp.c`, `pwm_us_to_duty.c`, nowy `pwm_out_logic.c` — zero nagłówków IDF; budowalne na hoście (grep czysty).
- Limity plików/funkcji: największy plik nadal `pwm_out.c` = 87 linii (<300); `pwm_out_resolve_duty` = ~9 linii; nowe pliki małe. OK.
- Asercje: zero osłabień, zero assertion-free — każdy z 7 nowych testów ma mocną asercję z konkretną wartością.

### Pozostałe P3 (bez zmian, niewymagane)

5 nitów z pierwszego review pozostaje otwartych (tautologiczne `channel < 0` — uwaga: przeniesione do `pwm_out_logic.c`, ale `channel` jest tam `int`, więc porównanie NIE jest już tautologiczne i guard jest sensowny; komentarz overflow; prefix `CHANNEL_MAP`/`GPIO_MAP`; `HTTPD_WS_SUPPORT`; konwencja PascalCase). Nie blokują fazy.

### Wniosek re-review

Oba P2 realnie naprawione, bez regresji, z mocnym pokryciem testowym (17/17 PASS) i czystym buildem ESP32. Faza 0 domknięta — gate CZYSTE.
