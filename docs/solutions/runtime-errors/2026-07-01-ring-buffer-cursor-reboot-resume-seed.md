---
title: "Ring buffer na flashu z licznikiem seq/session w RAM — zasiej cursor skanem regionu na init"
date: 2026-07-01
category: runtime-errors
severity: high
stack:
  - ESP-IDF
  - C
tags:
  - ring-buffer
  - flash
  - reboot
  - persistence
  - blackbox
  - host-test
status: verified
last_verified: 2026-07-01
---

# Reboot resetuje cursor/session ringu do 0 — zasiej ze skanu flasha

## Symptomy

- Po reboocie (lub brownout między wypływami) nowa sesja logu używa ponownie
  `session_seq=1` i nadpisuje ring od slotu 0, kolidując z wciąż ważnymi
  rekordami poprzedniej sesji.
- `read_all` streamuje dwa różne wypływy pod jednym `session_seq`, przeplecione
  w złej kolejności — dane wyglądają na spójne, ale są zafałszowane.
- Objaw niewidoczny w jednej sesji; ujawnia się dopiero po restarcie.

## Root Cause

Write cursor i licznik `session_seq` żyją TYLKO w RAM. Reboot restartuje oba do
zera, więc rekordy po restarcie lądują na slocie 0 z tym samym id, co poprzedni
wypływ — kolizja identyfikatorów i przeplot danych. Persistent ring wymaga
persistent (albo odtwarzalnego) stanu kursora.

## Rozwiązanie

Na init HAL skanuje region fizycznie (slot 0 w górę), klasyfikuje każdy slot
czystym kodekiem rekordu, a czysta funkcja składa wynik do ziarna wznowienia:
`session_seq = max(header seq)`, `cursor = ostatni ważny slot + 1`. Fold jest
strumieniowy (jeden slot naraz) — regionu nie da się zbuforować jako struktur.

```c
void blackbox_resume_scan_slot(blackbox_resume_scan *scan, uint32_t slot,
                               bool is_valid, bool is_header,
                               uint32_t session_seq)
{
    if (!is_valid) return;

    /* A valid record marks how far the write head reached; the cursor resumes
     * right after the highest such slot so new writes never collide with it. */
    scan->last_valid_slot = slot;
    scan->has_valid = true;

    if (is_header && (!scan->has_header ||
                      session_seq > scan->highest_session_seq)) {
        scan->highest_session_seq = session_seq;
        scan->has_header = true;
    }
}

blackbox_resume_seed blackbox_resume_decide(const blackbox_resume_scan *scan)
{
    blackbox_resume_seed seed;
    /* Fresh region: ids at 0 (first START yields 1), cursor at 0. */
    seed.session_seq = scan->has_header ? scan->highest_session_seq : 0U;
    seed.cursor      = scan->has_valid  ? scan->last_valid_slot + 1U : 0U;
    return seed;
}
```

Funkcja jest czysta (bez `esp_*`/`driver/*`), więc host-testowalna bez sprzętu
(`test_blackbox_resume.c`, 162 linie). HAL robi surowe odczyty i klasyfikację;
logika ziarna żyje osobno.

## Zapobieganie

- Każdy stan RAM napędzający persistent ring (cursor, licznik sesji) MUSI być
  odtwarzalny skanem regionu na init — inaczej reboot cicho resetuje do slotu 0.
- Test z mocą wyroczni: mutacja decyzji do stałego `{0,0}` MUSI wywalić testy.
  Podawaj wejście z niepustym regionem (nie pusty), by test faktycznie
  weryfikował wznowienie, a nie tożsamość.
- Trzymaj partycję data (spotlog) NA KOŃCU `partitions.csv` — nie w środku —
  aby dopisanie nie przesunęło offsetów istniejących partycji; dane i
  kalibracja przeżywają reflash layoutu.

## Powiązane

- docs/solutions/runtime-errors/2026-06-17-wrap-safe-recency-counter-domain.md
  (ten sam ring — wrap-safe indeksowanie; ten wpis dotyczy seedowania po reboocie)
- docs/solutions/testing-issues/2026-06-17-esp-idf-host-test-harness-pure-hal-separation.md
  (Pure⊥HAL — czysta funkcja wznowienia zza HAL)
- docs/solutions/testing-issues/2026-06-17-hard-clamp-test-oracle-power.md
  (moc wyroczni — test wznowienia mutowany do {0,0} musi FAILować)

## Kontekst

Zadanie spot-lock-blackbox, `fix(blackbox): wznowienie cursora/session_seq po
reboocie` (poprawki po review fazy 2). Firmware ESP32-S3, partycja `spotlog`
1 MiB, ring sektorowy 4 KB / rekord 64 B / 16384 slotów.
