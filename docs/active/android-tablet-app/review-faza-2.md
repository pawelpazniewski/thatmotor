# Code Review — Faza 2 (Kontrakt danych i transport)

Branch: `feature/android-tablet-app`
Data: 2026-06-20 (cykl 0) · 2026-06-20 (re-review, cykl 1)
Commity objęte review: `0989260` (kod+testy Unit 3–5), `f48ff00` (postęp), `a98a661`
(poprawki po review fazy 2, cykl 1).

## Severity gate (re-review, cykl 1)

✅ **GOTOWE DO KONTYNUACJI** — 0×P1, 0×P2, 8×P3.

Wszystkie 6×P2 z cyklu 0 (3×KOD + 3×TEST) **ROZWIĄZANE** bez regresji. Analiza
statyczna potwierdziła skuteczność poprawek i acykliczny graf zależności po
przeniesieniu orkiestracji do nowego pakietu `repository`. Pozostają 8×P3 (nity,
świadomie odroczone). Brak blokerów — można przejść do Fazy 3.

## Metodologia i ograniczenia środowiska

Re-review skupiony na 6 naprawionych findingach + regresja po przeniesieniu pakietu.
Weryfikacja: odczyt zmienionych plików, grep grafu importów, porównanie testów z
firmware (źródło prawdy).

**ŚRODOWISKO:** natywny Android (Kotlin/Gradle), brak JDK/Gradle/Android SDK/przeglądarki.
**Build/testy JVM/E2E NIE uruchomione** — ograniczenie środowiska, nie finding P1/P2.
Checkboxy `Weryfikacja:` wymagające sprzętu/emulatora/ESP32 pozostają poza zasięgiem.

## Weryfikacja naprawy 6×P2

### KOD

1. **✅ ROZWIĄZANE — cykl warstw `data ⇄ net`** (`data/TelemetryRepository.kt:9` → przeniesiony)
   - `TelemetryRepository` i `TelemetryUiState` przeniesione z pakietu `data` do nowego
     `com.thatmotor.kayak.repository`.
   - Graf importów zweryfikowany grepem: `data/` nie importuje `net`/`repository`/`okhttp`/
     `android.*` (czyste DTO). `domain/` pure (brak `android`/`okhttp`/`net`/`data`/`repository`).
     `net → data`. `repository → net + data + domain`. **Brak `net → repository`** (brak cyklu).
   - Brak martwych referencji: grep `data.TelemetryRepository`/`data.TelemetryUiState` = NONE.
     Żaden test ani plik źródłowy nie wskazuje na stary pakiet. Brak testów w `repository`
     (spójne — to cienki adapter HAL, decyzje host-testowane w `domain`).

2. **✅ ROZWIĄZANE — backpressure WS** (`net/TelemetrySocket.kt`)
   - Jawna polityka: `.buffer(capacity = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)`
     na końcu `callbackFlow` (lossy 10 Hz — dropuj najstarszą, nie blokuj listenera).
   - Wynik `trySend` sprawdzany: `if (sent.isFailure && !sent.isClosed)` → `Log.w`.
     Nie połykany cicho (coding-rules pkt 4, 13). `isClosed` wyłączony z logowania słusznie
     (normalne przy zamknięciu collectora).

3. **✅ ROZWIĄZANE — flood 10 emisji/s** (`repository/TelemetryRepository.kt`)
   - `onFrame`: gdy już `Live` → `current.copy(latestFrame = frame)` (nie nowy stan z fazą).
   - Rozdzielone strumienie: `connection = state.map { it.connection }.distinctUntilChanged()`
     oraz `latestFrame = state.map { it.latestFrame }.distinctUntilChanged()`. Konsument
     samej fazy połączenia (np. status badge) nie rekomponuje przy 10 Hz. Rozwiązuje
     ryzyko recomposition flood w Fazie 3.

### TEST

4. **✅ ROZWIĄZANE — `armReasonFromCode`** (`domain/MotorStateTest.kt`)
   - Happy path: `0→READY`; `1..4` mapowane przez asercję map-based (swap/rename wiersza enuma
     = fail). Error: `99→null`. Oracle zweryfikowany wobec firmware
     `state_machine.h::sm_arm_reason` (0..4): READY/NO_RC/THROTTLE_NOT_NEUTRAL/CALIBRATING/
     SETTINGS_APPLYING — **zgodność 1:1**.

5. **✅ ROZWIĄZANE — `CommandRequest(Command)` + serializacja** (`data/CommandTest.kt`)
   - `CommandRequest(Command.ARM).cmd == "arm"` (mapowanie enum→keyword) oraz serializacja
     `Json.encodeToString(...) == {"cmd":"arm"}` (dokładny kształt wire). Mocna wyrocznia.

6. **✅ ROZWIĄZANE — zgodność keywords z firmware** (`data/CommandTest.kt`)
   - Asercja kompletu: `assertEquals(expected.keys, Command.entries.toSet())` — dodanie/usunięcie
     komendy = fail (moc wyroczni na kompletność). Keywords `arm/disarm/deploy/stow` zweryfikowane
     wobec `command_parse.c::COMMAND_TABLE` (linie 13–16) — **zgodność 1:1**.

## Regresja

Brak regresji wykrytej w analizie statycznej:
- Import graph acykliczny po przeniesieniu pakietu; zero stale references.
- `net/CommandResultMapTest.kt` nadal importuje `data.ApiError` (kontrakt nienaruszony).
- KDoc w `Command.kt` cytuje `command_parse.h`, test cytuje `command_parse.c` — oba pliki
  istnieją, referencje poprawne.
- Czyste rdzenie (`domain`, `data/TelemetryUnits`, `net/CommandResult`) niezmienione →
  istniejące testy z cyklu 0 zachowują moc wyroczni.

## P3 — nit (8 otwartych, świadomie odroczone)

Bez zmian względem cyklu 0; ścieżki `TelemetryRepository.kt` odnoszą się teraz do pakietu
`repository`:
- 🟡 [P3] KOD — `repository/TelemetryRepository.kt:134` — `.catch { }` połyka throwable bez logu.
- 🟡 [P3] KOD — `repository/TelemetryRepository.kt:91-96` — watchdog `delay`-loop akumuluje drift.
  (Częściowo złagodzone: `tickWatchdog` ma teraz guard `if (!hasFrame) return` — brak jałowych
  flipów; sam drift okresu pozostaje.)
- 🟡 [P3] KOD — `repository/TelemetryRepository.kt:121-125` — pauza reconnectu jako poll 250 ms.
- 🟡 [P3] KOD — `domain/MotorState.kt:39-45` — niespójny wzorzec unknown-code (`MotorStateResult`
  sealed vs `armReasonFromCode` nullable).
- 🟡 [P3] KOD — `repository/TelemetryRepository.kt:138 vs :150` — `ConnectionState.Stale` w dwóch
  znaczeniach (cisza watchdoga vs reconnect).
- 🟡 [P3] TEST — `data/ApiEnvelopeParseTest.kt:43` — `assertNull(data)` przy fixture `data:null`
  to tożsamość (słaba wyrocznia na `data`).
- 🟡 [P3] TEST — `domain/ReconnectBackoff.kt:15-16` / `LinkWatchdog.kt:29` — `require(...)` guardy
  bez testu (boundary).
- 🟡 [P3] SECURITY — `net/CommandApi.kt:49` — surowy `IOException.message` w `TransportError`;
  ogólny komunikat dla UI + pełny log w debug.

## Pozytywy

- 6/6 P2 naprawione z realną skutecznością (nie kosmetycznie); testy z mocą wyroczni
  (map-based + completeness asserts), nie assertion-free, nie test-weakening.
- Refaktor pakietu `data → repository` czysty: acykliczny graf, zero martwych referencji,
  KDoc dokumentujący kierunek zależności.
- Backpressure jawny i udokumentowany; `trySend` nie połykany.
- Kontrakt z firmware ponownie zweryfikowany 1:1 (keywords, arm_reason 0..4).

## E2E / weryfikacja sprzętowa

**N/A** — brak UI/web w Fazie 2; brak emulatora/przeglądarki/ESP32/JDK/Gradle w środowisku.
Niezaznaczone `Weryfikacja:` (testy JVM zielone; arm/disarm na ESP32; link-down <1 s; WS ~10 Hz)
wymagają sprzętu/JVM poza zasięgiem — pozostają do weryfikacji przy integracji.
