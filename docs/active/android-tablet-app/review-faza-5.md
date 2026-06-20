# Review fazy 5 — Utrzymanie sesji (Unit 10) · RE-REVIEW cykl 2

Data: 2026-06-20 · Branch: `feature/android-tablet-app` · Commit naprawy: `16c89b0` (po `fe7c74f`, `f0b109e`)
Reviewer: dev-docs-review (multi-agent: architecture/lifecycle-crash-paths + kotlin-quality/test-oracle-power)
Cykl: 2 (re-review po naprawie 3×P2 z cyklu 1)

## Severity gate

✅ **GOTOWE DO KONTYNUACJI** — wszystkie 3×P2 z cyklu 1 rozwiązane. Zero P1, zero nowych P2.
Pozostają tylko P3 (carry-over + 2 nowe refinementy), wszystkie świadomie odnotowane jako dług.

- 🔴 P1: 0
- 🟠 P2: 0
- 🟡 P3: 7 (5 carry-over z cyklu 1 + 2 nowe nity z re-review)
- 🌐 E2E: N/A (natywny Android — brak JDK/Gradle/Android SDK/emulatora; testów JVM/buildu/E2E NIE uruchomiono; analiza statyczna)

Typy findingów per severity:
- P2: brak
- P3: 6×KOD, 1×KONFIG

---

## Status 3×P2 z cyklu 1

| # | P2 z cyklu 1 | Status | Dowód |
|---|---|---|---|
| 1 | `bindService` Boolean ignorowany → `unbindService` może rzucić `IllegalArgumentException` | ✅ ROZWIĄZANE | `MainActivity.kt:65` pole `isBound`; `:113` `isBound = bindService(...)`; `:124-127` unbind tylko `if (isBound)` + `isBound=false`; `:88` `onServiceDisconnected` zeruje `isBound` |
| 2 | Decyzja rotacja-vs-wyjście (`isFinishing`) nieekstrahowana/nietestowana | ✅ ROZWIĄZANE | `SessionPolicy.kt:72` `shouldTearDownSession(isFinishing)`; `MainActivity.kt:135` wpięte; `SessionPolicyTest.kt:88-99` 2 testy z mocą wyroczni |
| 3 | Teardown AP no-op (`connect()` nigdy nie wołany) — cross-phase | ✅ ROZWIĄZANE (udokumentowane jako dług, świadomie NIE wpięte) | `zadania.md:33` dług Fazy 1/Unit 2; `zadania.md:282` ocena; hook na `MainActivity`-owanej instancji (forward-compatible) |

---

## Weryfikacja zadana w briefie

### 1. Czy `isBound` poprawnie eliminuje wyciek/crash ServiceConnection (wszystkie ścieżki)

Prześledzone path-by-path (agent lifecycle):

| Ścieżka | Wynik | Dowód |
|---|---|---|
| bind fail (`bindService` → false) | ✅ brak crashu | `isBound=false` → `onStop:124` pomija `unbindService`, brak `IllegalArgumentException` |
| onStart → onStop (normalna) | ✅ single unbind | `:113` `isBound=true`; `:124-127` jeden unbind + reset |
| onServiceDisconnected → onStop | ✅ brak crashu/double-unbind | `:88` zeruje `isBound`; `onStop` pomija unbind |
| rotacja (onStop → onDestroy → onStart) | ✅ brak wycieku | każda instancja: jeden bind/unbind pair; `onDestroy:135` `isFinishing=false` → brak teardown |
| double-unbind | ✅ brak | jedyny `unbindService` w `onStop`, strzeżony `isBound`, natychmiast `isBound=false` |
| stale `isBound=true` przy zerwanym połączeniu | ✅ brak | jedyny kandydat `onServiceDisconnected` poprawnie zeruje |

Werdykt: crash `IllegalArgumentException` wyeliminowany na WSZYSTKICH osiągalnych ścieżkach; wyciek ServiceConnection zamknięty na ścieżce sukcesu; rotacja przeżywa; wake lock zwalniany na każdej ścieżce teardown (w tym gdy cleanup rzuca — `releaseWakeLock()` przed `try`, `super.onDestroy()` w `finally`).

### 2. Czy `SessionPolicy.shouldTearDownSession` czysta i testy mają moc wyroczni (nie test weakening)

✅ Funkcja czysta (HAL-free, `Boolean → Boolean`, brak importów `android.*`). Macierz degeneracji (agent kotlin-quality):

| Mutant | f(true) | f(false) | Złapany? |
|---|---|---|---|
| `always true` | pass | **fail** (test:98 oczekuje false) | ✅ |
| `always false` | **fail** (test:90 oczekuje true) | pass | ✅ |
| `!isFinishing` (inwersja) | **fail** (:90) | **fail** (:98) | ✅ |

Wszystkie mutanty w domenie boolean zabite. Pułapka "input == output" NIE występuje: wartości oczekiwane to hardcoded literały (`true`/`false`) wyprowadzone z semantyki ("real exit tears down" / "rotacja nie"), niezależne od implementacji — dla 1-bitowej funkcji 2 różne literały = maksymalna moc wyroczni. Behavior identyczny z `if (isFinishing)`. NIE jest to test weakening. Cały `SessionPolicyTest` (13 testów): każdy ≥1 asercja, brak osłabień, brak assertion-free, sparowane wyrocznie pozytyw/negatyw.

### 3. Czy cross-phase debt jest akceptowalny (anti-over-engineering pkt 5/11) czy finding dismissal (pkt 7)

✅ **AKCEPTOWALNE — realny dług cross-phase, NIE finding dismissal.**

Weryfikacja faktyczna (grep `src`):
- `fun connect(...)` zdefiniowane w `ApConnectionManager.kt:59`, ale `.connect(` **nie występuje nigdzie** w `app/src`.
- Jedyne użycia `apConnectionManager`: konstrukcja `by lazy` (`MainActivity:56`) i `disconnect()` w teardown hooku (`:80`).

Dlaczego to Unit 2 (Faza 1), nie braki Fazy 5:
- Wpięcie `connect(ssid, passphrase)` wymaga UI wyboru SSID + wpisania passphrase oraz przekazania `boundNetwork`/`socketFactory` do `EspHttpClient.build(...)` — to flow łączenia z `zadania.md:32` (weryfikacja Unit 2 **wciąż otwarta**), poza scope Unit 10.
- Wymuszanie teraz = over-engineering ze sztucznymi danymi SSID (pkt 5/11 — nie twórz abstrakcji/flow "na zapas").
- Dług JEST jawnie zapisany w `zadania.md:33` (Unit 2) + `zadania.md:282` (ocena cyklu 2), z planem naprawy i punktem wpięcia — to dokumentacja, nie racjonalizacja ukrywająca brak.
- Hook teardown wisi na **właściwej, `MainActivity`-owanej instancji** `ApConnectionManager` — gdy Unit 2 doda `connect()` na tej samej instancji, `disconnect()` zadziała bez zmian w fazie 5 (forward-compatible).

Test pkt 7 (finding dismissal): finding NIE jest odrzucony — jest zaakceptowany, przeklasyfikowany do właściwej fazy (Unit 2) z uzasadnieniem i odnotowany jako otwarty dług. To jest poprawne zarządzanie cross-phase, nie zamiatanie pod dywan.

### 4. Czy cel fazy 5 spełniony

✅ Wszystkie kryteria Unit 10:

| Kryterium | Stan | Dowód |
|---|---|---|
| Keep-screen-on przez sesję | ✅ | `MainActivity.applyKeepScreenOn():164` ↔ `SessionPolicy.shouldKeepScreenOn` (ACTIVE) |
| Telemetria przeżywa tło | ✅ | foreground service `START_STICKY` + `FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE`; wake lock przy screen-off (`onStop:123`) |
| Cleanup bez wycieków | ✅ | `onDestroy:177-195` release wake lock + teardown-once hook (guard `onSessionStopped=null` przed invoke); `super.onDestroy()` w `finally` |
| Brak wiszących callbacków/wake locków | ✅ | `removeCallbacks(reacquireRunnable)` na wszystkich ścieżkach; `releaseWakeLock` guard `isHeld`; `setReferenceCounted(false)` |
| Brak wycieku ServiceConnection | ✅ | bind/unbind sparowane + `isBound` guard (P2#1 rozwiązany) |
| Teardown tylko na realnym wyjściu | ✅ | `shouldTearDownSession(isFinishing)` (P2#2 rozwiązany, host-tested) |

---

## Pliki sprawdzone (6 + 1 test)

- `android/app/src/main/java/com/thatmotor/kayak/MainActivity.kt`
- `android/app/src/main/java/com/thatmotor/kayak/session/TelemetryService.kt`
- `android/app/src/main/java/com/thatmotor/kayak/session/SessionPolicy.kt`
- `android/app/src/main/java/com/thatmotor/kayak/session/SessionState.kt`
- `android/app/src/main/java/com/thatmotor/kayak/net/ApConnectionManager.kt`
- `android/app/src/main/AndroidManifest.xml`
- (test) `android/app/src/test/java/com/thatmotor/kayak/session/SessionPolicyTest.kt`

---

## 🟡 P3 — nit (nieblokujące)

### Nowe z re-review cyklu 2
- 🟡 [KOD] **MainActivity.kt:113,124** — idiom "ideal": przy `bindService` zwracającym `false` Android i tak wymaga `unbindService` by zwolnić rejestrację `ServiceConnection`. Strzeżenie unbind na wyniku `isBound` pomija to → latentny wyciek na ścieżce failed-bind. Dla lokalnego, same-process `BIND_AUTO_CREATE` serwisu `false` = błąd manifestu (nieosiągalne runtime), więc P3 nie P2. Docelowo: śledzić "bind requested" (call nie rzucił) osobno od "bind succeeded".
- 🟡 [KOD] **MainActivity.kt:86-89** — `onServiceDisconnected` zeruje `isBound`, ale disconnection ≠ unbound (rejestracja połączenia żyje, może reconnectować). Nieosiągalne dla in-process serwisu (nie umrze bez procesu app), stąd P3.

### Carry-over z cyklu 1 (nadal otwarte)
- 🟡 [KOD] **SessionPolicy.kt:29** — martwy KDoc-link `[SessionService]` (symbol nie istnieje; → `[TelemetryService]`).
- 🟡 [KOD] **SessionPolicy.kt:8** — `[android.app.Activity]` jako KDoc-link w pliku świadomie HAL-free; de-link/reword (+ niespójny styl nawiasów vs `:69`).
- 🟡 [KOD] **TelemetryService.kt:142,70** — niesprawdzone casty `as PowerManager`/`as NotificationManager`; niespójne z `requireNotNull(getSystemService(...))` w `MainActivity:58` (pkt 10).
- 🟡 [KOD] **TelemetryService.kt:55,65** — `@Volatile` na `onSessionStopped`/`wantsWakeLock` nadmiarowy (wszystkie dostępy na main thread); usunąć albo udokumentować future-proof.
- 🟡 [KONFIG] **AndroidManifest.xml:24** — `allowBackup="true"`; rozważyć `false`/`dataExtractionRules`.
- 🟡 [KOD] **TelemetryService.kt:187,191** — komentarz mówi "Error/Throwable", ale `catch (RuntimeException)` przepuszcza `Error`. Brak realnego wycieku (wake lock zwolniony przed `try`, `finally` woła `super`), więc doc-accuracy only.

---

## Pozytywne (zweryfikowane w re-review cyklu 2)

- **3/3 P2 z cyklu 1 rozwiązane** — patrz tabela wyżej.
- **isBound** eliminuje crash `IllegalArgumentException` na wszystkich osiągalnych ścieżkach; rotacja przeżywa; brak double-unbind.
- **`shouldTearDownSession`** czysta, host-tested, moc wyroczni potwierdzona macierzą degeneracji (always-true/always-false/inwersja wszystkie zabite). Identity-extract uzasadniony Pure ⊥ HAL (safety-critical, nieoczywisty invariant `onDestroy` ↔ rotacja), spójny z 5 siostrzanymi funkcjami — NIE over-engineering.
- **Cross-phase debt** udokumentowany (NIE dismissed): grep potwierdza `connect()` nieużywany; przeklasyfikowany do Unit 2 z uzasadnieniem i planem; hook forward-compatible na właściwej instancji.
- `SessionPolicyTest` — 13 testów, każdy ≥1 asercja, brak weakening/assertion-free, sparowane wyrocznie.
- Wszystkie pliki < 300 linii (max `TelemetryService.kt` = 255); funkcje < 50 linii; nesting ≤ 2; brak nieużywanych importów.

---

## Odchylenia od planu

Unit 10 (lifecycle + cleanup) — **zrealizowane**. Jedyne pozostające odchylenie: teardown AP inertny do czasu wpięcia `ApConnectionManager.connect()` — świadomie odnotowany dług Fazy 1/Unit 2 (`zadania.md:33,282`), poza scope Unit 10, forward-compatible.

## Środowisko

Natywny Android (Kotlin/Compose). Brak JDK/Gradle/Android SDK/emulatora — testów JVM, buildu (`assembleDebug`) i E2E NIE uruchomiono. Cała analiza statyczna. Wynik E2E: N/A. Ograniczenie środowiska, nie finding.
