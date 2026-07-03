# Plan: Multi-client telemetria WS

Branch: `feature/ws-telemetry-multi-client`
Ostatnia aktualizacja: 2026-07-03

## Podsumowanie wykonawcze

Telemetria WS firmware'u (`/ws`) obsługuje dziś **jednego klienta** (`s_client_fd`, jeden
`int`) — nowy handshake nadpisuje slot, więc podłączenie aplikacji iOS ucisza panel www (i
odwrotnie). Zmieniamy pojedynczy slot na **listę klientów + broadcast**, tak by panel i
aplikacja dostawały telemetrię równolegle. Zmiana firmware-only, izolowana w komponencie
`web_panel`; kontrakt WS dla iOS bez zmian.

## Cele i zakres

**Cele:**
- Telemetria WS jednocześnie do wielu klientów (panel + app + zapas) — R1.
- Nowy klient dokładany do listy, nie nadpisuje istniejących — R2.
- Klient rozłączony/wadliwy usuwany z listy (GC przy błędzie send); reszta dostaje dalej — R3.
- Serializacja JSON raz na tick, broadcast pętlą po fd — R4.
- Limit klientów spójny z limitem socketów httpd/LWIP (bez LRU-purge aktywnego klienta) — R5.

**Poza zakresem:**
- Zmiana formatu/treści/okresu telemetrii; per-klient stan/filtry; auth klientów WS; zmiana kontraktu WS dla iOS.

## Analiza obecnego stanu

`components/web_panel/src/ws_telemetry.c`: jeden `s_client_fd`; `ws_telemetry_register` nadpisuje;
`push_work` serializuje i wysyła do jednego fd, drop przy błędzie (`s_client_fd=-1`); `on_tick`
(timer, atomik `s_push_in_flight`, `httpd_queue_work`). `http_server.c`: `max_open_sockets`
nieustawione = domyślne 7; `CONFIG_LWIP_MAX_SOCKETS=10`.

**Inwariant wątkowości (fundament projektu):** `register` (z `ws_handler`) i `push_work` (z
`httpd_queue_work`) biegną na tym samym tasku httpd → mutacje listy jednowątkowe, **bez mutexa**.
`on_tick` (task esp_timer) NIE dotyka listy.

## Stan docelowy

Lista `ws_client_set` (stała tablica fd, `WS_TELEMETRY_MAX_CLIENTS=4`); `push_work` serializuje
raz i broadcastuje pętlą, usuwając fd przy błędzie; `register` dokłada (idempotentnie); `max_open_sockets`
ustawione jawnie na 7 z komentarzem wiążącym limity.

## Fazy wdrożenia

### Faza 1 — Multi-client telemetria (3 unity)
- **Unit 1** — Pure `ws_client_set` (lista fd) + host-testy.
- **Unit 2** — Broadcast w `ws_telemetry.c` (serializacja raz, send do wszystkich, GC per-fd).
- **Unit 3** — Sizing socketów httpd (`max_open_sockets` jawnie, komentarz wiążący limity).

## Kryteria akceptacji

- Panel www i aplikacja iOS pokazują telemetrię **jednocześnie** (ta sama treść, ~10 Hz).
- Rozłączenie jednego klienta nie przerywa telemetrii pozostałym; reconnect wraca.
- Serializacja JSON raz na tick (broadcast pętlą, nie per-klient alloc).
- `bash test/host/run.sh` zielone (nowy suite `ws_client_set` z mocą wyroczni); `idf.py build` (esp32s3) zielone.
- Pod obciążeniem HTTP aktywny klient WS nie zostaje wypchnięty (LRU).

## Ocena ryzyka i mitygacje

- **LRU-purge aktywnego klienta WS** przy pełnej tablicy socketów → `WS_MAX=4` + jawny `max_open_sockets=7` (zapas HTTP ≥3); komentarz wiążący limity.
- **Brak host-testu HAL send** → logika listy pokryta host-testami (Unit 1); broadcast weryfikowany na urządzeniu.
- **Regres jednowątkowości** (mutacja listy spoza taska httpd) → utrzymać inwariant, udokumentować w komentarzu modułu.

## Mierniki sukcesu

- Host-testy `ws_client_set`: mutacja „nadpisz mimo full" / „re-latch duplikatu" MUSI failować (moc wyroczni).
- Field-test: 2 klienci (panel+app) równolegle, drop/reconnect jednego nie rusza drugiego.

## Zależności

- Unit 2 zależy od Unit 1 (pure lista). Unit 3 zależy od Unit 2 (ustalony rozmiar listy).
- Bazuje na `feature/app-spot-lock` (kompletny firmware goto+spot-lock; `feature/kayak-motor-firmware-v1` NIE ma goto).

## Szacunki

- Unit 1: S · Unit 2: M · Unit 3: S. Całość ~1 dzień + weryfikacja na urządzeniu.

## Źródła

- Requirements doc: (brak — diagnoza 2026-07-03, pojedynczy slot `s_client_fd`)
- Plan techniczny: docs/plans/2026-07-03-001-feat-ws-telemetry-multi-client-plan.md
