---
date: 2026-06-29
topic: spot-lock-blackbox-logging
---

# Spot-lock blackbox — nagrywanie sesji na wodzie + auto-kalibracja przez USB

## Problem

Nastawy regulatora spot-locka (`spot_lock_deadband`, `spot_lock_max_throttle_pct`,
`spot_lock_gain_*`) są dziś łagodnymi placeholderami strojonymi w terenie metodą
prób i błędów. Operator zza drążków nie ma jak ocenić **dlaczego** kajak dryfuje,
oscyluje albo kręci donuty — nie widzi błędu pozycji, bearingu do celu ani
komend, jakie regulator wysyła. Strojenie jest ślepe i wolne.

Potrzebny jest „blackbox": ESP samo nagrywa na wodzie przebieg każdej sesji
spot-locka do wewnętrznego flasha (standalone, bez łączności). Po powrocie i
podłączeniu ESP do komputera (port COM) logi są zczytywane, a na ich podstawie
nastawy są kalibrowane i automatycznie zapisywane z powrotem. Kalibracja nigdy
nie dzieje się na wodzie — tylko offline, przy USB.

## Wymagania

- **R1. Nagrywanie standalone na wodzie.** Podczas używania ESP zapisuje przebieg
  sesji spot-locka do wewnętrznego flasha bez żadnej łączności (bez WiFi, bez
  telefonu, bez komputera). Pływanie i nagrywanie są w pełni autonomiczne.
- **R2. Trigger = aktywny spot-lock.** Nagrywane są wyłącznie chwile, gdy
  spot-lock jest włączony (`spot_lock_state != OFF`, czyli ACTIVE i PAUSED).
  Manualne pływanie pomiędzy sesjami nie jest zapisywane.
- **R3. Zawartość nagrania.** Dla każdej sesji: znacznik startu z celem (pozycja
  docelowa) i aktywnymi nastawami (deadband, max gaz, gainy) obowiązującymi w tej
  sesji, oraz szereg czasowy zachowania (błąd pozycji [m], bearing do celu vs
  heading dzioba, komendy serwa/ESC, stan sub-trybu, jakość fixu GPS, prędkość)
  z umiarkowaną częstotliwością. Cel: po fakcie da się zrekonstruować „co robił i
  z jakim skutkiem" oraz **przy jakich nastawach**.
- **R4. Zero wpływu na sterowanie.** Nagrywanie nie może stallować pętli 50 Hz
  ani w żaden sposób dotykać toru failsafe. Bufor w RAM + zapis na flash poza
  torem sterowania; utrata kilku próbek logu jest akceptowalna, jitter pętli — nie.
- **R5. Retencja pierścieniowa.** Gdy obszar logów się zapełni, nadpisywane są
  najstarsze sesje — zawsze dostępne są **najnowsze** (kalibracja patrzy na
  ostatnie wyjścia w teren).
- **R6. Odczyt przez USB/COM jako CSV.** Po podłączeniu ESP do komputera logi są
  zrzucane przez konsolę USB Serial/JTAG (port COM) w formacie nadającym się do
  bezpośredniej analizy przez Claude. Bez WiFi, bez USB Mass Storage.
- **R7. Auto-zapis nastaw przez USB.** Komenda konsolowa USB ustawia dostrojone
  nastawy i zapisuje je do NVS w pełni automatycznie (bez ręcznego potwierdzania).
  Firmware jest siatką bezpieczeństwa: zapis stosowany tylko w DISARMED, z
  walidacją zakresów i regułą single-writer (SI-6); komenda zwraca faktycznie
  zastosowane wartości.
- **R8. Cel strojenia = stabilność na pierwszym miejscu.** Kolejność priorytetów,
  gdy nastawy konkurują: (1) **spokój — trzymanie bez „polowania"** (główny), (2)
  dopływ bez przeregulowania, (3) brak donutów za rufą. Energia/cichość **nie**
  jest optymalizowana.

## Kryteria sukcesu

- Po jednej sesji na wodzie i podłączeniu USB Claude odtwarza z CSV przebieg
  błędu w czasie, aktywność komend i **które nastawy były aktywne**, i potrafi na
  tej podstawie zapisać poprawione nastawy jedną komendą.
- Nagrywanie wykazywalnie nie wpływa na timing pętli sterującej ani na failsafe
  (host-testy czystej logiki + brak regresji w torze 50 Hz / failsafe).
- Iterowanie pętli (wypłyń → wróć → Claude stroi → powtórz) widocznie redukuje
  polowanie, przeregulowanie i donuty między kolejnymi sesjami.

## Granice scope'u (non-goals)

- **Brak odczytu przez WiFi/panel ani USB MSC** — tylko zrzut przez konsolę
  USB/COM.
- **Brak kalibracji/strojenia na wodzie** — żadnych zmian UI strojenia na żywo;
  cała kalibracja offline przy USB.
- **Brak logowania trybów innych niż spot-lock** — manual, DEPLOY, sam failsafe
  nie są nagrywane.
- **Brak optymalizacji pod energię/baterię/hałas.**
- **Nie budujemy ogólnego rejestratora lotu** — blackbox jest celowo wąski, pod
  kalibrację spot-locka.

## Kluczowe decyzje

- **Inteligencja po stronie Claude, firmware „głupi".** Firmware tylko nagrywa,
  zrzuca CSV i stosuje przekazane wartości. Analiza przebiegów i wyliczenie
  nowych nastaw dzieją się offline (Claude z CSV), a wynik wraca jedną komendą
  zapisu. Uzasadnienie: trzyma firmware proste i host-testowalne, a logikę
  strojenia poza torem czasu rzeczywistego.
- **Auto-zapis bez potwierdzenia, z firmware jako siatką bezpieczeństwa.**
  Świadomie akceptujemy brak człowieka w pętli przy zapisie nastaw silnika —
  ryzyko ograniczają twarde reguły SI-6 (tylko DISARMED, walidacja zakresów,
  single-writer) i zwrot zastosowanych wartości do weryfikacji.
- **Nagrywanie tylko przy aktywnym spot-locku** (nie cała sesja ARMED) — wąski
  zakres pod cel, mniej danych do przegryzienia.
- **Stabilność > dokładność > brak donutów** jako jawna kolejność strojenia.

## Zależności / Założenia

- Buduje na ukończonej funkcji spot-lock: nastawy `spot_lock_deadband` /
  `spot_lock_max_throttle_pct` / `spot_lock_gain_*` istnieją w settings (schemat
  v6), a telemetria `control_loop_snapshot` zawiera już pola do zalogowania
  (stan, błąd, bearing, GPS, heading, komendy serwa/ESC, drążki).
- Na module 16 MB (N16R8) partycje zajmują ~1,6 MB — jest miejsce na dedykowany
  obszar logów.
- Konsola USB Serial/JTAG jest dostępna po podłączeniu (włączona w sdkconfig),
  więc istnieje naturalny kanał COM do zrzutu i komend.
- Praca na nowym feature branchu odgałęzionym od `feature/spot-lock-position-hold`
  (ukończona).

## Otwarte pytania

### Do rozwiązania przed planowaniem
- (brak — decyzje produktowe rozstrzygnięte)

### Odroczone do planowania
- [Dotyczy R3][Techniczne] Format rekordu (układ pól, rozmiar w bajtach),
  częstotliwość decymacji (orientacyjnie ~1–2 Hz z pętli 50 Hz), schemat nagłówka
  sesji.
- [Dotyczy R1/R5][Techniczne] Medium i rozmiar obszaru logów: dedykowana partycja
  surowa vs system plików; zmiana `partitions.csv`; sektorowy erase 4 KB;
  zachowanie pierścienia i wear.
- [Dotyczy R4][Techniczne] Konstrukcja bufora RAM + taska flush niskiego
  priorytetu tak, by erase flasha nie wprowadził jittera w pętlę 50 Hz; budżet
  pamięci.
- [Dotyczy R6][Techniczne] Schemat CSV i składnia komendy zrzutu; jak Claude
  parsuje strumień z portu COM (przez `idf.py monitor` / terminal szeregowy).
- [Dotyczy R7][Techniczne] Składnia i nazwa komendy zapisu nastaw; wpięcie w
  istniejący mechanizm pending/apply settings (SI-6) i zwrot zastosowanych wartości.
- [Dotyczy R8][Wymaga researchu] Konkretne metryki/heurystyki strojenia liczone z
  CSV (wykrycie polowania, przeregulowania, donuta) i mapowanie ich na zmianę
  nastaw — do dopracowania na etapie planowania/iteracji w terenie.

## Następne kroki
→ `/dev-plan` do planowania technicznego implementacji
