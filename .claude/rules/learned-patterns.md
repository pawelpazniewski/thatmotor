# Learned Patterns

Reguły wyciągnięte z rozwiązanych problemów w docs/solutions/. Zarządzane przez /dev-compound i /dev-compound-refresh.

<!-- rule-count: 5 -->

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
