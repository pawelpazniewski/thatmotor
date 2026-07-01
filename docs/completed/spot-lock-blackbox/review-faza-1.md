# Code Review — Faza 1 (Unit 1 + Unit 2)

**Zadanie:** spot-lock-blackbox
**Faza:** 1 — Fundamenty persystencji (partycja + czysty rdzeń)
**Commity:** `08af88a` (Unit 1 — partycja + geometria), `9c7e7eb` (Unit 2 — kodek + ring), `fd35386` (docs)
**Data review:** 2026-07-01

---

## Severity gate: ✅ CZYSTE — GOTOWE DO KONTYNUACJI

Brak problemów blokujących ani ważnych. Kod jest czysty, w pełni host-testowany,
zgodny z planem technicznym i regułami projektu. Faza 2 (Unit 3) może startować.

## Liczniki

| Severity | Liczba | Typy |
|----------|--------|------|
| 🔴 P1 (blocking) | 0 | — |
| 🟠 P2 (important) | 0 | — |
| 🟡 P3 (nit) | 1 | KOD |

Plików sprawdzonych: 7 (3 nagłówki, 2 źródła, 2 testy) + `partitions.csv`.

---

## 1. WRAP-SAFETY — ✅ OK (zweryfikowane empirycznie)

Cała arytmetyka indeksu ringu jest unsigned/modular i poprawna na obu granicach:

- `blackbox_ring_slot` = `seq % capacity` — poprawny wrap pojemności.
- `blackbox_ring_offset` = `slot * record_size` — zawsze wewnątrz regionu (nadpisuje, nie dopisuje poza).
- `blackbox_ring_needs_erase` = `slot % records_per_sector == 0` — erase tylko na pierwszym rekordzie sektora; przeżywa wrap pojemności.
- `blackbox_ring_seq_after` = `(int32_t)(a - b) > 0` — kanoniczne wrap-safe serial-number comparison (RFC 1982-style); poprawne przez granicę 2^32.
- `blackbox_ring_oldest_seq` = `newest - (count - 1)` — modular subtraction, poprawnie zawija przez 0.

**Moc wyroczni testów potwierdzona empirycznie.** Skompilowałem naiwne warianty
(`slot=seq`, `a>b`, saturating subtraction) przeciw asercjom testów:
- `slot(11,8)` naiwnie = 11, test wymaga 3 → **FAIL** (test_overflow_seq_overwrites_oldest_slot).
- `seq_after(0, UINT32_MAX)` naiwnie = 0/false, test wymaga true → **FAIL** (test_seq_after_across_u32_wrap).
- `oldest(1,4)` saturating = 0, test wymaga UINT32_MAX-1 → **FAIL** (test_oldest_seq_wraps_through_zero).

Wszystkie 3 naiwne implementacje łamią asercje → testy mają realną moc wyroczni
(wejścia POZA granicą, zgodnie z regułą oracle-power). Zero tożsamościowych testów.

## 2. INTEGRALNOŚĆ KODEKA — ✅ OK

- **Magic `0xB10C`** odróżnia rekord od pustego flasha (`0xFFFF` niemożliwe) i od zerowego slotu (`0x0000`). Sprawdzenie `is_erased` (wszystkie 64 B = 0xFF) daje EMPTY zanim padnie magic.
- **CRC32** zakotwiczony known-answer testem: `"123456789"` → `0xCBF43926` (kanoniczne IEEE 802.3/zlib). Parametry (poly `0xEDB88320`, init/final `0xFFFFFFFF`, reflected) poprawne.
- **Taksonomia porażek kompletna i rozróżnialna:** EMPTY / ERR_MAGIC / ERR_CRC / ERR_SCHEMA / ERR_TYPE / ERR_LENGTH / ERR_ARG — czytnik odróżni granicę ringu (erased) od korupcji (CRC) bez mylenia z danymi.
- **Round-trip wierny** — testowany field-by-field (nie memcmp), łącznie z ujemnymi `lat/lon_e7` (signed two's-complement round-trip) i flagami `false` (nie tylko `true`).
- **Dyskryminacja typu** — `decode_sample` odrzuca slot nagłówka (ERR_TYPE), nie dekoduje po cichu.
- **Brak straddle przez granicę sektora** — rekord 64 B dzieli sektor 4096 B równo (64/sektor); zweryfikowane `test_real_geometry_constants_are_consistent` (`SECTOR_SIZE % RECORD_SIZE == 0`, ostatni slot = `REGION_SIZE - RECORD_SIZE`).
- **Payload mieści się w slocie:** sample = 31 B payload + 4 B framing = 35 B; header = 24 B + 4 B = 28 B; CRC na offsecie 60. Zero ryzyka nadpisania CRC / overrun (56 B dostępne dla payloadu).

## 3. PARTYCJA — ✅ OK (zweryfikowane `gen_esp32part.py`)

Wygenerowana i zweryfikowana tablica partycji:

```
nvs,      data, nvs,     0x9000,   24K
phy_init, data, phy,     0xf000,   4K
factory,  app,  factory, 0x10000,  1536K
appcfg,   data, nvs,     0x190000, 24K
spotlog,  data, 64,      0x196000, 1M
```

- Offsety `nvs`/`phy_init`/`factory` (jawne) **niezmienione**.
- `appcfg` offset `0x190000` **niezmieniony** (auto-liczony z niezmienionego `factory`) → kalibracja/appcfg przeżywa reflash layoutu.
- `spotlog` dopisana **na końcu** (`0x196000`, 1 MiB, subtype `0x40`). Tablica przechodzi walidację narzędzia IDF.
- Suma layoutu ~2,66 MB << 16 MB flasha — dużo zapasu.

## 4. PURE ⊥ HAL — ✅ OK

- `grep esp_`/`driver/` w `components/blackbox/`: trafienia **wyłącznie w komentarzach** (blackbox_region.h:16, blackbox_ring.h:19-20, blackbox_record.h:30). Zero includów `esp_*`/`driver/*` w kodzie.
- Nagłówki włączają tylko `<stdint.h>`/`<stdbool.h>`/`<stddef.h>`; `.c` dokłada tylko `<string.h>`. Rdzeń w pełni host-testowalny.
- Brak pułapki `*/` w komentarzach (żaden komentarz nie zamyka się przedwcześnie).
- `components/blackbox/` celowo bez `CMakeLists.txt` — IDF ignoruje katalog w Fazie 1, rejestracja do buildu w Unit 3. **Zgodne z planem, NIE finding.**

## 5. REGUŁY PROJEKTU — ✅ OK (1 nit)

- Rozmiar plików: wszystkie < 300 linii **poza** `blackbox_record.c` = **302 linie** (2 ponad próg) → 🟡 P3 niżej.
- Rozmiar funkcji: wszystkie znacznie < 50 linii; jeden poziom abstrakcji (framing/payload/public rozdzielone helperami).
- Named constants: magic numbers wyciągnięte (CRC params, offsety framingu, geometria, flagi). Zero magic numbers w kodzie produkcyjnym.
- Nesting ≤ 2, early-return na walidacji argumentów (fail-fast).
- Nazewnictwo spójne (`blackbox_*`, `is_erased`, `put_/get_` kursory).
- Testy: happy path + error case per funkcja; asercje ścisłe (EQUAL_HEX32/UINT32/INT32); zero osłabień, zero assertion-free.
- Guardy `capacity==0` / `records_per_sector==0` w ringu bronią przed div-by-zero UB mimo kontraktu „> 0" — akceptowalne (tanie, chroni przed UB), nie flaguję.

---

## Findings

### 🟡 P3 (nit) — KOD

- [ ] 🟡 [nit] **components/blackbox/src/blackbox_record.c:1-302** — plik ma 302 linie, 2 ponad wytyczną 300. Kodek jest kohezyjny (encode+decode+framing dla dwóch typów rekordu, jedna odpowiedzialność); podział rozbiłby spójność bez realnej korzyści. Do rozważenia dopiero jeśli w Fazie 2+ dojdą kolejne typy rekordu — wtedy ewentualny split `framing` vs `payload codec`. Nie blokuje.

---

## Odchylenia od planu

Brak. Wszystkie pliki zaplanowane w Unit 1–2 istnieją, testy zdefiniowane w planie
(`test_blackbox_record.c`, `test_blackbox_ring.c`) istnieją, zawierają asercje i są
zarejestrowane w `test/host/CMakeLists.txt` + `test/host/test_main.c`. Wszystkie
scenariusze testowe z pliku zadań (round-trip, empty 0xFF, corrupt CRC, slot wrap,
seq u32 wrap, overflow overwrite) pokryte.

---

## Wyniki walidacji

- **Host-tests:** `test/host/run.sh` → **366 Tests, 0 Failures, 0 Ignored — OK** (23 nowe testy blackbox zielone).
- **idf.py build:** zielony (per kontekst; `components/blackbox/` bez CMakeLists → poza buildem IDF w Fazie 1, zgodnie z planem — walidacja tylko host-testami).
- **Oracle power:** potwierdzona empirycznie (3/3 naiwne implementacje łamią asercje).
- **Offsety partycji:** potwierdzone `gen_esp32part.py` — nvs/phy_init/factory/appcfg niezmienione.

## Weryfikacja E2E

Nie dotyczy — Faza 1 to czysta logika + layout flash, brak UI/przeglądarki.
Weryfikacja sprzętowa (realny zapis na flash, brak jittera 50 Hz przy erase)
świadomie odroczona do known-issues (Faza 2/3), zgodnie z planem.
