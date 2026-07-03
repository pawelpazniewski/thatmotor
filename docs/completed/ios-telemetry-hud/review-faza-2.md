# Code Review — Faza / Unit 2: Słownik komend trimu + AppModel

Data: 2026-07-03
Commit: `92ca732`
Branch: `feature/kayak-motor-firmware-v1`
Reviewer: dev-autopilot (multi-lens, natywny iOS/Swift)

## Zakres

Trzy nowe komendy trimu serwa w kontrakcie + trzy metody `AppModel`. Trywialna,
izolowana zmiana wzorowana na istniejącym `disarm()`.

Zmienione pliki:
- `ios/KayakKit/Sources/KayakContract/Command.swift` (+6 linii — 3 case'y enuma + 3 mapowania `cmdName`)
- `ios/KayakMotor/App/AppModel.swift` (+29 linii — `trimLeft()`/`trimRight()`/`saveTrim()`)
- `ios/KayakKit/Tests/KayakContractTests/HTTPCommandRequestTests.swift` (+26 linii — 3 testy)

Uwaga o środowisku: to natywny SwiftUI, NIE web. Unit 2 to czysty kontrakt + glue
warstwy modelu, bez UI. Agent 5 (E2E browser-verifier) — **N/A** (brak
URL/przeglądarki/localhost; brak warstwy UI w tym Unicie). Nie uruchamiano agent-browser.

## Severity gate

✅ **CZYSTE — GOTOWE DO KONTYNUACJI**

- 🔴 P1 (blocking): 0
- 🟠 P2 (important): 0
- 🟡 P3 (nit): 2

## Weryfikacja fazy

- `cd ios/KayakKit && swift test` → **56/56 zielone** ✔ (uruchomione, potwierdzone)
- app target `KayakMotor` — **BUILD SUCCEEDED** ✔
- `AppModel` eksponuje trzy metody trimu (`trimLeft`, `trimRight`, `saveTrim`) ✔
- Kryterium fazy "testy zielone; AppModel eksponuje trzy metody trimu" — **spełnione**.

## Cross-reference z firmware (ryzyko R5 z planu — zamknięte)

Nazwy komend zweryfikowane 1:1 wobec firmware `components/web_panel/src/command_parse.c`:

| Swift `cmdName` | Firmware (command_parse.c) | Zgodność |
|-----------------|----------------------------|----------|
| `trim_left`     | `{"trim_left",  {.trim_left  = true}}` | ✔ |
| `trim_right`    | `{"trim_right", {.trim_right = true}}` | ✔ |
| `trim_save`     | `{"trim_save",  {.trim_save  = true}}` | ✔ |

Bramka bezpieczeństwa potwierdzona w `components/control_loop/src/control_loop.c`
(`apply_trim_events`): `if (!loop_should_apply_pending(s_loop.state)) return;` —
**każdy event trimu jest ignorowany poza DISARMED**. Trim nie może ruszyć serwa
przy ARMED/FAILSAFE. Pierwszeństwo failsafe nietknięte. **Ryzyko R5 zamknięte.**

## Findings per lens

### 1. Security
Czyste. Analiza przeprowadzona bezpośrednio (zmiana trywialna, brak nowej powierzchni ataku):
- **Brak deserializacji niezaufanych danych** — trim to komendy WYCHODZĄCE. `httpBody()`
  buduje `{"cmd": <stała>}` z literałów, `JSONSerialization.data` na statycznym słowniku;
  brak interpolacji inputu użytkownika, brak eval, brak SQL.
- **Brak nowego endpointu / powierzchni** — komendy idą przez istniejący `commands.send`
  na `POST /api/command`; Unit 2 tylko dokłada wartości enuma.
- **Brak sekretów / logowania wrażliwych danych** — `lastActionMessage` niesie stałe PL
  komunikaty UI ("Trim w lewo", "Trim zapisany"), nie dane wrażliwe.
- **Inwariant bezpieczeństwa zachowany** — app nadal NIE uzbraja (brak `arm` w enumie);
  firmware bramkuje trim do DISARMED (patrz wyżej). Failsafe/RC nietknięte.
- **Error handling** — `catch let apiError as ApiError` (typed) + generyczny fallback z
  komunikatem; brak pustego `catch {}`, brak połknięcia błędu. Zgodne z regułą #4.

### 2. Performance
Czyste. `cmdName` to `switch` po enumie (O(1)); `httpBody()` alokuje jeden mały słownik
na komendę wyzwalaną tapem użytkownika (rzadkie, nie hot-path). Metody `AppModel`
spawnują `Task` przy interakcji — brak pętli, N+1, I/O w pętli RT. Bez wpływu na wydajność.

### 3. Architecture & Type safety
Czyste. Rozszerzenie enuma `Command` zgodne z istniejącym wzorcem (case → `cmdName` →
`httpBody`). Metody `AppModel` wiernie naśladują `disarm()`/`stopGoto()` (Task + typed
try/catch + `lastActionMessage`) — spójność zachowana. Granice warstw respektowane
(UI → AppModel → serwis komend → HTTP). Jedna odpowiedzialność per metoda, brak
circular deps. `[String: Any]` w `httpBody()` to istniejąca konwencja (wymóg
`JSONSerialization`), NIE nowy `any` wprowadzony w Unicie 2.

### 4. Scenario Exploration & Test Coverage
Czyste. Trzy scenariusze z pliku zadań pokryte:
- `trimLeftBody` — asertuje `cmd == "trim_left"` ORAZ brak `lat_e7`/`lon_e7`.
- `trimSaveBody` — asertuje `cmd == "trim_save"`.
- `trimCommandsCarryNoCoordinates` — pętla po wszystkich trzech; **moc wyroczni** w
  `obj.count == 1`: gdyby trim dokładał współrzędne, test byłby czerwony.

### 5. E2E Browser Verification
**N/A** — natywny iOS, brak UI w Unit 2, brak URL/przeglądarki. Nie stanowi FAIL.

## 🟡 P3 (nity, opcjonalne — nie blokują)

- 🟡 [nit] **Command.swift / AppModel.swift** — asymetria nazewnicza: case enuma to
  `.trimSave`, a metoda modelu to `saveTrim()` (podczas gdy `trimLeft`/`trimRight` są
  symetryczne). Zgodne z planem (plan explicite podał `saveTrim()`), ale para
  `trimSave`↔`saveTrim` może chwilowo mylić. Do rozważenia ujednolicenie, jeśli nie
  koliduje z czytelnością wywołań w widoku (Unit 5).
- 🟡 [nit] **AppModel.swift** — `trimLeft()`/`trimRight()`/`saveTrim()` nie mają
  dedykowanych testów jednostkowych (wymagałyby zamockowania wstrzykiwanego serwisu
  `commands`). Świadomie spójne z istniejącym, również nietestowanym `disarm()`/
  `stopGoto()`; zachowanie przycisków trimu jest zaplanowane do pokrycia E2E w Unit 5
  (DISARMED → tap "+" → wzrost `servo_trim_us`; ARMED → wyszarzone). Nie blokuje.

## Wniosek

Faza 2 zaimplementowana zgodnie z planem i kryteriami akceptacji. Nazwy komend i
bramka DISARMED zweryfikowane wobec firmware (R5 zamknięte). Testy zielone (56/56) z
mocą wyroczni na kluczowym inwariancie (brak współrzędnych w trimie). Dwa nity P3 bez
wpływu na poprawność. **Kontynuuj do Unit 3.**
