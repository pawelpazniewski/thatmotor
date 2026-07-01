---
title: "Flash I/O (erase/write) trzymaj w tasku tła, nigdy w pętli sterującej RT 50 Hz"
date: 2026-07-01
category: performance-issues
severity: high
stack:
  - ESP-IDF
  - FreeRTOS
  - C
tags:
  - real-time
  - jitter
  - flash
  - freertos
  - blackbox
  - task-priority
status: verified
last_verified: 2026-07-01
---

# Flash I/O w pętli RT = jitter; przenieś do tasku tła

## Symptomy

- Pętla sterująca real-time (50 Hz, budżet 20 ms) gubi deadline, gdy w tym
  samym tasku wykonuje się `esp_partition_erase_range` / `esp_partition_write`.
- Kasowanie sektora flash (4 KB) blokuje na dziesiątki ms — pojedynczy erase
  potrafi przekroczyć cały budżet cyklu, dając widoczny jitter serwa/ESC.
- Objaw jest przerywany (tylko przy zapisie rekordu/rotacji sektora), więc łatwo
  go przeoczyć w krótkim teście.

## Root Cause

Operacje flash (erase/write) na wewnętrznym SPI flash są synchroniczne i
długie (kasowanie sektora blokuje bus i może wstrzymać wykonanie). Umieszczone
w tasku pętli sterującej RT wprowadzają nieograniczony jitter do domeny, która
ma twardy deadline. Blackbox/log jest funkcją diagnostyczną — jego opóźnienie
jest nieszkodliwe, jitter pętli sterującej nie.

## Rozwiązanie

Cała logika I/O flash żyje w osobnym tasku tła niskiego priorytetu; pętla RT
tylko publikuje snapshot telemetrii (lock-free/best-effort), a task tła robi
nieblokujący peek i zapisuje.

```c
/* Low priority so the recorder can never preempt or stall the 50 Hz loop;
 * the blackbox is diagnostic and entirely outside failsafe. */
#define BLACKBOX_TASK_STACK 3072
#define BLACKBOX_TASK_PRIO  2

/* ~2 Hz peek of the telemetry snapshot. Losing a sample is acceptable; loop
 * jitter is not, so all flash I/O lives here, never in the control task. */
#define BLACKBOX_SAMPLE_PERIOD_MS 500U

/* Best-effort: a lost record must not take down the observer. */
static void append_or_warn(blackbox_status status, const char *what)
{
    if (status != BLACKBOX_OK) {
        ESP_LOGW(TAG, "%s append failed (status %d)", what, status);
    }
}
```

Zasady:
- Task tła prio niższy niż pętla RT (tu prio 2, pętla wyżej) — nigdy nie
  wywłaszcza sterowania.
- Pętla RT publikuje snapshot; task tła robi `control_loop_get_snapshot`
  (peek, nieblokujący). Utrata próbki logu = akceptowalna.
- Błąd flash = best-effort log-and-continue (`ESP_LOGW`), nigdy crash ani
  retry blokujący.
- Task tła jest czystym obserwatorem: nie dotyka failsafe ani wejść maszyny
  stanów.

## Komendy diagnostyczne

```bash
# Znajdź wywołania flash I/O i sprawdź, w którym tasku się wykonują.
rg "esp_partition_(erase|write)" components/
# Zweryfikuj priorytety tasków — task I/O musi być < pętla RT.
rg "xTaskCreate|_PRIO" components/ main/
```

## Zapobieganie

- Nigdy nie wywołuj erase/write flash (ani innego blokującego I/O) w tasku
  pętli RT z twardym deadlinem.
- Model: pętla RT publikuje stan → task tła niskiego prio konsumuje i zapisuje.
- Traktuj utratę próbki diagnostycznej jako akceptowalną; jitter pętli nie.

## Powiązane

- docs/solutions/runtime-errors/2026-06-29-failsafe-precedence-sensor-override-in-control-loop.md
  (ta sama zasada rozdziału: obserwator/diagnostyka poza ścieżką failsafe)
- docs/solutions/runtime-errors/2026-07-01-ring-buffer-cursor-reboot-resume-seed.md
  (ten sam blackbox — stan ringu na flashu)

## Kontekst

Zadanie spot-lock-blackbox, Unit 3 (`feat(blackbox): adapter flash HAL
esp_partition + recorder task tła`). Firmware ESP32-S3, pętla sterująca 50 Hz,
recorder tła ~2 Hz wzorowany na tasku `gps_reader`. Luka wiedzy — brak
wcześniejszego wpisu o umiejscowieniu I/O flash względem pętli RT.
