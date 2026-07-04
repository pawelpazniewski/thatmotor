# Review fazy 4 — Kompaktowy HUD (Unit 4)

Data: 2026-07-03
Commit: `cceff77` — feat(ios): Unit 4 — kompaktowy HUD telemetrii w lewym górnym rogu
Pliki objęte review:
- `ios/KayakMotor/Features/Telemetry/HudView.swift` (NOWY, 93 linie)
- `ios/KayakMotor/RootView.swift` (modyfikacja: `showTelemetryDetail`, `hudOverlay`, placeholder `.sheet`)

## Severity gate: ⚠️ ZASTRZEŻENIA

- P1 (blocking): **0**
- P2 (important): **1**
- P3 (nit): **3**
- E2E: 3 scenariusze — **wymagają ręcznej weryfikacji na symulatorze/urządzeniu** (natywny iOS, brak przeglądarki); logika zweryfikowana STATYCZNIE jako poprawna.

Decyzja: **KONTYNUUJ Z ZASTRZEŻENIAMI** — 1 problem P2 do naprawy (najlepiej przed/w trakcie Unit 5, bo Unit 5 dotknie tej samej logiki).

---

## Metodologia

Native iOS/SwiftUI — brak przeglądarki, brak interaktywnego symulatora w tym środowisku. `agent-browser` nieuruchamialny (zwróciłby błąd), więc scenariusze [E2E] zweryfikowano statycznie przez czytanie kodu (logika za scenariuszem), a nie jako PASS/FAIL. Perspektywy security / performance / architektura / scenariusze / E2E przeprowadzone bezpośrednio, z cross-referencją do planu technicznego `docs/plans/2026-07-03-002-feat-ios-telemetry-hud-plan.md` (Unit 4) oraz reguł projektu.

Zweryfikowano istnienie wszystkich symboli używanych przez `HudView`:
- Pola `Telemetry`: `gpsFix`, `gpsSats`, `gpsSpeedCms`, `imuHeadingDeg10`, `gotoState`, `gotoErrM`, `gotoBearingDeg10`, `spotLockState`, `spotLockErrM`, `spotLockBearingDeg10`, `state` — wszystkie istnieją.
- `TelemetryDisplay`: `placeholder`, `gpsQualityText`, `stateLabel`, `modeLabel`, `targetText`, `speedHeadingText`, `displayed`, `isFresh` — wszystkie istnieją.
- `SunlightTheme`: `panelBackground`, `cornerRadius`, `hairline`, `brand`, `rounded` — wszystkie istnieją.
- `TelemetryStore.latest` / `.linkState` — istnieją. `HoldState` cases (off/active/paused/unknown) zgodne z użyciem.
Kod powinien się kompilować (build app target wymaga `cd ios && xcodegen generate` — `project.pbxproj` jest gitignore; commit deklaruje BUILD SUCCEEDED + swift test 65/65).

---

## Perspektywa 1 — Security

**CZYSTE. Zero findingów.**
Widok tylko-do-odczytu konsumujący `TelemetryStore`. Brak inputów użytkownika, brak auth/RLS/SQL, brak sekretów/kluczy API, brak deserializacji niezaufanych danych w tej warstwie (dekodowanie WS jest w kontrakcie, poza zakresem fazy). `accessibilityLabel` to statyczny string. Brak ekspozycji danych wrażliwych.

## Perspektywa 2 — Performance

**CZYSTE. Zero findingów.**
- Brak pętli, N+1, ciężkich alokacji. Formatowanie stringów tanie.
- Brak animacji wartości → dobrze dla wydajności i reduced-motion.
- `HudView` czyta `store.latest`/`store.linkState` w `body` — `@Observable` zawęża re-render do tego widoku przy zmianie telemetrii (~10 Hz). Akceptowalne.
- Obserwacja (nie finding): `isFresh` liczony 4× na render (raz per wiersz) — trywialne porównanie equality, pomijalne.

## Perspektywa 3 — Architektura i jakość kodu

**Widok cienki — POTWIERDZONE dla formatowania:** każdy string składany jest przez `TelemetryDisplay` (gpsQualityText / stateLabel / modeLabel / targetText / speedHeadingText), zero duplikacji **formatu** w `HudView`. Staleness (R6) jednym predykatem `isFresh` przepuszczonym przez `displayed(_:isFresh:)` — brak zamrożonych liczb, brak force-unwrap (potwierdzone skanem: `try!`/`as!`/`!.` — brak).

**Type safety / styl:** brak `any`, brak force-unwrap (guard let + optional `.map`), camelCase/PascalCase OK, importy (SwiftUI + KayakContract) oba używane. Plik 93 linie, funkcje krótkie — zgodne z regułami rozmiaru.

**Finding P2 (patrz niżej):** `target(_:)` zawiera LOGIKĘ DECYZYJNĄ wyboru aktywnego źródła (goto>spot-lock), nie sam format — to logika host-testowalna, która wg decyzji projektu powinna żyć w `TelemetryDisplay`, i która duplikuje priorytet już zakodowany w `modeLabel`.

## Perspektywa 4 — Scenariusze i pokrycie testowe

- **Happy path:** żywa telemetria → 4 wiersze; pola i funkcje istnieją, logika poprawna. ✓
- **Brak ramki (`latest == nil`):** każdy wiersz zwraca `placeholder` „—" (guard let). ✓
- **Stale link (`latest != nil`, link ≠ connected):** `isFresh == false` → „—", nie zamraża. Poprawne R6. ✓
- **Fix false przy świeżym łączu:** `gpsQualityText` → „brak fix". ✓
- **Tryb ręczny (brak goto/spot-lock):** `target()` → nil → sam tryb bez celu. ✓
- **Concurrency:** `@MainActor` store, jedno źródło; brak race. ✓
- **Pokrycie testowe:** plan Unit 4 NIE definiuje pliku unit-testów dla widoku (świadoma decyzja: target aplikacji bez testów; logika stringów pokryta w Unit 3 `TelemetryDisplayTests`). Brak brakujących testów wg planu → brak P2 z tego tytułu.
- **Luka:** logika `target(_:)` (wybór źródła) NIE jest pokryta żadnym testem, bo siedzi w nietestowalnym targecie — to konkretny koszt findingu P2 (ekstrakcja do `TelemetryDisplay` uczyniłaby ją host-testowalną).

## Perspektywa 5 — E2E (symulator, weryfikacja ręczna)

Środowisko bez przeglądarki i bez interaktywnego symulatora — poniższe scenariusze **wymagają ręcznej weryfikacji na symulatorze/urządzeniu**, NIE oznaczone jako PASS ani FAIL. Weryfikacja statyczna logiki:

| Scenariusz | Weryfikacja statyczna |
|---|---|
| Start → HUD w lewym górnym rogu, środek mapy czysty, brak kolizji z zoom/waypoints | ✓ `hudOverlay` na `.topLeading`, `.padding(.top,56)` pod chipem; zoom na `.trailing`, waypointy góra-prawo; VStack kompaktowy (intrinsic + `lineLimit(1)`) — środek niezasłonięty |
| Tap HUD → arkusz; swipe w dół → chowa | ✓ `onTapGesture` → `showTelemetryDetail = true` → `.sheet` z `.presentationDetents([.medium,.large])` (placeholder Unit 5); swipe-down to natywne dismiss arkusza |
| Rozłącz link → wartości → „—", nie zamrażają | ✓ `isFresh(linkState)` fresh tylko `.connected`; `displayed()` zwraca „—" poza tym |

---

## Findingi

### 🟠 P2 (important)

- **`HudView.swift:82-92`** — `target(_:)` zawiera decyzję wyboru aktywnego źródła (goto gdy `gotoState ∉ {off,unknown}`, inaczej spot-lock, inaczej nil). To (a) **duplikuje priorytet goto>spot-lock** już zakodowany w `TelemetryDisplay.modeLabel` (rozjazd, gdy ktoś zmieni priorytet w jednym miejscu — anty-pattern #9 „context blindness / duplikuje logikę"), oraz (b) siedzi w nietestowalnym targecie aplikacji, wbrew decyzji projektu „logika host-testowalna w KayakContract, widoki cienkie" (kontekst, pkt 1). **Unit 5** (arkusz szczegółów) będzie potrzebował tego samego wyboru źródła → bez ekstrakcji powstanie TRZECIA kopia. Reguła „abstrakcja dopiero gdy 2+ użycia" jest już spełniona. Zalecenie: wyciągnąć do `TelemetryDisplay` czystą funkcję, np. `activeTargetText(gotoState:gotoErrM:gotoBearingDeg10:spotLockState:spotLockErrM:spotLockBearingDeg10:) -> String?` (albo enum `ActiveSource`), pokrytą host-testem z mocą wyroczni (mutacja odwracająca priorytet MUSI failować). Nie blokuje kontynuacji, ale najlepiej domknąć przed/w trakcie Unit 5.

### 🟡 P3 (nit)

- **`HudView.swift:31-32`** — `.accessibilityLabel("Telemetria — dotknij…")` NADPISUJE etykietę syntetyzowaną przez `children:.combine`, więc VoiceOver ogłasza tylko statyczny string, nie żywe wartości (GPS/stan/tryb/prędkość). Dane pozostają dostępne przez arkusz (wzorzec „przycisk-podsumowanie" → tap otwiera szczegóły), więc to drobne. Rozważ `.accessibilityValue(...)` niosące zwięzłe podsumowanie 4 wierszy, by VoiceOver czytał aktualny stan bez otwierania arkusza.
- **`HudView.swift:15-21`** — panel bez jawnego limitu szerokości; kompaktowość opiera się na krótkich stringach + `lineLimit(1)`. Przy realistycznych danych OK, ale długi wiersz trybu („Spot-lock (pauza) · 1234 m · 180°") mógłby rozciągnąć panel (frame ma `maxWidth:.infinity`). Rozważ `.frame(maxWidth: …)` lub `.fixedSize(horizontal:true, vertical:false)` dla gwarantowanej kompaktowości. (Środek mapy i tak niezasłonięty — kotwica top-leading.)
- **`HudView.swift:25-26` (odchylenie od planu)** — użyto `SunlightTheme.cornerRadius` (16) zamiast `panelRadius` (26) sugerowanego w tekście planu Unit 4. Nieszkodliwy wybór stylu (spójny z resztą paneli); jedynie do świadomej akceptacji.

---

## Odchylenia od planu

- Plan (Podejście) sugerował `RoundedRectangle(cornerRadius: panelRadius)`; implementacja użyła `cornerRadius` (16). Kosmetyczne, zaakceptowane jako P3.
- Poza tym implementacja zgodna z planem Unit 4: pliki `HudView.swift` (nowy) + `RootView.swift` (modyfikacja) dokładnie jak w sekcji **Pliki:**; brak zdefiniowanego w planie pliku unit-testów dla tej fazy (świadome — logika w Unit 3). Brak brakujących artefaktów testowych.

## Podsumowanie

Solidny, cienki widok — formatowanie w całości delegowane do `TelemetryDisplay`, staleness poprawny (R6), layout nie koliduje i nie zasłania środka mapy, tap→arkusz podłączony z właściwymi detentami, brak force-unwrap/`any`/sekretów. Jeden realny dług: decyzja wyboru aktywnego źródła (`target()`) powinna trafić do host-testowalnego kontraktu przed Unit 5, by uniknąć potrójnej duplikacji priorytetu goto>spot-lock. Scenariusze E2E do potwierdzenia ręcznie na symulatorze/urządzeniu.

---

## Re-review po cyklu 1

Data: 2026-07-03
Commit naprawczy: `a0c48b6` — fix(ios): poprawki po review fazy 4 (cykl 1) — ekstrakcja wyboru źródła do TelemetryDisplay

### Severity gate: ✅ CZYSTE

- P1: **0** · P2: **0** (był 1 — rozwiązany) · P3: **1** (świadomie pominięty accessibilityLabel — nie blokada, dane dostępne przez arkusz)
- `swift test`: **69/69** przechodzi (13 suit; było 65/65 → +4 nowe testy).

### Weryfikacja findingu P2 — ROZWIĄZANY

1. **Ekstrakcja do modułu pure — POTWIERDZONE.** `TelemetryDisplay.activeTargetText(gotoState:gotoErrM:gotoBearingDeg10:spotLockState:spotLockErrM:spotLockBearingDeg10:) -> String?` żyje w `ios/KayakKit/Sources/KayakContract/TelemetryDisplay.swift`. Plik importuje wyłącznie `Foundation` (linia 1) — BEZ SwiftUI, host-testowalny.
2. **HudView deleguje — POTWIERDZONE.** `HudView.target(_:)` to teraz cienki przelot pól do `TelemetryDisplay.activeTargetText(...)`; usunięto duplikat logiki wyboru (poprzednie `if gotoState != .off && != .unknown …` zniknęło). Zero decyzji priorytetu w widoku.
3. **Wspólna reguła priorytetu — POTWIERDZONE.** `modeLabel` i `activeTargetText` obie bramkują prywatnym `isEngaged(_:) = (state == .active || state == .paused)`. Jedno źródło reguły goto>spot-lock → brak ryzyka rozjazdu (usuwa anty-pattern #9).
4. **Host-testy z mocą wyroczni — POTWIERDZONE.** 4 nowe testy w `TelemetryDisplayTests.swift`:
   - `activeTargetPrefersGoto`: oba źródła `.active`, RÓŻNE pola (goto errM=100/bearing=900 vs spot-lock 200/1800) → oczekuje `"100 m · 90°"`. Odwrócenie priorytetu dałoby `"200 m · 180°"` → test FAILUJE. Prawdziwa wyrocznia, nie tożsamość.
   - `activeTargetFallsBackToSpotLock`: goto `.off` → wynik z pól spot-locka `"200 m · 180°"` (nie z goto) → potwierdza że wybrane errM/bearing pochodzą z właściwego źródła.
   - `activeTargetNoneWhenManual`: oba off/unknown → `nil`.
   - `activeTargetGotoPausedUsesGotoFields`: goto `.paused` bierze WŁASNE pola (55/100) mimo aktywnego spot-locka z innymi wartościami → potwierdza brak pomylenia pól między źródłami i że `.paused` liczy się jako engaged.
   Każdy test ma asercję, żaden nie jest assertion-free ani tożsamościowy.

### Weryfikacja P3 (trywialne) — ROZWIĄZANE

- `SunlightTheme.cornerRadius` (16) → `SunlightTheme.panelRadius` (26) w `background` i `overlay`. ✓
- Jawny limit szerokości panelu: `.frame(maxWidth: Self.maxPanelWidth, alignment: .leading)` ze stałą `maxPanelWidth = 240`. ✓
- `accessibilityLabel` — świadomie pominięty (P3 nit, dane dostępne przez arkusz). Nie blokada.

### Brak regresji

Poprzednio czyste lensy trzymają: Security (0), Performance (0), staleness R6 (predykat `isFresh` + `displayed()` nietknięte), layout (top-leading, środek mapy niezasłonięty — limit szerokości go wzmacnia). `modeLabel` przepisany na `isEngaged`, ale zachowuje semantykę (goto/goto-pauza/spot-lock/spot-lock-pauza/ręczny) — pokryte istniejącymi testami `modeLabel`, wszystkie zielone.

### Decyzja

**GOTOWE — faza 4 domknięta z perspektywy statycznej.** Pozostaje wyłącznie ręczna weryfikacja E2E na symulatorze/urządzeniu (3 scenariusze), niemożliwa w tym środowisku (natywny iOS, brak przeglądarki).
