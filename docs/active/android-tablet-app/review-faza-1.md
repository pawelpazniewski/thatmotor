# Code Review — Faza 1 (Fundament i łączność)

Zadanie: android-tablet-app · Branch: `feature/android-tablet-app`
Data review: 2026-06-20 (re-review po naprawie P2, cykl 1)
Zakres: Unit 1 (Bootstrap projektu Android) + Unit 2 (Adapter łączności z AP ESP32)

## Severity gate

✅ **GOTOWE DO KONTYNUACJI** — 0 × P1, 0 × P2, 8 × P3.

Re-review po naprawie 3 findingów P2 z cyklu 0. Wszystkie trzy P2 są **rozwiązane**
poprawnie i bez regresji. Pozostają wyłącznie nity (P3), w tym 7 przeniesionych z cyklu 0
(świadomie odroczone) + 1 nowy nit z analizy symetrii reducera. Brak blokad kontynuacji
Fazy 2.

## Liczniki

- 🔴 P1 (blocking): 0
- 🟠 P2 (important): 0
- 🟡 P3 (nit): 8

Typy findingów:
- KOD: P3 = 6
- TEST: P3 = 2
- E2E: N/A (brak środowiska — patrz niżej)

## Status poprzednich 3 findingów P2

| # | Finding | Plik | Status |
|---|---------|------|--------|
| P2-1 | `boundNetwork` thread-safety | `net/ApConnectionManager.kt` | ✅ ROZWIĄZANE |
| P2-2 | `requestNetwork` bez timeoutu (R7) | `net/ApConnectionManager.kt` | ✅ ROZWIĄZANE |
| P2-3 | `usesCleartextTraffic` globalny | `AndroidManifest.xml` + nowy `network_security_config.xml` | ✅ ROZWIĄZANE |

### P2-1 — `boundNetwork` thread-safety → ROZWIĄZANE
`@Volatile var boundNetwork: Network? = null` z `private set` (`ApConnectionManager.kt:45-47`).
`boundNetwork` to pojedyncza referencja (brak compound read-modify-write, brak inwariantu
wiążącego ją z innym polem), więc `@Volatile` w pełni gwarantuje widoczność zapisu (wątek
callbacku ConnectivityManager) → odczytu (wątek OkHttp). KDoc dokumentuje rationale. Poprawne.

### P2-2 — `requestNetwork` timeout → ROZWIĄZANE
`connect(ssid, passphrase, timeoutMs = DEFAULT_TIMEOUT_MS)` wywołuje 3-argumentowy overload
`requestNetwork(request, cb, timeoutMs)` (`ApConnectionManager.kt:96`). Weryfikacja API:
- `requestNetwork(NetworkRequest, NetworkCallback, int)` to publiczne API od **API 26** —
  dostępne przy minSdk 29, brak potrzeby `@RequiresApi`.
- `timeoutMs` w milisekundach; po timeoutcie framework woła `onUnavailable()` →
  `ApConnectionEvent.Unavailable` → `Failed`. Domyślne 30 s rozsądne (asocjacja WPA2 + DHCP).
- Dodano fail-fast `require(timeoutMs > 0)` (`ApConnectionManager.kt:62`), spójny ze
  starszymi `require` na ssid/passphrase. `30_000` jako nazwana stała `DEFAULT_TIMEOUT_MS`.

### P2-3 — Zawężenie cleartext → ROZWIĄZANE
Manifest: usunięto `android:usesCleartextTraffic="true"`, dodano
`android:networkSecurityConfig="@xml/network_security_config"` (`AndroidManifest.xml:26`).
Nowy `res/xml/network_security_config.xml`:
- `base-config cleartextTrafficPermitted="false"` (deny globalnie),
- `domain-config cleartextTrafficPermitted="true"` z `<domain>192.168.4.1</domain>`.
Weryfikacja subtelności:
- **Literał IP w `<domain>` działa** — Network Security Config dopasowuje po host stringu;
  literał IP jest dopasowany verbatim, `includeSubdomains="false"` jest właściwe dla IP.
- **`ws://` pokryte** — `cleartextTrafficPermitted` egzekwowane na poziomie socketu dla hosta
  niezależnie od schematu; OkHttp WebSocket idzie tym samym stosem, więc `ws://192.168.4.1`
  i `http://192.168.4.1` (web panel, R8) są dozwolone. `wss://`/`https://` bez zmian.

## Regresje

**Brak.** Link do web panelu `http://192.168.4.1` (R8) i telemetria `ws://192.168.4.1` nadal
dozwolone przez `domain-config`. Nowy `timeoutMs` to domyślny parametr końcowy — wstecznie
zgodny, brak złamanych call-site'ów (brak produkcyjnych wywołań poza testami). Czysty reducer
i typy stanu/zdarzeń **niezmienione** — istniejące 4 testy reducera zachowują pełną moc
wyroczni (test `lost is ignored when not live` nadal FAILuje po naiwnym mapowaniu `Lost→Lost`).
Brak nowych nieużywanych importów, dead code, magic numbers.

---

## Findings P3 — nit

### P3-NOWY [KOD] — `Unavailable` demuje `Connected → Failed` bezwarunkowo (symetria reducera)
`net/ApConnectionStateReducer.kt:38`

W reducerze `Unavailable` jest terminalny w KAŻDYM stanie (`Connected`/`Lost`/`Idle`),
podczas gdy `Lost` jest świadomie strzeżony (`when(current)`). Teoretyczny scenariusz: późny
`onUnavailable` po `onAvailable` zdemuje żywe `Connected` do `Failed` i wyzeruje `boundNetwork`.

**Klasyfikacja P3 (nie P2):** kontrakt Androida dla `requestNetwork(..., timeoutMs)` stanowi, że
`onUnavailable` jest wołane *zamiast* `onAvailable`, gdy żądanie nie może być spełnione w
timeoutcie — NIE po udanym `onAvailable` dla tego samego żądania. Strzeżenie tego = defensive
code na scenariusz wykluczony kontraktem frameworka (coding-rules pkt 5: „Nie twórz defensive
code na scenariusze które nie mogą wystąpić”; anti-pattern #10). Jeśli jednak zespół zdecyduje
o symetrii z `Lost` (czystość/czytelność reducera), poprawka MUSI iść z testami oracle-power
(`Connected + Unavailable → Connected`, FAILuje po usunięciu strażnika).

### P3-1 [KOD] — `allowBackup="true"` przed wprowadzeniem persystencji passphrase
`AndroidManifest.xml:20` (przeniesione z cyklu 0, otwarte)

Faza 1 nie zapisuje danych; `connect(ssid, passphrase)` sygnalizuje przyszłą persystencję
passphrase WPA2. Przy `allowBackup="true"` trafi do Auto Backup / `adb backup`. Rekomendacja:
`android:allowBackup="false"` (minimum-privileges). Nie jest regresją tych poprawek.

### P3-2 [KOD] — `Failed` bez powodu vs plan `Failed(reason)`
`net/ApConnectionStateReducer.kt:38`, `net/ApConnectionState.kt:23` (przeniesione z cyklu 0)

Po dodaniu timeoutu (P2-2) `Failed` może oznaczać teraz „timeout” LUB „odrzucenie/zła
passphrase” — UI ich nie odróżni. Rozważ `data class Failed(reason: ApFailureReason)` (czysty
enum, Pure ⊥ HAL zachowane). Dług na v2; akceptowalne dla Fazy 1.

### P3-3 [KOD] — KDoc w warstwie pure odwołuje się do typów Androida (martwy link)
`net/ApConnectionState.kt:7`, `net/ApConnectionStateReducer.kt:4` (przeniesione z cyklu 0)

KDoc-link `[android.net.ConnectivityManager.NetworkCallback]` w plikach bez importów Androida
nie zresolwuje się. Zamień na zwykły tekst.

### P3-4 [KOD] — Dwie deklaracje top-level w jednym pliku
`net/ApConnectionStateReducer.kt:1-43` (przeniesione z cyklu 0)

`sealed interface ApConnectionEvent` + `reduceApConnectionState` (coding-rules pkt 3). Oba pure,
ściśle sprzężone — kolokacja pomaga czytelności; opcjonalnie wydziel `ApConnectionEvent.kt`.

### P3-5 [KOD] — Anonimowy `NetworkCallback` w `connect()` podnosi rozmiar
`net/ApConnectionManager.kt:78-94` (przeniesione z cyklu 0)

Po dodaniu timeoutu `connect()` urósł (~39 linii), wciąż pod progiem 50. Przy rozroście wyciągnij
`buildApRequest(ssid, passphrase): NetworkRequest`.

### P3-6 [KOD] — MapLibre 11.5.2 / OkHttp-alpha nieużywane w Fazie 1 (waga APK)
`app/build.gradle.kts`, `gradle/libs.versions.toml` (przeniesione z cyklu 0)

Potwierdź potrzebę przed artefaktami milestone; zaplanuj ABI splits + R8 dla release. OkHttp
`5.0.0-alpha.14` — rozważ uzasadnienie/stabilną linię.

### P3-7 [TEST] — `require(timeoutMs > 0)` bez testu
`net/ApConnectionManager.kt:62` (nowy precondition, ale w warstwie HAL)

Jedyny czysty fragment dodany przez poprawki. Spójnie z istniejącymi nietestowanymi
`require(ssid…)`/`require(passphrase…)` na tej samej metodzie i z Pure ⊥ HAL (adapter cienki,
weryfikowany na urządzeniu) — akceptowalne bez testu. Jeśli strictness: wyekstrahuj czysty
`validateTimeout`/`validateConnectParams` + host-test (happy `1`, `30_000`; error `0`, `-1`).

### P3-8 [TEST] — Drugi test ma podwójną odpowiedzialność (słabsza wyrocznia)
`net/ApConnectionStateReducerTest.kt:19-29` (przeniesione z cyklu 0)

`connected then lost yields lost then available reconnects` sprawdza dwie rzeczy. Scenariusz z
planu; kosmetyka — rozbić lub dodać komentarz o bezwarunkowej ścieżce reconnectu.

---

## Uwaga środowiskowa

To natywny projekt Android (Kotlin/Gradle). W tym środowisku NIE ma JDK/Gradle/Android SDK/
emulatora ani przeglądarki:
- `./gradlew :app:assembleDebug` oraz testy JVM NIE zostały uruchomione — wymagają lokalnego
  SDK + JDK 17. Ocena testów i poprawności wyłącznie statyczna (czytanie kodu, weryfikacja API).
- E2E browser verification NIE dotyczy tej fazy (brak web UI). Wynik E2E: **N/A — brak środowiska**.

Brak możliwości uruchomienia tych narzędzi traktowany jako ograniczenie środowiska, nie finding.

## Wynik E2E

N/A — brak środowiska (emulator/przeglądarka). Niezaznaczone checkboxy `Weryfikacja:`
(zadania linie 21, 32) pozostają do weryfikacji na sprzęcie/emulatorze podczas integracji.

## Pliki sprawdzone (re-review)

- `android/app/src/main/AndroidManifest.xml`
- `android/app/src/main/res/xml/network_security_config.xml` (nowy)
- `android/app/src/main/java/com/thatmotor/kayak/net/ApConnectionManager.kt`
- `android/app/src/main/java/com/thatmotor/kayak/net/ApConnectionState.kt`
- `android/app/src/main/java/com/thatmotor/kayak/net/ApConnectionStateReducer.kt`
- `android/app/src/test/java/com/thatmotor/kayak/net/ApConnectionStateReducerTest.kt`
- Commit naprawczy: `b3b0682`
