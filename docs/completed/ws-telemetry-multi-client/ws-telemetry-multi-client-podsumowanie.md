# Podsumowanie ukończenia: Multi-client telemetria WS

**Zadanie:** ws-telemetry-multi-client
**Branch:** `feature/ws-telemetry-multi-client`
**Data ukończenia:** 2026-07-03

## Status końcowy

- **449/449 host-testów (Unity) PASS** — `bash test/host/run.sh` (+7 nowych `ws_client_set`, z mocą wyroczni).
- **`idf.py build` (target esp32s3, ESP-IDF v5.5) BUILD SUCCEEDED** — bin 37% wolne w partycji app.
- Wszystkie 3 unity (Faza 1, jedyna faza) zaimplementowane, zreviewowane i zacommitowane.
- **Review:** ✅ CZYSTE — 0× P1, 0× P2, 1× P3 (teoretyczny). Bez cyklu fix. Szczegóły: `review-faza-1.md`.
- Luki `[E2E/device]` (panel+app równolegle, drop/reconnect jednego klienta, obciążenie HTTP bez
  LRU-purge aktywnego klienta WS) **świadomie odłożone** — manualne na sprzęcie, nieautomatyzowalne
  bez AP silnika. NIE braki implementacji.

## Co zostało dostarczone

Telemetria WS firmware'u (`/ws`) obsługiwała dotąd **jednego klienta** (`s_client_fd`, jeden `int`) —
nowy handshake nadpisywał slot, więc podłączenie aplikacji iOS uciszało panel www (i odwrotnie).
Zmieniono pojedynczy slot na **listę klientów + broadcast**, tak by panel i aplikacja dostawały
telemetrię równolegle (~10 Hz, ta sama treść). Zmiana firmware-only, izolowana w komponencie
`web_panel`; kontrakt WS dla iOS bez zmian.

- **Unit 1 — Pure `ws_client_set` (lista fd) + host-testy [S]:**
  - Nowy czysty moduł: stała tablica `int fds[WS_TELEMETRY_MAX_CLIENTS]` (slot wolny = `-1`), zero
    zależności `esp_*`. API: `add`→enum `ADDED`/`ALREADY_PRESENT`/`FULL`, `remove(fd)`→bool,
    `contains`, `count`, `ws_client_set_at` (iteracja bez dziur, index 0..count-1 zawsze ważny).
  - 7 host-testów z mocą wyroczni: „add ponad limit → FULL, lista nienaruszona" (mutacja „nadpisz
    mimo full" MUSI failować), idempotencja duplikatu (`ALREADY_PRESENT`), remove środkowego bez dziur.
- **Unit 2 — Broadcast w `ws_telemetry.c` [M]:**
  - `s_client_fd` → `ws_client_set`. `push_work`: `count==0` → early-return; `snapshot_to_json` RAZ;
    pętla po `ws_client_set_at(i)` → `httpd_ws_send_frame_async`; per-fd błąd → zebranie do lokalnego
    `failed[]` i usunięcie DOPIERO po pętli (brak mutacji zbioru podczas iteracji po indeksie).
  - `register`: `add`; FULL → log-and-continue + `ESP_OK` (handshake się udaje, klient bez telemetrii),
    nie eksmituj istniejącego. `on_tick`: warunek `s_server==NULL` (usunięta zależność od pojedynczego fd).
    `stop`: re-init listy. Global `s_push_in_flight` zachowany (jedna serializacja+broadcast/tick).
  - **Leniwa inicjalizacja** (`ensure_clients_init` na tasku httpd): statyczny zero-init dałby fd==0
    (pozornie ważny klient) — stąd jawna inicjalizacja przed startem timera.
  - **Inwariant wątkowości** udokumentowany w nagłówku modułu: mutacje listy TYLKO na tasku httpd
    (`register` + `push_work`); `on_tick` (esp_timer) nie tyka listy → bez mutexa.
- **Unit 3 — Sizing socketów httpd [S]:**
  - `http_server.c`: `config.max_open_sockets = 7` jawnie + komentarz wiążący limity
    `WS_MAX(4) + HTTP_headroom(3) ≤ 7 ≤ CONFIG_LWIP_MAX_SOCKETS(10)-3`, by wzrost `WS_MAX` wymuszał
    świadomą rewizję i nie doszło do LRU-purge aktywnego klienta WS pod obciążeniem HTTP.

## Kluczowe decyzje

- **Lista jako pure `ws_client_set`** — testowalna z mocą wyroczni; HAL send cienki (Pure ⊥ HAL).
- **`WS_TELEMETRY_MAX_CLIENTS = 4`** — realnie 2 (panel+app), zapas 2; przy `max_open_sockets=7`
  zostają ≥3 sockety HTTP → LRU nie wypchnie klienta WS.
- **Pełna lista → odrzuć najnowszego** (log-and-continue), NIE eksmituj istniejącego.
- **GC per-fd przy błędzie send** — usuń TEN fd, kontynuuj do reszty; zebranie do `failed[]` po pętli
  zamiast mutacji podczas iteracji po indeksie.
- **Global `s_push_in_flight` zostaje** — jedna serializacja + broadcast na tick (nie per-klient alloc).
- **Usunięto `atomic_store(&s_push_in_flight,false)` z `register`** (relikt pojedynczego slotu) — reset
  flagi podczas zakolejkowanego push mógłby dać podwójne queue.

## Utworzone / zmodyfikowane pliki

**Utworzone:**
- `components/web_panel/include/ws_client_set.h` — pure API listy fd.
- `components/web_panel/src/ws_client_set.c` — pure implementacja.
- `test/host/test_ws_client_set.c` — 7 host-testów Unity (+ wpis w `test/host/CMakeLists.txt`).

**Zmodyfikowane:**
- `components/web_panel/src/ws_telemetry.c` — `s_client_fd` → `ws_client_set`; broadcast + GC per-fd;
  leniwa inicjalizacja; inwariant wątkowości w komentarzu modułu.
- `components/web_panel/CMakeLists.txt` — rejestracja `ws_client_set.c` (bez niej link failował).
- `components/web_panel/src/http_server.c` — `config.max_open_sockets = 7` + komentarz wiążący limity.

**Commity:** Unit 1 `50ff4e7`, Unit 2 `fd76522`, Unit 3 `dd5fca2`.

## Wyciągnięte wnioski

- **Statyczny zero-init a sentinel „wolny slot"** — gdy wolny slot = `0`/`-1` koliduje z prawidłową
  wartością danych (fd==0 to ważny deskryptor), zero-init tablicy tworzy fałszywego „klienta". Trzeba
  jawnej inicjalizacji sentinelem (tu `-1`), wykonanej deterministycznie przed pierwszym użyciem.
- **Nie mutuj kolekcji podczas iteracji po indeksie** — GC per-fd przez zebranie `failed[]` w pętli i
  usunięcie po niej; usuwanie w trakcie przesuwa indeksy i gubi elementy.
- **Inwariant jednowątkowości zamiast mutexa** — gdy wszystkie mutacje współdzielonej struktury biegną
  na tym samym tasku (tu httpd: `register` + `httpd_queue_work`→`push_work`), a inny task (esp_timer)
  tylko kolejkuje pracę, mutex jest zbędny. Inwariant MUSI być udokumentowany w komentarzu modułu, bo
  jego złamanie (mutacja spoza taska) jest ciche.
- **Komentarz wiążący limity** przy `max_open_sockets` — powiązanie `WS_MAX + headroom ≤ sockets ≤ LWIP`
  w komentarzu wymusza świadomą rewizję przy zmianie stałej, zamiast cichego dryfu do LRU-purge.
- **Moc wyroczni w teście listy** — test „add ponad limit" musi failować przy mutacji „nadpisz mimo
  full"; wejście przekraczające pojemność, nie tożsamościowe.

## Świadomie odroczone (nie blokuje archiwizacji)

- **device-E2E manualne na sprzęcie:** panel + app równolegle (oba żywe ~10 Hz), drop/reconnect jednego
  klienta nie rusza drugiego, do 4 WS + obciążenie HTTP (`/api/params`, ładowanie panelu) bez wypychania
  aktywnego klienta WS. Nieautomatyzowalne bez AP silnika (środowisko na wodzie/sprzęcie).
- **1× P3 teoretyczny:** `ws_client_set_add(set, fd)` dla `fd < 0` zwróciłby `ALREADY_PRESENT`
  (dopasowanie do wolnego slotu `-1`) zamiast odrzucić. Czysto teoretyczne — httpd zawsze dostarcza
  fd ≥ 0; nagłówek dokumentuje kontrakt „negative fds never stored". Opcjonalny explicit guard.
