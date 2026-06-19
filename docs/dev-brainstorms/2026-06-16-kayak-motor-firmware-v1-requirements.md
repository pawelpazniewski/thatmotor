---
date: 2026-06-16
topic: kayak-motor-firmware-v1
status: superseded
superseded_by: docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md
---

> **SUPERSEDED**
> This document has been superseded by:
> `docs/requirements/2026-06-16-kayak-motor-firmware-v1-requirements-v2.md`

# Firmware V1 — silnik elektryczny do kajaka

## Problem
Surowy sygnał RC sterujący serwem (skręt) i ESC (gaz) silnika trollingowego jest
zbyt gwałtowny i nie ma żadnego zabezpieczenia przy utracie sygnału na wodzie.
ESP32 ma wejść między odbiornik RC a aktuatory, żeby: wygładzić ruch serwa,
zrobić soft-start/soft-stop gazu, dodać deadband i failsafe, oraz dać prosty
panel WWW do podglądu live i strojenia parametrów. Użytkownik = budowniczy/operator
(jedna osoba), środowisko = otwarta woda. Priorytetem jest bezpieczeństwo
i przewidywalność, a dopiero potem wygoda.

## Wymagania

- R1. **Passthrough RC przez ESP32.** ESP32 czyta CH1 (skręt, GPIO34) i CH2 (gaz,
  GPIO35) z odbiornika i generuje sygnał serwa (GPIO18) oraz sygnał ESC (GPIO19).
  Bez ESP32 w torze nie ma sterowania — cały tor przechodzi przez firmware.

- R2. **Model gazu dwukierunkowy ze środkiem = stop.** Drążek CH2 w środku =
  silnik stoi, w górę = przód, w dół = tył. WP880 traktowany jako bidirectional
  brushed ESC. Deadband wokół środka (R5).

- R3. **Spowolnienie ruchu serwa (slew limiting).** Wyjście serwa zmienia się
  z ograniczoną prędkością — duży, szybki ruch drążka CH1 przekłada się na
  płynny, wolniejszy ruch serwa. Prędkość konfigurowalna (R10).

- R4. **Soft-start / soft-stop gazu (rampy ESC).** Zmiany gazu przechodzą przez
  rampy narastania i opadania (osobne dla ↑ i ↓), bez szarpnięć napędu.
  Konfigurowalne (R10).

- R5. **Deadband wokół neutralu gazu.** Małe wychylenia drążka wokół środka nie
  dają wyjścia na ESC (silnik stoi). Szerokość konfigurowalna (R10).

- R6. **Failsafe przy utracie sygnału RC.** Po wykryciu utraty sygnału (timeout
  konfigurowalny, R10): gaz przechodzi soft-stopem do neutralu, serwo TRZYMA
  ostatnią pozycję. Kajak zwalnia i dryfuje z ostatnim kursem steru.

- R7. **Arming z wymogiem neutralu.** Napęd jest rozbrojony (disarmed) po
  włączeniu zasilania oraz po wejściu w failsafe. Uzbrojenie (arm) następuje
  dopiero gdy gaz wróci do neutralu (±deadband). W stanie disarmed wyjście ESC
  pozostaje w neutralu niezależnie od drążka. Chroni przed skokiem mocy przy
  starcie i przy odzyskaniu sygnału.

- R8. **Sygnalizacja stanu: LED + panel.** Wbudowana dioda ESP32 (GPIO2)
  pokazuje stan wzorami migania (uzbrojony / rozbrojony / failsafe — konkretne
  wzory do ustalenia w planowaniu). Panel WWW pokazuje stan szczegółowo.

- R9. **Panel WWW po WiFi AP — podgląd live.** ESP32 stawia własny Access Point
  i serwuje panel pokazujący na żywo: wejścia RC (CH1, CH2, CH4), wyjście do
  serwa, wyjście do ESC, oraz status RC / failsafe / arm. AP aktywny zawsze,
  od startu urządzenia.

- R10. **Edycja parametrów z UI — tylko przy disarm.** Panel pozwala zmieniać
  parametry, ale ZAPIS jest możliwy tylko gdy napęd rozbrojony. Podgląd live
  działa zawsze. Parametry tunowalne:
  - prędkość serwa (slew rate)
  - rampa ESC narastanie (soft-start)
  - rampa ESC opadanie (soft-stop)
  - szerokość deadbandu
  - timeout failsafe
  - limit mocy gazu (max % w obie strony)
  - endpointy serwa (min/max kąt)
  - kalibracja wejścia RC (min/mid/max na kanał)
  - flagi reverse (serwo i/lub gaz)

- R11. **Trwały zapis ustawień.** Parametry przeżywają wyłączenie zasilania
  (zapis w pamięci nieulotnej ESP32). Po restarcie firmware używa zapisanych
  wartości.

- R12. **CH4 — czytaj i pokaż, bez akcji.** ESP32 czyta CH4 (GPIO32) i pokazuje
  surową wartość w panelu, ale nie steruje niczym. Weryfikuje okablowanie
  i przygotowuje grunt pod tryby w V2.

## Kryteria sukcesu
- Serwo i ESC podążają za drążkami przez ESP32 (R1) — bez ESP32 brak sterowania.
- Ruch serwa jest widocznie wolniejszy/płynniejszy niż surowy RC (R3).
- Rampy gazu obserwowalne, brak szarpnięć przy starcie i zatrzymaniu (R4).
- Drobne ruchy drążka wokół środka nie ruszają silnika (R5).
- Odłączenie/zagłuszenie RC → gaz soft-stopuje do neutralu, serwo trzyma,
  LED sygnalizuje failsafe (R6, R8).
- Po starcie i po failsafe napęd nie rusza dopóki gaz nie wróci do neutralu (R7).
- Panel WWW osiągalny po AP, pokazuje wartości live, edycja zablokowana gdy
  uzbrojony, zmiany przeżywają restart (R9, R10, R11).
- Ustawienia przeżywają cykl zasilania (R11).

## Granice scope'u
- Brak logiki trybów na CH4 — tylko odczyt/wyświetlanie (tryby = V2).
- Brak chmury / internetu — wyłącznie lokalny AP.
- Brak autoryzacji/multi-user na panelu — zaufany, pojedynczy AP.
- Brak GPS / autopilota / nawigacji / trzymania kursu.
- Brak logowania/zapisu telemetrii na SD lub w chmurze.
- Brak OTA (aktualizacja firmware przez WWW) — poza V1.

## Kluczowe decyzje
- Gaz dwukierunkowy, środek = stop: naturalne manewrowanie kajakiem, deadband
  wokół środka, failsafe celuje w neutral.
- Failsafe = soft-stop gazu + serwo trzyma: najłagodniejsze dla brushed ESC
  i mechaniki; przewidywalny dryf prosto zamiast szarpnięcia sterem.
- Arm/disarm z wymogiem neutralu: eliminuje skok mocy przy starcie i odzyskaniu
  sygnału — standard RC/dron.
- Edycja tylko przy disarm: chroni przed zmianą deadbandu/rampy w trakcie ruchu;
  tuning robiony na postoju.
- LED na płytce + panel: stan widoczny bez telefonu na wodzie, niemal zerowy koszt.

## Zależności / Założenia
- Mapowanie pinów (z opisu sprzętu, traktowane jako ustalone wejście):
  GPIO34←CH1, GPIO35←CH2, GPIO32←CH4, GPIO18→serwo, GPIO19→ESC, wspólna masa
  ESP32/odbiornik/ESC.
- GPIO34/35/32 to piny wejściowe-tylko ESP32 (bez wewn. pull-up) — OK dla
  odczytu RC; sygnał RC z odbiornika to standardowy PWM ~50 Hz.
- Zasilanie: buck 5V→ESP32; BEC 6V z WP880→odbiornik i serwo; LiFePO4 12V 100Ah.
- Serwo DS3240 (40 kg, 270°) — pełny zakres mechaniczny większy niż potrzebny
  ster, stąd endpointy serwa w R10.
- Jeden operator, środowisko otwartej wody, brak wymagań certyfikacyjnych.

## Otwarte pytania

### Do rozwiązania przed planowaniem
- (brak — wszystkie decyzje produktowe rozstrzygnięte)

### Odroczone do planowania
- [Dotyczy R6][Techniczne] Metoda detekcji utraty sygnału RC: brak impulsu vs
  impuls poza zakresem vs przekroczenie timeoutu — i jak wykrywać „zamrożony"
  ostatni impuls. Timeout sam jest parametrem (R10), ale mechanizm detekcji = planowanie.
- [Dotyczy R1, R7][Techniczne][Bezpieczeństwo] Stan pinów GPIO18/19 podczas
  bootu ESP32 — zapewnić, że linia ESC nie dostaje przypadkowego sygnału ruchu
  zanim firmware się zainicjalizuje (napęd ma startować w disarmed/neutral).
- [Dotyczy R1, R3, R4][Techniczne] Sposób odczytu PWM z odbiornika (interrupty /
  RMT / pulseIn) i generacji wyjścia (LEDC) przy jednoczesnym serwowaniu WWW
  bez zaburzania pętli sterującej ~50 Hz.
- [Dotyczy R9][Techniczne] Transport telemetrii live (WebSocket vs polling)
  i akceptowalna częstotliwość/latencja odświeżania panelu.
- [Dotyczy R9, R10][Techniczne] Stos WWW na ESP32 (np. async web server)
  i format wymiany parametrów.
- [Dotyczy R11][Techniczne] Mechanizm trwałego zapisu (NVS/Preferences vs
  system plików) i strategia wersjonowania/migracji schematu ustawień.
- [Dotyczy R10][Techniczne] Walidacja zakresów parametrów na granicy zapisu
  (np. endpointy serwa, limit mocy) zanim trafią do pamięci i na wyjścia.

## Następne kroki
→ `/dev-plan` do planowania technicznego implementacji
