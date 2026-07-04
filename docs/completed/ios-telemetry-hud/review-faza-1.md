# Code Review — Faza / Unit 1: Rozszerz kontrakt telemetrii

Data: 2026-07-03
Commit: `8f313f6`
Branch: `feature/kayak-motor-firmware-v1`
Reviewer: dev-autopilot (multi-lens, natywny iOS/Swift)

## Zakres

Pakiet Swift Package Manager `ios/KayakKit`, moduł `KayakContract`. Rozszerzenie
czystego typu `Telemetry` o pola `imu_calib`, `spot_lock_err_m`,
`spot_lock_bearing_deg10`, `servo_trim_us` + testy dekodowania.

Zmienione pliki:
- `ios/KayakKit/Sources/KayakContract/Telemetry.swift` (+13 linii)
- `ios/KayakKit/Tests/KayakContractTests/TelemetryDecodingTests.swift` (+41 linii)

Uwaga o środowisku: to natywny SwiftUI, NIE web. Unit 1 to czysty kontrakt bez UI.
Agent 5 (E2E browser-verifier) — **N/A** (brak URL/przeglądarki/localhost; Unit 1 nie
ma warstwy UI). Nie uruchamiano agent-browser.

## Severity gate

✅ **CZYSTE — GOTOWE DO KONTYNUACJI**

- 🔴 P1 (blocking): 0
- 🟠 P2 (important): 0
- 🟡 P3 (nit): 1

## Weryfikacja fazy

- `cd ios/KayakKit && swift test` → **53/53 zielone** ✔
- Nowe pola dostępne na `Telemetry` (`imuCalib`, `spotLockErrM`,
  `spotLockBearingDeg10`, `servoTrimUs`) + computed `spotLockBearingDegrees` ✔
- Kryterium fazy "swift test w KayakKit zielony; nowe pola dostępne na Telemetry" — **spełnione**.

## Cross-reference z firmware (ryzyko R1 z planu)

Klucze JSON zweryfikowane 1:1 wobec `components/web_panel/src/telemetry_json.c`:

| Klucz Swift CodingKey        | Firmware (telemetry_json.c) | Format | Zgodność |
|------------------------------|-----------------------------|--------|----------|
| `imu_calib`                  | `"imu_calib":%u`            | uint   | ✔        |
| `spot_lock_err_m`            | `"spot_lock_err_m":%u`      | uint   | ✔        |
| `spot_lock_bearing_deg10`    | `"spot_lock_bearing_deg10":%u` | uint | ✔        |
| `servo_trim_us`              | `"servo_trim_us":%d`        | **int (signed)** | ✔ |

`servo_trim_us` jest emitowane jako `%d` (ze znakiem) — dekodowanie do `Int` z
zachowaniem znaku jest poprawne, potwierdzone testem `-140`. Trzy pola `%u` mają
mały zakres (kalibracja 0-3, błąd w metrach, bearing 0-3600) i bezpiecznie mieszczą
się w 64-bit `Int`. **Ryzyko R1 (rozjazd kluczy) zamknięte.**

## Odchylenia od planu

Brak istotnych. Plan (Unit 1) dopuszczał opcjonalny computed `servoTrimMicroseconds`
("i ew."). Nie dodano go — słusznie (YAGNI; nieużywany aż do Unit 5, brak konsumenta).
Nie jest to finding.

## Findings per lens

### 1. Security
Czyste. Pure `Decodable`, brak auth/sekretów/SQL/eval. Wejście z sieci (untrusted)
dekodowane bez ręcznego castu `double→int`, więc antywzorzec "waliduj przed castem"
nie występuje. `decodeIfPresent(...) ?? 0` obsługuje tylko brak/null klucza; niezgodny
typ nadal rzuca i failuje całą ramkę (fail-fast) — zachowanie pożądane.

### 2. Performance
Czyste. Dekodowanie ~10 Hz, 4 dodatkowe skalary. Bez pętli, alokacji, I/O.

### 3. Architecture & Type safety
Czyste. `Int` dla skalarów niebędących e7 jest zgodne z istniejącą konwencją pliku
(`gpsSpeedCms`, `gotoErrM`, `imuHeadingDeg10` też `Int`; `Int32` zarezerwowany dla e7).
Nazewnictwo camelCase spójne; computed `spotLockBearingDegrees` zgodne z istniejącym
`gotoBearingDegrees`. Komentarz PL wyjaśnia tolerancję na starszy firmware. Brak `any`,
brak wymuszonych rozpakowań, brak dead code / nieużywanych importów. Jeden eksport
głównego typu per plik zachowany.

### 4. Scenario Exploration & Test Coverage
Czyste. Wszystkie 3 scenariusze z pliku zadań pokryte z mocą wyroczni:
- Happy path (pełna ramka, pola ≠ 0) — dodatkowo asertuje computed `spotLockBearingDegrees == 90.5`.
- Brak nowych pól (stary firmware) → pola = 0. Oracle: usunięcie `decodeIfPresent`
  (powrót do `decode`) uczyniłoby test czerwonym (rzuciłby `keyNotFound`).
- Ujemny `servo_trim_us` (−140) → znak zachowany. Oracle: dekodowanie jako unsigned failuje.

### 5. E2E Browser Verification
**N/A** — natywny iOS, brak UI w Unit 1, brak URL/przeglądarki. Nie stanowi FAIL.

## 🟡 P3 (nit, opcjonalne)

- 🟡 [nit] `TelemetryDecodingTests.swift` — brak testu granicznego dla
  `spot_lock_bearing_deg10` na krawędzi zakresu (np. 3600 → 360.0° / 0). Firmware
  emituje `%u` (0-3600), obecne testy pokrywają wartość środkową (905 → 90.5°).
  Wartość edukacyjna niska; do rozważenia przy Unit 3 (gdzie bearing wchodzi w
  format prezentacji). Nie blokuje.

## Wniosek

Faza 1 zaimplementowana zgodnie z planem i kryteriami akceptacji. Klucze i znak
zweryfikowane wobec firmware. Testy zielone z mocą wyroczni. **Kontynuuj do Unit 2/3.**
