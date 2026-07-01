# Spot-lock blackbox — procedura kalibracji (offline, przez USB)

Firmware jest „głupi": nagrywa sesje spot-locka na flash, zrzuca je jako CSV i stosuje
przekazane nastawy. Cała inteligencja strojenia jest po stronie Claude, offline. Ten dokument
opisuje pętlę: **dump → analiza CSV → `params set` → reflash off** oraz jak z kolumn CSV wyliczyć
metryki i w którą stronę zmieniać nastawy regulatora.

> **Priorytet: stabilność (R8).** Lepszy powolny, stabilny hold niż szybki z „polowaniem".
> Przy niepewności strojenia **zmniejszaj** gainy / zwiększaj deadband. Zmiany o mały krok
> (rząd ±20–30% wartości), jedna nastawa na iterację, weryfikacja na wodzie.

---

## 1. Sekwencja polowa

1. **Wypłyń i wykonaj sesję spot-locka** (ARMED + świeży fix + drążki na zerze + CH3 ON). Blackbox
   nagrywa nagłówek sesji na zboczu OFF→non-OFF i próbki ~2 Hz do non-OFF→OFF.
2. **Dumpuj PRZED odłączeniem/rebootem.** Podłącz ESP przez lewy USB-C (USB Serial/JTAG, `/dev/ttyACM*`),
   otwórz konsolę i wykonaj `spotlog dump`. Zapisz surowy CSV.
   - Powód „przed rebootem": po zawinięciu całego regionu (>16384 rekordów) kursor nie odtwarza
     dokładnej głowicy zapisu (brak per-record seq — patrz known-issues §4b). Jedno wypłynięcie
     ≪ 16384 slotów, więc w praktyce wystarczy dump po każdym wypłynięciu.
3. **Analiza (Claude):** parsuj CSV, policz metryki (sekcja 3), wybierz kierunek zmiany (sekcja 4).
4. **Zastosuj nastawy:** `params set <field> <value>` w **DISARMED** (SI-6: w ARMED tylko staged,
   aplikuje się po rozbrojeniu). Auto-zapis do NVS; przetrwa reflash i reboot.
5. **Reflash off / kolejne wypłynięcie:** powtórz z nowymi nastawami. Iteruj do stabilnego holdu.

---

## 2. Konsola USB (koegzystencja z logami)

Primary console = **USB Serial/JTAG** (lewy USB-C — ten sam port co flash/monitor). REPL i CSV
docierają na port operatora; `ESP_LOG` dzieli ten port (standardowy wzorzec esp_console —
prompt jest odrysowywany po linii logu). Dump to **surowy `printf`** bez prefiksu logu, więc parser
filtruje wiersze pasujące do schematu CSV (patrz sekcja 3). **Dumpuj, gdy pętla jest cicha**
(DISARMED / off-water), żeby ograniczyć przeplot z logami.

Komendy:
```
spotlog dump                      # zrzut regionu jako zdenormalizowany CSV
params get                        # aktywne nastawy spot_lock_*
params set <field> <value>        # strojenie (DISARMED = live, ARMED = staged)
  fields: deadband_m max_throttle_pct throttle_gain servo_gain
```

---

## 3. Format CSV (kontrakt parsera)

Naglówek == kolejność kolumn wiersza (stały kontrakt; zmiana = zmiana schematu):

```
session_id,t_ms,substate,err_m,bearing_deg10,heading_deg10,servo_us,esc_us,ch1_us,ch2_us,
lat_e7,lon_e7,sats,speed_cms,gps_fix,imu_ok,target_lat_e7,target_lon_e7,
deadband_m,max_throttle_pct,throttle_gain,servo_gain
```

CSV jest **zdenormalizowany i płaski**: KAŻDY wiersz-próbka niesie `session_id` + cel sesji
(`target_lat_e7`/`target_lon_e7`) + aktywne nastawy sesji (`deadband_m`, `max_throttle_pct`,
`throttle_gain`, `servo_gain`). Grupowanie po `session_id` daje przebieg jednej sesji bez joinu.

| Kolumna | Jednostka / znaczenie |
|---|---|
| `session_id` | monotoniczny id sesji (nagłówek na starcie sesji) |
| `t_ms` | ms od startu sesji |
| `substate` | 0=off, 1=active, 2=paused |
| `err_m` | błąd pozycji do celu, metry |
| `bearing_deg10` | namiar na cel, stopnie×10 |
| `heading_deg10` | kurs kajaka (kompas), stopnie×10 |
| `servo_us`, `esc_us` | komendy wyjściowe (po clampie), µs |
| `ch1_us`, `ch2_us` | surowe drążki steer/throttle, µs |
| `lat_e7`, `lon_e7` | pozycja, stopnie×1e7 |
| `sats`, `speed_cms` | satelity fixu; prędkość, cm/s |
| `gps_fix`, `imu_ok` | 0/1 |
| `target_*_e7` | cel holdu (pozycja na starcie sesji) |
| `deadband_m`, `max_throttle_pct`, `throttle_gain`, `servo_gain` | aktywne nastawy sesji |

---

## 4. Metryki → kierunek zmiany nastaw

Licz na wierszach jednej sesji z `substate ∈ {1,2}` (active/paused). Zakresy walidatora:
`deadband_m` 0–100, `max_throttle_pct` 0–100, gainy 0–1000.

### Polowanie (hunting) — oscylacja wokół celu
Objaw: `err_m` oscyluje (nie zbiega), `servo_us`/`esc_us` szarpią w przeciwne strony; wysokie odchylenie
`err_m` przy niskiej średniej. Metryka: liczba zmian znaku `d(err_m)/dt` lub wariancja `err_m` w oknie
ustabilizowanym.
- **Zmniejsz `throttle_gain`** (główny) — mniejsza reakcja na metr błędu.
- **Zwiększ `deadband_m`** — martwa strefa tłumi mikro-korekty przy celu.
- Ewentualnie **zmniejsz `servo_gain`** jeśli szarpie kierunkiem.

### Przeregulowanie (overshoot) — mija cel i wraca
Objaw: `err_m` spada do ~0, po czym rośnie po drugiej stronie (namiar `bearing_deg10` obraca ~180°).
- **Zmniejsz `throttle_gain`** i/lub **`max_throttle_pct`** — mniej pędu przy dobijaniu.
- **Zwiększ `deadband_m`** — wcześniej „odpuszcza" gaz.

### „Donut" — krążenie wokół celu zamiast dobijania
Objaw: `err_m` ~stały (kilka–kilkanaście m), `bearing_deg10` monotonicznie się obraca, kajak nie zbliża się.
Zwykle punkt za rufą (|różnica bearing−heading| > 60° → bramka łagodnego zawrotu).
- **Zmniejsz `servo_gain`** — łagodniejszy zawrót, mniej „obiegania".
- **Zmniejsz `throttle_gain`** — nie napędza obiegu.
- Priorytet stabilności: raczej wolniejsze dobijanie niż agresywny zawrót.

### Zbyt słaby hold — dryf, nie dobija
Objaw: `err_m` rośnie/utrzymuje się wysoko, `esc_us` blisko neutralu mimo dużego błędu.
- **Zwiększ `throttle_gain`** (mały krok) — silniejsza reakcja.
- **Zwiększ `max_throttle_pct`** jeśli gaz sięga sufitu (`esc_us` na limicie) a błąd duży.
- **Zmniejsz `deadband_m`** jeśli zbyt szeroka martwa strefa zostawia dryf.

### Utrata fixu/heading w trakcie
Objaw: przejścia `substate` 1↔2, `gps_fix`/`imu_ok` = 0. To zachowanie oczekiwane (PAUSED = stop
neutralny, ten sam cel po powrocie danych) — nie strojenie regulatora, lecz jakość sygnału (antena,
świeżość ≤1,5 s). Nie zmieniaj gainów na podstawie sesji z dużym udziałem PAUSED.

---

## 5. Zasady strojenia

- **Jedna nastawa na iterację**, mały krok, weryfikacja na wodzie. Wielokrotne zmiany naraz
  uniemożliwiają przypisanie efektu.
- **Przy niepewności → stabilność:** niższe gainy, większy deadband. Defaults są celowo łagodne
  (`deadband_m=3`, `max_throttle_pct=35`, `throttle_gain=30`, `servo_gain=20`).
- **`params get` po każdej zmianie** — potwierdź, że nastawa jest aktywna (i że nie była tylko staged
  bo ARMED).
- Strojenie jest **terenowe**: host-testy pokrywają tylko czystą logikę (kodek, ring, CSV, parsowanie
  argumentów), nie zachowanie na wodzie.
