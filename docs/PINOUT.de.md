# STC12C5A60S2 — Referenz

[🇬🇧 English](PINOUT.md) · 🇩🇪 Deutsch

Alles hier stammt aus dem offiziellen
[Datenblatt STC12C5A60S2 / STC12LE5A60S2](https://www.stcmicro.com/datasheet/STC12C5A60S2-en.pdf)
(STC MCU Limited, Stand 15.07.2011). Die Abschnittsnummern beziehen sich auf
dieses Dokument.

---

## Varianten

| Familie | Versorgung | Anmerkung |
|---|---|---|
| `STC12C5A60S2` | **3,5 – 5,5 V** | Der 5-V-Typ. Davon geht dieses Repository aus. |
| `STC12LE5A60S2` | **2,1 – 3,6 V** | Der Niederspannungsbruder. **5 V zerstören ihn.** |

Beiden gemeinsam: 60 KB Flash, 1280 B RAM (256 B intern + 1024 B Zusatz-RAM),
1 KB EEPROM, zwei UARTs, zwei DPTRs, vier Timer, achtkanaliger 10-Bit-ADC, SPI,
16-Bit-PCA / 2× 8-Bit-PWM, Watchdog, sieben externe Interrupts, die aus dem
Stromsparmodus wecken können.

Die `…AD`-Varianten haben einen UART und einen PCA-Kanal weniger, den
`…PWM`-Varianten fehlt der ADC. Die Pinbelegung ist bei allen gleich.

## Gehäuse → verfügbare Ports

| Gehäuse | I/O-Anzahl | Port 4 | Port 5 |
|---|---:|---|---|
| **PDIP-40**, QFN-40 | 36 | **nur P4.4 – P4.7** | — |
| PLCC-44, LQFP-44 | 40 | P4.0 – P4.7 | — |
| LQFP-48 | 44 | P4.0 – P4.7 | P5.0 – P5.3 |

## Pinbelegung PDIP-40

| Pin | Name | Pin | Name |
|---:|---|---:|---|
| 1 | P1.0 / ADC0 / CLKOUT2 | 40 | VCC |
| 2 | P1.1 / ADC1 | 39 | P0.0 / AD0 |
| 3 | P1.2 / ADC2 / ECI / RxD2 | 38 | P0.1 / AD1 |
| 4 | P1.3 / ADC3 / CCP0 / TxD2 | 37 | P0.2 / AD2 |
| 5 | P1.4 / ADC4 / CCP1 / SS | 36 | P0.3 / AD3 |
| 6 | P1.5 / ADC5 / MOSI | 35 | P0.4 / AD4 |
| 7 | P1.6 / ADC6 / MISO | 34 | P0.5 / AD5 |
| 8 | P1.7 / ADC7 / SCLK | 33 | P0.6 / AD6 |
| 9 | RST / P4.7 | 32 | P0.7 / AD7 |
| 10 | P3.0 / RxD / INT | 31 | P4.6 / EX_LVD / RST2 |
| 11 | P3.1 / TxD | 30 | P4.5 / ALE |
| 12 | P3.2 / INT0 | 29 | P4.4 / NA |
| 13 | P3.3 / INT1 | 28 | P2.7 / A15 |
| 14 | P3.4 / T0 / INT / CLKOUT0 | 27 | P2.6 / A14 |
| 15 | P3.5 / T1 / INT / CLKOUT1 | 26 | P2.5 / A13 |
| 16 | P3.6 / WR | 25 | P2.4 / A12 |
| 17 | P3.7 / RD | 24 | P2.3 / A11 |
| 18 | XTAL2 | 23 | P2.2 / A10 |
| 19 | XTAL1 | 22 | P2.1 / A9 |
| 20 | GND | 21 | P2.0 / A8 |

## Die Portmodi (§4.1, §4.3)

Zwei Register pro Port, ein Bit pro Pin:

| `PxM1` | `PxM0` | Modus | Verhalten |
|:---:|:---:|---|---|
| 0 | 0 | Quasi-bidirektional | **Standard nach Reset.** Senkt ≤20 mA; liefert typ. ~230 µA (spezifiziert 150–250 µA) über einen schwachen Pull-up. Gleichzeitig als Eingang lesbar. |
| 0 | 1 | Gegentakt (Push-Pull) | Kräftiger Pull-up. Liefert *und* senkt ≤20 mA. Immer einen Vorwiderstand vorsehen. |
| 1 | 0 | Nur Eingang | Hochohmig, mit Schmitt-Trigger. |
| 1 | 1 | Open Drain | Alle Pull-ups aus. Braucht einen externen Pull-up. |

Jeder Pin hat einen Schmitt-Trigger am Eingang. **Der Gesamtstrom des Chips
sollte unter ~120 mA bleiben**, auch wenn ein einzelner Pin 20 mA senken kann.

### Eine LED treiben (§4.6)

| Portmodus | Verdrahtung | Vorwiderstand |
|---|---|---|
| Quasi-bidirektional (Standard) | **Senken**: `+V ──[R]──▶|── Pin` | ≥1 kΩ empfohlen, 470 Ω absolutes Minimum |
| Gegentakt | **Speisen**: `Pin ──[R]──▶|── GND` | dito |

Dieses Repository verdrahtet **senkend** und schaltet zusätzlich in den
**Gegentaktmodus**: Senken funktioniert in jedem Modus, und Gegentakt macht den
Aus-Zustand zu einem harten VCC statt einem schwachen Pull-up.

### Die 4-Takt-Falle (§4.4)

Ein klassischer 8051 braucht 12 Takte pro I/O-Zugriff, der 1T-STC12 nur vier.
Wer einen Pin ändert und danach ein *externes* Signal zurückliest: der Befehl
ist abgeschlossen, die Außenwelt hat sich aber noch nicht eingeschwungen — ein
bis zwei `nop`s einfügen.

Ebenfalls aus §4.4: I²C, SPI und andere Open-Drain-Peripherie wollen einen
10-kΩ-Pull-up; ein I/O, der eine PNP-Basis treibt, braucht entweder einen
externen Pull-up passend zum Basiswiderstand oder den Gegentaktmodus.

### Einen Pin nach dem Reset auf Low bekommen (§4.8)

Nach dem Reset ist jeder Pin ein schwacher Pull-up, liegt also auf High. Soll
ein Pin beim Einschalten low sein, gehört ein Widerstand von 1 k/2 k/3 kΩ nach
Masse daran (hinter einem Vorwiderstand von ≥470 Ω). Der schwache interne
Pull-up kommt dagegen nicht an, der Pin liest also low, bis der Code den
Gegentaktmodus einstellt.

## SFR-Kurzreferenz

Nur die Register, die dieses Repository anfasst, plus die, nach denen man als
Nächstes greift.

| SFR | Adr. | Bit-adressierbar | Zweck |
|---|:---:|:---:|---|
| `P0` | 0x80 | ✓ | Port-0-Daten |
| `P1` | 0x90 | ✓ | Port-1-Daten |
| `P2` | 0xA0 | ✓ | Port-2-Daten |
| `P3` | 0xB0 | ✓ | Port-3-Daten |
| `P4` | 0xC0 | ✓ | Port-4-Daten (braucht `P4SW`, siehe unten) |
| `P5` | 0xC8 | ✓ | Port-5-Daten (nur LQFP-48) |
| `P0M1` / `P0M0` | 0x93 / 0x94 | ✗ | Portmodus 0 |
| `P1M1` / `P1M0` | 0x91 / 0x92 | ✗ | Portmodus 1 |
| `P2M1` / `P2M0` | 0x95 / 0x96 | ✗ | Portmodus 2 |
| `P3M1` / `P3M0` | 0xB1 / 0xB2 | ✗ | Portmodus 3 |
| `P4M1` / `P4M0` | 0xB3 / 0xB4 | ✗ | Portmodus 4 |
| `P5M1` / `P5M0` | 0xC9 / 0xCA | ✗ | Portmodus 5 |
| `P4SW` | 0xBB | ✗ | Schaltet P4.4/P4.5/P4.6 als GPIO frei |
| `AUXR` | 0x8E | ✗ | `T0x12 T1x12 UART_M0x6 BRTR S2SMOD BRTx12 EXTRAM S1BRS` |
| `AUXR1` | 0xA2 | ✗ | `- PCA_P4 SPI_P4 S2_P4 GF2 ADRJ - DPS` |
| `TMOD` | 0x89 | ✗ | Modus Timer 0/1 |
| `TCON` | 0x88 | ✓ | `TR0`, `TF0`, `TR1`, `TF1`, Flankenflags der Interrupts |

All das ist in SDCCs `<stc12.h>` deklariert — `#include <stc12.h>` und man hat
es beim Namen. (Das `board.h` dieses Repositories erledigt das bereits.)

### `P4SW` (0xBB) — Reset-Wert `x000,xxxx`

| Bit | Name | 0 (Standard) | 1 |
|:---:|---|---|---|
| 6 | `LVD_P4.6` | Pin 31 ist externe Unterspannungserkennung / RST2 | Pin 31 ist GPIO **P4.6** |
| 5 | `ALE_P4.5` | Pin 30 ist ALE (externer Datenspeicher) | Pin 30 ist GPIO **P4.5** |
| 4 | `NA_P4.4` | Pin 29 ist ein schwacher Pull-up ohne Funktion | Pin 29 ist GPIO **P4.4** |

`P4.7` (Pin 9) ist standardmäßig der Reset-Pin und lässt sich nur über die
ISP-Option zu GPIO umwidmen — und dann braucht man zwingend einen externen Takt.

### `AUXR1` (0xA2) — Peripherie auf Port 4 verschieben

`PCA_P4`, `SPI_P4` und `S2_P4` legen die Pins von PCA/PWM, SPI und UART2 von
Port 1 auf Port 3/4 um. Auf PDIP-40 nutzlos (P4.0–P4.3 sind nicht
herausgeführt), aber gut zu wissen, wenn man auf LQFP wechselt.

`DPS` schaltet zwischen den beiden Datenzeigern um — ein wirklich nützliches
STC12-Merkmal für Blockkopien.

## Takt (§2.1)

* **Interner RC**, aktiv wenn kein Quarz bestückt ist: **11 – 17 MHz bei 5 V**,
  8 – 12 MHz bei 3 V. Beim STC12 werkseitig nicht brauchbar abgeglichen —
  `stcgal -t` funktioniert erst ab STC15.
* **Externer Quarz** zwischen XTAL1 (19) und XTAL2 (18), je 22–47 pF nach Masse.
  11,0592 MHz ist die traditionelle Wahl, weil sich daraus exakte UART-Baudraten
  ergeben.
* **Externer Oszillator**: XTAL1 treiben, **XTAL2 offen lassen**.
* Oberhalb von 33 MHz einen aktiven Oszillator statt eines passiven Quarzes
  verwenden.

## Reset (§2.3)

* `RST` (Pin 9) ist **high-aktiv**. Mindestens zwei Maschinenzyklen halten.
* Unter 12 MHz: ein schlichter **1 kΩ nach Masse** ist die komplette
  Reset-Beschaltung.
* Über 12 MHz empfiehlt STC stattdessen den zweiten Reset-Pin `RST2` (Pin 31,
  `P4.6`).
* Das `10 kΩ + 10 µF`-Glied aus alten 8051-Schaltplänen gehört zu *low-aktiven*
  Reset-Bausteinen. Hier bitte nicht übernehmen.

## In-System-Programmierung (§13)

Der Bootloader steckt im Masken-ROM und spricht auf `P3.0`/`P3.1` ein serielles
Protokoll. Er läuft **ausschließlich unmittelbar nach einem Kaltstart** — ein
warmer Reset über den RST-Pin bringt einen nicht zurück hinein. Deshalb braucht
jeder Flashvorgang ein echtes Aus- und Wiedereinschalten der Versorgung.

`stcgal -P stc12` implementiert dieses Protokoll. Der Handshake läuft mit 2400
Baud und handelt danach auf die mit `-b` gewünschte Rate hoch.
