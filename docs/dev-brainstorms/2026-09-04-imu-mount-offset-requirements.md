---
date: 2026-09-04
topic: imu-mount-offset
---

# Odbicie IMU (yaw→kompas): montaż vs hardkodowany offset

## Problem

Commit `d91b517` naprawił lustrzane odwrócenie kierunku obrotu kompasu BNO085
(`yaw_to_compass_heading_deg10`, odbicie wokół osi 45°). Ta poprawka
skorygowała kierunek obrotu, ale ujawniła, że sam kierunek patrzenia (heading
przy stałym kursie) jest teraz przesunięty o kilka stopni. W niescommitowanych
zmianach (obecny working tree) to przesunięcie zaadresowano podnosząc
`COMPASS_MIRROR_AXIS_DEG10` z 450 (45.0°, teoretyczne) na 504 (50.4°,
wyznaczone empirycznie: telemetria 61.8° vs realny kompas 55.5°, deficyt
6.3°), z komentarzem tłumaczącym to jako korektę fizycznego przekrzywienia
montażu IMU względem dziobu.

Użytkownik zgłasza, że to założenie jest błędne: IMU jest zamontowane
kanonicznie — strzałka X do przodu (dziób), a strona z diagramem osi X/Y/Z
(tył płytki, bez widocznego czipu) do góry (w stronę nieba). Zadanie: (1)
zweryfikować, czy montaż faktycznie jest poprawny, (2) ustalić, czy i jak
zmienić hardkodowaną stałą offsetu.

`heading_deg10` nie jest tylko telemetrią diagnostyczną — zasila
`spot_lock.c` (`heading_error_deg10`) i realnie wpływa na sterowanie w trybie
spot-lock/goto (poza failsafe, ale nie poza zachowaniem łodzi).

## Co już ustalono

- **Datasheet BNO08X (CEVA, rev 1.17, rozdz. 4 "BNO08X Orientation")**:
  domyślne mapowanie (bez FRS remap 0x2D3E — firmware go nie ustawia) to
  fizyczne X=East, Y=North, Z=Up → kwaternion tożsamościowy. To potwierdza,
  że matematyczne założenie w kodzie (`quat_to_yaw_deg10` mierzy CCW od X,
  konwencja ENU) jest poprawne teoretycznie, o ile fizyczne osie czujnika
  faktycznie pokrywają się z tym, co widać na sitodruku płytki.
- Użytkownik potwierdził: strzałka X → dziób, strona z diagramem osi (tył,
  bez czipu) → góra/niebo, czip → w stronę kadłuba/pokładu.
- **Otwarta niepewność**: diagram osi na tej konkretnej płytce (klon
  GY-BNO08X) jest wydrukowany na stronie BEZ czipu — czyli na stronie
  przeciwnej niż ta, względem której datasheet definiuje oś Z (górna,
  oznaczona strona samego czipu). Nie da się ze zdjęcia jednoznacznie
  rozstrzygnąć, czy sitodruk producenta płytki poprawnie odwzorowuje
  rzeczywiste osie czujnika, czy czip jest fizycznie "do góry nogami"
  względem tego diagramu (czyli Z realnie skierowane w dół, ku kadłubowi) —
  to byłby prawdziwy fizyczny błąd montażu, nie kwestia konwencji
  matematycznej CCW/CW.
- Firmware obecnie NIE loguje surowego kwaternionu (i, j, k, real) — tylko
  przetworzony `heading_deg10`/`raw_yaw_deg10` (`bno085.c`, `usb_console.c`).
  Nie da się tym odróżnić "poziomo, Z w górę" od "do góry nogami", bo atan2
  zawsze zwróci jakąś wartość niezależnie od realnego przechylenia.
- **R2 ROZSTRZYGNIĘTE (2026-09-04, na sprzęcie)**: dodano diagnostyczny log
  `ESP_LOGI` surowego kwaternionu w `bno085.c` (R1, wdrożone), sflashowano,
  odczytano przez `idf.py monitor`/surowy port szeregowy z płytką leżącą
  poziomo, stroną z diagramem osi do góry (potwierdzone przez użytkownika).
  Wynik, odtworzony w dwóch niezależnych przechwyceniach ~3 min od siebie:
  `i≈-0.93, j≈-0.36, k≈-0.015, w≈0.02` (Q14, znormalizowane). i i j
  dominują, k i w prawie zerowe — to sygnatura obrotu ~178° wokół osi
  leżącej blisko płaszczyzny poziomej, NIE obrotu czysto wokół Z. Wniosek:
  **czujnik ma fizycznie odwróconą oś Z** względem referencji z datasheetu
  (Figure 4-1) — strona z diagramem osi na tej konkretnej płytce nie
  odpowiada górnej/oznaczonej stronie samego czipu, którą datasheet przyjmuje
  jako Z w górę. Strzałka X wciąż wskazuje dziób poprawnie (bez offsetu
  rotacyjnego w płaszczyźnie poziomej) — problem jest tylko w osi Z.
- **Remontaż NIE jest teraz opcją**: IMU jest zainstalowane w obudowie
  silnika z podłączonymi przewodami; użytkownik nie będzie go teraz
  odkręcał/odwracał tylko żeby zweryfikować hipotezę. Korekta zostaje więc
  po stronie software'u.
- Formuła `quat_to_yaw_deg10` to ogólna ekstrakcja yaw w konwencji ZYX Euler
  — matematycznie poprawna niezależnie od roll/pitch (poza gimbal lockiem
  przy pitch=±90°, którego tu nie ma).
- **POPRAWKA (dalsza część tej samej sesji)**: rozbicie obu zalogowanych
  kwaternionów na roll/pitch/yaw dało roll≈-178° do -179°, pitch≈-2.4° do
  -2.5°, yaw≈40° do 44° — roll i pitch STABILNE mimo że płytka leżała
  nieruchomo (yaw dryfował o ~4°, spójne z niedokończoną kalibracją
  magnetometru, nie z ruchem). Kluczowa własność dekompozycji ZYX: kierunek
  osi X w świecie zależy TYLKO od yaw i pitch, NIGDY od roll (obrót wokół
  własnej osi X nie rusza tej osi). Skoro roll≈-178° (blisko czystego
  obrotu wokół X), a strzałka X i tak wskazuje dziób z dokładnością ~±1° —
  ten obrót nie wymaga ŻADNEJ korekty w `yaw_math`. Wcześniejsze
  utrzymanie 504 ("no ale coś tam trzeba skompensować") było błędem: nie
  ma już żadnej fizycznej podstawy do przesunięcia poza teoretyczne 45°.
  **Stałą cofnięto do `COMPASS_MIRROR_AXIS_DEG10 = 450`** (45.0°).
  Zaktualizowano też `test_quat_to_yaw.c` (4 testy, oracle-rewrite
  potwierdzone: failują przeciw staremu 504 przed przywróceniem 450) i
  komentarz w `quat_to_yaw.c`, żeby opisywał tę pełną historię (dlaczego
  504 było błędem, nie tylko że jest błędem).
- Deficyt 6.3° z jednopunktowej kalibracji na wodzie (2026-09-03) NIE jest
  już wyjaśniany montażem w żadnej formie. Najbardziej prawdopodobne
  przyczyny: (a) niezbiegnięta kalibracja magnetometru w momencie pomiaru
  (obserwowany dryf yaw ~4°/3 min w bezruchu), (b) dewiacja magnetyczna od
  silnika/baterii blisko IMU — a jeśli to (b), to poprawka dopasowana do
  JEDNEGO kursu byłaby błędna na innych kursach, więc NIE powinna być
  zaszyta jako płaska stała w tym miejscu niezależnie od przyczyny.

## Wymagania

- R1. ✅ ZROBIONE. Dodano jednorazowy/diagnostyczny log surowych składowych
  kwaternionu (i, j, k, real) z Rotation Vector w `bno085.c` (`ESP_LOGI`,
  throttlowany do ~1 Hz — `ESP_LOGD` odpadał, bo `CONFIG_LOG_MAXIMUM_LEVEL`
  w `sdkconfig` to INFO, więc debug jest wycinany na etapie kompilacji).
- R2. ✅ ROZSTRZYGNIĘTE. Test na sprzęcie (płytka poziomo, jak na kajaku)
  wykazał i≈-0.93, j≈-0.36, k≈-0.015, w≈0.02 (odtworzone w dwóch
  przechwyceniach) → fizyczna oś Z jest odwrócona względem referencji z
  datasheetu. Ponieważ remontaż nie jest teraz opcją (IMU zamontowane w
  silniku z podłączonymi przewodami), korekta zostaje w software'ie —
  dokładnie tak jak już jest zaimplementowana (stały offset w
  `yaw_to_compass_heading_deg10`), tylko z poprawną dokumentacją przyczyny.
- R3. ✅ ZROBIONE. `COMPASS_MIRROR_AXIS_DEG10` cofnięte z 504 (50.4°) do 450
  (45.0°, teoretyczne). Rozbicie zalogowanego kwaternionu na roll/pitch/yaw
  (ZYX Euler) pokazało, że ~178° roll (Z-inversion) jest wokół osi bliskiej
  X, więc matematycznie nie wpływa na `yaw_math` (yaw/pitch-only formuła).
  Skoro montaż jest czysty poza tym roll-invariant flipem, nie ma już
  podstawy do trzymania 504. Komentarz w `quat_to_yaw.c` opisuje pełne
  rozumowanie (łącznie z tym, dlaczego 504 było błędem).
- R4. ✅ ZROBIONE. `test_quat_to_yaw.c` zaktualizowany na 450/900 (oracle-
  rewrite, zweryfikowane: 4 testy failują przeciw staremu 504 przed
  przywróceniem 450, potwierdzając że to przesunięcie wyroczni, nie jej
  osłabienie). 498/498 testów hosta zielone.

## Kryteria sukcesu

- `COMPASS_MIRROR_AXIS_DEG10` i jego komentarz odzwierciedlają zweryfikowaną
  fizyczną orientację czujnika, nie założenie przyjęte bez testu.
- Telemetria `heading_deg10` zgadza się z realnym kompasem w granicach
  rozsądnego błędu pomiaru (nie 6°+) przy potwierdzonej poprawnej orientacji
  Z, LUB — jeśli źródłem deficytu są zakłócenia magnetyczne/deklinacja — ta
  przyczyna jest udokumentowana zamiast domyślnie przypisana montażowi.

## Granice scope'u

- Nie badamy teraz źródła deklinacji magnetycznej / interferencji od
  silnika jako osobnego zadania — tylko jako możliwe wyjaśnienie deficytu
  6.3°, jeśli R2 wykluczy błąd fizycznego montażu. Osobne dochodzenie w
  razie potrzeby.
- Diagnostyczny log i/j/k (R1) jest tymczasowy — nie zostaje w kodzie jako
  stała funkcjonalność telemetrii, chyba że okaże się przydatny na stałe
  (osobna decyzja).

## Otwarte pytania

### Do rozwiązania przed planowaniem
(brak — R2 rozstrzygnięte na sprzęcie, R3 nie wymaga już decyzji
użytkownika żeby ruszyć dalej)

### Odroczone do planowania
- [Dotyczy R3][Wymaga researchu] Deficyt 6.3° z pomiaru 2026-09-03 nie jest
  już wyjaśniany montażem. Przy najbliższym wyjeździe na wodę: (1) poczekać
  aż `imu_calib` w pełni się zbiegnie I ustabilizuje (nie tylko pokaże 3/3)
  przed porównaniem z realnym kompasem, (2) jeśli deficyt się utrzyma,
  sprawdzić czy zależy od kursu (typowe dla dewiacji magnetycznej silnik/
  bateria) porównując na kilku różnych kursach, nie jednym — jeśli zależy
  od kursu, NIE nadaje się do skompensowania płaską stałą w tym miejscu.
- [Dotyczy R3][Techniczne] Jeśli kiedyś pojawi się okazja przy przebudowie
  okablowania silnika, żeby fizycznie odwrócić IMU (Z naprawdę w górę) —
  to nie zmieni już nic w `quat_to_yaw_deg10`/`yaw_to_compass_heading_deg10`
  (formuła jest roll-invariant), ale warto to wtedy zanotować jako
  potwierdzenie. Nieaktualne, dopóki remontaż nie jest planowany.

## Następne kroki
Diagnostyka zakończona i wdrożona (R1-R4). Brak blokerów dla dalszej pracy —
`COMPASS_MIRROR_AXIS_DEG10` wraca do teoretycznych 450 (45.0°); deficyt
6.3° z jednopunktowej kalibracji 2026-09-03 to osobny, nierozwiązany trop
(kalibracja magnetometru / dewiacja magnetyczna) do zbadania przy
najbliższym wyjeździe na wodę, nie wymaga osobnego planowania technicznego.
