---
title: "feat: Multi-client telemetria WS (panel www + app jednocześnie)"
type: feat
status: active
date: 2026-07-03
---

# feat: Multi-client telemetria WS

## Przegląd

Telemetria WS firmware'u (`/ws`) obsługuje dziś **jednego klienta** (`s_client_fd`, jeden
`int`). Nowy handshake **nadpisuje** slot, więc podłączenie aplikacji iOS ucisza panel www
(i odwrotnie). Zmieniamy pojedynczy slot na **listę klientów** i broadcast, tak by panel i
aplikacja dostawały telemetrię równolegle.

## Ujęcie problemu

Obserwacja użytkownika: po podłączeniu aplikacji panel www przestał pokazywać telemetrię.
Przyczyna (nie bug, istniejące ograniczenie): `ws_telemetry_register` ustawia jeden
`s_client_fd`; „najnowszy klient wygrywa". Chcemy jednoczesnego podglądu z wielu klientów
(realnie: panel na laptopie + aplikacja w ręce).

## Śledzenie wymagań

- R1. Telemetria WS dostarczana **jednocześnie do wielu klientów** (panel + app + zapas).
- R2. Nowy klient **dokładany do listy**, nie nadpisuje istniejących.
- R3. Klient rozłączony/wadliwy **usuwany z listy** (GC przy błędzie send); pozostali dostają dalej.
- R4. **Serializacja JSON raz na tick** (hot path), broadcast pętlą po fd — bez per-klient alloc.
- R5. Limit klientów **spójny z limitem socketów** httpd/LWIP — bez ryzyka LRU-purge aktywnego klienta.

## Granice scope'u

- Bez zmiany formatu/treści telemetrii ani okresu (`WS_TELEMETRY_PERIOD_MS=100`).
- Bez per-klient stanu (różne subskrypcje/filtry) — wszyscy dostają ten sam snapshot.
- Bez zmiany kontraktu WS dla aplikacji iOS (ta sama ramka JSON).
- Bez uwierzytelniania klientów WS (AP jest już WPA2 — poza zakresem).

## Kontekst i research

### Relevantny kod

- `components/web_panel/src/ws_telemetry.c` — cały moduł: `s_client_fd` (jeden slot),
  `push_work` (serializacja + send do jednego fd, drop przy błędzie: `s_client_fd=-1`),
  `on_tick` (timer, atomik `s_push_in_flight`, `httpd_queue_work`), `ws_telemetry_register`
  (nadpisuje slot), `ws_telemetry_stop`.
- `components/web_panel/include/ws_telemetry.h` — `ws_telemetry_register(server, fd)`, `WS_TELEMETRY_PERIOD_MS`.
- `components/web_panel/src/http_server.c` — `ws_handler` (:237) woła `ws_telemetry_register`
  przy handshake (`req->method == HTTP_GET`); `httpd_config_t` (:268) — `max_uri_handlers=8`,
  `lru_purge_enable=true`, `stack_size` wyprowadzony; **`max_open_sockets` NIEUSTAWIONE = domyślne 7**.
- `sdkconfig`: `CONFIG_LWIP_MAX_SOCKETS=10` → httpd `max_open_sockets` max = 7 (LWIP rezerwuje 3).

### Kluczowa obserwacja o wątkowości (upraszcza projekt)

`ws_telemetry_register` (z `ws_handler`) i `push_work` (z `httpd_queue_work`) biegną **na tym
samym tasku httpd** → mutacje listy klientów są **jednowątkowe, bez mutexa**. `on_tick` biegnie
na tasku `esp_timer`, ale **nie dotyka listy** — tylko czyta `s_server`, ustawia atomik i
kolejkuje pracę. Ten inwariant MUSI zostać zachowany (żadnych mutacji listy z `on_tick`).

### Wzorce repo

- Pure ⊥ HAL (`docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md`)
  — logika listy jako czysta funkcja, HAL send cienki.
- Moc wyroczni w host-testach Unity (`test/host/`).

## Kluczowe decyzje techniczne

- **Lista jako pure `ws_client_set`** — stała tablica `int fds[WS_TELEMETRY_MAX_CLIENTS]`,
  operacje `add` (idempotentne dla istniejącego fd; sygnalizuje pełną listę), `remove(fd)`,
  `contains`, `count`, iteracja. Pure, host-testowana z mocą wyroczni. HAL (`ws_telemetry.c`)
  tylko serializuje raz i wysyła w pętli.
- **`WS_TELEMETRY_MAX_CLIENTS = 4`** — realny przypadek to 2 (panel+app); 4 daje zapas, a przy
  `max_open_sockets=7` zostają ≥3 sockety na HTTP (ładowanie panelu, `/api/*`), więc tablica
  socketów nie zapełni się przy normalnym użyciu → LRU nie wypcha aktywnego klienta WS.
- **`max_open_sockets` ustawione jawnie na 7** (dziś domyślne, ale zapiszmy explicite obok
  `WS_TELEMETRY_MAX_CLIENTS`, żeby zależność „WS_MAX + HTTP_headroom ≤ max_open_sockets" była
  widoczna i nie zdryfowała). Bez zmiany `CONFIG_LWIP_MAX_SOCKETS` (10 wystarcza).
- **Pełna lista → odrzuć najnowszego** (log-and-continue), NIE eksmituj istniejącego — istniejący
  podgląd ważniejszy niż nowy; przy dobranym rozmiarze i tak nierealne. Handshake WS się uda
  (socket żyje), po prostu nie dostanie telemetrii; klient to wykryje po braku ramek.
- **GC przy błędzie send** — w broadcast pętli błąd `httpd_ws_send_frame_async` dla danego fd
  usuwa TEN fd z listy i kontynuuje do pozostałych (zamiast dzisiejszego zerowania jedynego slotu).
- **Global `s_push_in_flight` zostaje** — jedna serializacja + broadcast na tick; flaga chroni
  re-entrancy `push_work`, nie per-klient.

## Otwarte pytania

### Rozwiązane podczas planowania
- Mutex na liście? → Nie; wszystkie mutacje na tasku httpd (register + push_work). `on_tick` nie tyka listy.
- Zachowanie przy pełnej liście? → Odrzuć najnowszego + log (rozmiar dobrany tak, by nie zachodziło).
- Rozmiar listy? → 4 (zapas nad realnymi 2), spójny z `max_open_sockets=7`.

### Odroczone do implementacji
- Czy `on_tick` ma czytać atomowy licznik klientów (pominąć `httpd_queue_work` gdy 0), czy zawsze
  kolejkować i pozwolić `push_work` no-opować przy pustej liście (jak dziś dla `fd<0`). Rekomendacja:
  wariant no-op (prostszy, bez cross-task licznika); ewentualny atomowy `count` jeśli zależy na oszczędności.
- Czy hookować `httpd` close-callback dla natychmiastowego GC rozłączonego klienta, czy wystarczy
  GC-przy-send (staleness ~1 tick = 100 ms). Rekomendacja: GC-przy-send (prostsze, wystarczające).

## Implementation Units

- [ ] **Unit 1: Pure `ws_client_set` (lista fd) + host-testy**

**Cel:** Czysta, testowalna logika listy klientów (add/remove/contains/count/iteracja).

**Wymagania:** R2, R3 (część logiczna)

**Zależności:** Brak.

**Pliki:**
- Stwórz: `components/web_panel/include/ws_client_set.h`, `components/web_panel/src/ws_client_set.c`
- Test (unit): `test/host/test_ws_client_set.c` (+ wpis w `test/host/CMakeLists.txt`)

**Podejście:**
- Struct `{ int fds[WS_TELEMETRY_MAX_CLIENTS]; }` (albo nieprzezroczysty z akcesorami). `add(set, fd)`
  → enum/bool: `ADDED` / `ALREADY_PRESENT` / `FULL`. `remove(set, fd)` → bool. `count`, `contains`,
  `for_each` (callback lub indeks). Slot wolny = `-1`.
- Bez zależności `esp_*` (pure). Stała `WS_TELEMETRY_MAX_CLIENTS` w nagłówku współdzielonym.

**Wzorce do naśladowania:** inne pure moduły w `components/*/src` (np. `command_parse`, `goto_grab`); styl Unity z `test/host/`.

**Scenariusze testowe:**
- [Unit] add do pustej → `ADDED`, `count==1`, `contains(fd)`.
- [Unit] add tego samego fd → `ALREADY_PRESENT`, `count` bez zmian (idempotencja — httpd może wołać register ponownie).
- [Unit] add ponad `WS_TELEMETRY_MAX_CLIENTS` → `FULL`, lista nienaruszona (mutacja „nadpisz mimo full" MUSI failować).
- [Unit] remove środkowego fd → `count-1`, pozostałe zachowane, brak dziur w iteracji.
- [Unit] remove nieobecnego fd → `false`, bez zmian.

**Weryfikacja:** `bash test/host/run.sh` zielone; nowy suite `test_ws_client_set` w liczniku.

---

- [ ] **Unit 2: Broadcast w `ws_telemetry.c` (serializacja raz, send do wszystkich, GC per-fd)**

**Cel:** Zamiana pojedynczego slotu na listę; broadcast snapshotu do wszystkich klientów.

**Wymagania:** R1, R2, R3, R4

**Zależności:** Unit 1.

**Pliki:**
- Modyfikuj: `components/web_panel/src/ws_telemetry.c` (zamień `s_client_fd` na `ws_client_set`;
  `push_work`: serializuj raz, pętla send, per-fd błąd → `remove`; `register`: `add`; `stop`: wyczyść listę)
- Modyfikuj: `components/web_panel/include/ws_telemetry.h` (jeśli `WS_TELEMETRY_MAX_CLIENTS` tu ląduje)

**Podejście:**
- `push_work`: jeśli `count==0` → wyczyść flagę i wróć (jak dziś `fd<0`). Inaczej `control_loop_get_snapshot`
  + `snapshot_to_json` RAZ do lokalnego bufora, potem `for_each` fd: `httpd_ws_send_frame_async`;
  przy błędzie odłóż fd do usunięcia i loguj (usuń po pętli albo bezpiecznie w trakcie — na tasku httpd,
  jedno-wątkowo). Na końcu wyczyść `s_push_in_flight`.
- `ws_telemetry_register`: `add`; przy `FULL` → log-and-continue (odrzuć najnowszego), zwróć `ESP_OK`
  (handshake się udaje, klient po prostu bez telemetrii). Przy `ADDED`/`ALREADY_PRESENT` — `ensure_timer`.
- `on_tick`: warunek `s_server==NULL` (usuń zależność od pojedynczego fd; opcjonalny check „0 klientów").
- Zachowaj inwariant: **mutacje listy tylko na tasku httpd** (register + push_work); `on_tick` nie tyka listy.

**Notatka wykonawcza:** Zmiana dotyka HAL (httpd/esp_timer) — nie host-testowalna wprost; poprawność logiki
listy pokrywa Unit 1. Ten unit weryfikowany przez `idf.py build` + device (§ Weryfikacja).

**Wzorce do naśladowania:** dzisiejszy `push_work`/`on_tick` (zachowaj strukturę flagi in-flight i queue_work).

**Scenariusze testowe:**
- [Unit] (poprzez Unit 1) logika add/remove/full już pokryta — tu bez nowych host-testów logiki.
- [E2E/device] Panel + app jednocześnie → OBA pokazują żywą telemetrię (ta sama treść, ~10 Hz).
- [E2E/device] Rozłącz jednego klienta → drugi dostaje dalej bez przerwy; log „dropping client (fd=…)".
- [E2E/device] Reconnect klienta → wraca do listy, telemetria wznowiona.

**Weryfikacja:** `idf.py build` (esp32s3) zielone; na urządzeniu panel i aplikacja pokazują telemetrię
równolegle; odłączenie/podłączenie jednego nie ubija drugiego.

---

- [ ] **Unit 3: Sizing socketów httpd (spójność limitów)**

**Cel:** Zapewnić, że `WS_TELEMETRY_MAX_CLIENTS` + zapas HTTP mieści się w `max_open_sockets` ≤ LWIP.

**Wymagania:** R5

**Zależności:** Unit 2 (rozmiar listy ustalony).

**Pliki:**
- Modyfikuj: `components/web_panel/src/http_server.c` (`config.max_open_sockets = 7;` jawnie, z komentarzem
  wiążącym `WS_TELEMETRY_MAX_CLIENTS(4) + HTTP_headroom(3) ≤ 7 ≤ CONFIG_LWIP_MAX_SOCKETS(10)-3`)

**Podejście:**
- Ustaw `max_open_sockets` jawnie i skomentuj zależność, żeby wzrost `WS_TELEMETRY_MAX_CLIENTS` w
  przyszłości wymuszał świadomą rewizję (inaczej LRU-purge mógłby wypchnąć aktywnego klienta WS).
- Jeśli kiedyś potrzeba >4 klientów: bump `CONFIG_LWIP_MAX_SOCKETS` (sdkconfig.defaults) i `max_open_sockets`
  — poza tym planem, odnotowane.

**Scenariusze testowe:**
- [E2E/device] Panel + app + 1-2 zakładki panelu (do 4 WS) → wszyscy dostają telemetrię; równoległe
  `/api/params`/ładowanie panelu nie wypycha klienta WS (brak LRU-purge aktywnego).

**Weryfikacja:** `idf.py build` zielone; komentarz wiążący limity obecny; na urządzeniu brak wypychania
klienta WS pod obciążeniem HTTP.

## Wpływ systemowy

- **Graf interakcji:** `ws_handler` (handshake) → `ws_telemetry_register`/`add`. Timer → `on_tick` →
  `httpd_queue_work` → `push_work` (broadcast + GC). Bez zmian kontraktu WS dla iOS (ta sama ramka).
- **Ryzyka cyklu życia:** stale fd po cichym rozłączeniu → usuwany przy pierwszym błędnym send (~1 tick).
  Reuse numeru fd przez httpd bezpieczny, bo każdy nowy handshake woła `register`.
- **Parytet:** aplikacja iOS i panel to ci sami konsumenci tego samego snapshotu — brak rozjazdu.

## Ryzyka i zależności

- **LRU-purge aktywnego klienta WS** przy pełnej tablicy socketów → mitygacja: `WS_MAX=4` + jawny
  `max_open_sockets=7` (zapas HTTP ≥3); komentarz wiążący limity (Unit 3).
- **Brak host-testu HAL send** (Unit 2) → mitygacja: logika listy pokryta host-testami (Unit 1);
  broadcast weryfikowany na urządzeniu.
- **Regres jednowątkowości** (gdyby ktoś dodał mutację listy z `on_tick`/innego tasku) → utrzymać
  inwariant „mutacje listy tylko na tasku httpd"; udokumentować w komentarzu modułu.

## Źródła i referencje

- Powiązany kod: `components/web_panel/src/ws_telemetry.c`, `components/web_panel/src/http_server.c`
- Wzorzec pure⊥HAL: `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md`
- Kontekst odkrycia: pojedynczy slot `s_client_fd` (najnowszy klient wygrywa) — diagnoza 2026-07-03.
