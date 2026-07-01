# Learned Patterns

Reguły wyciągnięte z rozwiązanych problemów w docs/solutions/. Zarządzane przez /dev-compound i /dev-compound-refresh.

<!-- rule-count: 10 -->

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
