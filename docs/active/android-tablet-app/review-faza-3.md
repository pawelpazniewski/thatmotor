# Code Review — Faza 3 (UI operacyjny — Compose)

Branch: `feature/android-tablet-app`
Data: 2026-06-20

## Historia review

- **Cykl 0** (commit `d645942`): ⚠️ ZASTRZEŻENIA — 0×P1, 8×P2, 7×P3.
- **Cykl 1 — re-review po naprawach** (commit `df10ae3`): ✅ **CZYSTE** — 0×P1, 0×P2, 7×P3 (nity przeniesione, nie blokują).

---

## Severity gate (cykl 1 — re-review)

✅ **GOTOWE DO KONTYNUACJI** — 0×P1, 0×P2, 7×P3 (nity, opcjonalne).

Wszystkie 8 findingów P2 z cyklu 0 zostały **rozwiązane realnie** (poprawa kodu/dodanie
testów z mocą wyroczni), bez antywzorców (brak test weakening, brak obniżenia progów,
brak obejścia narzędzi). Refaktor wprowadzający seam `TelemetrySource` i
`TelemetryViewModelFactory` **nie wprowadził regresji** — graf zależności pozostał czysty
i acykliczny, brak wiszących referencji do usuniętego API.

**ŚRODOWISKO:** natywny Android (Kotlin/Gradle/Compose). Brak JDK/Gradle/Android
SDK/emulatora. Build/testy JVM/Compose preview/E2E **NIE uruchomione** — ograniczenie
środowiska, nie finding. Analiza wyłącznie statyczna.

---

## Weryfikacja rozwiązania 8×P2 (per finding)

### KOD

| # | Finding (cykl 0) | Status | Dowód |
|---|------------------|--------|-------|
| 1 | ViewModel ręcznie jako pole Activity → nie przeżywa configuration change | ✅ ROZWIĄZANE | `MainActivity.kt:28` `by viewModels { TelemetryViewModelFactory() }`; nowy `TelemetryViewModelFactory.kt` wiruje zależności; `repository.start()` w `init` (l.44), `stop()` w `onCleared` (l.122-125). Brak `by lazy`/pola Activity. |
| 2 | `sendCommand` nie re-waliduje availability (okno dialog→klik) | ✅ ROZWIĄZANE | `TelemetryViewModel.kt:100-105` re-derywuje `commandAvailability` ze **świeżego** `source.state.value` (nie z `WhileSubscribed` flow) i publikuje `Rejected` zamiast wysyłać. Test wyroczni: `command on a stale link is vetoed before reaching the transport` (sprawdza `sender.lastCommand == null`). |
| 3 | Brak guardu in-flight (podwójne wysłanie) | ✅ ROZWIĄZANE | `TelemetryViewModel.kt:96` early-return gdy `_isSending.value`; `finally` zwalnia (l.111-113); `CommandBar.kt:50-71` `enabled = … && !isSending`. Test wyroczni: `a second command while one is in flight is dropped` (`GatedCommandSender`, asercja `callCount == 1`). |
| 4 | Rekompozycja 10×/s całego ekranu (monolityczny State) | ✅ ROZWIĄZANE | `TelemetryViewModel.kt:48-73` cztery slice'y `map{}.distinctUntilChanged().stateIn`; `TelemetryScreen.kt:65-103` każda sekcja (`HeaderRow`/`SafetyBannerSection`/`FrameCardsSection`/`CommandBarSection`) collectuje własny slice → 10 Hz dotyka tylko `FrameCardsSection`. |

### TEST

| # | Finding (cykl 0) | Status | Dowód |
|---|------------------|--------|-------|
| 5 | Brak pokrycia `ESC_CALIBRATION` / nieznanego kodu w CommandAvailability | ✅ ROZWIĄZANE | `CommandAvailabilityTest.kt:77-97` (`ESC_CALIBRATION → off`, `unknown 99 → off`). Wyrocznia: komentarz wskazuje gałąź DISARMED dawałaby ARM/DEPLOY. |
| 6 | Brak testu nieznanego kodu w SafetyIndicators | ✅ ROZWIĄZANE | `SafetyIndicatorsTest.kt:87-101` `unknown state code (99, Live) → failsafe=false, armed=false, linkDown=false`. |
| 7 | Brak wariantu `ConnectionState.Connecting` | ✅ ROZWIĄZANE | `CommandAvailabilityTest.kt:99-110` (`Connecting → off`) + `SafetyIndicatorsTest.kt:103-114` (`Connecting → linkDown` ze zdrową ramką). Pokryty 4. wariant `ConnectionState`. |
| 8 | Nieprzetestowane gałęzie `successMessage` (DEPLOY/DISARM/STOW) | ✅ ROZWIĄZANE | `CommandActionTest.kt:46-74`: DEPLOY → "Deployed — motor raised", DISARM → "Disarmed", STOW → "Stowed". Asercje na konkretny string (wyrocznia per-komenda). |

**Wynik: 8/8 rozwiązane.**

---

## Ocena test weakening w `TelemetryViewModelTest.kt` (kluczowe)

**WERDYKT: BRAK test weakening. Zmiana uzasadniona zmianą kontraktu, nie obejściem.**

Fix agent zmodyfikował istniejący `TelemetryViewModelTest.kt`. Analiza diffu (`git show df10ae3`):

1. **Zmiana kontraktu była konieczna i prawdziwa.** ViewModel zmienił zależność z
   konkretnego `TelemetryRepository` na seam `TelemetrySource` (finding #1/#2). Stary setup
   (`newRepository(scope)` z żywym `TelemetrySocket`) nie dawał deterministycznego
   `connection`/`frame`, a NOWY guard `sendCommand` (#2) re-waliduje availability ze
   `source.state` — więc test MUSI dostarczyć znany stan linku. Zastąpienie żywego socketu
   `FakeTelemetrySource` emitującym `Live + DISARMED` to **adaptacja do nowego zachowania**,
   nie osłabienie. Mockowany jest TYLKO zewnętrzny seam (źródło/transport), nie testowana
   logika (`commandAvailability`, `commandFeedback` pozostają prawdziwe).

2. **Asercje wzmocnione, nie osłabione.** Istniejące testy zachowały oryginalne asercje
   (`assertEquals("Armed", …)`, `is CommandFeedback.Rejected` + treść). DODANO dwa nowe
   testy z realną mocą wyroczni:
   - `command on a stale link is vetoed` — `assertNull(sender.lastCommand)`: usunięcie
     guardu #2 spowodowałoby wysłanie i `lastCommand != null` → test FAILuje. Prawdziwa wyrocznia.
   - `a second command while one is in flight is dropped` — `assertEquals(1, callCount)`:
     usunięcie guardu #3 dałoby `callCount == 2` → FAIL. Prawdziwa wyrocznia.

3. **Brak antywzorców z katalogu:** żadnego `toBe→toBeDefined`, żadnego assertion-free
   testu, żadnego usunięcia testu, żaden test nie mockuje testowanej funkcji.

Reguła kciuka „czy test FAILuje, gdy usunę testowaną transformację?" — spełniona dla
obu nowych testów ViewModelu oraz dla nowych testów domenowych (każdy ma komentarz-wyrocznię
wskazujący alternatywną gałąź, która dałaby inny wynik).

---

## Weryfikacja braku regresji po seam `TelemetrySource` + Factory

- `TelemetryViewModel` zależy WYŁĄCZNIE od interfejsu `TelemetrySource` (brak importu
  `TelemetryRepository`) — odwrócenie zależności poprawne.
- `TelemetryRepository : TelemetrySource` (`repository/TelemetryRepository.kt:51`),
  `start()`/`stop()` oznaczone `override`, sygnatury zgodne z interfejsem.
- `CommandApi : CommandSender` — transport za seam (bez zmian, nadal jedyny mock w teście).
- `TelemetryViewModelFactory.create` waliduje `modelClass` (fail-fast `require`), buduje
  `OkHttpClient(network=null)` zgodnie z istniejącą sygnaturą `EspHttpClient.build(Network?)`.
- `MainActivity` — brak wiszących referencji do usuniętego `by lazy`/pola `repository`.
- Graf pakietów acykliczny: `ui → repository (interface) ← repository (impl) → net → data`.
  Brak cyklu. ViewModel nie sięga do `net`/`data` z pominięciem warstwy.

Drobna uwaga (nie finding): `TelemetryRepository` nadal eksponuje publiczne pola
`connection`/`latestFrame` (Flow) obok `state` z interfejsu — obecnie nieużywane przez
ViewModel (który derywuje slice'y sam ze `state`). To martwy-ish API powierzchni repo
(dead code candidate), ale nie błąd i nie regresja z tej fazy.

---

## Findingi pozostałe (P3 — nity, opcjonalne, nie blokują)

Wszystkie z cyklu 0, świadomie odłożone (potwierdzone w `…-zadania.md`):

- 🟡 [nit] **CommandBar.kt:94** — `Color` fully-qualified inline zamiast importu (coding-rules pkt 8).
- 🟡 [nit] **CommandFeedback.kt** — pure/host-testowana w pakiecie `ui`; spójniej w `domain`.
- 🟡 [nit] **WebPanelLink.kt:14 / CommandApi.kt:37 / TelemetrySocket.kt:37** — `192.168.4.1`
  zduplikowany w ≥3 miejscach; wspólna stała `EspAp.BASE_URL` (coding-rules pkt 3/6).
- 🟡 [nit] **GpsCard / CompassCard** — `String.format(Locale.US, …)` per-rekompozycja na
  hot-path 10 Hz; `remember(frame) { … }`.
- 🟡 [nit] **SafetyBanner.kt** — `activeBanners(indicators)` (buildList) w ciele composable; `remember(indicators)`.
- 🟡 [nit] **TelemetryScreen.kt:45-49** — `_feedback` (MutableStateFlow) może zgubić komunikat
  przy szybkiej serii komend; `Channel`/`SharedFlow(replay=0)`. Świadomy trade-off v1.
- 🟡 [nit] **CommandFeedback.kt:33 → TelemetryScreen.kt** — surowy `message` z firmware bez
  limitu długości w snackbarze; `take(120)`. Brak ryzyka injekcji (Compose `Text`, lokalny AP).

---

## Pozytywnie zweryfikowane (kluczowe, utrzymane)

- `SafetyIndicators.linkDown` napędzany WYŁĄCZNIE przez `ConnectionState` — wymóg z briefu
  spełniony i pokryty mocnymi testami wyroczni (Stale + zdrowa ramka, Connecting + zdrowa ramka).
- `commandAvailability` fail-safe do `NONE` przy: nie-Live / brak ramki / nieznany kod /
  ESC_CALIBRATION — wszystkie czTERY ścieżki teraz pokryte testem.
- Defense-in-depth: re-walidacja w `sendCommand` ze świeżego state (nie z `WhileSubscribed`
  flow, którego wartość jest świeża tylko gdy collectowana) — przemyślany, poprawny detal.
- Mockowane TYLKO seamy zewnętrzne (`CommandSender`, `TelemetrySource`); logika domenowa
  i mapowanie feedbacku — prawdziwe.
- DEPLOY/STOW (ruch sprzętu) za `ConfirmDialog`; ARM/DISARM bez — sensowna gradacja.
- `collectAsStateWithLifecycle` per-slice; `distinctUntilChanged().stateIn(WhileSubscribed)`.

## Odchylenia od planu

Brak. Pliki Unit 6/7 z planu istnieją wraz z testami. Dodane poza planem, zgodne z intencją:
seam `TelemetrySource`, `TelemetryViewModelFactory` (rozwiązanie findingu lifecycle) —
poprawa testowalności i lifecycle, nie scope creep.
