---
title: "Multi-client WS telemetry: zero-init vs sentinel gdy 0 jest ważną wartością, i inwariant jednowątkowy zamiast mutexa"
date: 2026-07-03
category: runtime-errors
severity: high
stack:
  - ESP-IDF
  - C
  - FreeRTOS
  - esp_http_server
tags:
  - zero-init
  - sentinel
  - file-descriptor
  - concurrency
  - threading-invariant
  - mutex
  - websocket
  - iterate-and-mutate
  - pure-hal
status: verified
last_verified: 2026-07-03
---

# Multi-client WS telemetry: zero-init vs sentinel i inwariant jednowątkowy zamiast mutexa

Rozszerzenie telemetrii WebSocket z jednego klienta na listę (panel + app + spare)
wymagało trzymania zbioru deskryptorów plików (fd) klientów. Podczas implementacji
ujawniły się dwie nieintuicyjne pułapki: (1) zero-init tablicy fd daje fałszywego
klienta, bo `fd==0` jest WAŻNYM deskryptorem; (2) kuszące dodanie mutexa vs tańszy
inwariant kanalizacji mutacji do jednego taska.

## Symptomy

- Statyczna tablica `int fds[N]` w pliku (BSS) jest zero-initowana przez C → wszystkie
  sloty = `0`. Jeśli slot "pusty" jest kodowany jako `0`, to każdy slot wygląda jak
  podłączony klient z `fd==0`. `stdin` to fd 0; realny WS fd też bywa niskim
  dodatnim intem — `0` jest w domenie WAŻNYCH deskryptorów, więc broadcast poszedłby
  do fikcyjnego/cudzego fd (ciche `httpd_ws_send_frame_async` na złym sockecie).
- Lista klientów mutowana z dwóch tasków (httpd handler + callback timera `esp_timer`)
  = data race na współdzielonej strukturze bez widocznego błędu kompilacji.
- Usuwanie fd, który zawiódł przy wysyłce, w TRAKCIE pętli iterującej po indeksie
  = przeskoczenie/podwójne odwiedzenie elementu (mutacja zbioru podczas iteracji).

## Root Cause

1. **Zero-init ≠ "pusty" gdy 0 należy do domeny wartości.** Domyślny zero-init C
   (BSS/`static`) inicjalizuje `int` na `0`. Deskryptor `0` jest poprawny (to `stdin`,
   a i sam serwer może przydzielić niski fd), więc `0` nie może jednocześnie znaczyć
   "wolny slot" i być realną wartością. Bez jawnej inicjalizacji na sentinel `-1`
   pusty zbiór wygląda jak zbiór N klientów o fd 0.
2. **Kuszący mutex tam, gdzie wystarcza inwariant.** Współdzielona lista sugeruje
   blokadę, ale wszystkie MUTACJE dają się skanalizować do jednego taska (httpd):
   `register` woła się z ws_handlera (task httpd), a `push_work` jest dyspozowane
   przez `httpd_queue_work` → też task httpd. Callback timera (`on_tick`, task
   `esp_timer`) świadomie NIE tyka listy — tylko czyta `s_server`, pstryka atomową
   flagę in-flight i kolejkuje `push_work`. Jedno źródło mutacji = brak wyścigu bez
   mutexa.
3. **Iterate-and-mutate.** Zbieranie i usuwanie nieudanych fd w tej samej pętli
   po indeksie narusza spójność iteracji.

## Rozwiązanie

### 1. Sentinel `-1` + wymuszona (leniwa) inicjalizacja — nigdy nie polegaj na zero-init

```c
/* Free-slot sentinel. A valid WS fd is always >= 0, so -1 is unambiguous. */
#define WS_CLIENT_FREE_SLOT (-1)

void ws_client_set_init(ws_client_set *set) {
    for (size_t i = 0; i < WS_TELEMETRY_MAX_CLIENTS; ++i) {
        set->fds[i] = WS_CLIENT_FREE_SLOT;   /* NIE zostawiaj 0 z zero-init */
    }
}
```

Static-storage instancja MUSI zostać jawnie zainicjalizowana zanim ktokolwiek ją
odczyta — tu leniwie, na tasku httpd (respektuje inwariant mutacji):

```c
static ws_client_set s_clients;
static bool s_clients_ready;

/* Static zero-init would leave fds at 0 (a valid-looking fd), so an explicit
 * init is required. Runs on the httpd task (register). */
static void ensure_clients_init(void) {
    if (!s_clients_ready) {
        ws_client_set_init(&s_clients);
        s_clients_ready = true;
    }
}
```

### 2. Inwariant jednowątkowy zamiast mutexa — udokumentowany jako "do not break"

```c
/*
 * Threading invariant (CRITICAL — do not break):
 *   The client list (s_clients) is mutated ONLY on the httpd task:
 *     - ws_telemetry_register (called from the httpd ws_handler)
 *     - push_work            (dispatched via httpd_queue_work -> httpd task)
 *   on_tick runs on the esp_timer task and MUST NOT touch the list. It only
 *   reads s_server, flips the atomic in-flight flag, and queues push_work.
 *   Because every list mutation is serialised on the single httpd task, the set
 *   needs no mutex. Adding a list mutation to on_tick (or any other task) would
 *   introduce a data race and require locking — don't.
 */
```

Callback timera nie dotyka listy — deleguje pracę na task httpd:

```c
static void on_tick(void *arg) {
    if (s_server == NULL) return;
    bool expected = false;
    if (atomic_compare_exchange_strong(&s_push_in_flight, &expected, true)) {
        httpd_queue_work(s_server, push_work, NULL);  /* mutacja listy zajdzie na tasku httpd */
    }
}
```

### 3. GC kolekcji po pętli, nie w trakcie iteracji

```c
/* Collect failed fds and remove them AFTER the loop so we never mutate the
 * set while iterating it by index. */
int failed[WS_TELEMETRY_MAX_CLIENTS];
size_t failed_count = 0;
for (size_t i = 0; i < count; ++i) {
    int fd = ws_client_set_at(&s_clients, i);
    if (httpd_ws_send_frame_async(s_server, fd, &frame) != ESP_OK) {
        failed[failed_count++] = fd;            /* zbierz, nie usuwaj teraz */
    }
}
for (size_t j = 0; j < failed_count; ++j) {
    ws_client_set_remove(&s_clients, failed[j]); /* usuń DOPIERO po pętli */
}
```

### 4. Pure ⊥ HAL + sprzężenie budżetu socketów

- Lista fd jako czysty, framework-agnostyczny `ws_client_set` (bez `esp_*`),
  host-testowana z mocą wyroczni (m.in. "add mimo full → `WS_CLIENT_FULL`, lista
  nienaruszona"). Broadcast HAL cienki.
- `config.max_open_sockets` ustawione JAWNIE i sprzężone z capem klientów:
  `WS_TELEMETRY_MAX_CLIENTS(4) + HTTP_headroom(3) = 7 <= CONFIG_LWIP_MAX_SOCKETS(10)`,
  by `lru_purge` nie eksmitował aktywnego klienta telemetrii.

## Komendy diagnostyczne

```bash
# Host-testy czystego zbioru (moc wyroczni: add-when-full nie mutuje listy)
cd test/host && make && ./build/host_tests

# Grep za slotami/sentinelami — czy gdzieś 0 udaje "pusty"?
grep -rn "FREE_SLOT\|== 0\|zero-init\|memset" components/web_panel/src/ws_client_set.c

# Weryfikacja że on_tick nie mutuje listy (tylko queue_work)
grep -n "s_clients\|ws_client_set_" components/web_panel/src/ws_telemetry.c
```

## Zapobieganie

- Gdy `0` jest WAŻNĄ wartością domeny (fd, index, id, klucz), NIE koduj "pusty/wolny"
  jako `0` i NIE polegaj na zero-init — użyj sentinela poza domeną (`-1`) i wymuś
  jawną inicjalizację przed pierwszym odczytem (host-test: świeży zbiór ma count==0).
- Zanim sięgniesz po mutex, sprawdź czy da się skanalizować WSZYSTKIE mutacje do
  jednego taska (np. `httpd_queue_work`). Jeśli tak — udokumentuj inwariant w nagłówku
  modułu jako "do not break" i wskaż które funkcje/taski wolno, a które nie tykają
  stanu. Taniej i prościej niż lock, ale tylko gdy inwariant jest jawny i pilnowany.
- Nigdy nie mutuj kolekcji podczas iteracji po indeksie — zbieraj do lokalnego bufora
  i aplikuj usunięcia po pętli.

## Powiązane

- docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md
  (wzorzec pure ⊥ HAL — tu zastosowany do listy fd)
- docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md
  (moc wyroczni — test "add mimo full" musi failować bez ochrony przed przepełnieniem)
- docs/solutions/runtime-errors/2026-07-01-httpd-task-stack-overflow-panel-reload.md
  (ten sam moduł httpd; stack_size i max_open_sockets jako świadome, sprzężone budżety)

## Kontekst

- Feature: `ws-telemetry-multi-client`, branch `feature/ws-telemetry-multi-client`.
- Pliki: `components/web_panel/include/ws_client_set.h`,
  `components/web_panel/src/ws_client_set.c`,
  `components/web_panel/src/ws_telemetry.c`,
  `components/web_panel/src/http_server.c`,
  `test/host/test_ws_client_set.c`.
- Weryfikacja: host-testy zielone (449/0 wg review Fazy 1), ESP-IDF build zielony.
- ESP-IDF `esp_http_server`: `httpd_queue_work` dyspozuje pracę na task httpd —
  to jest mechanizm kanalizacji mutacji wykorzystany w inwariancie.
