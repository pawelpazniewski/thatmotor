# Learned Patterns

Reguły wyciągnięte z rozwiązanych problemów w docs/solutions/. Zarządzane przez /dev-compound i /dev-compound-refresh.

<!-- rule-count: 5 -->

- **Recency/elapsed w jednej domenie licznika, wrap-safe**: Różnicę czasu/licznika licz przez unsigned modular subtraction (`now - last`) w JEDNEJ domenie zegara; trzymaj raw tick i konwertuj na jednostki fizyczne dopiero przy porównaniu. Nie mieszaj `esp_timer_get_time()` z licznikiem capture — daje cicho błędną recency po wrapie. Dodaj host-test wokół granicy 2^N.
  Source: docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md

- **Test clampu/limitu z mocą wyroczni**: Testując transformację ograniczającą (clamp/saturate/limit) podawaj wejście POZA zakresem docelowym — wejście == oczekiwane wyjście jest tożsamością i przechodzi z transformacją LUB bez niej (brak wyroczni). Reguła kciuka: "czy test FAILuje, gdy usunę testowaną transformację?".
  Source: docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md

- **Pure ⊥ HAL: wyciągaj czyste funkcje decyzyjne zza HAL**: Dla każdej nietrywialnej decyzji za HAL (NVS/PWM/HTTP) wyekstrahuj czystą funkcję `(inputs)→(outputs)` bez include `esp_*.h`/`driver/*.h` i pokryj host-testem; HAL adapter trzymaj cienki (mapuj `esp_err_t` na enum domenowy, decyzję deleguj). Pozwala testować logikę firmware na hoście bez sprzętu.
  Source: docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md

- **Zdrowie łącza z fazy połączenia, nie z zamrożonej ramki**: Flagę zdrowia łącza (np. `linkDown`) wyprowadzaj WYŁĄCZNIE ze stanu/fazy połączenia (watchdog/ConnectionState), nigdy z pól ostatniej odebranej ramki — zamrożona zdrowa ramka jest nieodróżnialna od żywego łącza i cicho pokaże stan bezpieczny mimo zerwania.
  Source: docs/solutions/testing-issues/2026-06-20-android-pure-hal-host-testable-logic.md

- **Cleanup foreground service bez wycieków**: W `onDestroy` zwalniaj wake lock w `finally` przed `super.onDestroy()`, stosuj guard "teardown-once" (null-uj callback po przejęciu) i flagę `isBound` przed `unbindService` (chroni przed `IllegalArgumentException` po nieudanym `bindService`).
  Source: docs/solutions/testing-issues/2026-06-20-android-pure-hal-host-testable-logic.md
