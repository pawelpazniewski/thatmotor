# Kontekst: Multi-client telemetria WS

Branch: `feature/ws-telemetry-multi-client`
Ostatnia aktualizacja: 2026-07-03

## Stan implementacji (Faza 1 — ukończona logika + compile-verify)

- **Unit 1** ✅ `ws_client_set` (pure) + 7 host-testów (moc wyroczni: „add mimo full → FULL, lista nienaruszona", idempotencja duplikatu). Commit `50ff4e7`.
- **Unit 2** ✅ broadcast w `ws_telemetry.c` (serializacja RAZ, pętla `ws_client_set_at`, GC per-fd po pętli przez zebranie `failed[]`, `register`→`add`/FULL log+ESP_OK, `on_tick`→`s_server==NULL`, `stop`→wyczyść listę, inwariant wątkowości w komentarzu modułu). Zarejestrowano `ws_client_set.c` w `components/web_panel/CMakeLists.txt`. Commit `fd76522`.
- **Unit 3** ✅ `http_server.c` `config.max_open_sockets = 7` + komentarz wiążący limity. Commit `dd5fca2`.
- Walidacja: `bash test/host/run.sh` = 449 testów / 0 failures (7 nowych). `idf.py build` (esp32s3, ESP-IDF v5.5) = zielone, bin 37% free.
- **Pozostaje dla review:** device-E2E (panel+app równolegle, drop/reconnect, obciążenie HTTP bez wypychania WS) — manualne na sprzęcie.

### Nota implementacyjna (GC per-fd)
`push_work` zbiera fd z nieudanym send do lokalnego `failed[WS_TELEMETRY_MAX_CLIENTS]` i usuwa je z listy DOPIERO po pętli — nie mutuje zbioru w trakcie iteracji po indeksie (`ws_client_set_at`). `s_clients` ma leniwą inicjalizację (`ensure_clients_init` na tasku httpd), bo statyczny zero-init dałby fd==0 (pozornie ważny).

## Powiązane pliki

### Do stworzenia
- `components/web_panel/include/ws_client_set.h` — pure API listy fd.
- `components/web_panel/src/ws_client_set.c` — pure implementacja (stała tablica, add/remove/contains/count/iteracja).
- `test/host/test_ws_client_set.c` — host-testy Unity (+ wpis w `test/host/CMakeLists.txt`).

### Do modyfikacji
- `components/web_panel/src/ws_telemetry.c` — `s_client_fd` → `ws_client_set`; `push_work` serializuje raz + broadcast pętlą + GC per-fd; `register` → `add`; `stop` → wyczyść listę.
- `components/web_panel/include/ws_telemetry.h` — ew. `WS_TELEMETRY_MAX_CLIENTS` (albo w ws_client_set.h).
- `components/web_panel/src/http_server.c` — `config.max_open_sockets = 7;` jawnie + komentarz wiążący limity (`ws_handler` przy :237 woła `ws_telemetry_register`).

### Referencje (nie ruszać logiki)
- `sdkconfig`: `CONFIG_LWIP_MAX_SOCKETS=10` → `max_open_sockets` max = 7.
- `components/web_panel/include/ws_telemetry.h`: `WS_TELEMETRY_PERIOD_MS=100`.

## Decyzje techniczne

- **Lista jako pure `ws_client_set`** — testowalna z mocą wyroczni; HAL send cienki (pure⊥HAL).
- **`WS_TELEMETRY_MAX_CLIENTS = 4`** — realnie 2 (panel+app), zapas 2; przy `max_open_sockets=7` zostają ≥3 sockety HTTP → LRU nie wypchnie klienta WS.
- **`max_open_sockets = 7` jawnie** — komentarz wiążący `WS_MAX(4) + HTTP_headroom(3) ≤ 7 ≤ LWIP(10)-3`, by wzrost `WS_MAX` wymuszał świadomą rewizję.
- **Pełna lista → odrzuć najnowszego** (log-and-continue), NIE eksmituj istniejącego; handshake i tak się udaje.
- **GC przy błędzie send** — usuń TEN fd, kontynuuj do reszty (zamiast zerowania jedynego slotu).
- **Global `s_push_in_flight` zostaje** — jedna serializacja + broadcast na tick.

## Inwariant do zachowania

**Mutacje listy klientów TYLKO na tasku httpd** (`register` + `push_work`). `on_tick` (task esp_timer) nie tyka listy — tylko `s_server`, atomik, `httpd_queue_work`. Bez tego inwariantu potrzebny byłby mutex. Udokumentować w komentarzu modułu.

## Wiedza instytucjonalna

- `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md` — pure⊥HAL, host-test decyzji, HAL cienki.
- `docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md` — moc wyroczni (mutacja „add mimo full" MUSI failować).
- `docs/solutions/runtime-errors/2026-07-01-httpd-task-stack-overflow-panel-reload.md` — kontekst httpd task/stack/socket (dlaczego sizing socketów ma znaczenie).

## Zależności

- Unit 1 (pure lista, niezależny) → Unit 2 (broadcast używa listy) → Unit 3 (sizing po ustaleniu rozmiaru).
- Baza: `feature/app-spot-lock` (kompletny firmware goto+spot-lock+hold; base `feature/kayak-motor-firmware-v1` NIE ma goto → nie użyta).

## Otwarte pytania (odroczone do implementacji)

- `on_tick`: czytać atomowy licznik klientów czy zawsze kolejkować + `push_work` no-op przy pustej liście? Rekomendacja: no-op (prostsze, bez cross-task licznika).
- Hook httpd close-callback dla natychmiastowego GC czy wystarczy GC-przy-send (~1 tick staleness)? Rekomendacja: GC-przy-send.

## Źródła

- Requirements doc: (brak — diagnoza 2026-07-03)
- Plan techniczny: docs/plans/2026-07-03-001-feat-ws-telemetry-multi-client-plan.md
