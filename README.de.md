# stc12c5a60s2-lab

[🇬🇧 English](README.md) · 🇩🇪 Deutsch

Bare-Metal-Experimente mit dem **STC12C5A60S2**, komplett unter macOS und
ausschließlich mit offenen Werkzeugen — kein Windows, kein STC-ISP.exe, kein
spezielles Programmiergerät.

Das erste Beispiel lässt zwei LEDs blinken. Alles andere in diesem Repository
existiert nur dafür, dass dieses erste Blinken aus einer Tüte Bauteile heraus
reproduzierbar wird.

Das eigentliche Ziel weiter hinten: eine **[BrickWright](https://github.com/CrispStrobe/brickwright)-Erweiterung**
für diesen Chip — Scratch-Blöcke → C → `.hex`, genau so, wie BrickWright heute
schon nach LEGO-NXT-/EV3-Bytecode transpiliert. Siehe
[docs/ROADMAP.de.md](docs/ROADMAP.de.md).

---

## Inhalt

1. [Pinbelegung](#1-pinbelegung)
2. [Verbindung zum IC unter macOS](#2-verbindung-zum-ic-unter-macos)
3. [Aufbau und Verdrahtung](#3-aufbau-und-verdrahtung)
4. [Das Programm](#4-das-programm)
5. [Bauen und flashen](#5-bauen-und-flashen)
6. [Fehlersuche](#6-fehlersuche)
7. [Aufbau des Repositories](#7-aufbau-des-repositories)
8. [Wohin das führt](#8-wohin-das-führt)

---

## 1. Pinbelegung

Der STC12C5A60S2 ist ein 8051-Kern mit 1T-Architektur (ein Taktzyklus pro
Maschinenzyklus) — bei gleichem Takt also grob 8- bis 12-mal schneller als ein
klassischer 12T-8051. 60 KB Flash, 1280 B RAM (256 B intern + 1024 B
Zusatz-RAM), 1 KB EEPROM, zwei UARTs, achtkanaliger 10-Bit-ADC, SPI, PCA/PWM.

Es gibt ihn als PDIP-40, PLCC-44, LQFP-44, LQFP-48 und QFN-40. **Dieses
Repository geht durchgehend von PDIP-40 aus**, dem bedrahteten Gehäuse, das man
ins Steckbrett stecken kann.

### PDIP-40 Pinbelegung

```
                      ┌─────────∪──────────┐
  CLKOUT2/ADC0/P1.0 ──│  1              40 │── VCC
          ADC1/P1.1 ──│  2              39 │── P0.0/AD0
 RxD2/ECI/ADC2/P1.2 ──│  3              38 │── P0.1/AD1
TxD2/CCP0/ADC3/P1.3 ──│  4              37 │── P0.2/AD2
  SS/CCP1/ADC4/P1.4 ──│  5              36 │── P0.3/AD3
     MOSI/ADC5/P1.5 ──│  6              35 │── P0.4/AD4
     MISO/ADC6/P1.6 ──│  7              34 │── P0.5/AD5
     SCLK/ADC7/P1.7 ──│  8              33 │── P0.6/AD6
           RST/P4.7 ──│  9    STC12     32 │── P0.7/AD7
       INT/RxD/P3.0 ──│ 10   C5A60S2    31 │── EX_LVD/RST2/P4.6
           TxD/P3.1 ──│ 11              30 │── ALE/P4.5
          INT0/P3.2 ──│ 12              29 │── NA/P4.4
          INT1/P3.3 ──│ 13              28 │── P2.7/A15
CLKOUT0/INT/T0/P3.4 ──│ 14              27 │── P2.6/A14
CLKOUT1/INT/T1/P3.5 ──│ 15              26 │── P2.5/A13
            WR/P3.6 ──│ 16              25 │── P2.4/A12
            RD/P3.7 ──│ 17              24 │── P2.3/A11
              XTAL2 ──│ 18              23 │── P2.2/A10
              XTAL1 ──│ 19              22 │── P2.1/A9
                GND ──│ 20              21 │── P2.0/A8
                      └────────────────────┘
```

Pin 1 liegt an der Seite mit der Kerbe. Die Namen `INT`, `WR` und `RD` sind im
Datenblatt low-aktiv (dort mit Überstrich gezeichnet); hier steht schlichtes
ASCII.

### Pin-Tabelle

| Pin | Name | Was es ist |
|----:|------|------------|
| 1–8 | **P1.0 – P1.7** | GPIO. Gleichzeitig die acht ADC-Eingänge (ADC0–ADC7), UART2 (P1.2/P1.3), SPI (P1.4–P1.7), PCA/CCP (P1.3/P1.4) und CLKOUT2 (P1.0). |
| 9 | **RST / P4.7** | Reset. **High-aktiv** — mindestens zwei Maschinenzyklen auf High halten. Standardmäßig Reset; wird nur dann zu GPIO P4.7, wenn man es in den ISP-Optionen umstellt (und dann braucht man zwingend einen externen Takt). |
| 10 | **P3.0 / RxD** | UART1 Empfang — **das ist der ISP-Download-Pin**. |
| 11 | **P3.1 / TxD** | UART1 Senden — **das ist der ISP-Upload-Pin**. |
| 12 | **P3.2 / INT0** | GPIO / externer Interrupt 0. |
| 13 | **P3.3 / INT1** | GPIO / externer Interrupt 1. |
| 14 | **P3.4 / T0 / CLKOUT0** | GPIO / Timer-0-Eingang / programmierbarer Taktausgang. |
| 15 | **P3.5 / T1 / CLKOUT1** | GPIO / Timer-1-Eingang / programmierbarer Taktausgang. |
| 16 | **P3.6 / WR** | GPIO / Schreibsignal für externen Speicher. |
| 17 | **P3.7 / RD** | GPIO / Lesesignal für externen Speicher. |
| 18 | **XTAL2** | Quarz-Ausgang. Muss **offen bleiben**, wenn XTAL1 von einem externen Oszillator getrieben wird. |
| 19 | **XTAL1** | Quarz-Eingang. |
| 20 | **GND** | Masse. |
| 21–28 | **P2.0 – P2.7** | GPIO. Zusätzlich das obere Adressbyte A8–A15 bei externem Speicher. |
| 29 | **NA / P4.4** | GPIO **erst nach** Setzen von `P4SW.4`. Sonst nur ein schwacher Pull-up ohne Funktion. |
| 30 | **ALE / P4.5** | Address Latch Enable. GPIO erst nach Setzen von `P4SW.5`. |
| 31 | **EX_LVD / RST2 / P4.6** | Unterspannungserkennung / zweiter Reset-Eingang. GPIO erst nach Setzen von `P4SW.6`. |
| 32–39 | **P0.7 – P0.0** | GPIO — Achtung, **absteigend**: Pin 32 ist P0.7, Pin 39 ist P0.0. Zusätzlich AD0–AD7. |
| 40 | **VCC** | Versorgung. **3,5 – 5,5 V** beim Typ `STC12C…`. |

> [!IMPORTANT]
> **Es gibt keinen `EA`-Pin.** Wer vom AT89C51/52 kommt, ist es gewohnt, `EA`
> auf High zu legen, damit die CPU aus dem internen Flash läuft. STC hat den
> Pin weggelassen — der STC12 läuft immer aus dem internen Flash. An Pin 31
> sitzt stattdessen `EX_LVD/RST2/P4.6`.

> [!WARNING]
> **`STC12C5A60S2` ist ein 5-V-Typ (3,5–5,5 V)** und läuft bei 3,3 V nicht
> zuverlässig. Der 3,3-V-taugliche Bruder heißt `STC12LE5A60S2` (2,1–3,6 V) —
> und *der* nimmt bei 5 V Schaden. Vor dem Anlegen der Spannung also den
> Aufdruck auf dem Chip prüfen.

### Auf PDIP-40 existieren nur P4.4–P4.7

Das Datenblatt führt einen vollständigen 8-Bit-Port 4 auf, aber im 40-poligen
Gehäuse sind nur **P4.4, P4.5, P4.6 und P4.7** herausgeführt. Für `P4.0–P4.3`
braucht man PLCC-44, LQFP-44 oder LQFP-48; für Port 5 (P5.0–P5.3) LQFP-48.

### Die vier Portmodi

Jeder Pin jedes Ports lässt sich einzeln in einen von vier Modi schalten, über
je ein Registerpaar `PxM1` / `PxM0` (ein Bit pro Pin):

| `PxM1` | `PxM0` | Modus | Treiberverhalten |
|:---:|:---:|---|---|
| 0 | 0 | **Quasi-bidirektional** — Standard nach dem Einschalten, klassisches 8051-Verhalten | Senkt bis zu 20 mA nach Masse, liefert aber nur ca. 230 µA über einen schwachen Pull-up |
| 0 | 1 | **Gegentakt (Push-Pull)** | Liefert *und* senkt bis zu 20 mA — immer einen Vorwiderstand vorsehen |
| 1 | 0 | **Nur Eingang** (hochohmig, Schmitt-Trigger) | — |
| 1 | 1 | **Open Drain** | Senkt nur; braucht einen externen Pull-up |

Registeradressen: `P0M1`=0x93 `P0M0`=0x94, `P1M1`=0x91 `P1M0`=0x92,
`P2M1`=0x95 `P2M0`=0x96, `P3M1`=0xB1 `P3M0`=0xB2, `P4M1`=0xB3 `P4M0`=0xB4.

> [!NOTE]
> **Diese Asymmetrie ist die mit Abstand häufigste Anfängerfalle.** Direkt nach
> dem Reset kann ein Pin 20 mA nach Masse *ziehen*, aber nur etwa ein Viertel
> Milliampere *liefern*. Genau deshalb sind die LEDs in diesem Repository
> **low-aktiv** verdrahtet (siehe §3) — das ist die einzige Beschaltung, die in
> jedem Modus funktioniert. Der gesamte Chip sollte unter ~120 mA bleiben.

### Eine Falle, die aus der 1T-Architektur folgt

Auf einem klassischen 8051 dauert ein I/O-Zugriff 12 Takte, was externer
Hardware praktischerweise Zeit zum Einschwingen ließ. Auf dem STC12 sind es
**vier**. Wer einen Pin treibt und unmittelbar danach ein *externes* Signal
zurückliest, braucht ein bis zwei `nop`s — der Befehl ist fertig, die Außenwelt
aber noch nicht.

---

## 2. Verbindung zum IC unter macOS

Dieser Chip hat **kein JTAG und kein SWD**, und man braucht auch kein
Programmiergerät. Im Masken-ROM sitzt ein Bootloader (der „ISP-Monitor“), der
auf `P3.0`/`P3.1` ein serielles Protokoll spricht. Die gesamte Verbindung ist
also:

```
   Mac ──USB──▶ USB-TTL-Adapter ──3 Leitungen──▶ STC12C5A60S2
```

### 2.1 Der Adapter

Jeder 5-V-taugliche USB-TTL-Seriell-Adapter geht. Die drei üblichen:

| Chip | macOS-Treiber | Erscheint als |
|---|---|---|
| **CH340 / CH341** | seit macOS 11 Big Sur eingebaut | `/dev/cu.wchusbserial*` |
| **CP2102 / CP210x** | seit macOS 11 eingebaut | `/dev/cu.usbserial-*` oder `/dev/cu.SLAB_USBtoUART` |
| **FT232R** | in macOS eingebaut (AppleUSBFTDI) | `/dev/cu.usbserial-*` |
| **Ein Arduino Uno** | der Treiber, den der Uno ohnehin nutzt | `/dev/cu.usbmodem*` |

> [!TIP]
> **Kein Adapter zur Hand? Ein Arduino Uno ist einer.** Sein ATmega16U2 ist eine
> USB-Seriell-Brücke. `RESET` auf `GND` brücken, damit der ATmega328P aus dem
> Weg ist, und dann die Stiftleisten-Pins `RX`/`TX` als Adapter benutzen — die
> sind allerdings *durchverbunden*, also Uno `TX`(1) → MCU-Pin 11 und Uno
> `RX`(0) → MCU-Pin 10, genau umgekehrt zur Kreuzung aus §3.3. Er arbeitet mit
> 5 V, und das ist genau das, was wir brauchen.

> [!CAUTION]
> **„ISP“ heißt hier nicht SPI.** Wer von AVR kommt, versteht unter `ISP` ein
> Programmiergerät mit MOSI/MISO/SCK wie USBasp oder mySmartUSB. Der STC12 hat
> **keine solche Schnittstelle** — die Pins gibt es zwar (P1.5–P1.7), aber sie
> sind gewöhnliche SPI-Peripherie und kein Programmieranschluss. Der einzige Weg
> hinein ist der serielle Bootloader auf P3.0/P3.1. Das ist die mit Abstand
> häufigste Fehlannahme zu diesem Chip.

Auf einem aktuellen macOS sollte nichts installiert werden müssen. Falls der
Adapter gar nicht auftaucht: Herstellertreiber installieren
([WCH](https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html),
[Silicon Labs](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers),
[FTDI](https://ftdichip.com/drivers/vcp-drivers/)) und unter
**Systemeinstellungen → Datenschutz & Sicherheit** freigeben.

> [!IMPORTANT]
> **Den Spannungsjumper des Adapters auf 5 V stellen** und immer den
> `/dev/cu.*`-Knoten benutzen, niemals `/dev/tty.*`. Unter macOS blockiert der
> `tty.*`-Knoten auf Carrier Detect — das Flashen hängt dann einfach. Der
> `cu.*`-Knoten („call-up“) tut das nicht.

Nachsehen, was da ist:

```bash
make ports
```

### 2.2 Die Toolchain

Drei Teile: ein Compiler, ein Hex-Packer und das ISP-Flashprogramm.

```bash
# Compiler + packihx + makebin (SDCC kann die mcs51-/8051-Familie)
brew install sdcc

# stcgal — der quelloffene Ersatz für STCs Windows-only STC-ISP.exe
brew install pipx && pipx ensurepath
pipx install stcgal
```

Oder einfach das Hilfsskript:

```bash
./tools/setup-macos.sh
```

Prüfen:

```bash
sdcc --version     # 4.x, muss "mcs51" unter den Targets auflisten
stcgal --version   # 1.10 oder neuer
```

> **Warum `pipx` und nicht `pip3 install`?** Das Python von Homebrew verweigert
> globale `pip install`-Aufrufe (PEP 668). `pipx` gibt `stcgal` eine eigene
> virtuelle Umgebung und legt das Programm trotzdem in den `PATH`.

`stcgal` steht unter MIT. SDCC ist GPL, und der mitgelieferte Header
`mcs51/stc12.h` — aus dem alle unsere `P1M0`-, `P4SW`- und `AUXR`-Definitionen
stammen — steht unter GPL-2+ **mit Linking-Ausnahme**; die damit gebauten
Binaries gehören also uneingeschränkt dir.

### 2.3 Wie der ISP-Einstieg wirklich funktioniert

Das ist die Stelle, über die alle stolpern:

> **Der STC-Bootloader lauscht nur in den ersten Millisekunden nach einem
> Kaltstart.** Ein Druck auf den Reset-Taster genügt *nicht*. Die
> Versorgungsspannung muss tatsächlich weg und wiederkommen, und zwar
> während `stcgal` schon läuft und wartet.

Der Ablauf beim Flashen ist deshalb:

1. `make flash` starten. Es erscheint `Waiting for MCU, please cycle power:`.
2. *Erst dann* die Spannung des Chips aus- und wieder einschalten.
3. `stcgal` fängt den Handshake mit 2400 Baud ab, handelt auf 115200 hoch und
   schreibt den Flash.

Wer Schritt 2 automatisieren will, führt die **DTR**-Leitung des Adapters auf
einen Transistor, der VCC des MCU schaltet, und hängt `-a` an den
stcgal-Aufruf — bei [rgm3/ledcube444](https://github.com/rgm3/ledcube444) gibt
es ein Foto genau dieser Bastelei. Bis dahin reicht ein kleiner Schalter oder
schlicht das Abziehen der VCC-Leitung.

---

## 3. Aufbau und Verdrahtung

### 3.1 Stückliste

| Anz. | Bauteil |
|---:|---|
| 1 | STC12C5A60S2, PDIP-40 |
| 1 | 40-poliger DIP-Sockel (optional, aber schont den Chip) |
| 1 | USB-TTL-Adapter, 5 V |
| 2 | LEDs (beliebige Farbe) |
| 2 | 1-kΩ-Widerstände — Vorwiderstände für die LEDs |
| 1 | 1-kΩ-Widerstand — Pulldown für Reset |
| 1 | 100-nF-Keramikkondensator (Aufdruck `104`) — Abblockung |
| 1 | 10-µF-Elko — Stützkondensator |
| — | Steckbrett und Steckbrücken |
| *opt.* | 11,0592-MHz-Quarz + 2× 22–47 pF |

### 3.2 Die Minimalbeschaltung

Der STC12 braucht fast nichts, um zu laufen:

```
   +5V ─────┬──────────┬────────────────────── Pin 40   VCC
            │          │
          ──┴──      ──┴──
          10 µF      100 nF     ← beide so nah wie physikalisch möglich
          ──┬──      ──┬──         an die Pins 40/20 setzen
            │          │
   GND ─────┴──────────┴───┬────────────────── Pin 20   GND
                           │
                           └──[ 1 kΩ ]──────── Pin 9    RST
```

* **Reset (Pin 9):** Reset ist hier **high-aktiv**, muss zum Laufen also auf
  Low gehalten werden. Unterhalb von 12 MHz genügt ein 1-kΩ-Widerstand nach
  Masse. (Das klassische 10 kΩ + 10 µF aus alten 8051-Schaltplänen gehört zu
  *low-aktiven* Resets — hier bitte nicht übernehmen.)
* **Takt:** XTAL1/XTAL2 (Pin 19/18) dürfen **komplett leer bleiben**. Der Chip
  läuft dann auf seinem internen RC-Oszillator. Zum Blinken reicht das.
  * Der interne RC ist nur mit **11–17 MHz bei 5 V** spezifiziert, die
    Zeitbasis ist also ungenau — `FOSC` muss man dann nachziehen (siehe §4).
  * Für alles, was echte Zeiten braucht (UART, PWM), einen 11,0592-MHz-Quarz
    zwischen Pin 19 und 18 setzen, je 22–47 pF von jedem Pin nach Masse, und
    `FOSC=11059200` verwenden.
* **Kein `EA`-Pin**, der auf High müsste — siehe §1.

### 3.3 Die Programmierverbindung

Drei Leitungen. **TX und RX werden gekreuzt.**

```
   USB-TTL-Adapter              STC12C5A60S2
   ───────────────              ────────────
        TXD  ──────────────────▶  Pin 10   P3.0 / RxD
        RXD  ◀──────────────────  Pin 11   P3.1 / TxD
        GND  ──────────────────   Pin 20   GND
        5V   ──────────────────   Pin 40   VCC   (siehe Hinweis)
```

> [!CAUTION]
> **Genau eine Spannungsquelle wählen.** Entweder versorgt der Adapter den MCU
> mit seinen 5 V (für zwei LEDs völlig ausreichend, der Verbrauch liegt bei
> wenigen mA), *oder* ein separates 5-V-Netzteil. Niemals beides gleichzeitig.
> Bei separatem Netzteil trotzdem GND des Adapters mit GND des MCU verbinden —
> sonst hat die serielle Verbindung keinen gemeinsamen Bezugspunkt und der
> Handshake kommt nie zustande.

Wer aus dem Adapter versorgt, macht den „Power Cycle“ aus §2.3 einfach durch
kurzes Abziehen der 5-V-Steckbrücke — das ist der sauberste Weg.

### 3.4 Die LEDs

**Low-aktiv** verdrahtet: der MCU zieht den Strom nach Masse, VCC liefert ihn.

```
                                        ┌──────────────┐
   +5V ──[ 1k ]──▶|── LED1 ─────────────┤  1   P1.0    │
                                        │              │
   +5V ──[ 1k ]──▶|── LED2 ─────────────┤  2   P1.1    │
                                        └──────────────┘

   Anode ──[ Widerstand ]── +5V      Kathode ── MCU-Pin
```

Eine **0** auf dem Pin lässt die LED leuchten, eine **1** schaltet sie aus.

Warum herum? Wegen der Asymmetrie aus §1: ein quasi-bidirektionaler Pin liefert
rund 230 µA, was für eine sichtbar leuchtende LED nicht reicht — nach Masse
senkt er dagegen problemlos 20 mA. STCs eigenes Datenblatt (§4.6) schreibt
genau das vor: *„Für schwach hochziehende / quasi-bidirektionale I/O den
Senkenstrom zum Treiben der LED benutzen, Vorwiderstand größer als 1 kΩ,
mindestens jedoch 470 Ω.“*

Der Code schaltet P1.0/P1.1 zusätzlich in den **Gegentaktmodus**. An der
Verdrahtung ändert das nichts — es macht nur den Aus-Zustand zu einem harten
VCC statt einem schwachen Pull-up, sodass die LEDs richtig dunkel werden.

> Wer die LEDs lieber andersherum verdrahtet (Pin → Widerstand → LED → GND,
> also „high-aktiv“), kann das tun — *weil* der Code Gegentakt einstellt. Dann
> müssen aber `LED_ON` / `LED_OFF` in `include/board.h` vertauscht werden, und
> die LEDs leuchten nach jedem Reset für ein paar Millisekunden, bis
> `board_init()` durchgelaufen ist.

### 3.5 Kontrolle vor dem Einschalten

Vor dem Einstecken von USB mit dem Multimeter prüfen:

- [ ] Pin 40 ↔ Pin 20: **kein** Kurzschluss.
- [ ] Pin 9 (RST) liegt im Betrieb bei ~0 V.
- [ ] GND des Adapters und GND des MCU sind dasselbe Netz.
- [ ] TXD des Adapters geht auf Pin 10, RXD auf Pin 11 (gekreuzt, nicht gerade).
- [ ] Jede LED hat einen Vorwiderstand in Reihe.

Dann, noch ganz ohne geflashten Code:

```bash
make info
```

Auf Aufforderung die Spannung aus- und einschalten. Wenn Chiptyp,
Bootloader-Version und MCU-ID ausgegeben werden, stimmen Verdrahtung *und*
Toolchain — der schwierige Teil ist damit erledigt.

---

## 4. Das Programm

[`src/01-blink/main.c`](src/01-blink/main.c) lässt die beiden LEDs sechsmal
abwechselnd blinken und danach zweimal gemeinsam im Sekundentakt — endlos. Die
1-Hz-Phase ist bewusst so gebaut, dass man eine Uhr danebenhalten und die
Taktannahme überprüfen kann.

```c
#include "board.h"
#include "delay.h"

void main(void)
{
    unsigned char i;

    board_init();
    delay_init();

    for (;;) {
        /* Phase 1: sechsmal abwechselnd, je 150 ms. */
        for (i = 0; i < 6; i++) {
            LED1 = LED_ON;  LED2 = LED_OFF;  delay_ms(150);
            LED1 = LED_OFF; LED2 = LED_ON;   delay_ms(150);
        }

        /* Phase 2: zweimal beide gemeinsam, 1 s an / 1 s aus. */
        for (i = 0; i < 2; i++) {
            LED1 = LED_ON;  LED2 = LED_ON;   delay_ms(1000);
            LED1 = LED_OFF; LED2 = LED_OFF;  delay_ms(1000);
        }
    }
}
```

### Was die beiden Header tun

**[`include/board.h`](include/board.h)** — sämtliche Hardware-Fakten an einer
Stelle:

```c
#define LED1    P1_0        /* PDIP-40 Pin 1 */
#define LED2    P1_1        /* PDIP-40 Pin 2 */
#define LED_ON  0           /* low-aktiv: der MCU senkt den Strom */
#define LED_OFF 1

static void board_init(void)
{
    P1M1 &= ~0x03;          /* M1 = 0 ┐                          */
    P1M0 |=  0x03;          /* M0 = 1 ┴─ P1.0, P1.1 auf Gegentakt */
    LED1 = LED_OFF;
    LED2 = LED_OFF;
}
```

`P1_0` und `P1M0` kommen aus dem von SDCC mitgelieferten `<stc12.h>`, das
`board.h` einbindet. Es gibt kein Hersteller-SDK zu installieren.

**[`include/delay.h`](include/delay.h)** — eine Millisekundenverzögerung auf
Basis von **Timer 0** statt einer Warteschleife. Damit hängt die Genauigkeit
nur an `FOSC_HZ` und nicht daran, wie SDCC gerade Lust hatte, eine `for`-Schleife
zu übersetzen:

```c
#define T0_RELOAD (65536UL - (FOSC_HZ / 12UL / 1000UL))

static void delay_init(void)
{
    AUXR &= ~0x80;                  /* T0x12 = 0 -> Timer 0 läuft mit FOSC/12 */
    TMOD  = (TMOD & 0xF0) | 0x01;   /* Timer 0, Modus 1 (16 Bit)              */
}

static void delay_ms(unsigned int ms)
{
    while (ms--) {
        TL0 = (unsigned char)(T0_RELOAD & 0xFF);
        TH0 = (unsigned char)(T0_RELOAD >> 8);
        TF0 = 0;
        TR0 = 1;
        while (!TF0) ;              /* auf den Überlauf des 16-Bit-Zählers warten */
        TR0 = 0;
        TF0 = 0;
    }
}
```

Der STC12-Kern ist 1T und *könnte* Timer 0 direkt mit FOSC takten. Wir behalten
aber bewusst den klassischen Vorteiler ÷12 (`AUXR.T0x12 = 0`), damit eine
Millisekunde bei jedem sinnvollen Takt noch in einen 16-Bit-Nachladewert passt.
Bei 11,0592 MHz sind das 11059200 ÷ 12 ÷ 1000 = 921 Schritte, also
`T0_RELOAD` = 65536 − 921 = 64615 = `0xFC67` — und genau das steht am Ende auch
im erzeugten Assemblercode.

### `FOSC` justieren

Ohne bestückten Quarz liegt der interne RC-Oszillator irgendwo zwischen 11 und
17 MHz, die „eine Sekunde“ stimmt dann nicht. Also die 1-Hz-Phase stoppen und
neu bauen:

```bash
make flash FOSC=13000000
```

`FOSC` wird als `-DFOSC_HZ=…UL` an den Compiler durchgereicht — es muss also
keine Datei bearbeitet werden.

---

## 5. Bauen und flashen

```bash
make                    # kompilieren -> build/stc12c5a60s2/01-blink/01-blink.hex
make ports              # welches /dev/cu.* ist der Adapter?
make flash              # bauen + flashen, dann auf Aufforderung Spannung aus/ein
```

Erwartete Ausgabe:

```
$ make
sdcc -mmcs51 --std-c99 --iram-size 256 --xram-size 1024 --code-size 61440 \
     -I include -DFOSC_HZ=11059200UL -o build/stc12c5a60s2/01-blink/ src/01-blink/main.c
packihx build/stc12c5a60s2/01-blink/main.ihx > build/stc12c5a60s2/01-blink/01-blink.hex
packihx: read 19 lines, wrote 27: OK.
built build/stc12c5a60s2/01-blink/01-blink.hex
```

308 Byte Code, von 61440 verfügbaren.

```
$ make flash PORT=/dev/cu.usbserial-1420
Flashing build/stc12c5a60s2/01-blink/01-blink.hex via /dev/cu.usbserial-1420 ...
>>> Power-cycle the MCU now (unplug/replug its VCC) <<<
Waiting for MCU, please cycle power: done
Target model:
  Name: STC12C5A60S2
  Magic: F002
  Code flash: 60.0 KB
  EEPROM flash: 1.0 KB
Loading flash: 308 bytes
Switching to 115200 baud: done
Erasing flash: done
Writing flash: 308/308 bytes
Setting options: done
Disconnected!
```

### Übersetzen, ohne etwas zu installieren

Wer SDCC nicht installieren will — oder das Ganze aus einem Browser heraus
ansteuert — findet unter **<https://stc-compiler.vercel.app>** einen gehosteten
Compiler, der genau die oben beschriebene Toolchain ausführt:

```bash
./tools/compile-remote.sh                  # 01-blink -> 01-blink.hex
FOSC=12000000 ./tools/compile-remote.sh    # Takt überschreiben
```

```
==> Amalgamating 01-blink ...
==> Compiling via https://stc-compiler.vercel.app (FOSC=11059200) ...
wrote 01-blink.hex (740 bytes)
  ROM/EPROM/FLASH 0x0000 0x00ed 238 61440
```

Geflasht wird danach ganz normal mit `stcgal`.

Der Dienst übersetzt **eine einzige Übersetzungseinheit**. `src/01-blink/main.c`
lässt sich also nicht direkt hinschicken — das scheitert an `board.h: No such
file`. Das Skript nimmt einem das ab: es entfernt die lokalen
`#include "..."`-Zeilen und fügt `include/board.h`, `include/delay.h` und das
Beispiel zu einer Datei zusammen. System-Includes wie `<stc12.h>` bleiben
unangetastet, die hat der Server über SDCC selbst.

Der Quelltext liegt in [`CrispStrobe/stc-compiler`](https://github.com/CrispStrobe/stc-compiler).
Den Dienst gibt es, weil das BrickWright-Backend aus [docs/ROADMAP.de.md](docs/ROADMAP.de.md)
aus dem Browser heraus übersetzen muss, wo SDCC nicht laufen kann — aus
demselben Grund, aus dem es `legacy-lego-compiler` für NXT- und EV3-Bytecode
gibt.

### Dasselbe als Pseudocode

Dieselben Programme gibt es auch als **Pseudocode** in [`pseudocode/`](pseudocode/),
in dem Dialekt, den BrickWright verwendet — GROSSBUCHSTABEN für Struktur und
Kontrollfluss, Kleinbuchstaben für Anweisungen, Einrückung für Verschachtelung:

```
DEVICE STC12C5A60S2:
  CLOCK 11059200

  PIN led1 = P1.0 OUTPUT ACTIVE LOW      # PDIP-40 Pin 1
  PIN led2 = P1.1 OUTPUT ACTIVE LOW      # PDIP-40 Pin 2

  WHEN started:
    FOREVER:
      turn on led1
      turn off led2
      wait 0.15 seconds
      turn off led1
      turn on led2
      wait 0.15 seconds
```

```bash
./tools/compile-remote.sh -p 01-blink        # -> 01-blink.hex + 01-blink.c
./tools/compile-remote.sh -p 02-button       # Taster schaltet die LEDs um
./tools/compile-remote.sh -p 03-potentiometer
```

Das Skript legt den erzeugten C-Code neben die `.hex`, es bleibt also nichts
verborgen — die Ausgabe hat dieselbe Form wie die handgeschriebenen Beispiele
in `src/`.

Entscheidend ist `ACTIVE LOW`: weil ein quasi-bidirektionaler Pin 20 mA nach
Masse senkt, aber nur ca. 230 µA liefert (§1), werden LEDs low-aktiv
verdrahtet und `turn on` muss eine `0` ausgeben. Wird die Polarität einmal
deklariert, sagt der Rest des Programms genau das, was er meint. Die
vollständige Grammatik — `DEFINE`-Prozeduren, `WHILE`, `REPEAT UNTIL`,
`wait until`, `ANALOG`-Pins, die über den ADC gelesen werden — steht in der
[stc-compiler-README](https://github.com/CrispStrobe/stc-compiler#pseudocode).

Das ist die vordere Hälfte des BrickWright-Backends aus
[docs/ROADMAP.de.md](docs/ROADMAP.de.md): sobald Blöcke diesen Pseudocode
erzeugen, steht die gesamte Kette vom Scratch-Projekt bis zum geflashten Chip.

### Alle make-Ziele

| Ziel | Tut |
|---|---|
| `make` | Übersetzt nach `build/$(EXAMPLE)/$(EXAMPLE).hex` |
| `make flash` | Übersetzt und flasht dann über ISP |
| `make info` | Liest Chip-ID und Optionen, flasht nichts — der beste Verbindungstest |
| `make erase` | Löscht den Flash |
| `make ports` | Listet mögliche serielle Geräte |
| `make size` | Zeigt SDCCs Speicheraufteilung |
| `make clean` | Löscht `build/` |

### Alle Stellschrauben

| Variable | Standard | Bedeutung |
|---|---|---|
| `EXAMPLE` | `01-blink` | Welches Verzeichnis unter `src/` gebaut wird |
| `PART` | `stc12c5a60s2` | Ziel-Chip — auch `stc15f2k60s2`. Bestimmt XRAM-Größe, stcgal-Protokoll und Bauverzeichnis |
| `FOSC` | `11059200` | Takt in Hz — muss der Realität entsprechen, sonst stimmen die Zeiten nicht |
| `PORT` | erstes passendes `/dev/cu.*` | Serielles Gerät |
| `BAUD` | `115200` | Übertragungsrate. Bei zickigem Flashen auf `19200` senken |
| `HANDSHAKE` | `2400` | Handshake-Rate des Bootloaders — nicht anfassen |
| `PROTOCOL` | `stc12` | stcgal-Protokoll. `auto` geht auch |

### Flashen aus dem Browser, ohne Terminal

<https://crispstrobe.github.io/stc-compiler/> übersetzt Pseudocode direkt in der
Seite und kann das Ergebnis über Web Serial auf ein Board schreiben — einen
STC12 über dessen ISP (samt kaltem Einschalten), einen ATmega über den
Arduino-Bootloader, einen micro:bit über die MicroPython-REPL. Nur Chrome oder
Edge; Web Serial verlangt einen sicheren Kontext, den die gehostete Seite hat
und eine lokale Kopie der Datei nicht.

**Keiner dieser drei Wege hat bisher echte Hardware programmiert.** Sie werden
gegen Simulatoren entwickelt, und der STC-Weg wird Byte für Byte gegen ein
Protokoll geprüft, das `stcgal` selbst erzeugt hat — das belegt, dass die Bytes
stimmen, und nichts darüber, ob es auf der Leitung funktioniert. `make flash`
bleibt der Weg, der nachweislich geht. [docs/BENCH-FLASHING.md](docs/BENCH-FLASHING.md)
beschreibt das Vorgehen, um das zu klären, und nennt für jedes Board den
wahrscheinlichsten Verdacht. (Nur auf Englisch: es ist ein Prüfvertrag für eine
Implementierung, kein Einstiegsdokument.)

---

## 6. Fehlersuche

**`Waiting for MCU, please cycle power:` läuft nie durch**

Der häufigste Fehler, und fast immer eine dieser fünf Ursachen:

1. **TX/RX nicht gekreuzt.** TXD des Adapters an Pin 10, RXD an Pin 11. Einfach
   tauschen und nochmal probieren — kaputtgehen kann dabei nichts.
2. **Reset gedrückt statt Spannung getrennt.** Der ISP-Monitor läuft nur nach
   einem *Kaltstart*. VCC muss wirklich unterbrochen werden.
3. **Auf `/dev/tty.*` unterwegs.** `/dev/cu.*` benutzen.
4. **Keine gemeinsame Masse** zwischen Adapter und MCU.
5. **Der Port ist noch woanders offen** — ein serieller Monitor, `screen`, die
   Arduino-IDE. Schließen.

**Die Verbindung steht, das Schreiben bricht mittendrin ab**

Übertragungsrate senken: `make flash BAUD=19200`. Und das USB-Kabel prüfen —
reine Ladekabel sind ein Klassiker unter den Zeitfressern.

**Sauber geflasht, aber die LEDs tun nichts**

* Sind die LEDs verkehrt herum drin? Die Kathode (abgeflachte Seite / kurzes
  Bein) gehört an den **MCU-Pin**, die Anode über den Widerstand an **+5 V**.
* Liegt RST (Pin 9) tatsächlich auf Low? Wenn er hochfloatet, bleibt der Chip
  im Reset.
* Prüfen, ob der Chip überhaupt lebt: `make info` funktioniert auch bei
  komplett falscher LED-Verdrahtung.

**Die LEDs leuchten, aber dunkel — oder eine deutlich schwächer**

Vermutlich läuft der Port noch quasi-bidirektional und die LEDs werden aus dem
Pin gespeist. Prüfen, ob `board_init()` wirklich aufgerufen wird und ob die LEDs
wie in §3.4 low-aktiv verdrahtet sind.

**Die Blinkfrequenz stimmt nicht**

`FOSC` passt nicht zum tatsächlichen Takt. Siehe §4.

**`sdcc: command not found` trotz `brew install sdcc`**

Auf Apple Silicon installiert `brew` nach `/opt/homebrew/bin` — das muss im
`PATH` liegen. `stcgal` aus `pipx` landet in `~/.local/bin`: einmal
`pipx ensurepath` ausführen und eine neue Shell öffnen.

---

## 7. Aufbau des Repositories

```
.
├── Makefile                 bauen / flashen / löschen / auslesen
├── include/
│   ├── board.h              Pinbelegung, LED-Polarität, Portmodus-Setup
│   ├── delay.h              Millisekunden-Verzögerung über Timer 0
│   ├── live-proto.h         das Wireformat der Debug-Verbindung
│   ├── live-frame.h         dessen Codec — reines C, damit der Host ihn testen kann
│   └── live-sfr.h           das kuratierte SFR-Fenster (siehe unten)
├── src/
│   ├── 01-blink/main.c      das C-Beispiel
│   ├── 02-adc/main.c        ADC-Prüfung — auf Silizium UNGEPRÜFT
│   └── 10-live-firmware/    On-Chip-Debug-Monitor — auf Silizium UNGEPRÜFT
├── pseudocode/
│   └── *.bw                 dasselbe als BrickWright-Pseudocode
├── tests/
│   └── frame_test.c         der Codec, auf dem Host getestet: make test
├── tools/
│   ├── setup-macos.sh       installiert sdcc + stcgal
│   ├── find-port.sh         findet das serielle Gerät
│   ├── compile-remote.sh    baut über den gehosteten Compiler, ohne SDCC
│   └── live-monitor.py      das Host-Ende der Debug-Verbindung
└── docs/
    ├── PINOUT.md            vollständige Pin- und SFR-Referenz  (de: PINOUT.de.md)
    ├── ROADMAP.md           der Plan für die BrickWright-Erweiterung  (de: ROADMAP.de.md)
    ├── STC12-PERIPHERAL-MODEL.md   was dieser Chip tut — der gemeinsame Vertrag
    ├── DEBUG-CONTROL-MODEL.md      Ablaufsteuerung, für Emulatoren und für Silizium
    └── BENCH-FLASHING.md           die Browser-Flasher auf Silizium prüfen
```

### Debuggen auf echtem Silizium

Dieser Baustein hat keine On-Chip-Debug-Einheit und keinen `PSEN`-Pin. Der Weg
von Keils Monitor-51 — einen Trap-Opcode in den Programmspeicher schreiben —
ist hier also nicht bloß unbequem, sondern gar nicht baubar.
`src/10-live-firmware` tut das, was das Silizium zulässt: Es hält an den
Yield-Punkten des kooperativen Schedulers an, geht dort schrittweise weiter,
setzt dort Haltepunkte und liest Speicher, Register und Position über UART1.
`docs/DEBUG-CONTROL-MODEL.md` sagt genau, welche Debugger-Funktionen das
überleben und welche nicht (nur auf Englisch — interne Spezifikation).

Der Framing-Codec ist reines C ohne SFR-Zugriffe. `make test` prüft damit
genau den Parser, der auch auf dem Chip läuft, und vergleicht ihn mit einer
unabhängigen Python-Implementierung in `tools/live-monitor.py`. **Die Firmware
selbst lief noch nie auf Hardware.**

---

## 8. Wohin das führt

Zwei blinkende LEDs sind Schritt null. Der Plan ist, daraus einen kleinen,
gut verstandenen Satz von Primitiven wachsen zu lassen — GPIO, PWM, ADC, UART,
Timer — und genau diese Primitive dann als **BrickWright-Blöcke** anzubieten,
die nach C transpilieren und über dieselbe SDCC-plus-stcgal-Kette wie oben zu
einer `.hex` werden.

BrickWright transpiliert bereits verlustfrei in beide Richtungen zwischen
Scratch-Blöcken, Pseudocode, Python und JavaScript und hat Backends für LEGO-NXT-
und EV3-Bytecode. Das STC12-Backend ist dieselbe Idee, gerichtet auf einen
nackten 8051:

```
   Scratch-Blöcke ──▶ BrickWright-IR ──▶ C (SDCC) ──▶ .ihx ──▶ stcgal ──▶ Chip
```

Der vollständige Entwurf — Blockvokabular, IR-Abbildung, Ressourcenzuteilung
und wie das Flashen aus dem Browser heraus angesteuert wird — steht in
[docs/ROADMAP.de.md](docs/ROADMAP.de.md).

### 8.1 Eine Familie ist nicht die andere: die 1T/12T-Falle

Der STC12C5A60S2 passt **Pin für Pin in einen STC89C52-Sockel** — Versorgung,
Masse und die Standard-I/O liegen gleich, und umgekehrt gilt dasselbe. Der
Haken ist die Zeit, nicht die Verdrahtung: STC12 (und STC15) sind
**1T**-Kerne, der STC89 und jeder klassische 8051 sind **12T**. Code, der
Zyklen zählt — geschachtelte `for`-Warteschleifen, mit `_nop_()` getaktetes
Bit-Banging von I2C/SPI/1-Wire — läuft nach dem Tausch grob **6–12× zu
schnell** und scheitert an echter Peripherie (ein DS18B20 antwortet nicht
auf einen 1-Wire-Reset, der zwölfmal zu kurz ist).

Zwei Konsequenzen stecken im Werkzeug:

- **Alles aus Pseudocode Erzeugte hängt an Timer 0 mit FOSC/12**, einem
  Modus, den 12T- wie 1T-Kerne identisch zählen — dasselbe Programm ist
  damit auf `STC12C5A60S2`, `STC89C52RC` und `STC15F2K60S2` zeitkorrekt
  (alle drei sind gültige `DEVICE`-Angaben; der Emitter weiß, welche
  Port-Modus-Register, das AUXR-1T-Bit oder einen ADC haben).
- **Der Keil-Übersetzer warnt**, wenn migrierter Code Software-Warteschleifen
  oder `_nop_()`-Ketten enthält — genau diese Falle.

---

## Quellen und Vorarbeiten

* [STC12C5A60S2 / STC12LE5A60S2 Datenblatt (Englisch, 15.07.2011)](https://www.stcmicro.com/datasheet/STC12C5A60S2-en.pdf) — maßgebliche Quelle für jede Zahl in §1
* [grigorig/stcgal](https://github.com/grigorig/stcgal) — das ISP-Flashprogramm (MIT)
* [SDCC](https://sdcc.sourceforge.net/) — der Compiler; liefert `mcs51/stc12.h` mit (GPL-2+ mit Linking-Ausnahme)
* [rgm3/ledcube444](https://github.com/rgm3/ledcube444) — 4×4×4-LED-Würfel auf genau diesem Chip, und Ursprung des DTR-Autoreset-Tricks (MIT)
* [tomazas/ledcube8x8x8](https://github.com/tomazas/ledcube8x8x8) — 8×8×8-Würfel, größere SDCC-Codebasis (MIT)
* [kabirz/c51_sdcc](https://github.com/kabirz/c51_sdcc) — CMake-Gerüst für SDCC + STC12 (MIT)

> **Zur Lizenzlage von fremdem STC-Code.** Der allermeiste STC12C5A60S2-Code auf
> GitHub steht **unter gar keiner Lizenz** — darunter
> `Alpha02/MCU_STC12C5A60S2Lib`, `zeimao77/stc12` und
> `cch1997/SDCC-STC-header-file`. „Keine Lizenz“ heißt: alle Rechte vorbehalten,
> also nicht übernehmbar. STC MCU Limited veröffentlicht ebenfalls kein
> Open-Source-Repository — nur das Datenblatt als PDF und das proprietäre,
> Windows-only STC-ISP.exe. Deshalb stützt sich dieses Repository für die
> Registerdefinitionen auf **SDCCs eigenes `stc12.h`**: sauber lizenziert, mit
> dem Compiler mitgeliefert, und die Adressen decken sich exakt mit dem
> Datenblatt. Alles Übrige hier ist aus dem Datenblatt heraus geschrieben — und
> das sind Fakten, kein Code.

## Lizenz

MIT — siehe [LICENSE](LICENSE).
