# Code Review — Faza 1: Kanał celu z aplikacji (goto-waypoint-navigation)

Commit: `4e0b4c0` — feat(goto): kanał celu z aplikacji — walidacja + transport (Faza 1)
Data review: 2026-07-01
Reviewerzy: security-sentinel, architecture-strategist, test-coverage/scenario (3 agenty równolegle)

---

## RE-REVIEW (cykl fix 1) — 2026-07-01

Commit fixa: `ffbc43a` — fix(goto): walidacja celu w domenie double przed castem.

### Severity gate: ✅ CZYSTE
Liczniki po fixie: **P1=0, P2=0, P3=2** (przeniesione, zaakceptowane nity — bez zmian).

### Walidacja
- Host-tests (`./test/host/run.sh`): **405 Tests, 0 Failures, 0 Ignored — PASS** (było 400; +5 nowych oracle-testów `goto_target_from_double`).
- `idf.py build` (esp32s3): **PASS** (green, 37% wolnej partycji app).

### Poprzedni P1 — ✅ ROZWIĄZANY
`extract_goto_target` to teraz cienki adapter cJSON delegujący do czystej `goto_target_from_double(lat_d, lon_d, *lat_e7, *lon_e7)` (`goto_target.h/.c`):
- `isfinite(lat_d) && isfinite(lon_d)` PRZED castem → INF/NaN odrzucone (brak UB C11 6.3.1.4).
- Porównanie z `GOTO_*_E7_MIN/MAX` w domenie **double** PRZED castem → wrap-into-range (4.39e9 → 1e8) niemożliwy. Zakres ±1.8e9 (i cały int32) dokładnie reprezentowalny w double (< 2^53), porównanie exact.
- Outputs pisane WYŁĄCZNIE na sukces (kontrakt „written only on success"); w `post_command` odrzucenie → 400 przed postem do mailbox. Brak regresji.
- `goto_target_valid` w `post_command:222` pozostawiony jako obrona warstwowa — zgodnie z rekomendacją pierwszego review, nie duplikat-problem.
- Pure ⊥ HAL zachowane: `goto_target.h` grep-clean (`stdbool.h`/`stdint.h` only); `math.h` tylko w `.c` (stdlib, nie HAL).

Moc wyroczni nowych testów (weryfikacja: mutacja „naive cast przed walidacją" MUSI failować):
- `test_from_double_out_of_int32_wrap_is_rejected` (4.39e9/-4.39e9): bez double-domain checku 4.39e9 zawija do ~1e8 i PRZECHODZI → test failuje. Silna wyrocznia. ✓
- `test_from_double_infinity/nan_is_rejected`: guard UB + outputs-untouched (lat_e7=7 zachowane). ✓
- `test_from_double_just_out_of_range_is_rejected` (900000001): granica inclusive+1. ✓
- `test_from_double_valid_narrows_to_int32`: ścieżka pozytywna, exact narrowing. ✓

### Poprzedni P2 — ✅ ROZWIĄZANY
`zadania.md:37` przełączony `[x]`→`[ ]` z adnotacją „ODROCZONE do Unit 4" (brak asercji na skopiowany cel, `to_ui_events`/`extract_goto_target` static+cJSON poza host-harness). Bonus: linia 39 (`goto_cancel`, dawny P3 #6) doprecyzowana adnotacją „pokryte tylko na poziomie flagi parsera". Stan checkboxów zgodny z rzeczywistością — anty-pattern #7 usunięty.

### Nowe findingi: BRAK
Fix zlokalizowany, bez regresji. Pozostałe P3 (przeniesione, zaakceptowane): `control_loop.c:350` martwy predykat `sensor_freshness_stamp(...,true)`; podwójny `cJSON_Parse` `reqbuf` (trade-off prostota > DRY). Oba świadomie odroczone/zaakceptowane, nie blokują.

**Werdykt re-review: ✅ CZYSTE — GOTOWE DO KONTYNUACJI (Faza 2).**

---

## Severity gate: ⛔ BLOKUJE (pierwszy review — historyczny, poniżej)

Znaleziono 1 problem P1 blokujący. Fix mały i zlokalizowany (walidacja w domenie `double` przed castem).

## Liczniki
- 🔴 P1 (blocking): 1
- 🟠 P2 (important): 1
- 🟡 P3 (nit): 5
- 🌐 E2E: N/A (firmware ESP-IDF, brak przeglądarki)

## Walidacja
- Host-tests (`./test/host/run.sh`): **400 Tests, 0 Failures, 0 Ignored — PASS**
- `idf.py build` (esp32s3): **PASS** (green)
- Grep czystości `goto_target.h`: bez `esp_*`/`driver/*` — OK (Pure ⊥ HAL)

---

## 🔴 P1 — blocking

### 1. `http_server.c:168-169` (`extract_goto_target`) — cast `(int32_t)valuedouble` PRZED walidacją zakresu = UB + obejście oracle
Walidacja `goto_target_valid` sprawdza wartość **już zawężoną** przez cast, nie oryginalną liczbę z JSON. JSON nie zna typu int32 — aplikacja (lub wrogi klient przez WiFi AP) może wysłać dowolną liczbę:

1. **Undefined behavior**: konwersja `double → int32` gdy wartość nie mieści się w int32 jest UB (C11 6.3.1.4p1). `{"lat_e7":1e400}` → `valuedouble = HUGE_VAL/INF`, `(int32_t)INF` = UB na Xtensa/gcc.
2. **Wrap-into-range bypass**: `lat_e7 = 4394967296` (~4.39e9) po `(int32_t)` zawija się mod 2^32 do `100000000` (1e8), mieści się w ±9e8 i **przechodzi** walidację. Kontrakt z `goto_target.h` („out-of-range target never reaches the control loop") niespełniony dla wejść spoza int32.

Zakres geo (±1.8e9) mieści się w int32 i jest dokładnie reprezentowalny w `double` — walidację trzeba zrobić w domenie `double` PRZED castem:
```c
double lat_d = lat->valuedouble, lon_d = lon->valuedouble;
if (!isfinite(lat_d) || !isfinite(lon_d)) return false;   /* INF/NaN */
if (lat_d < GOTO_LAT_E7_MIN || lat_d > GOTO_LAT_E7_MAX ||
    lon_d < GOTO_LON_E7_MIN || lon_d > GOTO_LON_E7_MAX) return false;
*lat_e7 = (int32_t)lat_d;   /* teraz bezpieczne (w zakresie int32) */
*lon_e7 = (int32_t)lon_d;
```
Uwaga NaN: `NaN > MAX` jest zawsze false → bez `isfinite` NaN by przeszedł; dodaj `#include <math.h>`. Wywołanie `goto_target_valid` w `post_command` staje się redundantne (zostaw jako obrona warstwowa lub przenieś oracle tutaj).

Konsekwencja odroczona (konsumpcja staged state = Unit 4), ale dziura jest osiągalna trywialnym payloadem i to jedyny deliverable Unitu 2 (twarda walidacja na granicy API). Konsensus 3 agentów co do defektu; security-sentinel klasyfikuje P1, architecture i test-coverage P2 — przyjęto ocenę security-sentinel (autorytet klasyfikacji + argument o UB na granicy walidacji).

---

## 🟠 P2 — important

### 2. `goto-waypoint-navigation-zadania.md:37` — overstated `[x]` (anty-pattern #7)
`[x] Test: poprawny goto ... → UI event z goto_request i skopiowanym celem` oznaczony jako zrobiony, ale **żadna asercja nie weryfikuje skopiowanego celu** (lat/lon) przez `to_ui_events`→`apply_goto_events`. Test parse-flag sprawdza tylko `goto_request==true` przy zerowym lat/lon. `to_ui_events`/`extract_goto_target` są `static` w `http_server.c` i zależą od cJSON, którego **nie ma w harnessie host** (`grep cjson test/host/` = 0) — więc genuinnie nie-host-linkowalne, obowiązuje klauzula-ucieczka planu (pokrycie w Unit 4). Ale checkbox powinien być `[ ]` z adnotacją „odroczone do Unit 4", nie `[x]`.

---

## 🟡 P3 — nit

3. `control_loop.c:350` — `sensor_freshness_stamp(s_last_goto_ms, now_ms(), true)` z literałem `true` jest tożsamościowe z `s_last_goto_ms = now_ms()`. Predykat „refresh only on valid reading" martwy. Rozważ bezpośrednie przypisanie z komentarzem o domenie zegara, albo zostaw świadomie dla spójności z `gps_reader.c`.
4. `http_server.c:211,221` — podwójny `cJSON_Parse` tego samego `reqbuf` (`extract_command` + `extract_goto_target`). Akceptowalne (małe payloady, prostota > DRY, reguła 11); świadomy trade-off, nie wymaga zmiany.
5. `test_goto_target.c` — brak host-testu na wektor spoza int32 / INF (obecne oracle-testy operują na wartościach mieszczących się w int32). Po naprawie P1 dodaj test na warstwę walidacji double z wejściami `1e400`, `4.39e9`, `-4.39e9` — każdy MUSI dać 400.
6. `goto-waypoint-navigation-zadania.md:39` — `goto_cancel → UI event` `[x]` pokryte tylko na poziomie flagi parsera, nie mapowania `to_ui_events`. Ta sama klasa co P2, mniejsza waga (pojedyncza flaga bool).
7. `http_server.c` — malformed JSON / puste body / brak pól lat_e7,lon_e7 obsłużone poprawnie w kodzie (cJSON_Parse→NULL→400; cJSON_IsNumber(NULL)→false→400) ale bez testu (uzasadnione ograniczeniem harnessu). Do rozważenia: rozbić `extract_goto_target` na cienki cJSON-adapter + czystą decyzję → host-testowalne z mocą wyroczni.

---

## Obszary czyste (zweryfikowane)
- **Pure ⊥ HAL**: `goto_target.{h,c}` grep-clean; HAL cienki, decyzja delegowana do czystej funkcji.
- **Named constants**: `GOTO_LAT/LON_E7_MIN/MAX` inclusive, przetestowane wprost.
- **Wzorce spójne**: tablica `COMMAND_TABLE` keyword→flagi; `to_ui_events`/`apply_*_events` zachowane; single writer (loop) dla staged state.
- **Moc wyroczni (Unit 1)**: wejścia poza zakresem po obu osiach + granice inclusive; mutacje `return true`/`<=`→`<`/usunięcie lon-check FAILują testy. Zero osłabienia asercji, zero regresji `command_parse`.
- **Kontrakt 400**: testowany przez czyste bloki (`goto_target_valid` + `api_build_error`) — poprawne obejście nielinkowalności `esp_http_server`.
- **cJSON**: brak wycieków (`cJSON_Delete` na wszystkich ścieżkach po parse; NULL→brak delete OK). NULL handling, malformed JSON, brak pól — fail-fast do 400.
- **Odroczona konsumpcja staged goto (Unit 4)**: zgodna z planem — `apply_goto_events` tylko latchuje stan.

## Odchylenia od planu
- Plan (linie 290-291) przewiduje pokrycie mapowania payloadu w Unit 4/5 gdy `to_ui_events` nie wydzielone jako pure — spełnione. Jedyne odchylenie formalne: checkbox linii 37 zadania.md oznaczony `[x]` mimo braku asercji (P2 wyżej).

## E2E
N/A — firmware ESP-IDF (C), brak przeglądarki. Weryfikacja hardware/na wodzie odłożona do `known-issues.md` (zgodnie z planem, Faza 4/domknięcie).
