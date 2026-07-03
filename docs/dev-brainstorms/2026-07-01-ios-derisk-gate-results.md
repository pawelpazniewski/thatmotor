---
date: 2026-07-01
topic: ios-derisk-gate
status: PENDING (do wypełnienia na urządzeniu)
---

# Wynik bramki de-risk (Unit 0) — łączność iPhone ↔ 192.168.4.1

App: `ios/DeRiskProbe` (target DeRiskProbe). Uruchomić na realnym iPhonie (iOS 17+,
**aktywne LTE**), podłączonym do działającego silnika (AP `kayak-motor`).

## Procedura
1. „Dołącz do AP" — zanotuj czas join (powtórz ~10×, złap ogon buga DHCP ~130 s).
2. „HTTP POST" — POST `/api/command {"cmd":"goto_cancel"}`; oczekiwane 200
   `{"data":null,"error":null}`. Równolegle sprawdź, że internet po LTE działa.
3. „Start WebSocket" — licznik ramek powinien rosnąć ~10 Hz.
4. Test odmowy Local Network: odrzuć prompt → potwierdź, że HTTP „cicho" failuje
   (nie crash) i że recovery przez Ustawienia działa.
5. Stress: background 20 s + lock ekranu → czy asocjacja/socket wracają.

## Wyniki (WYPEŁNIĆ)
| Test | Wynik | Uwagi |
|------|-------|-------|
| Join AP (czas, %prób z ogonem 130 s) | — | |
| HTTP POST → 200 | — | |
| Internet LTE równolegle | — | |
| WS ramki ~10 Hz ≥60 s | — | |
| Odmowa Local Network → ciche fail + recovery | — | |
| Background 20 s → reconnect | — | |
| Potrzebny `NWConnection` fallback zamiast URLSession? | — | |

## Werdykt
- [ ] **PASS** — HTTP + WS działają do 192.168.4.1 przy żywym LTE; asocjacja przeżywa
  krótkie tło; działa fallback manual-join + recovery uprawnienia. → kontynuujemy plan.
- [ ] **FAIL** — stop, rozmowa o Plan B (BLE, dotyka firmware).

## Decyzje wyprowadzone dla dalszej implementacji
- Transport HTTP: URLSession / NWConnection(.wifi) — (wybór po teście)
- Czy app dołącza do AP programowo, czy kieruje do Ustawień — (obserwacja)
