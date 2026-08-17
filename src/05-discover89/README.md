# 05-discover89 — measured board map

Board: AliExpress "51 MCU minimum system" dev board (item 1005007287392911),
purple, CH340G, socketed STC89C52RC. Everything below was **measured by the
firmware on 2026-08-17**, not read from a listing.

| peripheral | pins | polarity | how it was measured |
|---|---|---|---|
| 4 black buttons | P3.2, P3.3, P3.4, P3.5 | active low | pin-change reports over UART, two full press rounds |
| 8-LED marquee | P1 (and/or P0/P2 — 03-blink89 lit them all) | active low | owner-confirmed blink; 12-marquee89 walks P1 |
| 4-digit 7-seg | P0 segments + select lines (exact select pins unmapped) | common anode | blinked with 03-blink89 |
| DS18B20 | **no presence pulse on any pin** | — | 1-Wire reset probe over all of P0–P3 (minus P3.0/P3.1) |
| 2 red buttons | power / reset (reset is ACTIVE HIGH on STC) | — | no pin reports; reset re-runs the boot banner |
| UART/ISP | P3.0 RxD, P3.1 TxD via CH340 | 9600 8N1 app-side | 04-hello89 two-way |

DS18B20: the board advertises the *circuit*; the sensor itself is either not
populated or behind a jumper. Re-run this probe after checking the footprint.

The buttons landing on INT0/INT1/T0/T1 is the classic arrangement — P3.2 and
P3.3 can be serviced by real edge interrupts later, not just polling.

Chip facts from the same session: STC89C52RC/LE52RC, magic F002, 8 KB flash,
6 KB EEPROM, BSL 6.6C, crystal measured 11.030 MHz, 12T.
