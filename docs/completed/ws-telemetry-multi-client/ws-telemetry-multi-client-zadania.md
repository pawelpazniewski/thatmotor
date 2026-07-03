# Zadania: Multi-client telemetria WS

Branch: `feature/ws-telemetry-multi-client`
Ostatnia aktualizacja: 2026-07-03

Nakład: S ≤0.5d · M ~1d · L ~2d · XL >2d

---

## Faza 1 — Multi-client telemetria

### Unit 1: Pure `ws_client_set` (lista fd) + host-testy — **S**

Zależności: brak. Realizuje: R2, R3 (logika).

**Implementacja:**
- [x] Stwórz `components/web_panel/include/ws_client_set.h` — API: `add`→enum `ADDED`/`ALREADY_PRESENT`/`FULL`, `remove(fd)`→bool, `contains`, `count`, iteracja; stała `WS_TELEMETRY_MAX_CLIENTS`.
- [x] Stwórz `components/web_panel/src/ws_client_set.c` — stała tablica `int fds[WS_TELEMETRY_MAX_CLIENTS]`, slot wolny = `-1`, bez zależności `esp_*`.
- [x] Dodaj `test/host/test_ws_client_set.c` do `test/host/CMakeLists.txt`.

**Testy:**
- [x] Test: add do pustej → `ADDED`, `count==1`, `contains(fd)`.
- [x] Test: add tego samego fd → `ALREADY_PRESENT`, `count` bez zmian (idempotencja).
- [x] Test: add ponad `WS_TELEMETRY_MAX_CLIENTS` → `FULL`, lista nienaruszona (mutacja „nadpisz mimo full" MUSI failować).
- [x] Test: remove środkowego fd → `count-1`, pozostałe zachowane, brak dziur w iteracji.
- [x] Test: remove nieobecnego fd → `false`, bez zmian.

**Weryfikacja:**
- [x] Weryfikacja: `bash test/host/run.sh` zielone; nowy suite `test_ws_client_set` w liczniku (449 testów, 0 failures; 7 nowych ws_client_set).

---

### Unit 2: Broadcast w `ws_telemetry.c` (serializacja raz, send do wszystkich, GC per-fd) — **M**

Zależności: Unit 1. Realizuje: R1, R2, R3, R4.

**Implementacja:**
- [x] `components/web_panel/src/ws_telemetry.c` — zamień `s_client_fd` na `ws_client_set`.
- [x] `push_work`: `count==0` → wyczyść flagę i wróć; inaczej `snapshot_to_json` RAZ, pętla `for_each` fd → `httpd_ws_send_frame_async`; per-fd błąd → `remove` + log; na końcu wyczyść `s_push_in_flight`.
- [x] `ws_telemetry_register`: `add`; `FULL` → log-and-continue, zwróć `ESP_OK`; `ADDED`/`ALREADY_PRESENT` → `ensure_timer`.
- [x] `on_tick`: warunek `s_server==NULL` (usuń zależność od pojedynczego fd).
- [x] `ws_telemetry_stop`: wyczyść listę.
- [x] Komentarz modułu: inwariant „mutacje listy tylko na tasku httpd".

**Testy:**
- [x] Test: (poprzez Unit 1) logika add/remove/full pokryta — bez nowych host-testów logiki.
- [ ] Test [E2E/device]: panel + app jednocześnie → OBA pokazują żywą telemetrię (~10 Hz, ta sama treść).
- [ ] Test [E2E/device]: rozłącz jednego klienta → drugi dostaje dalej; log „dropping client (fd=…)".
- [ ] Test [E2E/device]: reconnect klienta → wraca do listy, telemetria wznowiona.

**Weryfikacja:**
- [ ] Weryfikacja: `idf.py build` (esp32s3) zielone ✅ (2026-07-03, ESP-IDF v5.5, bin 37% free); na urządzeniu panel i app równolegle; drop/reconnect jednego nie ubija drugiego ⏳ device-E2E.

---

### Unit 3: Sizing socketów httpd (spójność limitów) — **S**

Zależności: Unit 2. Realizuje: R5.

**Implementacja:**
- [x] `components/web_panel/src/http_server.c` — `config.max_open_sockets = 7;` jawnie + komentarz wiążący `WS_TELEMETRY_MAX_CLIENTS(4) + HTTP_headroom(3) ≤ 7 ≤ CONFIG_LWIP_MAX_SOCKETS(10)-3`.

**Testy:**
- [ ] Test [E2E/device]: panel + app + 1-2 zakładki panelu (do 4 WS) → wszyscy dostają telemetrię; równoległe `/api/params`/ładowanie panelu nie wypycha klienta WS (brak LRU-purge aktywnego).

**Weryfikacja:**
- [ ] Weryfikacja: `idf.py build` zielone ✅ (2026-07-03); komentarz wiążący limity obecny ✅; na urządzeniu brak wypychania klienta WS pod obciążeniem HTTP ⏳ device-E2E.

---

## Źródła

- Requirements doc: (brak — diagnoza 2026-07-03)
- Plan techniczny: docs/plans/2026-07-03-001-feat-ws-telemetry-multi-client-plan.md
