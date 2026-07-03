# Learned Patterns

Reguły wyciągnięte z rozwiązanych problemów w docs/solutions/. Zarządzane przez /dev-compound i /dev-compound-refresh.

<!-- rule-count: 16 -->

- **Recency/elapsed w jednej domenie licznika, wrap-safe**: Różnicę czasu/licznika licz przez unsigned modular subtraction (`now - last`) w JEDNEJ domenie zegara; trzymaj raw tick i konwertuj na jednostki fizyczne dopiero przy porównaniu. Nie mieszaj `esp_timer_get_time()` z licznikiem capture — daje cicho błędną recency po wrapie. Dodaj host-test wokół granicy 2^N.
  Source: docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md

- **Test clampu/limitu z mocą wyroczni**: Testując transformację ograniczającą (clamp/saturate/limit) podawaj wejście POZA zakresem docelowym — wejście == oczekiwane wyjście jest tożsamością i przechodzi z transformacją LUB bez niej (brak wyroczni). Reguła kciuka: "czy test FAILuje, gdy usunę testowaną transformację?".
  Source: docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md

- **Pure ⊥ HAL: wyciągaj czyste funkcje decyzyjne zza HAL**: Dla każdej nietrywialnej decyzji za HAL (NVS/PWM/HTTP) wyekstrahuj czystą funkcję `(inputs)→(outputs)` bez include `esp_*.h`/`driver/*.h` i pokryj host-testem; HAL adapter trzymaj cienki (mapuj `esp_err_t` na enum domenowy, decyzję deleguj). Pozwala testować logikę firmware na hoście bez sprzętu.
  Source: docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md

- **Override sterowania nie może osłabić failsafe — pierwszeństwo strukturalne**: Sensor-driven override licz PO maszynie stanów i wykonuj WYŁĄCZNIE w bezpiecznej gałęzi (np. ARMED); poza nią forsuj OFF i bezwarunkowo ustępuj. Sensory podpinaj jako wejścia override'u, NIGDY do wejść failsafe/maszyny stanów (rc_valid, sm_inputs). Pierwszeństwo failsafe = jedna bramka, nie warunki rozsiane po kodzie. Test pierwszeństwa musi wchodzić w stan, który bez bramki by przeciekł (moc wyroczni).
  Source: docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md

- **fresh ≠ valid: jakość re-waliduj co cykl, nie tylko przy wejściu**: Okno świeżości (timestamp ostatniego dobrego odczytu) bywa wciąż "fresh" gdy realny sygnał właśnie zniknął (seed-fresh). Bramkuj sterowanie na realnym warunku jakości (np. gps_has_fix) KAŻDY cykl podczas hold, nie tylko przy wejściu — sam predykat świeżości nie świadczy o ważnym odczycie.
  Source: docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md

- **Flash I/O (erase/write) do tasku tła, nigdy w pętli RT**: Kasowanie/zapis sektora flash blokuje na dziesiątki ms i wprowadza jitter do pętli sterującej z twardym deadlinem. Trzymaj CAŁE I/O flash (log/blackbox) w osobnym tasku niskiego prio: pętla RT publikuje snapshot, task tła robi nieblokujący peek i zapisuje best-effort (błąd = log-and-continue). Utrata próbki diagnostycznej akceptowalna, jitter pętli nie.
  Source: docs/solutions/performance-issues/2026-07-01-rt-loop-flash-io-background-task.md

- **Cursor/seq ringu na flashu zasiej skanem regionu na init**: Write cursor i licznik sesji żyjące w RAM po reboocie resetują się do 0 → nowa sesja nadpisuje ring od slotu 0 z tym samym id (kolizja + przeplot danych, cicho). Na init skanuj region fizycznie i złóż czystą funkcją ziarno: `session_seq=max(header seq)`, `cursor=ostatni ważny slot+1`. Host-testuj z mocą wyroczni (mutacja decyzji do {0,0} MUSI FAILować). Partycję data trzymaj NA KOŃCU partitions.csv, by nie przesunąć offsetów.
  Source: docs/solutions/runtime-errors/2026-07-01-ring-buffer-cursor-reboot-resume-seed.md

- **Nowe źródło sterowania (sieć/BLE) = własny watchdog świeżości, degradacja to PAUSE nie abort**: Gdy źródłem komend staje się link inny niż RC, dołóż osobny predykat świeżości (reuse `sensor_is_fresh` w domenie `now_ms`) i bramkuj nim TYLKO to źródło; nigdy nie podpinaj go do `rc_valid`/`sm_inputs`/failsafe. Utrata linku → PAUSED z zachowanym latchem celu (transient loss wznawia), NIE abort. Latch kasuje wyłącznie manualny override/preempt, liczony tylko w gałęzi ARMED. Rozszerza pierwszeństwo failsafe (2026-06-29) na źródła sieciowe.
  Source: docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md

- **Waliduj untrusted liczby w domenie double PRZED castem na int**: `isfinite()` + sprawdzenie zakresu rób w domenie `double` zanim rzutujesz na `int32`. Cast `INF`/`NaN` na int to UB, a walidacja PO castcie widzi już zawiniętą wartość (`4.39e9` → `~1e8` przechodzi naiwny zakres). Moc wyroczni: test z wejściem POZA `int32` MUSI failować naiwny cast-then-check.
  Source: docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md

- **Retencja stanu (cel w PAUSE) = własność czystego rdzenia, nie kontrakt na stabilny upstream**: Gdy stan pauzy ma zachować referencję, rdzeń re-latchuje `ref_*` z wejścia TYLKO na wejściu w stan albo gdy źródło świeże; w pauzie zachowuje ostatnią dobrą wartość i NIE nadpisuje z wejścia. Nie polegaj na tym, że upstream nie wyzeruje wejścia — wyzerowany upstream + bezwarunkowy re-latch = ciche null-island (0,0). Mutacja „bezwarunkowy re-latch" MUSI failować test retencji.
  Source: docs/solutions/runtime-errors/2026-07-01-goto-app-override-validation-retention.md

- **Reset przy operacji sieciowej: ustal reset reason ze sprzętu ZANIM założysz przyczynę**: Nie zgaduj „to WDT/brownout". Złap `rst:0xNN (REASON)` z portu (`0xc`=RTC_SW_CPU_RST/panic, `0x0f`=brownout, `0x07/08`=TG WDT) i zdekoduj `Saved PC`/backtrace przez `xtensa-*-addr2line -e build/*.elf`. Błędna diagnoza „Task WDT" naprawiła zły trop (przeniesienie pętli na CPU1), a crash był w tasku httpd. USB-CDC gubi banner panicu — użyj readera z reopen-on-disconnect albo `esp_reset_reason()` w bootlogu.
  Source: docs/solutions/runtime-errors/2026-07-01-httpd-task-stack-overflow-panel-reload.md

- **Odwracasz dostarczony failsafe = chirurgiczny flip JEDNEJ flagi + jawna ochrona ortogonalnych inwariantów; zmiana spec = oracle-rewrite testu, NIE osłabienie**: Gdy współdzielony predykat pełni kilka ról (np. `comms_fresh` = link-failsafe ORAZ bramka re-latchu/anty-null-island), flipnij tylko rolę objętą zmianą i jawnie zachowaj+przetestuj resztę (retencja, sensor-pause, RC-failsafe). Test asertujący stare zachowanie PRZEPISZ na nowe z zachowaniem mocy wyroczni: przywrócenie starego kodu MUSI czynić test czerwonym (potwierdź test-first). To odróżnia legalny oracle-rewrite (przeniesienie wyroczni) od anty-patternu „test weakening" (zdjęcie wyroczni). Nowy sub-feature zwijaj na istniejące źródło (`goto(własny fix)`), a rozróżnienia rób lokalnie u konsumenta, nie w telemetrii; czysty helper kładź w komponencie bez cyklu importów (duplikacja stałych < circular dependency).
  Source: docs/solutions/testing-issues/2026-07-03-safe-failsafe-inversion-oracle-rewrite-on-spec-change.md

- **Nie zostawiaj domyślnego 4 KB stosu httpd gdy handlery mają duże bufory na stosie**: `HTTPD_DEFAULT_CONFIG().stack_size`=4096 przepełnia się, gdy handler kładzie na stosie bufory JSON (req+body+nested serialize ~kilka KB) + rekursja cJSON → FreeRTOS canary → panic → `RTC_SW_CPU_RST`. Ustaw `config.stack_size` WYPROWADZONY z rozmiaru buforów (np. `2*BODY_MAX + SERIALIZE_MAX + headroom`), żeby rósł razem z liczbą pól i nie zdryfował po cichu.
  Source: docs/solutions/runtime-errors/2026-07-01-httpd-task-stack-overflow-panel-reload.md

- **Gdy 0 jest WAŻNĄ wartością domeny, NIE koduj „pusty" jako 0 ani nie polegaj na zero-init**: fd/index/id gdzie `0` to poprawna wartość (fd 0 = stdin, serwer daje niskie fd) — „wolny slot" koduj sentinelem POZA domeną (`-1`) i wymuś jawną inicjalizację przed pierwszym odczytem. Zero-init tablicy (BSS/`static`) zostawia sloty=0 → pusty zbiór wygląda jak N klientów o fd 0 (cichy broadcast na zły socket). Host-test: świeży zbiór ma count==0.
  Source: docs/solutions/runtime-errors/2026-07-03-ws-client-set-zero-init-sentinel-single-thread-invariant.md

- **Inwariant jednowątkowy zamiast mutexa gdy mutacje da się skanalizować do jednego taska**: Zanim dodasz lock do współdzielonego stanu, sprawdź czy WSZYSTKIE mutacje da się zepchnąć na jeden task (np. `httpd_queue_work` → task httpd), a inne taski (callback timera) tylko czytają/pstrykają atom i kolejkują pracę, NIGDY nie tykają struktury. Jeśli tak — udokumentuj inwariant w nagłówku modułu jako „do not break" (które funkcje/taski wolno, które nie) i pomiń mutex. Taniej i prościej niż lock, ale tylko gdy inwariant jest jawny i pilnowany. Dodatkowo: nie mutuj kolekcji podczas iteracji po indeksie — zbieraj do lokalnego `failed[]` i usuwaj DOPIERO po pętli.
  Source: docs/solutions/runtime-errors/2026-07-03-ws-client-set-zero-init-sentinel-single-thread-invariant.md

- **Klient chce „na żywo", urządzenie ma celowy state-gate → dostosuj UI do gate'u, NIE osłabiaj failsafe i NIE polegaj na błędzie serwera**: Gdy firmware stosuje komendę tylko w bezpiecznym stanie (np. trim tylko DISARMED), a poza nim zwraca 200 OK i cicho ją ignoruje — bramkuj przycisk/sekcję w UI wg stanu z TELEMETRII (`isEnabled(state)`), bo 200 OK ≠ wykonano i nie ma błędu do wykrycia. UI-guard odwzorowuje gate urządzenia, nie zastępuje go (realny gate zostaje w firmware, kod safety-critical nietknięty). Wartość zapisywaną na urządzeniu trzymaj jako jedno źródło prawdy z telemetrii (bez lokalnej kopii/optimistic-UI — opóźnienie ramki rozjeżdża kopie). Logikę decyzji (format, staleness→„—", wybór aktywnego źródła, gate) wypchnij z widoku do czystego modułu bez UI-frameworka (Pure⊥HAL na kliencie) i pokryj host-testami z mocą wyroczni; widok składa gotowe stringi.
  Source: docs/solutions/testing-issues/2026-07-03-ios-client-mirror-firmware-silent-gate-pure-presentation.md
