# Code Review — Faza 2 (Unit 5: Łańcuchy gazu i serwa, pure)

**Commit:** `c908fd7` — feat(kayak-motor-firmware): Faza 2 — łańcuchy gazu i serwa + rampa/slew (pure)
**Data review:** 2026-06-16
**Zakres:** `components/signal_chain/*` + testy hosta `test_throttle_chain.c`, `test_servo_chain.c`, `test_ramp.c`
**Metodologia:** dev-docs-review (5 perspektyw), cross-reference z planem Unit 5.

---

## Severity gate: ⚠️ ZASTRZEŻENIA (KONTYNUUJ Z ZASTRZEŻENIAMI)

| Severity | Liczba |
|----------|--------|
| 🔴 P1 blocking | 0 |
| 🟠 P2 important | 2 |
| 🟡 P3 nit | 4 |

**Typy findingów:** KOD: 2 (P2) + 3 (P3); TEST: 1 (P3). E2E: **N/A** (brak UI, Unit 5 w pełni pure).

**Weryfikacja na żywo:**
- Testy hosta: `./test/host/run.sh` → **81 Tests, 0 Failures, 0 Ignored — OK** ✅
- Build: `. "$HOME/esp/idf-env.sh" && idf.py set-target esp32 && idf.py build` → **EXIT=0**, bin 0x30af0 B (87% partycji wolne) ✅

---

## Ocena pozytywna (spełnione wymagania zakresu)

- **Łańcuch gazu** (`throttle_chain.c`): normalizacja → deadband → reverse (`shape_command`) → limit mocy → resolve_target (override) → rampa → map ESC → **clamp**. Kolejność zgodna ze źródłem i planem.
- **Łańcuch serwa** (`servo_chain.c`): normalizacja → deadband → reverse → endpointy (map) → override (FAILSAFE→center) → slew → **clamp**. Zgodne.
- **SI-3 bezpieczeństwo:** KAŻDY łańcuch kończy `clamp_pwm_us` jako bezwarunkowy ostatni krok (`throttle_chain.c:76`, `servo_chain.c:56`). Żaden wynik nie omija clampu. Potwierdzone testami `test_output_never_exceeds_clamp_window`, `test_output_stays_within_clamp_window`.
- **R6 soft-stop/centrowanie:** override działa na TARGET, nie na wyjściu. FAILSAFE→target 0 (gaz) / center (serwo) realizowane PRZEZ rampę/slew (nie skok) — udowodnione testami `test_failsafe_override_soft_stops_to_neutral` i `test_failsafe_slews_to_center` (asercja "po jednym cyklu wciąż off-neutral").
- **Rampa/slew:** osobne tempa ↑/↓ gazu (`ramp_step`), symetryczny slew serwa (`slew_step`), krok per cykl deterministyczny, nigdy nie przeskakuje targetu (clamp do dystansu w `step_up`/`step_down`).
- **Architektura:** PURE — zero include ESP-IDF/HAL (zweryfikowano grepem; tylko `stdint`, `stdbool`, `safety_clamp.h`, `settings_model.h`). Współdzielony `chain_math` bez duplikacji normalize/deadband/reverse/map. Zero circular deps. Wszystkie pliki <300 linii (max 90), funkcje <50 (max ~15), nesting ≤2, named constants.
- **Matematyka:** `scale_half` używa int64 (brak overflow/UB), rounding symetryczny dla ujemnych delt (`scale_half(-7,500)=-14` == `-scale_half(7,500)`), deadband inclusive (`magnitude <= deadband`), normalizacja off-center skaluje dwie połowy niezależnie. Brak dzielenia przez zero: jedyny dzielnik `/ span` w `scale_half`; w `normalize_us` span gwarantowany >0 przez walidator (strict `rc_min<rc_mid<rc_max`), w `deadband_us_to_normalized` jest guard `if (span<=0) return 0`, a `map_normalized_to_us` nie dzieli przez span kalibracji (dzieli przez stałą FULL_SCALE).
- **Test coverage:** wszystkie scenariusze checklisty Unit 5 pokryte realnymi asercjami; zero testów assertion-free; granica deadbandu przetestowana inclusive (`test_deadband_at_threshold_is_inclusive_neutral`).

---

## Findingi

### 🟠 [P2-important] — KOD

**P2-1 — `components/chain_math.c:59` (vs `:32-37`) — asymetria deadbandu przy off-center mid.**
`deadband_us_to_normalized` zwija deadband do BLIŻSZEJ (min) połowy zakresu, ale `normalize_us` skaluje każdą połowę NIEZALEŻNIE. Przy off-center kalibracji efektywny deadband w µs różni się między połowami drążka.
Przykład: `rc_min/mid/max = 1000/1300/2000`, `throttle_deadband_us = 80` → low_span=300, high_span=700, próg liczony z min=300 → znormalizowany deadband = 267. Drążek po niskiej stronie wychodzi z deadbandu po **81 µs**, po wysokiej stronie dopiero po **188 µs** (2,3× asymetria). Nagłówek (`chain_math.h:53-65`) wspomina "relative to the nearer half-range" jako intencję, ale per-side asymetria jest nieoczywista i **żaden test jej nie pokrywa** (wszystkie testy używają wyśrodkowanego 1000/1500/2000). Domenowo akceptowalne dla domyślnej kalibracji symetrycznej, ale dla strojonej off-center daje niespójny deadband. Rekomendacja: liczyć deadband per-połowa (spójnie z `normalize_us`) albo udokumentować jako świadomą decyzję z uzasadnieniem.

**P2-2 — `components/settings/src/settings_validate.c:107-120` — brak cross-field invariantu `esc_reverse_max < esc_neutral < esc_forward_max`.**
Walidator sprawdza tylko `esc_forward_min ≤ esc_forward_max` i `esc_reverse_min ≥ esc_reverse_max`, ale NIE waliduje `esc_neutral` względem maxów forward/reverse. Konfiguracja `esc_neutral=1600` (max dozwolony), `esc_forward_max=1000` przechodzi walidację per-field i cross-field, a w `map_normalized_to_us` (`chain_math.c:77`) daje UJEMNY span → full-forward mapuje się PONIŻEJ neutralu (odwrócone mapowanie). Hard clamp utrzymuje wyjście w [1000,2000], więc to NIE jest niebezpieczne (output nie wychodzi poza okno), ale mapowanie jest cicho błędne. Brak testu. Uwaga: to dotyka warstwy `settings` (Unit 4) ujawnionej przez chain math; domknąć w Unit 5/8 dodając invariant.

### 🟡 [P3-nit]

**P3-1 — `components/signal_chain.h:58,74` — niespójna numeracja kroków.** Nagłówek mówi "Steps 3-10" dla obu łańcuchów, ale inline numeracja w `.c` kończy na "Step 9" + nienumerowany clamp, a enum-doc (`signal_chain.h:33,45`) odwołuje się do "step 7". Mylące, nie niebezpieczne — ujednolicić.

**P3-2 — `components/signal_chain.h:80-83` + `servo_chain.c:51` — kontrakt seed `slew_state` (µs) vs `ramp_state` (znormalizowane) tylko w prozie.** Oba out-paramy to `int32_t*` o różnych jednostkach. Testy seedują `slew = servo_center_us` (poprawnie), ale jeśli integrator (Unit 7) zaseeduje serwo na 0 (jak gaz), wystąpi startowy transjent slewujący od 0 do ~center. Udokumentować wymaganą wartość seed (center) w nagłówku przed integracją Unit 7.

**P3-3 — `throttle_chain.c:11-12` + `servo_chain.c:10-11` — zduplikowana stała okna SI-3 `1000/2000` w dwóch plikach.** Named constants (reguła spełniona), ale to samo okno zdefiniowane dwukrotnie; przy zmianie pasma dwie edycje. Niskie ryzyko.

**P3-4 — TEST — `test_throttle_chain.c` — brak testów off-center rc_mid.** Asymetrie z P2-1 (deadband), reverse (równe offsety µs po obu stronach dają różne magnitudy po negacji) oraz osiągania limitu mocy przy off-center mid są niepokryte. Dodać test z `rc_mid=1300` weryfikujący per-side zachowanie. (Drobny duplikat: `test_deadband_small_signal_maps_to_neutral` i `test_deadband_at_threshold_is_inclusive_neutral` oba używają 1580 µs.)

---

## Odrzucone findingi (false / nieaktualne)

- **"Odwrócone asercje clampu lower-bound"** (`test_throttle_chain.c:220`, `test_servo_chain.c:169`) — ZWERYFIKOWANE jako FALSE. Sygnatura Unity to `TEST_ASSERT_GREATER_OR_EQUAL_UINT32(threshold, actual)` → asercja `actual >= threshold`. Zapis `(1000U, esc_us)` poprawnie sprawdza `esc_us >= 1000`. Dolna granica clampu JEST testowana poprawnie (potwierdzone w `esp-idf/components/unity/unity_internals.h:943`).
- Diagnostyka clang IDE (-mlongcalls, *.h not found, settings_params unknown, mach-o przy IRAM_ATTR) — false positives per kontekst środowiska; autorytatywny realny build (EXIT=0) + 81/81 testy.

---

## Odchylenia od planu (docs/plans/ — Unit 5)

- **Lokalizacja testów:** Plan (linia 406) definiował `components/signal_chain/test/test_*.c`. Faktycznie testy są w `test/host/` (`test_throttle_chain.c`, `test_servo_chain.c`, `test_ramp.c`) — spójne z konwencją harnessu hosta ustaloną w Fazach 0–1. Wszystkie 3 zdefiniowane pliki testowe ISTNIEJĄ i zawierają asercje → nie jest to brak (zob. SKILL: brakujący plik testowy = P2; tu pliki istnieją, tylko inny katalog). Świadome odchylenie, akceptowalne.
- **Wszystkie pliki implementacji z planu** (`signal_chain.h`, `throttle_chain.c`, `servo_chain.c`, `ramp.c/.h`) utworzone. Dodatkowo wydzielono `chain_math.c/.h` (współdzielona logika pure) — zgodne z duchem "bez duplikacji", nie było jawnie w planie, ale uzasadnione.
- **Kolejność stała ze źródła** (deadband przed skalowaniem/rampą, reverse po deadbandzie, limit przed rampą, override na target) — w pełni zachowana.

---

## Pliki objęte review

- `components/signal_chain/include/signal_chain.h`
- `components/signal_chain/src/chain_math.c`, `chain_math.h`
- `components/signal_chain/src/ramp.c`, `ramp.h`
- `components/signal_chain/src/throttle_chain.c`
- `components/signal_chain/src/servo_chain.c`
- `components/signal_chain/CMakeLists.txt`
- `test/host/test_throttle_chain.c`, `test_servo_chain.c`, `test_ramp.c`, `test_main.c`, `CMakeLists.txt`

---

## Re-review po cyklu 1

**Commit naprawczy:** `ceb99bc` — fix(kayak-motor-firmware): poprawki po review fazy 2 (cykl 1)
**Diff:** `c908fd7..ceb99bc` (8 plików, +125/-21)
**Data re-review:** 2026-06-16

### Severity gate: ✅ CZYSTE (KONTYNUUJ)

| Severity | Przed | Po cyklu 1 |
|----------|-------|------------|
| 🔴 P1 blocking | 0 | 0 |
| 🟠 P2 important | 2 | **0** |
| 🟡 P3 nit | 4 | 4 (nieblokujące, niezaadresowane — OK) |

### Weryfikacja na żywo

- Testy hosta: `./test/host/run.sh` → **83 Tests, 0 Failures, 0 Ignored — OK** ✅ (było 81, +2 nowe testy regresyjne)
- Build: `. "$HOME/esp/idf-env.sh" && idf.py set-target esp32 && idf.py build` → **EXIT=0**, bin 0x30af0 B (87% partycji wolne) ✅

### P2-1 — asymetria deadbandu off-center → ✅ ROZWIĄZANE

- `deadband_us_to_normalized` (brał BLIŻSZĄ połowę dla obu stron) zastąpione `shape_deadband(value, deadband_us, min, mid, max)` w `chain_math.c:66-77`. Funkcja wybiera połowę zgodną ze ZNAKIEM `value` (`value<0 → mid-min`, inaczej `max-mid`) i konwertuje próg w TEJ SAMEJ domenie, w której `normalize_us` skalował tę stronę (`scale_half` z tym samym spanem). Guard `span<=0 → 0` zachowany w wydzielonym `deadband_threshold_for_span`.
- Domena spójna: zarówno `normalize_us` (`:32-37`) jak i próg deadbandu używają `scale_half(delta, halfSpan)` z FULL_SCALE=1000 → próg i wartość żyją w identycznym układzie. Martwy obszar = stałe 80 µs deflekcji po OBU stronach.
- Oba łańcuchy zmigrowane (`throttle_chain.c:22-25`, `servo_chain.c:22-25`); `apply_deadband` nadal robi inclusive (`magnitude <= threshold`).
- Test `test_offcenter_deadband_is_consistent_both_sides` (1000/1300/2000, deadband 80) REALNIE potwierdza symetrię: mid±80 (1220 i 1380) → neutral na obu stronach; 1219 → reverse (poniżej neutralu), 1381 → forward (powyżej). Sprawdza zarówno próg jak i kierunek wyjścia. Asercje konkretne (`EQUAL_UINT32`, `LESS_THAN`, `GREATER_THAN`), nie osłabione.

### P2-2 — brak invariantu monotoniczności ESC → ✅ ROZWIĄZANE

- Nowy `validate_esc_map_monotonic` (`settings_validate.c:91-106`) egzekwuje `esc_reverse_max_us < esc_neutral_us < esc_forward_max_us`; przy naruszeniu przywraca CAŁĄ trójkę z defaults (1100/1500/1900) → mapa zawsze monotoniczna. Wpięty w `validate_cross_fields` (`:143`) → wynik `MIXED_RECOVERED` + `defaults_used` przez istniejący mechanizm `repaired`.
- Pola pokrywają DOKŁADNIE te użyte w `map_normalized_to_us` z `throttle_chain.c:57-59` (`esc_reverse_max_us` = -full, `esc_neutral_us` = center, `esc_forward_max_us` = +full). Invariant trafny względem realnego użycia.
- Test `test_cross_field_esc_map_inverted_rejected` realny: neutral=1600 (max dozwolony) > forward_max=1550, per-field i forward-band check (1550≤1550) przechodzą → wyłącznie nowy invariant fires; asercje na `MIXED_RECOVERED`, `defaults_used`, oraz odtworzoną monotoniczność. Nie short-circuituje na wcześniejszych checkach (zweryfikowane).

### Regresje: BRAK

- Wcześniejsze testy deadband/reverse/limit/clamp/failsafe — wszystkie PASS (`test_deadband_*`, `test_reverse_*`, `test_power_limit_*`, `test_failsafe_override_soft_stops_to_neutral`, `test_output_never_exceeds_clamp_window`).
- SI-3: `clamp_pwm_us` nadal bezwarunkowy ostatni krok w obu łańcuchach (`throttle_chain.c:75`, `servo_chain.c`). Niezmienione przez diff.
- FAILSAFE soft-stop bez skoku: `resolve_target`→0 + `ramp_step`/`slew_step` niezmienione — ścieżka override→ramp nietknięta.
- Ekstrakcja `validate_esc_map_monotonic` nie złamała innych cross-field (RC monotonic, servo, forward/reverse band) — wszystkie checki zachowane, nowy wywołany na końcu.
- Limity: `chain_math.c` 93 linii, `settings_validate.c` 191 linii (<300); `shape_deadband` 12 linii, `validate_esc_map_monotonic` 14 linii (<50). Nesting ≤2.
- Brak osłabionych asercji, brak assertion-free testów, brak nowych `any`/secrets/console.

### Status findingów

- [x] P2-1 (KOD) — asymetria deadbandu off-center → ROZWIĄZANE
- [x] P2-2 (KOD) — invariant monotoniczności ESC → ROZWIĄZANE
- [ ] P3-1..P3-4 — nieblokujące, świadomie nieadresowane w tym cyklu (dopuszczalne)
