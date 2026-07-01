---
title: "Reload panelu WWW resetował ESP32 (przepełnienie stosu tasku httpd)"
date: 2026-07-01
category: runtime-errors
severity: critical
stack:
  - ESP-IDF
  - ESP32-S3
  - esp_http_server
  - cJSON
  - FreeRTOS
tags:
  - stack-overflow
  - httpd
  - reset-reason
  - misdiagnosis
  - failsafe
  - hardware-verified
status: verified
last_verified: 2026-07-01
---

# Reload panelu WWW resetował ESP32 (przepełnienie stosu tasku httpd)

## Symptomy

- Przeładowanie panelu WWW (`http://192.168.4.1`) resetowało ESP32-S3; silnik się
  rozbrajał (reboot → DISARMED), wymagał ponownego uzbrojenia.
- Na sprzęcie widoczne jako: ikona/LED failsafe gaśnie na ~2-3 s (czas bootu), potem
  wzorzec failsafe wraca.
- Reset reason z portu szeregowego: `rst:0xc (RTC_SW_CPU_RST)` przy KAŻDYM reloadzie.
- `Saved PC` po dekodowaniu `addr2line` → `cJSON print_string_ptr` (cJSON.c:977).
- Banner panicu ginął przez USB-CDC (re-enumeracja portu przy resecie), przez co
  wcześniej łatwo było błędnie zdiagnozować przyczynę.

## Root Cause

Task serwera `esp_http_server` startował z domyślnym stosem **4096 B**
(`HTTPD_DEFAULT_CONFIG().stack_size`, bez nadpisania). Handlery kładły na tym stosie
duże bufory JSON: POST `/api/params` to `reqbuf[HTTP_REQ_MAX]` + `body[HTTP_BODY_MAX]`
i zagnieżdżone `params_api` `data[PARAMS_JSON_MAX]` (~3.7 KB razem), a GET `/api/params`
— `body` + zagnieżdżone `data` (~2.4 KB); do tego overhead frameworka httpd i rekursja
cJSON. To przekraczało 4 KB stosu → FreeRTOS **stack canary**
(`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`) → abort → `esp_restart` →
`RTC_SW_CPU_RST`. Bufory są wyprowadzone z `PARAMS_JSON_SERIALIZE_MAX`, który rośnie z
liczbą pól — dodawanie parametrów (spot-lock, potem goto → 32 pola) stopniowo zbliżało
i przekroczyło próg, więc bug „pojawił się" z czasem.

**Błędna wcześniejsza diagnoza:** commit `a11e991` założył, że to zagłodzenie pętli RT
na CPU0 → Task WDT panic, i przeniósł pętlę 50 Hz na dedykowany task CPU1. Nie pomogło,
bo crash jest w **tasku httpd** — kompletnie niezwiązany z pętlą sterującą ani Task WDT.
Diagnoza nigdy nie została zweryfikowana na sprzęcie (commit sam to zaznaczał).

## Rozwiązanie

Wyprowadź `config.stack_size` z realnego rozmiaru buforów handlerów zamiast zostawiać
domyślne 4 KB. Rozmiar rośnie automatycznie razem z `PARAMS_JSON_SERIALIZE_MAX`, więc
nie zdryfuje przy dodawaniu pól (commit `abf40eb`,
`components/web_panel/src/http_server.c`):

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.max_uri_handlers = 8;
config.lru_purge_enable = true;
/* POST /api/params: reqbuf + body + nested params_api data (~3.7 KB) + framework
 * frames + cJSON recursion overflowed the default 4 KB httpd stack -> canary abort
 * -> RTC_SW_CPU_RST on every panel reload. Size from the buffers + headroom. */
config.stack_size = 2U * HTTP_BODY_MAX + PARAMS_JSON_SERIALIZE_MAX + 4096U; /* ~7.8 KB */
```

Zweryfikowane na sprzęcie: 5-6 szybkich reloadów → **zero `rst:0xc`** w logu portu,
LED failsafe mruga nieprzerwanie.

## Komendy diagnostyczne

```bash
# 1. Reset reason ze sprzętu — odporny na re-enumerację USB-CDC (surowy `cat` gubi
#    początek linii bootu; użyj pyserial reopen-on-disconnect albo idf.py monitor w TTY).
#    Szukaj: "rst:0xNN (REASON)". 0xc=RTC_SW_CPU_RST (panic/sw), 0x0f=BROWNOUT, 0x07/08=TG WDT.

# 2. Zdekoduj Saved PC (i każdy adres z backtrace) na funkcję:
. "$HOME/esp/idf-env.sh" >/dev/null 2>&1
xtensa-esp32s3-elf-addr2line -pfiaC -e build/kayak-motor-firmware.elf 0x4200c3dc

# 3. Sprawdź konfigurację WDT/brownout/stack-check (rozróżnia klasy resetu):
grep -nE 'HTTPD_STACK|CHECK_STACKOVERFLOW|TASK_WDT|INT_WDT|BROWNOUT' sdkconfig

# 4. Domyślny stos httpd w IDF:
grep -n 'stack_size' "$HOME/esp/esp-idf/components/esp_http_server/include/esp_http_server.h"
```

## Zapobieganie

- **Najpierw ustal reset reason ze sprzętu, potem naprawiaj.** `rst` code + `addr2line`
  na `Saved PC`/backtrace jednoznacznie klasyfikuje reset (WDT vs brownout vs panic vs
  stack overflow). `a11e991` zgadł WDT bez tej weryfikacji i naprawił zły trop.
- **Nie zostawiaj `HTTPD_DEFAULT_CONFIG().stack_size` (4 KB) gdy handlery mają duże
  bufory na stosie.** Skaluj `stack_size` z rozmiaru buforów, najlepiej wyprowadzając go
  ze stałej rozmiaru (tu `PARAMS_JSON_SERIALIZE_MAX`), żeby rósł razem z nią.
- Rozważ przeniesienie dużych scratch-buforów poza stos (static, httpd jest single-worker)
  jeśli presja na stos wróci.
- USB-CDC gubi banner panicu — jeśli potrzebny pełny backtrace, dodaj log
  `esp_reset_reason()` na starcie albo core dump do flashu.

## Powiązane

- `docs/solutions/performance-issues/2026-07-01-rt-loop-flash-io-background-task.md`
  — inny wątek jitteru pętli RT; TU pętla NIE była przyczyną (odwrotny wniosek).
- Commit `a11e991` (pętla 50 Hz na CPU1) — sam w sobie sensowna izolacja RT od WiFi,
  ale NIE był lekiem na ten crash; jego opis zawiera błędną diagnozę (Task WDT).
- Commit `abf40eb` — właściwy fix.

## Kontekst

ESP-IDF 5.5, ESP32-S3-WROOM-1 N16R8. Panel serwowany z `esp_http_server`, params jako
JSON przez cJSON. `PARAMS_JSON_FIELD_COUNT=32` (28×uint16 + 3×bool + schema_version),
`PARAMS_JSON_SERIALIZE_MAX≈1187 B`, `HTTP_BODY_MAX≈1251 B`. Zasilanie tylko z USB przy
teście (brownout wykluczony przez reset reason 0xc, nie 0x0f). Bug istniał przed
feature goto — narastał z każdą partią nowych pól params.
