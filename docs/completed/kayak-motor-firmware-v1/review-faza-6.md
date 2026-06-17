# Code Review — Faza 6 (Unit 10 + 11)

**Commit:** `e8b0f32` — feat(kayak-motor-firmware): Faza 6 — SoftAP WPA2 + serwer WWW + WS telemetria + API params + panel + LED
**Zakres:** WiFi SoftAP (WPA2-PSK), `esp_http_server` (panel + REST + WS), lossy telemetria WS ~10 Hz, API parametrów (cJSON, re-walidacja, 409 gate, pending mailbox), pure `api_contract` + `wifi_ap_config` + `led_pattern`, panel vanilla JS, LED GPIO2.
**Metodologia:** 4 perspektywy równolegle (Security, Performance, Architecture/Quality, Test Coverage). E2E/HW odroczone (brak sprzętu i przeglądarki — decyzja użytkownika).

---

## Severity gate: ⚠️ KONTYNUUJ Z ZASTRZEŻENIAMI

| Severity | Liczba |
|----------|--------|
| 🔴 P1 (blocking) | **0** |
| 🟠 P2 (important) | **4** |
| 🟡 P3 (nit) | **8** |

Typy: KOD = 3 (P2-1 read_body, P2-2 post_command, P2-4 typed enum) · TEST = 1 (P2-3 params_api/json) · E2E = 0 (odroczone).

**Weryfikacja na żywo:**
- Testy hosta: **184/184 PASS** (154→184, +30: 8 api_contract + 8 wifi_ap_config + 14 led_pattern). Liczba zgodna z planem.
- Build: `idf.py set-target esp32 && idf.py build` → **EXIT=0** ("Project build complete").

---

## Werdykt bezpieczeństwa

### 1. AP non-OPEN guard — REALNY (PASS)
`assert_ap_secure` (`wifi_ap.c:33-44`) używa `abort()`, **nie** `assert()` — działa niezależnie od `NDEBUG`. Wołany w `wifi_ap_start` (`wifi_ap.c:63`) **PRZED** jakimkolwiek dotknięciem radia (`esp_wifi_init/set_config/start`). `AP_AUTHMODE` zahardkodowany `WIFI_AUTH_WPA2_PSK` — brak ścieżki runtime/Kconfig do OPEN. Pure walidator (`wifi_ap_config.c:5-18`) odrzuca non-WPA2/NULL/<8/>63; pusty string → `strlen==0 < 8` → odrzucony. Hasło to udokumentowany placeholder (`CONFIG_KAYAK_AP_PASSWORD` default `"CHANGE-ME-kayak"`, Kconfig opisuje fail-fast); `sdkconfig` w `.gitignore`, brak realnego sekretu w repo. **R14 spełnione, dowód testowalny na hoście.**

### 2. JSON parsing (OOB/injection) — BEZPIECZNY (PASS, 1×P2 robustness)
- Każde aplikowane pole re-walidowane server-side: `params_api_handle_post` woła `settings_validate` (`params_api.c:73`) i **odrzuca cały POST** (400 VALIDATION_FAILED) jeśli jakiekolwiek pole poza zakresem / inwariant cross-field naruszony. Nieznane klucze ignorowane, złe typy pomijane (`cJSON_IsNumber`/`cJSON_IsBool`), brakujące pola zachowują seed z aktywnych. `clamp_u16` zapobiega OOB castowi przed range-checkiem.
- `append_escaped` (`api_contract.c:40-70`) overflow-safe (`*pos+len >= cap` przed każdym zapisem), NUL-terminacja przez `finalise`. Oba enwelopy zawsze `{data,error}`. `code`/`message` escapowane; `data_json` server-generated (cJSON), nigdy echo user-inputu → brak wektora injection.
- Panel: `app.js` używa `createElement`+`textContent`/`value`, brak `innerHTML` z telemetrią → brak DOM XSS.
- 🟠 **P2-1**: `read_body` (`http_server.c:84`) robi pojedynczy `httpd_req_recv` traktowany jako całe ciało; partial read (TCP) → cicha truncacja. Fail-safe (400, NUL-terminowane na `received`, brak buffer-bugu), ale legalny większy POST może spuriously failować.

### 3. 409 / single-writer — EGZEKWOWANE W FIRMWARE (PASS)
- Gate firmware'owy, nie tylko UI: `params_api_handle_post` sprawdza `controller_is_disarmed()` (`params_api.c:67`) → 409 `SETTINGS_WRITE_REJECTED_NOT_DISARMED` przed stagingiem.
- **Brak eksploatowalnego TOCTOU**: autorytatywny gate jest przy aplikacji — `maybe_apply_pending` (`control_loop.c`) re-sprawdza `state==DISARMED` w pętli i zostawia pending zastagowane jeśli ARMED. POST wyścigujący do ARMED nie zmieni aktywnych params.
- **Single-writer (SI-6)**: `params_api` nigdy nie dotyka `s_params`; tylko `control_loop_post_pending` → `xQueueOverwrite` (mailbox length-1). Pętla = jedyny pisarz `s_params`.

### 4. Telemetria WS — lossy, non-blocking (PASS)
`on_tick` (esp_timer task) + `push_work` (httpd task) — **nigdy** na pętli kontrolnej. Atomic CAS in-flight (`atomic_compare_exchange_strong`) → wolny klient gubi ramki zamiast backpressure. `snprintf` do bufora stack 256 B (payload ~150 B), overflow sprawdzony. Brak alokacji na hot path. Snapshot JSON zawiera tylko telemetrię operacyjną — brak sekretów. Pętla 50 Hz pozostaje alloc-free i lock-free; wolny klient nie zablokuje pętli.

---

## Regresje: BRAK
184/184 PASS (poprzednio 150/150 w fazie 5; +34 łącznie z wcześniejszymi fazami, +30 w tej fazie). Zmiany w `control_loop` (publish_snapshot, UI mailbox, LED co cykl) nie złamały pętli ani single-writer: `s_params` nadal pisany wyłącznie przez pętlę (init + apply); `apply_ui_events` używa non-blocking `xQueueReceive(...,0)`; mailbox length-1 z `xQueueOverwrite`. Build esp32 czysty.

---

## Findingi P2 (important)

- 🟠 **P2-1 KOD** — `components/web_panel/src/http_server.c:84` — `read_body` pojedynczy `httpd_req_recv`, brak pętli do `received == content_len`; partial read truncuje ciało (fail-safe 400, ale legalny duży POST może failować). Fix: pętla recv do kompletu lub error/timeout.
- 🟠 **P2-2 KOD** — `components/web_panel/src/http_server.c:107-138` — `post_command` omija warstwę `*_api`: inlinuje parsowanie (`strstr` substring) i literał enwelopy błędu w warstwie transportu (duplikacja kontraktu `api_contract`, §14 layer boundary + §4). Dodatkowo substring matching: ciało `{"note":"do not arm"}` dopasuje `"arm"` i uzbroi (choć i tak gated przez state machine: `can_arm` wymaga rc_valid + throttle_neutral). Fix: wydzielić `command_api_handle_post(...)` + parsować `cmd` przez cJSON (exact match) + budować błąd przez `api_build_error`.
- 🟠 **P2-3 TEST** — `params_api.c` / `params_json.c` bez testów hosta (zależą od cJSON/control_loop, nie w harness). Niepokryte: ścieżka 409/NOT_DISARMED→status, BAD_REQUEST-on-parse-fail, INTERNAL-on-post-fail, parowania status↔code (409/400/500), round-trip serialize/parse + clamp_u16 + partial-overlay. Mitygacja: `settings_validate` ma 7 testów (re-walidacja delegowana), `api_contract` ma stały string kodu — ale samo *wiring* handlerów (headline scenariusz "zapis w ARMED → 409") niezweryfikowane. Fix: stub cJSON+control_loop do harness albo zalogować jako known coverage gap w closeout.
- 🟠 **P2-4 KOD** — `components/control_loop/include/control_loop.h:50` — `control_loop_ui_events.calib_event` typowane `int` zamiast `calib_event` (dwa casty: `apply_ui_events` `(calib_event)ev.calib_event`, `parse_command` przypisuje enum jako int). §10 type-safety. Fix: typ `calib_event` + `#include "esc_calibration.h"` w nagłówku (pure header, `state_machine` już REQUIRE — coupling nie rośnie).

## Findingi P3 (nit)

- 🟡 **P3-1** — `http_server.c:127` (`reqbuf[256]`) i `ws_telemetry.c:49` (`json[256]`) — gołe `256`, brak named constant (kontrast z `HTTP_REQ_MAX`/`HTTP_BODY_MAX`). §1/§6.
- 🟡 **P3-2** — `api_contract.c:88` `finalise`: `out[ok && pos < out_size ? pos : 0] = '\0'` — gęsty ternary, łamie 5-sek regułę czytelności (§11). Early-return.
- 🟡 **P3-3** — `http_server.c:97` — overflow ścieżka `post_params` woła `params_api_handle_post("", ...)` tylko by reużyć 400 envelope; couplinguje do empty-string handlingu `params_json_parse`. Czytelniej `api_build_error` bezpośrednio.
- 🟡 **P3-4** — Brak auth/authz na endpointach i WS; jedyna granica = WPA2-PSK. Świadoma decyzja (single-operator SoftAP, bezpieczeństwo fizyczne gated przez rc_valid+DISARMED), ale zapisać jawne założenie zaufania: **asocjacja do AP == pełna władza sterowania.** Brak CSRF (cross-origin form POST możliwy gdy operator asocjowany). Niskie ryzyko na izolowanym SoftAP.
- 🟡 **P3-5** — Lossy snapshot tearing: `control_loop_get_snapshot`/`get_active_params` robią unlocked struct copy współbieżnie z pętlą (brak mutexu). Udokumentowany lossy design, benign dla read-only display; jedna niespójna ramka JSON w najgorszym razie. `controller_is_disarmed()` czyta z tego samego snapshotu, ale autorytatywny re-check jest w pętli (defense-in-depth).
- 🟡 **P3-6** — Brak bezpośrednich asercji stabilnego stringa dla kodów `BAD_REQUEST`/`VALIDATION_FAILED`/`INTERNAL_ERROR` oraz gałęzi `API_OK→INTERNAL` (`api_contract.c:112-113`). Tylko `NOT_DISARMED` kontraktowo zapięty.
- 🟡 **P3-7** — Brak testu too-small-buffer dla `api_build_success` (tylko `api_build_error` ma `test_too_small_buffer_returns_zero_and_terminates`). Ta sama maszyneria `append_raw`/`finalise`, niskie ryzyko.
- 🟡 **P3-8** — LED FAILSAFE "niezależny od poprzedniego stanu": `led_pattern_on` bezstanowy (brak arg poprzedniego stanu), więc gwarantowane strukturalnie; test asercjuje tylko oś `calibrated`.

---

## Dobre praktyki (odnotowane)
- `wifi_ap.c:33-44` fail-fast WPA2 guard delegujący do pure host-testowalnego `wifi_ap_config_valid` — intencja R14 dowodliwa na hoście bez linkowania esp_wifi.
- `ws_telemetry.c` lossy push z atomic in-flight guard — wolny klient nie backpressuruje pętli (§13).
- `params_json.c` table-driven field mapping (key↔offset w jednym miejscu); partial-POST overlay seedowany z aktywnych params.
- Single-writer discipline na aktywnych params utrzymana przez nowe gettery.
- Wszystkie pliki <300 linii, funkcje <50, zero circular deps, PURE/HAL kontrakt utrzymany (api_contract/wifi_ap_config/led_pattern grep-clean z esp_*/IDF/cJSON).

---

## Zgodność z planem (Odchylenia)

| Element | Plan | Implementacja | Status |
|---------|------|---------------|--------|
| `wifi_ap`, `http_server`, `ws_telemetry`, `params_api`, `api_contract` + panel | Unit 10 | Wszystkie obecne | ✅ |
| `led_pattern` (pure) + `led_driver` (HAL), driven co cykl | Unit 11 | Obecne | ✅ |
| `params_json.c` (+`.h`) | Nie w planie explicite (plan mówił parse/serialize w `params_api`) | Wydzielony osobny moduł | ✅ Odchylenie korzystne (SRP, table-driven) |
| `POST /api/command` + `control_loop_ui_events` mailbox | Nie w planie explicite | Dodany (arm/disarm/calib z panelu) | ⚠️ Odchylenie — uzasadnione (panel potrzebuje produkować zdarzenia UI, granica Unit 10), ale patrz P2-2 (omija warstwę api) |
| `api_contract` test host | Plan: `components/web_panel/test/test_api_contract.c` | Faktycznie `test/host/test_api_contract.c` (kolokacja w host harness) | ✅ Plik istnieje, ścieżka inna ale spójna z konwencją projektu |
| Test `params_api`/`params_json` | Plan nie żądał explicite host-testu (zależność cJSON) | Brak | ⚠️ P2-3 (coverage gap) |

---

## E2E / HW — ODROCZONE (N/A, do ręcznej weryfikacji na sprzęcie)

E2E browser-verifier nie może uruchomić panelu (brak żywej aplikacji + brak przeglądarki). Poniższe scenariusze do ręcznej weryfikacji na sprzęcie (known-issues), **NIE są failures**:

**[HW] Unit 10 — WiFi/panel:**
1. AP widoczny jako WPA2 (nie otwarty); połączenie hasłem → panel ładuje (`/`, `/app.js`, `/style.css`).
2. Live WS pokazuje CH1/CH2/CH4 + wyjścia servo/ESC + flagi R16 (settings_source/settings_valid/calibrated/defaults_used/nvs_error).
3. Przy pustym/skorumpowanym NVS → panel pokazuje UNCALIBRATED/defaults_used.
4. W DISARMED zmiana parametru → 200 OK, wartość przeżywa restart.
5. W ARMED próba zapisu → 409 + kod `SETTINGS_WRITE_REJECTED_NOT_DISARMED`; live podgląd działa zawsze.
6. CH4 widoczny w telemetrii bez wpływu na sterowanie.
7. **[HW] Assert non-OPEN**: build z pustym `CONFIG_KAYAK_AP_PASSWORD` → fail-fast (abort) przy starcie, AP nigdy nie wstaje.
8. POST `/api/command` arm/disarm/calib_start/calib_next/calib_cancel → przejścia stanu (gated przez rc_valid + throttle_neutral).

**[HW] Unit 11 — LED (GPIO2):**
9. DISARMED calibrated → wolne miganie 0.5 Hz; DISARMED uncalibrated → double-blink overlay.
10. ARMED → solid on.
11. FAILSAFE → szybkie 5 Hz przez cały czas trwania (niezależnie od poprzedniego stanu).
12. ESC_CALIBRATION → double-blink.

---

## Pliki kluczowe
- `/Users/pawelpazniewski/projects/that_motor/components/web_panel/src/wifi_ap.c` — fail-fast AP guard (R14, poprawny)
- `/Users/pawelpazniewski/projects/that_motor/components/web_panel/src/wifi_ap_config.c` — pure walidator
- `/Users/pawelpazniewski/projects/that_motor/components/web_panel/src/params_api.c` — gate 409 + re-walidacja + mailbox (poprawny)
- `/Users/pawelpazniewski/projects/that_motor/components/web_panel/src/params_json.c` — table-driven (de)serialize
- `/Users/pawelpazniewski/projects/that_motor/components/web_panel/src/api_contract.c` — pure {data,error}, escaping
- `/Users/pawelpazniewski/projects/that_motor/components/web_panel/src/http_server.c` — transport (P2-1, P2-2)
- `/Users/pawelpazniewski/projects/that_motor/components/web_panel/src/ws_telemetry.c` — lossy WS push
- `/Users/pawelpazniewski/projects/that_motor/components/control_loop/include/control_loop.h` — facade (P2-4)
- `/Users/pawelpazniewski/projects/that_motor/components/control_loop/src/control_loop.c` — snapshot/LED/UI mailbox
- `/Users/pawelpazniewski/projects/that_motor/components/led_status/src/led_pattern.c` — pure pattern

---

## Re-review po cyklu 1

**Commit naprawczy:** `8f365ad` — fix(kayak-motor-firmware): poprawki po review fazy 6 (cykl 1)
**Diff:** `git diff e8b0f32 8f365ad` (14 plików, +478/-62)
**Metodologia:** weryfikacja realności napraw per finding + skan regresji. Diagnostyka clang IDE = false positives (ignored). E2E/HW = odroczone (N/A).

### Severity gate: ✅ CZYSTE (P1=0, P2=0, P3=8 — wszystkie P3 nadal otwarte, nit-only, bez blokady)

### Na żywo
- **Testy hosta:** `./test/host/run.sh` → **198 Tests, 0 Failures, 0 Ignored, OK** (184→198, +14: 9 `command_parse` + 5 `params_decide`).
- **Build:** `idf.py set-target esp32 && idf.py build` → **Project build complete** (EXIT 0). `kayak-motor-firmware.bin` 0xd90e0 B, 43% wolne.

### Weryfikacja per finding

**P2-1 (read_body partial read) — ✅ ROZWIĄZANY (realnie).**
`read_body` pętli `httpd_req_recv` aż `received == content_len`. Obsługa: `content_len >= buf_size` → `-1` (odrzuca, NIGDY nie truncuje — 413-style); `HTTPD_SOCK_ERR_TIMEOUT` → bounded retry (`HTTP_RECV_MAX_TIMEOUTS=4`) potem `-1`; `n <= 0` → `-1` (peer-close przed pełnym ciałem / recv error). NUL-terminacja po pełnym odbiorze. Niekompletne ciało nigdy nie trafia do parsera (caller zwraca 400 przez `api_build_error`). Magic `256` → named `HTTP_CMD_MAX`.

**P2-2 (command_parse) — ✅ ROZWIĄZANY (realnie).**
Nowy pure moduł `command_parse(const char*)`: zero IDF (`#include`: `command_parse.h`, `<string.h>`), table-driven `strcmp` EXACT-match (nie substring). Stary `parse_command`/`strstr` w http_server USUNIĘTY (grep: brak). Kontrakt keywordów w JEDNYM miejscu (tabela). http_server: `extract_command` (cJSON exact field `cmd`, case-sensitive) → `command_parse` → odpowiedzi przez `api_build_success`/`api_build_error` (zero inline literałów JSON — grep potwierdza tylko `api_build_*` wywołania). Testy: `test_unknown_keyword_is_rejected`, `test_substring_of_known_keyword_is_rejected` (`"do not arm"` → `ok=false`, nie uzbraja), NULL/empty rejected. 24 asercje.

**P2-3 (params_decide wiring 409) — ✅ ROZWIĄZANY (realnie, mocniejsza opcja).**
Z dwóch dopuszczonych ścieżek (host-stub LUB log known gap) wybrano ekstrakcję pure `params_decide_write(sm_state, bool)` — pełny łańcuch decyzyjny host-testowalny. `params_api` to teraz cienki glue: parse → re-walidacja (`candidate_is_valid`) → delegacja do `params_decide_write(controller_state(), fields_valid)`. Testy host pełnego łańcucha z realnymi asercjami: ARMED+valid → 409 `API_ERR_NOT_DISARMED` + `api_error_code_str` == `SETTINGS_WRITE_REJECTED_NOT_DISARMED`; ARMED+invalid → nadal 409 (precedencja gate>validity); FAILSAFE → 409; DISARMED+invalid → 400 `VALIDATION_FAILED`; DISARMED+valid → 200 ACCEPT. 13 asercji. Discriminated enum `params_write_decision` (zgodnie z §10).

**P2-4 (calib_event enum) — ✅ ROZWIĄZANY (realnie).**
`control_loop_ui_events.calib_event` przetypowane `int` → `calib_event` (`#include "esc_calibration.h"` w control_loop.h — pure header). Cast `(calib_event)ev.calib_event` w `apply_ui_events` USUNIĘTY (bezpośrednie przypisanie). `command_parse_result.calib_event` też typu `calib_event`. Brak castów (grep). Enum `CALIB_EVENT_{NONE,NEXT,CANCEL}` z esc_calibration.h.

### Regresje: BRAK (198/198 PASS)
- Single-writer / SI-6: `maybe_apply_pending` z TOCTOU re-check (`loop_should_apply_pending(state)`), mailbox length-1 (`xQueueCreate(1,...)` + `xQueueOverwrite`), `params_api` nie pisze `s_params` — INTAKT, niezmieniony przez ekstrakcje.
- 409-enforcement: egzekwowane podwójnie (boundary gate w `params_decide_write` + apply-time re-check w pętli) — wzmocnione, nie osłabione.
- Guard non-OPEN AP (`wifi_ap.c`) — nietknięty.
- Limity §1: `http_server.c` 233 < 300; nowe moduły 34/24 linii; funkcje < 50 linii (`post_command` zrefaktorowany na `extract_command`/`to_ui_events`/`send_command_error`, `params_api` na `stage_and_render`).
- Asercje: zero osłabionych, zero assertion-free (command_parse 24, params_decide 13).
- Pure moduły: `command_parse.c`/`params_decide.c` bez include IDF, dodane do `PURE_SOURCES` + `TEST_SOURCES` + `run_*_tests` w test_main.c. `json` (cJSON) w `REQUIRES` web_panel.

### Otwarte (nieblokujące)
8 P3 (nit) pozostaje otwartych — m.in. P3 §1 (gołe `256` w `ws_telemetry.c:49` — w http_server już naprawione przez `HTTP_CMD_MAX`), `finalise` ternary, brak auth/CSRF (świadoma decyzja single-operator AP), unlocked snapshot copy (udokumentowany lossy design). Wszystkie nit-only, do opcjonalnego cleanup w closeout.
