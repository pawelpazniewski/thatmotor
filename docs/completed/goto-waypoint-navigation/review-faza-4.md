# Review Faza 4 — Parametry + telemetria + panel (Unit 5 domknięcie + Unit 6)

Commit: `fcc8743` | Branch: `feature/goto-waypoint-navigation`
Data: 2026-07-01 | Reviewer: dev-autopilot (multi-perspective)

## Severity gate: ✅ CZYSTE

- **P1 (blocking): 0**
- **P2 (important): 0**
- **P3 (nit): 2**

Walidacja: host-tests **429/429 PASS** (+4 vs Faza 3), `idf.py build` (esp32s3) **PASS** (bin 0xf1ce0, 37% partycji wolne). Zero regresji, zero test-weakeningu.

## Zakres sprawdzony

Pliki (diff commita fcc8743):
- `components/control_loop/include/control_loop.h` — 7 pól `goto_*` w `control_loop_snapshot`
- `components/control_loop/include/loop_step.h` — 4 pola goto w `loop_telemetry`
- `components/control_loop/src/loop_step.c` — populacja goto telemetrii gated na `target_source==SRC_GOTO`
- `components/control_loop/src/control_loop.c::publish_snapshot` — mapowanie do snapshotu
- `components/web_panel/src/ws_telemetry.c::snapshot_to_json` — serializacja ints/bools, bufor 512→640
- `web/index.html`, `web/app.js` — karta "Goto (app nav)"
- `test/host/test_params_decide.c` — `test_armed_rejects_goto_comms_timeout_write` (409-w-ARMED)
- `test/host/test_loop_step.c` — 3 testy populacji telemetrii goto

## Werdykt punktów 1–6

1. **Goto telemetria TYLKO dla SRC_GOTO — ✅ POTWIERDZONE.** `loop_step.c`: `goto_owns_target = state->spot_lock.target_source == SPOT_LOCK_SRC_GOTO`; pola goto = wartości spot_lock tylko gdy `goto_owns_target`, inaczej `SPOT_LOCK_OFF`/0. Test `test_goto_telemetry_reads_off_under_ch3_hold` ma **realną moc wyroczni**: asertuje `spot_lock_substate==ACTIVE` (ten sam silnik pracuje pod CH3-hold) ORAZ `goto_substate==OFF` + `goto_err_m==0` + `goto_arrived==false`. Naiwne mirrorowanie spot_lock bez bramki źródła → `goto_substate==ACTIVE` → test FAILuje. Parytet rozróżnienia źródła zachowany.

2. **Serializacja ints/bools only — ✅ POTWIERDZONE.** Wszystkie specyfikatory to `%u`/`%d`/`%s` (booleany jako `"true"`/`"false"`). Zero `%f`, zero floatów na łączu. Bufor 640: dla realnych wartości operacyjnych (GPS ~10 znaków, err/bearing małe) z zapasem; nawet w skrajnych kombinacjach guard `len<=0 || (size_t)len >= sizeof(json)` w `push_work` przechwytuje truncation → snapshot pomijany (snprintf ograniczony przez `n`, **brak przepełnienia/korupcji**). Zob. P3-1.

3. **Nazwy pól goto_* stabilne/opisowe — ✅ POTWIERDZONE.** Nagłówek `control_loop.h` jawnie dokumentuje "stable telemetry contract for the iOS app (parity with /api WS)". Nazwy spójne z konwencją `spot_lock_*`/`gps_*`. Panel (`app.js`) czyta te same klucze; enum `GOTO_STATE_NAMES` mirror `[off,active,paused]` == enum firmware (0/1/2, zweryfikowane w `spot_lock.h`).

4. **Test 409-w-ARMED moc wyroczni — ✅ POTWIERDZONE (z zastrzeżeniem P3-2).** `params_decide_write(SM_STATE_ARMED, true)` z `fields_valid=true` izoluje bramkę stanu SI-6; asercje: `decision==REJECT_NOT_DISARMED`, `code==API_ERR_NOT_DISARMED`, `http_status==409`, `api_error_code_str`. Gdyby bramka keyowała na walidacji pola zamiast stanu, `fields_valid=true` przepuściłoby write → asercje FAILują. Realna wyrocznia na izolację stanu.

5. **Zero test-weakeningu / zero regresji — ✅ POTWIERDZONE.** 429/429 (+4), żadna istniejąca asercja nie osłabiona ani usunięta; nowe testy dodają asercje, nie racjonalizują. Inwarianty Faz 1–3 nietknięte.

6. **Panel renderuje bezpiecznie — ✅ POTWIERDZONE.** `setText` używa `el.textContent` (nie `innerHTML`) → brak wektora XSS przy wstawianiu wartości telemetrii. Podziały (`/1e7`, `/10`) tylko display-side; wire pozostaje int. Indeksowanie `GOTO_STATE_NAMES[t.goto_state] || t.goto_state` bezpieczne dla 0/1/2 (0→"off" truthy) z fallbackiem dla wartości poza zakresem.

## Findingi

### 🟡 P3-1 [nit] — ws_telemetry.c: bufor 640 dobrany pod typowe wartości, nie pełny zakres typów
Suma pól przy hipotetycznym maksimum WSZYSTKICH pól uint jednocześnie (`65535`/`4294967295`) przekracza 640 (~727–875 B). W praktyce nieosiągalne (GPS e7 ~10 zn., err/bearing małe, stany 1-cyfrowe), a guard `(size_t)len >= sizeof(json)` w `push_work` przechwytuje ewentualny truncation → snapshot pomijany bez przepełnienia. To ta sama własność projektowa co przy poprzednim buforze 512 (nie regres wniesiony przez goto). Opcjonalnie: bump do ~768 dla marginesu lub jednolinijkowy komentarz o zależności rozmiaru od fizycznych granic pól. **Bez akcji blokującej — safety intact (snprintf bounded + guard).**

### 🟡 P3-2 [nit] — test_params_decide.c: `test_armed_rejects_goto_comms_timeout_write` jest near-duplikatem
`params_decide_write` jest field-agnostyczny (bierze `state` + `fields_valid`, nie wie o `goto_comms_timeout_ms`), więc ten test wywołuje identyczną ścieżkę co istniejący `test_armed_rejects_new_spot_lock_param_write` (oba `params_decide_write(ARMED, true)`). Wartość testu jest **dokumentacyjna** (jawnie wiąże SI-6 z nowym parametrem v7), nie pokrywa nowej gałęzi kodu. Dopuszczalne per coding-rules §11 (duplikacja > złożoność); nie osłabia niczego. **Bez akcji.**

## Uwagi kontekstowe (NIE findingi)
- `[E2E]` panelu/app słusznie odłożone do `known-issues.md §4d` (brak przeglądarki/hardware/aplikacji iOS). Kod panelu zweryfikowany statycznie — poprawny.
- `snapshot_to_json` static+IDF-dep → niehost-linkowalny; populacja `loop_telemetry.goto_*` pokryta host-testem, sama serializacja przez `idf.py build` (PASS). Zgodne z Pure ⊥ HAL.
- Diagnostyki clangd (`-mlongcalls`/`hal.h`/`esp_err.h`) = znane false-positives, pominięte.

## Rekomendacja
Faza 4 **gotowa do domknięcia zadania**. Findingi P3 opcjonalne, nie blokują. Pozostają domknięcia zadaniowe: dokumentacja kontraktu API iOS (goto/goto_cancel/keepalive/pola `goto_*`) oraz rozważenie `/dev-compound` dla wzorca app-driven override.
