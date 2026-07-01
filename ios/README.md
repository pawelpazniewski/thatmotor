# KayakMotor — aplikacja iOS (sterowanie silnikiem kajaka)

Cienki klient firmware ESP32 (`kayak-motor`): mapa offline + tap-to-goto. Plan:
`docs/plans/2026-07-01-002-feat-ios-app-goto-navigation-plan.md`.
Zadania: `docs/active/ios-app-goto-navigation/`.

## Struktura
- `KayakKit/` — Swift Package z **czystą logiką kontraktu** (konwersja współrzędnych,
  dekodowanie telemetrii, koperta komend). Bez UIKit/MapLibre → host-testowalny.
- `DeRiskProbe/` — **Unit 0**: throwaway app bramki de-risk (łączność iPhone↔AP).
- `project.yml` — spec XcodeGen; `KayakMotor.xcodeproj` jest z niego generowany.

## Wymagania
- Xcode 26+, iOS 17+ (iPhone). Realne urządzenie do bramki de-risk i testów E2E —
  Simulator NIE dołączy do AP silnika ani nie osiągnie `192.168.4.1`.

## Generowanie projektu
```
brew install xcodegen      # jednorazowo
cd ios && xcodegen generate
```

## Host-testy czystej logiki (bez Simulatora)
```
cd ios/KayakKit && swift test
```

## Build (Simulator, sanity)
```
cd ios
xcodebuild -project KayakMotor.xcodeproj -target DeRiskProbe -sdk iphonesimulator CODE_SIGNING_ALLOWED=NO build
```

## Bramka de-risk (Unit 0) — NA URZĄDZENIU
1. Otwórz `KayakMotor.xcodeproj`, wybierz target **DeRiskProbe**, ustaw swój Team
   (podpisywanie) — entitlement Hotspot Configuration wymaga profilu.
2. Uruchom na iPhonie z **aktywnym LTE**.
3. Wykonaj po kolei: „Dołącz do AP" → „HTTP POST" → „Start WebSocket".
4. Zapisz wynik (czasy, PASS/FAIL, zachowanie odmowy Local Network) w
   `docs/dev-brainstorms/2026-07-01-ios-derisk-gate-results.md`.
5. **Zielone** → kontynuujemy plan. **Czerwone** → stop, rozmowa o BLE (Plan B).
