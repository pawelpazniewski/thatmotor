---
title: "Android: czyste funkcje decyzyjne za HAL — host-testowalna logika UI/sesji/sieci bez emulatora"
date: 2026-06-20
category: testing-issues
severity: medium
stack:
  - Android
  - Kotlin
  - JUnit
tags:
  - pure-hal
  - host-tests
  - foreground-service
  - lifecycle
  - safety-indicators
  - connection-state
status: verified
last_verified: 2026-06-20
---

# Android: czyste funkcje decyzyjne za HAL — host-testowalna logika UI/sesji/sieci

## Symptomy

- Logika UI/sesji/sieci zaszyta w klasach Android (`Service`, `NetworkCallback`,
  MapLibre, OkHttp) jest testowalna tylko na emulatorze/urządzeniu — wolno, flaky,
  wymaga SDK w CI.
- Wskaźnik bezpieczeństwa (`linkDown`) liczony z ostatniej ramki telemetrii: gdy
  ramka się "zamraża" (link padł, ale ostatnia ramka czytała się zdrowo), banner
  pokazywał stan bezpieczny mimo zerwanego łącza.
- Foreground service: ryzyko wycieku wake locka i `IllegalArgumentException` przy
  `unbindService`, gdy `bindService` nie powiódł się, oraz podwójny teardown
  zasobów sesji.

## Root Cause

1. Brak separacji decyzji od HAL — logika splątana z frameworkiem Androida, więc
   nie da się jej wywołać na czystej JVM.
2. `linkDown` zależał od pola zamrożonej ramki, a nie od osobnego źródła prawdy o
   stanie łącza — zamrożona zdrowa ramka jest nieodróżnialna od żywego łącza.
3. Cleanup w `onDestroy` bez guardów: brak release wake locka w `finally`, brak
   flagi `isBound` chroniącej przed unbind nieudanego bind, brak "teardown-once".

## Rozwiązanie

### 1. Pure ⊥ HAL na Androidzie — wyciągnij czyste funkcje decyzyjne

Każdą nietrywialną decyzję wyciągnij do czystej funkcji `(inputs)→(outputs)` bez
importów `android.*`; adapter Android (Service/NetworkCallback/MapLibre/OkHttp)
pozostaje cienki i tylko mapuje wejścia/wyjścia. Testuj na JVM (JUnit, `test/`),
bez emulatora.

Zrealizowane czyste moduły (`android/app/src/main/java/com/thatmotor/kayak/`):
`domain/SafetyIndicators.kt`, `domain/CommandAvailability.kt`,
`domain/LinkWatchdog.kt`, `map/BoatMarkerProjection.kt`,
`session/SessionPolicy.kt`, `map/OfflineStyle.kt`,
`net/ApConnectionStateReducer.kt` — każdy z odpowiadającym testem w `test/`.

```kotlin
// LinkWatchdog.kt — czysta decyzja, jeden monotoniczny zegar, bez android.*
fun linkStatus(
    lastFrameElapsedMs: Long,
    nowElapsedMs: Long,
    thresholdMs: Long = DEFAULT_STALE_THRESHOLD_MS,
): LinkStatus {
    require(thresholdMs > 0) { "thresholdMs must be positive" }
    val elapsed = nowElapsedMs - lastFrameElapsedMs   // single clock domain
    return if (elapsed > thresholdMs) LinkStatus.STALE else LinkStatus.LIVE
}
```

### 2. `linkDown` zależny WYŁĄCZNIE od ConnectionState, nie od zamrożonej ramki

```kotlin
// SafetyIndicators.kt
return SafetyIndicators(
    linkDown = connection != ConnectionState.Live,  // stan łącza, NIE pole ramki
    failsafe = isFailsafe,
    armed = isArmed,
    uncalibrated = !frame.calibrated,
)
```

Zamrożona zdrowa ramka nie może wyglądać bezpiecznie — flaga zdrowia łącza musi
wynikać z osobnego źródła prawdy (faza połączenia/watchdog), nie z treści ostatniej
odebranej ramki.

### 3. Foreground service — cleanup bez wycieków

```kotlin
// TelemetryService.onDestroy(): teardown-once + release w finally
override fun onDestroy() {
    wantsWakeLock = false
    mainHandler.removeCallbacks(reacquireRunnable)
    releaseWakeLock()                 // przed super.onDestroy
    val cleanup = onSessionStopped
    onSessionStopped = null           // teardown-once guard
    try {
        cleanup?.invoke()
    } catch (e: RuntimeException) {
        Log.e(TAG, "Session cleanup failed", e)
    } finally {
        super.onDestroy()             // zawsze, nawet gdy cleanup rzuci
    }
}

// MainActivity: flaga isBound przeciw IllegalArgumentException
isBound = bindService(intent, serviceConnection, BIND_AUTO_CREATE)
// ...
if (isBound) { unbindService(serviceConnection); isBound = false }
```

## Komendy diagnostyczne

```bash
# Host testy (JVM) — bez emulatora
cd android && ./gradlew testDebugUnitTest

# Sprawdź że czyste moduły nie importują android.*
grep -rL "import android" android/app/src/main/java/com/thatmotor/kayak/domain
grep -rn "import android" android/app/src/main/java/com/thatmotor/kayak/domain || echo "clean"
```

## Zapobieganie

- Dla każdej nietrywialnej decyzji za HAL (Service/NetworkCallback/MapLibre/OkHttp)
  wyekstrahuj czystą funkcję bez `android.*` i pokryj host-testem; adapter trzymaj
  cienki.
- Flagi zdrowia łącza wyprowadzaj z fazy połączenia/watchdoga, nie z pól ostatniej
  (możliwie zamrożonej) ramki.
- W `onDestroy` foreground service: release wake locka w `finally` przed
  `super.onDestroy`, guard "teardown-once" (null-uj callback po przejęciu), flaga
  `isBound` przed `unbindService`.

## Powiązane

- `docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md`
  — ten sam wzorzec Pure ⊥ HAL na firmware ESP-IDF (ta sesja uogólnia go na Androida).
- `docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md`
  — `LinkWatchdog` stosuje jeden monotoniczny zegar (`elapsedRealtime()`).

## Kontekst

Autopilot android-tablet-app (natywny Android Kotlin, katalog `android/`), 5 faz.
Czyste moduły w `com.thatmotor.kayak.domain/session/net/map`, testy JVM kolokowane
w `test/`. Foreground service: `session/TelemetryService.kt`, bind w `MainActivity.kt`.
