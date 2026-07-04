# Review fazy 5 (Unit 5) — Dolny arkusz telemetrii + sekcja trimu

Data: 2026-07-03
Commit: `4512d8e` — feat(ios): Unit 5 — dolny arkusz telemetrii + regulacja trimu serwa (DISARMED-gated)
Branch: `feature/kayak-motor-firmware-v1`

## Severity gate: ✅ CZYSTE (GOTOWE DO KONTYNUACJI)

- 🔴 P1 (blocking): **0**
- 🟠 P2 (important): **0**
- 🟡 P3 (nit): **3**
- 🌐 E2E (symulator): 4 scenariusze — logika zweryfikowana STATYCZNIE jako poprawna; render/interakcja na żywo wymaga ręcznej weryfikacji na symulatorze (natywny iOS, brak przeglądarki — NIE FAIL/NIE PASS)

## Zakres sprawdzony (5 plików)

- `ios/KayakMotor/Features/Telemetry/TelemetryDetailView.swift` (nowy, 147 linii)
- `ios/KayakMotor/RootView.swift` (podpięcie sheet zamiast placeholdera)
- `ios/KayakKit/Sources/KayakContract/TelemetryDisplay.swift` (+2 helpery: `boolText`, `holdStateLabel`)
- `ios/KayakKit/Tests/KayakContractTests/TelemetryDisplayTests.swift` (+2 testy)
- docs (kontekst/zadania) — zaktualizowane

Walidacja: `swift test` KayakKit **71/71 zielone** (potwierdzone lokalnie). Build app target: BUILD SUCCEEDED (wg kontekstu).

## Weryfikacja wymagań

### R5 gate trimu (KRYTYCZNE) — ✅ POPRAWNE
- Cała sekcja trimu ma `.disabled(!isEnabled)` na poziomie `Section` (TelemetryDetailView.swift:105). W SwiftUI `.disabled` na kontenerze propaguje się do WSZYSTKICH kontrolek potomnych → przy ARMED/FAILSAFE/KALIBRACJA/WYSUWANIE/`nil` przyciski `−`/`+`/„Zapisz" NIE wywołają `trimLeft/trimRight/saveTrim`. Zweryfikowane strukturalnie.
- `isEnabled = TelemetryDisplay.isTrimEnabled(state: store.latest?.state ?? .unknown)` — bezpieczny default `.unknown` (nil ramka → wyłączone).
- Notka „Rozbrój, aby wyregulować neutral" renderowana tylko gdy `!isEnabled`. OK.
- `isTrimEnabled` host-testowany z mocą wyroczni (`trimEnabledOnlyDisarmed`: mutacja „zawsze true" MUSI failować — potwierdzone: test asertuje `false` dla 5 pozostałych stanów).

### R5 jedno źródło prawdy — ✅ POPRAWNE
- Wartość trimu czytana wyłącznie z telemetrii: `TelemetryDisplay.trimText(servoTrimUs: $0.servoTrimUs)`. Brak lokalnej kopii stanu, która mogłaby się rozjechać. `TelemetryStore` jest `@Observable` → po komendzie nowa ramka (~100 ms) odświeży wiersz automatycznie.

### R4 scope — ✅ POPRAWNE
- Tylko kurowany zestaw: GPS (fix/sat/prędkość/pozycja), Kompas (kurs/kalibracja IMU/OK), Spot-lock (stan/błąd/namiar), Goto (stan/błąd/namiar/cel/dotarto), Łącze-RC (RC poprawny/link świeży).
- Brak surowych µs RC / okresów / servo-esc µs / NVS/źródła ustawień. Zgodne z „poza zakresem" planu.

### R6 staleness — ✅ POPRAWNE
- Każdy wiersz danych przechodzi przez `valueRow` → `TelemetryDisplay.displayed(make($0), isFresh:)`. `isFresh = TelemetryDisplay.isFresh(store.linkState)` (true tylko `.connected`). Brak zamrożonych liczb.
- Brak force-unwrap: `store.latest.map { … } ?? placeholder`. Nil ramka → myślnik.

### Widok cienki — ✅ POPRAWNE
- Cała logika warunkowa/switch w module pure `TelemetryDisplay` (bez SwiftUI), host-testowana: `holdStateLabel` (exhaustive, unknown→„—"), `boolText`, `isTrimEnabled`, `isFresh`, `displayed`.
- Inline w widoku pozostają WYŁĄCZNIE bezwarunkowe formattery (`degreesText` = `"%.0f°"`, `coordinateText` = `"%.5f, %.5f"`, prędkość `"%.1f m/s"`, `"\(errM) m"`) — brak przeciekniętego warunku/switcha bez testu.
- Format współrzędnych reużywa `LatLonE7.latDegrees/lonDegrees` i jest identyczny z `WaypointListView.swift:46-47` (`"%.5f, %.5f"`). Spójność potwierdzona.

### Sheet / detenty — ✅ POPRAWNE
- `RootView.swift:52-54`: `.sheet(isPresented:$showTelemetryDetail){ TelemetryDetailView(...).presentationDetents([.medium,.large]) }`. Placeholder „Szczegóły — Unit 5" z Unit 4 usunięty.

## Perspektywy review

- **Security:** brak powierzchni ataku. Komendy trim idą przez istniejący `/api/command` (Unit 2), brak sekretów, brak wstrzyknięć, brak ekspozycji danych. CZYSTE.
- **Performance:** statyczna `List`, brak N+1, brak pętli z fetch. `@Observable` reaktywność bez nadmiarowych re-renderów. Widok = value type, brak wycieków/`useEffect`-cleanup problemów. CZYSTE.
- **Architektura / type-safety:** brak `any`, brak force-unwrap, brak pustego `catch`. Plik 147 < 300 linii, funkcje < 50. Jedna odpowiedzialność. Import: `SwiftUI` + `KayakContract`, oba używane. Warstwy respektowane (widok → contract, brak logiki biznesowej w widoku). CZYSTE.
- **Testy:** happy path (`boolText true/false`, `holdStateLabel` wszystkie 4 stany), boundary (`unknown`→„—"), gate (`isTrimEnabled` oracle) pokryte. Target app bez testów zgodnie z planem (logika w KayakContract). CZYSTE.

## E2E (symulator — wymaga ręcznej weryfikacji, logika zweryfikowana statycznie)

| Scenariusz | Logika (statycznie) | Status |
|---|---|---|
| DISARMED → tap „+" → trim rośnie o krok po ~100 ms | `model.trimRight` → `.trimRight`; wartość wraca kolejną ramką przez `servoTrimUs` (jedno źródło, `@Observable`) | ✅ logika OK — wymaga symulatora |
| ARMED → sekcja wyszarzona, przyciski nieaktywne, notka | `.disabled(!isEnabled)` na Section propaguje do przycisków + notka `if !isEnabled` | ✅ logika OK — wymaga symulatora |
| stale link → pola „—", nie stare liczby | `displayed(_,isFresh:)`, `isFresh` false poza `.connected` | ✅ logika OK — wymaga symulatora |
| „Zapisz" (DISARMED) → potwierdzenie w `lastActionMessage` | `model.saveTrim` → `lastActionMessage = "Trim zapisany"`/błąd; `RootView` renderuje toast | ✅ logika OK — wymaga symulatora |

## Findingi (wszystkie P3-nit)

- 🟡 **TelemetryDetailView.swift:86** — bramka WŁĄCZENIA przycisków trimu opiera się tylko na `store.latest?.state`, NIE na świeżości. Gdy łącze przejdzie w `stale` przy ostatniej ramce `.disarmed`, przyciski pozostają aktywne, mimo że wiersz „Neutral serwa" pokazuje już „—". Kryterium Weryfikacji mówi „z żywym odczytem". Brak zagrożenia bezpieczeństwa (disarmed = bezpieczne; firmware ignoruje trim przy ARMED; wysyłka przy stale i tak zwróci błąd do `lastActionMessage`), ale drobna niespójność UX. Rozważyć `isEnabled = isTrimEnabled(...) && isFresh`.
- 🟡 **TelemetryDetailView.swift:93** — przycisk „Zapisz" używa domyślnego `.buttonStyle(.bordered)` bez wymuszonego `.frame(height: 44)`; wysokość dotykowa może być nieco < 44 pt (HIG). Przyciski `−`/`+` mają jawne `44×44`. Kosmetyczne.
- 🟡 **TelemetryDetailView.swift:139** — `degreesText` żyje inline w widoku zamiast w `TelemetryDisplay`. Jest bezwarunkowym formatem (bez logiki), a inline-format jest spójny ze wzorcem `WaypointListView`, więc akceptowalne; ewentualnie przenieść do kontraktu dla jednolitości. Nieblokujące.

## Wniosek

Faza 5 domyka feature (Unit 1-5). Wszystkie krytyczne wymagania (R4 scope, R5 gate DISARMED + jedno źródło prawdy, R6 staleness) zweryfikowane jako POPRAWNE statycznie. Zero P1/P2. Trzy nity do świadomej decyzji. E2E na symulatorze do ręcznego domknięcia.
