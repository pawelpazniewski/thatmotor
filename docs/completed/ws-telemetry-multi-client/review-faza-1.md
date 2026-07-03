# Review — Faza 1 (Multi-client telemetria, Unit 1-3) — ws-telemetry-multi-client

Data: 2026-07-03
Reviewer: dev-autopilot (orkiestrator — bezpośrednio; wrapper-agent review pominięty z powodu wcześniejszego stalla na zagnieżdżonym fan-oucie; zmiana mała i dobrze ograniczona → review wielosoczewkowy w pętli głównej)

## Severity gate: ✅ CZYSTE (tylko 1 P3, brak P1/P2)

### Liczniki
- 🔴 P1: **0** · 🟠 P2: **0** · 🟡 P3: **1**
- Typy: P3 (1) KOD (teoretyczny)
- E2E: N/D automatycznie (firmware C); device-E2E manualne odroczone

## Walidacja
- **Host-testy:** `bash test/host/run.sh` → **449/0** (7 nowych `ws_client_set`, moc wyroczni).
- **`idf.py build`:** PASS (ESP-IDF v5.5, esp32s3, bin 37% free). Naprawiono brakującą rejestrację `ws_client_set.c` w `components/web_panel/CMakeLists.txt` (link failował przed → potem zielone).

## Weryfikacja kluczowych punktów

**Unit 1 — `ws_client_set` (pure):**
- `add`: skanuje duplikat (→ ALREADY_PRESENT) + śledzi pierwszy wolny slot; brak wolnego → FULL (lista nienaruszona); insert → ADDED. ✓
- `remove`/`contains`/`count` poprawne; `ws_client_set_at` iteruje bez dziur (index 0..count-1 zawsze ważne). ✓
- Host-testy z mocą wyroczni: „add ponad limit → FULL, lista nienaruszona" (mutacja „nadpisz mimo full" failuje), idempotencja duplikatu, remove środkowego bez dziur. ✓

**Unit 2 — broadcast `ws_telemetry.c`:**
- `push_work`: `count==0` → early-return; serializacja `snapshot_to_json` RAZ; pętla po `ws_client_set_at(i)` → `httpd_ws_send_frame_async`; per-fd błąd → `failed[]`, usunięcie PO pętli (brak mutacji podczas iteracji). ✓
- **Inwariant wątkowości** udokumentowany w nagłówku modułu; mutacje listy tylko na tasku httpd (`register` + `push_work`), `on_tick` (esp_timer) nie tyka listy → bez mutexa. ✓
- **Leniwa inicjalizacja** (`s_clients_ready`/`ensure_clients_init`): zapobiega fałszywemu klientowi fd==0 ze statycznego zero-init; wykonana w `register` przed startem timera (a więc przed jakimkolwiek `push_work`). ✓
- `register`: `add`, FULL → log + `ESP_OK` (handshake się udaje, klient bez telemetrii). `stop`: re-init listy + `s_server=NULL`. ✓
- Global `s_push_in_flight` zachowany (jedna serializacja+broadcast/tick). ✓

**Unit 3 — sizing:**
- `config.max_open_sockets = 7` jawnie + komentarz wiążący `WS_MAX(4)+HTTP(3)≤7≤LWIP(10)-3`. ✓

## Decyzje implementacyjne (ocenione OK)
- GC per-fd przez `failed[]` zebrane w pętli, usunięte po — czyste, unika mutacji podczas iteracji po indeksie.
- Usunięcie `atomic_store(&s_push_in_flight,false)` z `register` (relikt pojedynczego slotu) — reset flagi podczas zakolejkowanego push mógłby dać podwójne queue; słusznie usunięte.
- `on_tick` sprawdza tylko `s_server==NULL` (wariant no-op w `push_work` przy 0 klientów) — zgodne z rekomendacją planu.

## Finding P3 (nit — nieblokujący, niefixowany)
- 🟡 [P3][KOD] `ws_client_set_add(set, fd)` dla `fd < 0` zwróciłby `ALREADY_PRESENT` (dopasowanie do wolnego slotu `-1`) zamiast odrzucić. Czysto teoretyczne — httpd zawsze dostarcza fd ≥ 0; nagłówek dokumentuje kontrakt „negative fds never stored". Opcjonalny explicit guard `if (fd < 0) return ...`.

## Device-E2E (odroczone — wymaga sprzętu z AP silnika)
- Panel + app równolegle → oba żywe; drop/reconnect jednego nie rusza drugiego; do 4 WS + obciążenie HTTP bez LRU-purge aktywnego klienta. Natywnie na wodzie/sprzęcie.

## Decyzja
Severity gate CZYSTE (1× P3 teoretyczny) → zakończenie fazy bez cyklu fix. Jedyna faza → przejście do complete/compound.
