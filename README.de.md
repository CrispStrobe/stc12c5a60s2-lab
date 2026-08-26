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
[docs/ROADMAP.md](docs/ROADMAP.md) (englisch).

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
9. [Erstes Silizium — was jetzt auf echter Hardware verifiziert ist](#9-erstes-silizium--was-jetzt-auf-echter-hardware-verifiziert-ist)

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
> jedem Modus funktioniert. §4.1 sagt: „the whole chip had better drive
> lower than **120 mA**". ~80 mA pro Port ist 8051-Familienrichtlinie (nicht
> in diesem Datenblatt). Ca. 10 mA pro LED einplanen oder multiplexen.

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

> [!NOTE]
> **Für einen STC12 ist die Wahl womöglich enger als diese Tabelle.** stcgals
> FAQ führt eine eigene Liste getesteter Brückenchips, und darin ist **CP2102
> der einzige, bei dem macOS als getestete Plattform steht**; CH340/CH341 sind
> nur für Windows und Linux vermerkt. Der STC12-Bootloader verlangt **gerade
> Parität** (§6), anders als die STC89-Familie. Am 26.08.2026 nahm ein CH341T
> (`1a86:5523`) unter macOS 26 eine 8E1-Konfiguration an und bestand einen
> byte-genauen Loopback-Test; derselbe UART-Pfad identifizierte einen STC89.
> Das verifiziert den lokalen Treiber und die Verdrahtung, aber nicht die
> STC12-Kompatibilität: eine STC12-Bootloader-Antwort blieb aus.

Manche Module haben **zwei** Jumper: einen für die Spannung (3,3 V / 5 V) und
einen für die Betriebsart (TTL / I²C). Beim CH341T-Modul müssen beide stimmen —
**5 V** und **TTL**. Bei 3,3 V liegt der STC12 unter seinen 3,5 V Minimum, und
sein V<sub>IH</sub> von 0,7 × VCC = 3,5 V wäre ohnehin nicht mehr erreicht.

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
3. `stcgal` fängt den Handshake ab, handelt eine Übertragungsrate aus und
   schreibt den Flash.

> [!IMPORTANT]
> **2400 ist stcgals Vorgabe, nicht die einzig gültige Rate.** Erfolgreiche
> Sitzungen mit genau diesem Typ gibt es bei 2400, 4800 und 9600 Baud. Ein
> Bericht brauchte `-l 9600`, während `my1stcflash` 2400 verwendet. Mit der
> Vorgabe beginnen und bei ausbleibender Erkennung
> `HANDSHAKE=9600 BAUD=9600` probieren; Näheres in §6.

Wer Schritt 2 automatisieren will, führt die **DTR**-Leitung des Adapters auf
einen Transistor, der VCC des MCU schaltet, und hängt `-a` an den
stcgal-Aufruf — bei [rgm3/ledcube444](https://github.com/rgm3/ledcube444) gibt
es ein Foto genau dieser Bastelei. Bis dahin reicht ein kleiner Schalter oder
schlicht das Abziehen der VCC-Leitung. Nicht jedes Modul führt DTR überhaupt
heraus: ein CH341T-Modul hat nur TXD/RXD/GND/VCC (plus SDA/SCL), damit ist `-a`
dort keine Option. Ob Steuerleitungen vorhanden sind, verrät ein Blick auf
`serial.Serial(...).cts` / `.dsr` — stehen alle auf `False`, gibt es sie nicht.

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
| *opt.* | 1-kΩ-Widerstand — in Reihe zu TXD des Adapters |
| *opt.* | 470-Ω-Widerstand — VCC-GND-Entladung gegen Phantomversorgung |
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
  Low gehalten werden. Unterhalb von 12 MHz genügt wegen des internen
  Power-on-Resets ein 1-kΩ-Widerstand nach Masse. Auch die traditionelle
  Schaltung mit 10 kΩ nach GND und 10 µF nach VCC ist high-aktiv: sie erzeugt
  beim Einschalten einen kurzen High-Puls. Sie steht in STCs
  Applikationsschaltung, doch ein RST-Puls startet ISP nicht; dafür braucht es
  weiterhin ein echtes Einschalten.
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
        TXD  ──[ 1k optional ]─▶  Pin 10   P3.0 / RxD
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

Bei Versorgung aus dem Adapter bleibt Pin 40 dauerhaft mit der Ziel-VCC-Schiene
verbunden; geschaltet wird die 5-V-Zuleitung des Adapters *vor* dieser Schiene.
UART-Pins können den MCU trotz abgezogener 5-V-Zuleitung phantomversorgen. Ein
1-kΩ-Widerstand in TXD und ein 470-Ω-Entladewiderstand von Ziel-VCC nach GND
sind am Steckbrett hilfreich. Vor einem Kaltstart-Test an Pin 40 gegen Pin 20
messen: die Spannung muss deutlich unter 3,5 V, vorzugsweise unter 1 V fallen.
Eine umgesteckte Brücke allein beweist nicht, dass der Chip aus war.

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
rund 230 µA (§4.1), was für eine sichtbar leuchtende LED nicht reicht — nach
Masse senkt er dagegen problemlos 20 mA. §4.6 schreibt
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
  Magic: D17E
  Code flash: 60.0 KB
  EEPROM flash: 2.0 KB
Loading flash: 308 bytes
Switching to 115200 baud: done
Erasing flash: done
Writing flash: 308/308 bytes
Setting options: done
Disconnected!
```

`stcgal` meldet für Magic `D17E` 2 KB, STCs Auswahltabelle dagegen 1 KB
Benutzer-EEPROM. Das Beispiel oben folgt dem Werkzeug, die Funktionsübersicht
der Herstellertabelle. Bis zur Prüfung auf STC12-Silizium ist die Abweichung
konservativ zu behandeln. Das alte Beispiel zeigte hier fälschlich den
STC89-Eintrag `F002`.

### Übersetzen, ohne etwas zu installieren

Wer SDCC nicht installieren will — oder das Ganze aus einem Browser heraus
ansteuert — findet unter **<https://stc-compiler.vercel.app>** einen gehosteten
Compiler:

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

**Es ist nicht derselbe Compiler wie oben, und das Abbild ist nicht dasselbe
Abbild.** Der gehostete Dienst benutzt **SDCC 4.0.0**, `brew install sdcc`
liefert **4.5.0**. Das ist dort Absicht — der Dienst läuft auf einem Host mit
glibc 2.34, und 4.5.0 verlangt GLIBC 2.36 und startet dort nicht —, bedeutet
aber, dass ein entfernt und ein lokal übersetztes Programm unterschiedliche
Firmware sind. Gemessen an `01-blink`, gleicher C-Code, gleiche Schalter:

| übersetzt von | Größe |
|---|---|
| lokal, SDCC 4.5.0 | 996 Bytes |
| gehostet, SDCC 4.0.0 | 888 Bytes |

Beide funktionieren. Aber ein entferntes `.hex` nicht mit einem lokalen
vergleichen und daraus auf einen Fehler schließen — und ein entferntes Abbild
niemals mit einer lokal erzeugten Symboltabelle paaren. Die Seite unter
<https://crispstrobe.github.io/stc-compiler/> nennt deshalb jetzt den Compiler
neben der Byte-Zahl. In Arbeit ist SDCC als WebAssembly: das läuft im Browser,
hat keine glibc, an die es gebunden wäre, und dann sind beide Wege 4.5.0 und
dieser Hinweis erledigt sich.

Der Dienst übersetzt **eine einzige Übersetzungseinheit**. `src/01-blink/main.c`
lässt sich also nicht direkt hinschicken — das scheitert an `board.h: No such
file`. Das Skript nimmt einem das ab: es entfernt die lokalen
`#include "..."`-Zeilen und fügt `include/board.h`, `include/delay.h` und das
Beispiel zu einer Datei zusammen. System-Includes wie `<stc12.h>` bleiben
unangetastet, die hat der Server über SDCC selbst.

Der Quelltext liegt in [`CrispStrobe/stc-compiler`](https://github.com/CrispStrobe/stc-compiler).
Den Dienst gibt es, weil das BrickWright-Backend aus [docs/ROADMAP.md](docs/ROADMAP.md) (englisch)
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
[docs/ROADMAP.md](docs/ROADMAP.md) (englisch): sobald Blöcke diesen Pseudocode
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
| `HANDSHAKE` | `2400` | Handshake-Rate; bei fehlender Erkennung auch `4800` oder `9600` probieren |
| `PROTOCOL` | `stc12` | stcgal-Protokoll. `auto` geht auch |

### Flashen aus dem Browser, ohne Terminal

<https://crispstrobe.github.io/stc-compiler/> übersetzt Pseudocode direkt in der
Seite und kann das Ergebnis über Web Serial auf ein Board schreiben — einen
STC12 über dessen ISP (samt kaltem Einschalten), einen ATmega über den
Arduino-Bootloader, einen micro:bit über die MicroPython-REPL. Nur Chrome oder
Edge; Web Serial verlangt einen sicheren Kontext, den die gehostete Seite hat
und eine lokale Kopie der Datei nicht.

Der Browser-/Web-Serial-Pfad hat noch keine echte STC12-Hardware programmiert.
Er wird Byte für Byte gegen ein von `stcgal` erzeugtes Protokoll geprüft — das
belegt die Bytes, nicht die Leitung. Die JavaScript- und Rust-Flasher der
Kommandozeile haben dagegen beide einen STC89 auf echter Hardware programmiert;
siehe §9 und [docs/BENCH-FLASHING.md](docs/BENCH-FLASHING.md).

---

## 6. Fehlersuche

**`Waiting for MCU, please cycle power:` läuft nie durch**

Der häufigste Fehler, und fast immer eine dieser fünf Ursachen:

1. **TX/RX nicht gekreuzt.** TXD des Adapters an Pin 10, RXD an Pin 11. Einfach
   tauschen und nochmal probieren — kaputtgehen kann dabei nichts. Die
   Beschriftung mancher Module meint allerdings das *Ziel*, dann ist gerade
   richtig; siehe den STC12-Abschnitt weiter unten.
2. **Reset gedrückt statt Spannung getrennt.** Der ISP-Monitor läuft nur nach
   einem *Kaltstart*. VCC muss wirklich unterbrochen werden.
3. **Auf `/dev/tty.*` unterwegs.** `/dev/cu.*` benutzen.
4. **Keine gemeinsame Masse** zwischen Adapter und MCU.
5. **Der Port ist noch woanders offen** — ein serieller Monitor, `screen`, die
   Arduino-IDE. Schließen.

**Ein STC12 antwortet nicht, obwohl derselbe Adapter einen STC89 flasht**

Aus einer Bench-Sitzung am 09.08.2026, die **nicht** erfolgreich war: der Chip
hat nie geantwortet, und woran es lag, ist offen. Die Punkte unten sind
trotzdem belegt — jeder aus dem stcgal-Quelltext, dem Datenblatt oder fremden
Sitzungsmitschnitten. Sie stehen hier, weil sie zusammen mehrere Stunden
gekostet haben.

**Die YL-39-Jumper wählen kein STC12-Protokoll.** Das verfügbare
[YL-39-Handbuch](https://www.100y.com.tw/pdf_file/57-YL-39.pdf) ordnet JP1 der
8-LED-Reihe, JP2 der vierstelligen Anzeige und JP3 dem Summer zu. Sie verbinden
nur Peripherie und sollen bei Nichtbenutzung entfernt werden. Der getrennte
Schalter `51/AVR` gehört auf `51`. Programmiert wird mit der gewöhnlichen
STC-UART-Folge: Board ausschalten, in STC-ISP Download starten, kurz warten,
dann das Board einschalten. Bei fehlender Erkennung empfiehlt das Handbuch,
minimale und maximale Baudrate beide auf 2400 zu setzen.

Unter dieser Beschreibung werden deutlich verschiedene Boards verkauft. Das
Handbuch wirbt mit USB-Download für einen `STC52` und nennt eine
PL2303-Schnittstelle; das hier geprüfte Board und aktuelle Marktplatztexte
nennen einen CH340G und werben mit dem STC12C5A60S2. Kein gefundenes Dokument
beschreibt einen versteckten STC12-Jumper oder zweiten Programmieranschluss.
STC12-Unterstützung bedeutet daher Sockel-/Pin-Kompatibilität, Quarz,
Netzschalter und UART-Kompatibilität — keinen anderen Downloadmodus. Eine
verifizierte Käuferbewertung meldet zudem Boards, die nicht den Angebotsfotos
entsprachen; deshalb zählt die tatsächliche PCB-Revision, nicht Titel oder
KI-Zusammenfassung.

**Eine 40-polige STC/AT89-Entwicklungsplatine ist nicht automatisch ein
STC-Programmiergerät.** Bei Platinen wie dem DollaTek-ZIF-Minimalsystem ist die
beworbene 10-polige `ISP`-Buchse für den SPI-artigen Programmer des AT89S52.
Einen STC12 kann sie nicht programmieren. Für ihn sind ZIF-Sockel,
Netzschalter, Quarz-/Reset-Schaltung und herausgeführte Pins nützlich; ein
externer UART muss trotzdem P3.0/P3.1 erreichen, und die Platine muss bei
wartendem Loader kalt eingeschaltet werden.

**Der USB-Writer1A benutzt keine alternative Rettungsschnittstelle.** Abschnitt
2.2.17 des aktuellen STC12C5A60S2-Handbuchs zeichnet den Writer1A an MCU-VCC,
GND, P3.0/RxD und P3.1/TxD (mit den erforderlichen Taktbauteilen); Abschnitt
2.2.18 beschriftet dieselben Signale an seinem Programmieranschluss. Sein
40-poliger ZIF-Sockel, die gesteuerte Zielversorgung und die offizielle
STC-ISP-Integration automatisieren den gewöhnlichen ROM-UART-Bootloader. Damit
ist er eine wertvolle unabhängige Referenz, kann aber keinen Chip retten,
dessen ROM-Bootloader gar nicht anläuft.

**Nicht von einer einzigen Handshake-Rate ausgehen.** stcgal verwendet 2400
als Vorgabe, und das auf genau diesem Modell getestete `my1stcflash` ebenfalls.
stcgals Protokollmitschnitt verwendet 9600; die erfolgreiche Sitzung in
[stcgal#12](https://github.com/grigorig/stcgal/issues/12) verband sich zuerst
mit `-l 9600`, ihre spätere Baud-Matrix aber bei 2400, 4800 und 9600. Zuerst
die Vorgabe, dann Folgendes probieren:

```bash
make info HANDSHAKE=9600 BAUD=9600
```

**Der STC12-Bootloader spricht 8E1, der STC89 8N1.** Im stcgal-Quelltext:
`Stc12BaseProtocol.PARITY = serial.PARITY_EVEN` („Parity for error correction
was introduced with STC12"), demgegenüber `Stc89Protocol.PARITY = PARITY_NONE`
(„these don't use any parity"). Daraus folgt etwas Unangenehmes: **der in §9 auf
Silizium bewiesene CH340-Pfad hat nie Parität benutzt.** Er sagt über den
STC12 nichts aus. stcgals FAQ nennt als einzigen bekannten Totalausfall die
Raspberry-Pi-Mini-UART, ausdrücklich weil sie *„lacks parity support"* — und in
der dortigen Tabelle getesteter Brückenchips ist **CP2102 der einzige mit
macOS** in der Spalte. Wer selbst mitschneidet, muss den Port ebenfalls mit
`PARITY_EVEN` öffnen, sonst prüft er etwas anderes als stcgal.

**stcgal 1.10 gibt beim Warten keine Punkte aus.** `StcBaseProtocol.connect()`
schreibt `Waiting for MCU, please cycle power:` einmal und schweigt dann, bis
ein gültiges Statuspaket ankommt; Framing-Fehler werden stillschweigend
verworfen. Eine Ausgabe ohne Punkte ist der Normalzustand, kein Symptom — und
sie unterscheidet nicht zwischen „nichts kommt zurück" und „es kommt Müll
zurück". Wer das trennen will, schreibt sich sechs Zeilen, die `0x7f` senden
und *jedes* empfangene Byte melden. Eine gültige Antwort beginnt mit `46 B9`.

**Eine abgezogene VCC-Leitung beweist nicht, dass der Chip aus ist.** TXD kann
ihn über die Schutzdiode an RxD weiter speisen. In der Sitzung vom 26.08.2026
lagen an Pin 40 noch etwa 5 V, nachdem die 5-V-Zuleitung des Adapters entfernt
war. Wie stcgals FAQ empfiehlt: Serienwiderstand in TXD und nötigenfalls einen
Entladewiderstand unter 1 kΩ von Ziel-VCC nach GND verwenden. Pin 40 bleibt an
der Ziel-VCC-Schiene; geschaltet wird davor, damit Entladewiderstand und beide
Abblockkondensatoren am MCU bleiben. Am Chip prüfen: Pin 40 gegen Pin 20 muss
vor der nächsten steigenden Flanke unter den Arbeitsbereich fallen.

Ziel-GND mit einem richtigen Low-Side-MOSFET zu schalten ist eine weitere
dokumentierte Lösung. Eine beliebige GND-Steckbrücke von Hand zu ziehen ist
nicht gleichwertig, wenn LEDs, ISP-Eingangsbrücken, Quarzkondensatoren oder
andere Boardverdrahtung Rückwege schaffen.

> [!CAUTION]
> Die Versorgungsschienen **nicht** kurzschließen, um den Stützkondensator zu
> entladen, solange die 5-V-Leitung des Adapters noch steckt. Das ist ein
> satter Kurzschluss auf VBUS: der USB-Port schaltet ab, das Gerät
> verschwindet aus `/dev`, und pyserial quittiert mit
> `OSError: [Errno 6] Device not configured`.

**Die TXD/RXD-Beschriftung ist nicht verlässlich.** Manche CH34x-Module
beschriften die Leiste aus Sicht des *Ziels*; in
[ledcube8x8x8#8](https://github.com/tomazas/ledcube8x8x8/issues/8) funktionierte
nur TXD→TXD / RXD→RXD. Ein Loopback (die beiden Pins direkt verbinden, Bytes
zurücklesen) beweist, dass es ein UART-Paar ist — **nicht**, welcher Pin welcher
ist. Im Zweifel beide Richtungen probieren, und zwar bei 9600/8E1.

**Nach jedem Chipwechsel die echten IC-Beine auf Durchgang prüfen.** Ein
verbogener Pin kann eingesetzt aussehen und trotzdem die Feder im Steckbrett
verfehlen. Am 26.08.2026 schwieg der STC89-Positivtest nach einem Wechsel
vollständig, weil Pin 40 verbogen war. Die Steckbrettreihe sah messtechnisch
richtig aus, das freiliegende Metall von Pin 40 nicht. Stromlos muss jedes
Versorgungsbein zu seiner eigenen Steckbrettreihe ungefähr 0 Ω zeigen.

**Ein gebrauchter Chip kann auf externen Takt eingestellt sein.** Steht das
Optionsbyte `clock_source` auf `external` und sitzt kein Quarz an Pin 19/18,
läuft überhaupt nichts — auch der Bootloader nicht, und der Chip schweigt bei
tadellosen Gleichspannungen an jedem Pin. Jeder auffindbare Erfolgsbericht für
diesen Chip hatte einen Quarz bestückt (stcgal#12: 11,059 MHz; ledcube8x8x8#8:
23,846 MHz). §3.2 sagt „XTAL darf leer bleiben" — das gilt für einen
fabrikneuen Chip, nicht zwingend für einen vom Marktplatz.

**`bsl_pindetect_enabled` kann den ISP-Einstieg an P1.0/P1.1 knüpfen.** Ist das
Optionsbyte gesetzt, steigt der Bootloader nur ein, wenn beide Pins beim
Einschalten auf Low liegen. Zwei Drahtbrücken nach GND kosten nichts.

**Die Stromaufnahme ist die schnellste Lebendprüfung.** Multimeter im
mA-Bereich in Reihe zur 5-V-Leitung: **10–25 mA** = Oszillator läuft, CPU
arbeitet; **unter 1 mA** = kein Takt. Das trennt „falsch verdrahtet" von „Chip
läuft nicht" in einer einzigen Messung — schneller als jede Spannungsmessung an
Einzelpins, und es war in der Sitzung oben die Messung, die viel zu spät kam.
(Vorher am 1-kΩ-Widerstand gegen 5 V prüfen, dass der mA-Bereich überhaupt
misst: ~5 mA. Eine durchgebrannte Sicherung im mA-Pfad zeigt 0,0 an und ist von
„zieht nichts" nicht zu unterscheiden.)

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

**Ein STC89C52 antwortet auf `make info`, lässt sich aber nicht flashen
(`PROTOCOL=stc89`)**

Neuere STC89C52RC-Chargen tragen eine Bootloader-Revision, die das
`stc89`-Protokoll des Mainline-stcgal nicht kennt. Zuerst
`PROTOCOL=stc89a` probieren; kennt das installierte stcgal `stc89a` noch
nicht, stcgal aktualisieren (die Variante wurde erst nach einem lange
gepflegten Fork übernommen). Der Chip ist in Ordnung — es ist reine
Handshake-Dialektik, auch `PROTOCOL=auto` kann sie aushandeln.

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
│   ├── 02-adc/main.c        ADC-Prüfung — UNGEPRÜFT (braucht STC12-Silizium)
│   ├── 03…09,11-13/         auf STC89-Silizium verifizierte Beispiele:
│   │                        Blink, UART in beide Richtungen, Board-
│   │                        Erkundung, Matrix-Scan, 7-Segment, LED-Suche,
│   │                        Tastenfeld-auf-Anzeige, Punktmatrix, LCD-Probe,
│   │                        Timer-2-Baud bei 115200 — siehe §9
│   └── 10-live-firmware/    On-Chip-Debug-Monitor — UNGEPRÜFT (STC12)
├── pseudocode/
│   └── *.bw                 dasselbe als BrickWright-Pseudocode
├── tests/
│   └── frame_test.c         der Codec, auf dem Host getestet: make test
├── tools/
│   ├── setup-macos.sh       installiert sdcc + stcgal
│   ├── find-port.sh         findet das serielle Gerät
│   ├── compile-remote.sh    baut über den gehosteten Compiler, ohne SDCC
│   └── live-monitor.py      das Host-Ende der Debug-Verbindung
├── tools/stcbsl/            unser eigener ISP-Flasher in Rust (MIT) —
│                            Arbeitskopie; veröffentlicht unter
│                            github.com/CrispStrobe/stcbsl und auf
│                            crates.io als `stcbsl`
└── docs/
    ├── PINOUT.md            vollständige Pin- und SFR-Referenz  (de: PINOUT.de.md)
    ├── ROADMAP.md           der Plan für die BrickWright-Erweiterung  (nur englisch)
    ├── STC12-PERIPHERAL-MODEL.md   was dieser Chip tut — der gemeinsame Vertrag
    ├── DEBUG-CONTROL-MODEL.md      Ablaufsteuerung, für Emulatoren und für Silizium
    ├── BOARD-PRECHIN-A2.md         ein echtes Board, Pin für Pin vermessen
    ├── isp-captures/               byte-genaue ISP-Sitzungsprotokolle
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

Zwei blinkende LEDs waren Schritt null. Die Primitive — GPIO, PWM (PCA 8-Bit
und 16-Bit Compare/Match), ADC, UART, Timer — sind **gebaut und zwischen zwei
Emulatoren gegengeprüft**; seit den ersten Bench-Sitzungen (§9) sind GPIO,
UART und die Timer auf echtem STC89-Silizium verifiziert, während ADC und
PCA/PWM noch auf einen STC12 im Sockel warten (siehe `BENCH-ADC`,
`BENCH-PWM` in [docs/BENCH-SESSION.md](docs/BENCH-SESSION.md)).

Diese Primitive werden als **BrickWright-Blöcke** angeboten, die nach C
transpilieren und über dieselbe SDCC-plus-stcgal-Kette wie oben zu einer
`.hex` werden. Vierzehn Geräteblöcke (Servo, Motor, Relais, LCD, 7-Segment,
RGB-LED, NeoPixel, Matrix, Sensoren) haben echte C-Treiber; der Roundtrip
`Pseudocode → C → Pseudocode` ist ein verifizierter Fixpunkt über 54
Galerie-Beispiele.

```
   Scratch-Blöcke ──▶ BrickWright-IR ──▶ C (SDCC) ──▶ .ihx ──▶ stcgal ──▶ Chip
                                      └─▶ MicroPython ──▶ bw flash ──▶ Pi Pico
```

Die zweite Zeile ist nicht hypothetisch — siehe §9.1. Derselbe Pseudocode
erreicht über SDCC einen 8051 und über MicroPython einen RP2040; genau dafür
sitzt eine IR in der Mitte.

Der vollständige Entwurf — Blockvokabular, IR-Abbildung, Ressourcenzuteilung
und wie das Flashen aus dem Browser heraus angesteuert wird — steht in
[docs/ROADMAP.md](docs/ROADMAP.md) (englisch).

### 8.1 Eine Familie ist nicht die andere: die 1T/12T-Falle

Der STC12C5A60S2 passt **Pin für Pin in einen STC89C52-Sockel** — Versorgung,
Masse und die Standard-I/O liegen gleich, und umgekehrt gilt dasselbe. Der
Haken ist die Zeit, nicht die Verdrahtung: STC12 (und STC15) sind
**1T**-Kerne, der STC89 und jeder klassische 8051 sind **12T**. Code, der
Zyklen zählt — geschachtelte `for`-Warteschleifen, mit `_nop_()` getaktetes
Bit-Banging von I2C/SPI/1-Wire — läuft nach dem Tausch grob **6–12× zu
schnell** und scheitert an echter Peripherie (ein DS18B20 antwortet nicht
auf einen 1-Wire-Reset, der zwölfmal zu kurz ist).

Drei Konsequenzen stecken im Werkzeug:

- **Alles aus Pseudocode Erzeugte hängt an Timer 0 mit FOSC/12**, einem
  Modus, den 12T- wie 1T-Kerne identisch zählen — dasselbe Programm ist
  damit auf `STC12C5A60S2`, `STC89C52RC` und `STC15F2K60S2` zeitkorrekt
  (alle drei sind gültige `DEVICE`-Angaben; der Emitter weiß, welche
  Port-Modus-Register, das AUXR-1T-Bit oder einen ADC haben).
- **Eine Wartezeit unter einer Millisekunde wird abgelehnt, nicht gerundet.**
  Der Takt ist eine Millisekunde, `wait 0.4 ms` lässt sich also gar nicht
  ausdrücken. Früher wurde daraus eine Wartezeit von null — die Pause
  verschwand ersatzlos, die Schleife lief mit voller Geschwindigkeit, und
  nichts in der Ausgabe wies darauf hin. Seit dem 09.08.2026 bricht der
  Compiler stattdessen ab und nennt die Auflösung. Die Grenze schließt die
  **halbe Millisekunde ein**: auch `wait 0.5 ms` wird abgelehnt, denn beim
  Runden zur geraden Zahl wird aus 0,5 eine 0. Wer kürzer warten muss — etwa
  für die Lagenzeit eines POV-Würfels — braucht eine Mikrosekunden-Pause, die
  es noch nicht gibt; sie wird an einen Timer gebunden sein und nicht an eine
  gezählte Schleife, und zwar aus genau dem Grund, um den es in diesem
  Abschnitt geht.
- **Der Keil-Übersetzer warnt**, wenn migrierter Code Software-Warteschleifen
  oder `_nop_()`-Ketten enthält — genau diese Falle.

---

## 9. Erstes Silizium — was jetzt auf echter Hardware verifiziert ist

Am 17./18.08.2026 liefen die ersten Bench-Sitzungen, auf zwei
STC89C52RC-Boards (einem Minimalsystem und einem Prechin 普中51-单核-A2).
Alles Folgende ist damit von „zwischen zwei Emulatoren gegengeprüft" zu
**auf echtem Chip gemessen** aufgestiegen:

- **Die gesamte Werkzeugkette**: `make` → SDCC → stcgal über einen CH340,
  von macOS aus, ohne ein einziges Windows-Werkzeug. Flashen, Löschen,
  Identifizieren — Dutzende Sitzungen.
- **Timer**: Timer 0 bei FOSC/12, quarzgenau (1-Hz-Blinken, mit der
  Stoppuhr geprüft); Timer 1 in Modus 2 als 9600-Baud-Takt, UART in beide
  Richtungen — der Host liest die Ausgabe der Firmware über dasselbe Kabel
  zurück.
- **Timer 2 als Baudraten-Generator bei 115200** (`src/13-hello115`):
  Nachladewert `0xFFFD` = 11 059 200 / 32 / 3 — byte-genau am Host. Auf
  dieser Tatsache steht der geplante STC89-Port des Live-Monitors.
- **Blöcke auf Silizium**: BrickWright-Pseudocode → der gehostete
  Compiler → ein laufender Chip, einschließlich `PART KEYPAD4X4` und
  `PART 74HC595` des Dialekts auf vermessenen Pins. Tastenfeld,
  7-Segment, LED-Reihe und 8×8-Matrix des A2 wurden **von Firmware
  vermessen, nicht dem Datenblatt geglaubt** —
  [docs/BOARD-PRECHIN-A2.md](docs/BOARD-PRECHIN-A2.md) hält jede Messung
  fest, auch den Ausgangs-Freigabe-Jumper J24, der fünf dunkle
  Flash-Versuche gekostet hat.
- **Unser eigener Flasher**: [`stcbsl`](https://github.com/CrispStrobe/stcbsl)
  (MIT, Rust, [auf crates.io](https://crates.io/crates/stcbsl)) spricht
  das STC89-ISP-Protokoll vollständig bei 115200, gebaut aus byte-genauen
  Sitzungsmitschnitten ([docs/isp-captures/](docs/isp-captures/)) und
  gegen Silizium durch fünf echte Fehler debuggt — darunter der
  CH340-Treiber von macOS, der nackte termios-Baudwechsel stillschweigend
  ignoriert; pyserial verdeckt das mit einem `IOSSIOSPEED`-ioctl, und
  jedes Rust-Serienprogramm sollte davon wissen.
- **Drei sichtbar verschiedene CLI-Flash-Pfade am 26.08.2026**: der eingebaute
  JavaScript-Flasher der npm-CLI installierte einen Lauf über D3–D8; npm
  `--engine rust` installierte abwechselnd ungerade/gerade LEDs; und direktes
  `stcbsl flash program.bw` übersetzte Pseudocode mit seiner eingebetteten
  JavaScript-Engine, rief SDCC auf und installierte D1–D4/D5–D8 abwechselnd in
  einem Befehl. Jedes geänderte Bild wurde auf dem YL-39-STC89 beobachtet.

Die STC12-Sitzung vom 26.08.2026 hat **keinen** funktionierenden STC12-Pfad
nachgewiesen. Drei Chips mit der gemeinsamen Aufschrift
`12C5A60S2 35I-PDIP40 1901H4Y043` blieben im YL-39, im Prechin A2 und in
direkten CH341T-Steckbrettversuchen stumm. Die Untersuchung fand echte
Störgrößen: Phantomversorgung, einen indirekten/geteilten Versorgungspfad am
Steckbrett und nach vielen Wechseln einen verbogenen Pin 40. Die endgültige
CH341T-Schaltung identifizierte den STC89 durch 1 kΩ in TXD und 470 Ω zwischen
den Versorgungsschienen; die danach geplante STC12-Wiederholung wurde bewusst
abgebrochen. Die gemeinsame STC12-Charge ist damit verdächtig, aber weder
defekte Chips noch eine kaputte STC12-Protokollimplementierung sind bewiesen.
Bekannt funktionierende, fremd programmierte STC12 wurden absichtlich nicht
angerührt.

Noch offen, bis ein STC12 im Sockel steckt (die Sockel beider Boards
nehmen ihn pinkompatibel auf): der **ADC-Pfad** (`src/02-adc`),
**PCA/PWM**, der **BRT** und der **Live-Debug-Monitor**
(`src/10-live-firmware`). Diese bleiben als UNGEPRÜFT markiert, bis
dieser Bench-Tag kommt.

### 9.1 Eine zweite Architektur: der Raspberry Pi Pico (19.08.2026)

Der Dialekt ist nicht auf den 8051 festgelegt, und das hier ist der Beleg
dafür. Am 19.08.2026 lief auf einem **Raspberry Pi Pico** ein Taschenrechner,
geschrieben als BrickWright-Pseudocode — siebzehn direkt verdrahtete Tasten an
GP2–GP18 und ein **GME12864-70**-OLED (SH1106-Klasse, 128×64, I²C an
GP0/GP1). Die internen Pull-downs des Pico übernehmen die Arbeit: eine
gedrückte Taste liest HIGH, externe Widerstände sind nicht nötig.

Geflasht hat es das eigene CLI dieses Projekts, kein Hersteller-Werkzeug:

```bash
cd ../sb3-creator
node bin/bw.mjs flash examples/70-calculator/program.bw \
     --port /dev/cu.usbmodem11101
```

`bw flash` schreibt `main.py` über eine eigene Raw-REPL-Implementierung,
prüft den Schreibvorgang auf dem Gerät nach und startet es neu. Liest man die
Datei mit `mpremote` zurück, ist sie **bytegleich** mit dem, was
`bw transpile --to micropython` erzeugt — auf dem Chip läuft also genau das,
was der Dialekt generiert hat. Der Rechner wurde vom Besitzer auf der Werkbank
als funktionierend bestätigt.

Bis dahin waren vier Korrekturen am MicroPython-Backend nötig, jede auf dem
Chip gemessen statt aus dem Quelltext hergeleitet. Die überraschendste: der
Compiler von MicroPython entfernt `if False:` vollständig, sodass der
Dead-Yield-Trick, der eine Funktion ohne `yield` zum Generator macht,
stillschweigend eine gewöhnliche Funktion erzeugt, die `None` zurückgibt —
und jeder Aufruf über `yield from` stirbt mit
`TypeError: 'NoneType' object isn't iterable`, bevor das erste Bild gezeichnet
ist.

**Eine Falle, die Wiederholung verdient:** Beim Pico ist **Gehäuse-Pin 22
gleich GP17**. Gehäusenummerierung und GPIO-Nummerierung sind zwei
verschiedene Koordinatensysteme, und „Pin 22" ist mehrdeutig, solange niemand
sagt, welches gemeint ist — dieselbe Disziplin, die §1 für den STC12
verlangt.

Auf der Werkbank noch unbestätigt: eine spätere Fassung des Anzeigecodes (ein
I²C-Transfer pro Bild statt einem pro Ausgabe, dazu eine rechtsbündige
Eingabezeile) ist geflasht und startet sauber, wurde aber noch von keinem
menschlichen Auge begutachtet.

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

MIT — siehe [LICENSE](LICENSE) — mit einer Ausnahme.

`examples/08-seven-segment` und `examples/09-shift-register` sind abgeleitet von
[treideme/stc89c52-demos](https://github.com/treideme/stc89c52-demos), das unter
**Apache-2.0** steht. Beide sind verändert: im Pseudocode-Dialekt neu
formuliert, für einen anderen Baustein, über eine andere Toolchain. Diese Lizenz
gilt für sie weiterhin — der Text liegt in
[LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt), die Zuschreibung in
[NOTICE](NOTICE) und zusätzlich im Kopf jeder Datei.
