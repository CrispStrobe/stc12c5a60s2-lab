# Das Peripheriemodell des STC12C5A60S2 — ein Vertrag, mehrere Umsetzungen

[🇬🇧 English](STC12-PERIPHERAL-MODEL.md) · 🇩🇪 Deutsch

**Warum diese Datei existiert.** Mindestens drei Dinge müssen sich darüber einig sein, was
dieser Chip tut:

| Umsetzung | Lizenz | Wo sie leben darf |
|---|---|---|
| ein ucsim-Fork mit STC12-Modell | **GPL-2** (Teil von SDCC) | CI-/Entwickler-Orakel, serverseitig. **Nie mitgeliefert.** |
| ein emu8051-Fork mit STC12-Modell | **MIT** | mitlieferbar — WASM im Browser, wofür `brickwright-lite` existiert |
| die Board-Schicht des Simulators | unsere, MIT | `sb3-creator/reference/simulation.md` |

Schreibt jede ihr eigenes Modell, wird die Arbeit dreimal gemacht *und* wir haben am Ende drei
verschiedene Antworten auf „was tut dieser Chip“ — schlimmer, als eine zu haben. **Dieses
Dokument ist die eine Antwort. Umsetzungen berufen sich darauf; sie leiten sie nicht neu ab.**

## Quellen, und was Tatsache ist gegenüber dem, was noch gelesen werden muss

**Registeradressen sind Tatsachen** — nimm sie von hier. Sie sind zwischen SDCCs
`mcs51/stc12.h` und der `SFR`-Tabelle in `stc-compiler/stc_disasm.py` gegengeprüft (die selbst
gegen diesen Header gebaut wurde), und sie stimmen überein.

**Bit-Belegungen und Zeiten mit ⚠ sind hier NOCH NICHT verifiziert.** Lies sie im Datenblatt
und **nenne den Abschnitt**, wenn du sie einträgst. Nicht raten, und nicht aus einem Tutorial
übernehmen: sehr viele „STC12“-Tutorials sind AT89C51-Text mit ausgetauschter Typnummer und
liegen bei P0, ALE, PSEN und EA falsch.

- Datenblatt: <https://www.stcmicro.com/datasheet/STC12C5A60S2-en.pdf> (15.07.2011)
- `docs/PINOUT.de.md` — Pinbelegung, SFR- und Portmodus-Referenz
- `stc-compiler/stc_pseudocode.py` — `PARTS` sowie `emit_c`, woran man genau sieht, welche
  Register erzeugter Code anfasst. Diese Menge ist das **Minimum**, das ein Modell treffen muss.

## 1. Kern und Zeitverhalten — das zuerst richtig machen

Das folgenreichste Verhalten, und dasjenige, bei dem ein plausibel aussehendes Modell still
und leise falsch liegt.

- **1T-Kern.** Befehle brauchen 1–6 Takte, nicht die klassischen 12. Ein I/O-Zugriff braucht
  4 Takte; vor dem Zurücklesen eines externen Signals ein oder zwei `nop` einfügen.
- **`AUXR` (0x8E) Bit 7 wählt den Takt von Timer 0.** ⚠ *Bitnamen und Polarität im Datenblatt
  bestätigen.* Der erzeugte Code löscht es:
  - `AUXR.7 = 0` → Timer 0 zählt mit **FOSC/12** (die 12T-Rate). Das ist der Reset-Standard und
    die Grundlage von allem, was wir erzeugen.
  - `AUXR.7 = 1` → Timer 0 zählt mit **FOSC** (1T). Ein für 12T geschriebenes Programm läuft
    dann etwa 12× zu schnell.
- **Alles, was die Toolchain erzeugt, hängt an Timer 0, Modus 1, mit FOSC/12** — der eine
  Modus, den ein 12T-STC89 und ein 1T-STC12/STC15 *identisch* zählen. Genau das macht ein
  Programm auf beiden Chips richtig, und deshalb enthält **kein erzeugter Code jemals eine
  taktgezählte Warteschleife** (README §8.1: ein 1T-Teil läuft damit 6–12× zu schnell — der
  klassische Sockeltausch-Fehler).
- Der 1-ms-Nachladewert, den der Generator benutzt:

  ```
  T0_RELOAD = 65536 − (FOSC_HZ / 12 / 1000)
  ```

  Bei `FOSC = 11059200` also `65536 − 921 = 64615` (0xFC67). **Ein konformes Modell muss damit
  bei `AUXR.7 = 0` alle 1,000 ms ticken und bei `AUXR.7 = 1` etwa alle 0,083 ms.** Beide
  Zustände ausdrücklich testen — ein Modell, das nur `AUXR.7 = 0` je sieht, wirkt auf unseren
  Programmen richtig und ist auf allem anderen falsch.
- Der interne RC-Oszillator liegt bei 5 V zwischen 11 und 17 MHz und driftet mit der
  Temperatur. Ein Modell sollte FOSC einstellbar machen und nicht vorgeben, der interne RC sei
  exakt.

## 2. SFR-Karte

Gegengeprüft, belastbar. `stc-compiler/stc_disasm.py` ist die maschinenlesbare Fassung.

| Adr. | Name | Adr. | Name | Adr. | Name |
|---|---|---|---|---|---|
| 0x80 | P0 | 0x9D | **P1ASF** | 0xC0 | P4 |
| 0x81 | SP | 0xA0 | P2 | 0xC8 | P5 |
| 0x82 | DPL | 0xA2 | AUXR1 | 0xC9 | **P5M1** |
| 0x83 | DPH | 0xA8 | IE | 0xCA | **P5M0** |
| 0x87 | PCON | 0xA9 | SADDR | 0xD0 | PSW |
| 0x88 | TCON | 0xB0 | P3 | 0xD8 | **CCON** |
| 0x89 | TMOD | 0xB1 | **P3M1** | 0xD9 | **CMOD** |
| 0x8A | TL0 | 0xB2 | **P3M0** | 0xDA | **CCAPM0** |
| 0x8B | TL1 | 0xB3 | **P4M1** | 0xDB | **CCAPM1** |
| 0x8C | TH0 | 0xB4 | **P4M0** | 0xE0 | ACC |
| 0x8D | TH1 | 0xB6 | IP2H | 0xE9 | **CL** |
| 0x8E | **AUXR** | 0xB7 | IPH | 0xF0 | B |
| 0x90 | P1 | 0xB8 | IP | 0xF2 | **PCA_PWM0** |
| 0x91 | **P1M1** | 0xB9 | SADEN | 0xF3 | **PCA_PWM1** |
| 0x92 | **P1M0** | 0xBB | **P4SW** | 0xF9 | **CH** |
| 0x93 | **P0M1** | 0xBC | **ADC_CONTR** | 0xFA | **CCAP0H** |
| 0x94 | **P0M0** | 0xBD | **ADC_RES** | 0xFB | **CCAP1H** |
| 0x95 | **P2M1** | 0xBE | **ADC_RESL** | | |
| 0x96 | **P2M0** | 0x98 | SCON | 0x99 | SBUF |
| 0x97 | CLK_DIV | 0x9A | S2CON | 0x9B | S2BUF |
| | | 0x9C | BRT | | |

**Fett = STC-spezifisch, in einem generischen 8051/8052-Modell nicht vorhanden.** Das sind die
Ergänzungen.

Bitnamen, die ein Modell bereitstellen muss (alle Standard-8051, außer wo vermerkt): `TR0`
0x8C, `TF0` 0x8D, `ET0` 0xA9, `EA` 0xAF, `IT0` 0x88, `IE0` 0x89. Portbits heißen `Pn_m`.

## 3. Portmodi — worauf der Schaltungssimulator aufbaut

Der Modus jedes Pins ergibt sich aus je einem Bit in `PxM1` und `PxM0`:

| `PxM1` | `PxM0` | Modus | Elektrisches Verhalten |
|---|---|---|---|
| 0 | 0 | **quasi-bidirektional** (Reset-Standard) | kräftig nach Masse, *schwach* nach VCC. Auch lesbar. |
| 0 | 1 | **Push-Pull** | kräftig in beide Richtungen |
| 1 | 0 | **nur Eingang** (hochohmig) | treibt nichts |
| 1 | 1 | **Open Drain** | kräftig nach Masse, überhaupt kein Pull-up |

**Die Asymmetrie zwischen Senken und Speisen ist die zentrale elektrische Tatsache dieses
Chips und der Grund für die ganze Active-Low-Konvention:** ein quasi-bidirektionaler Pin
**senkt 20 mA, speist aber nur ~230 µA** (Datenblatt §4.6). Deshalb werden LEDs als
`+5 V → 1 kΩ → LED → Pin` verdrahtet, und eine `0` lässt sie leuchten.

**Für die Board-Schicht jeden Modus als Thévenin-Ersatzschaltung modellieren** — das ist es,
was den Simulator die Active-Low-Verdrahtung *erklären* lässt statt sie nur zu behaupten:

| Modus | treibt 0 | treibt 1 |
|---|---|---|
| quasi-bidirektional | ≈ 0 V, kleiner Innenwiderstand (senkt 20 mA) | ≈ VCC über einen **großen** Widerstand (Größenordnung ~20 kΩ, also ~230 µA) ⚠ Wert aus der Speisestromkennlinie des Datenblatts herleiten |
| Push-Pull | ≈ 0 V, niederohmig | ≈ VCC, niederohmig |
| nur Eingang | — | — (hochohmig; allein das äußere Netz bestimmt den Knoten) |
| Open Drain | ≈ 0 V, niederohmig | hochohmig (braucht einen äußeren Pull-up) |

⚠ Die genauen Durchgangswiderstände sollten an die U-I-Kennlinien des Datenblatts angepasst
werden. Für die Lehre genügt die richtige Größenordnung; Präzision ist nicht nötig.

Hinweis zum Gehäuse: im PDIP-40 existieren nur **P4.4–P4.7**. `P4SW` (0xBB) wählt bei einigen
P4-Pins die Alternativfunktionen ⚠ (aus dem Datenblatt ergänzen).

## 4. Der ADC — ⚠ UND AUF SILIZIUM WEITERHIN UNBESTÄTIGT

**Das hier vor der Umsetzung lesen.** Die folgende Registerfolge wurde aus dem Datenblatt
geschrieben und **nie an Hardware bestätigt**. `src/02-adc` in diesem Repo ist die
flashfertige Prüfung und wurde nicht ausgeführt. Ein Emulatormodell, das aus demselben
Datenblatt entsteht, kann zeigen, dass die Folge *in sich stimmig* ist; **es kann nicht
bestätigen, dass die Folge richtig ist.** Wer das umsetzt, sage klar, welches von beiden er
gezeigt hat.

Was der Generator tut (`stc_pseudocode.emit_c`) und ein Modell tragen muss:

```c
P1ASF = <Maske der Analogpins>;  /* 0x9D — Analogfunktion auf P1 wählen */
P1M1 |=  mask;  P1M0 &= ~mask;   /* hochohmiger Eingang */
ADC_CONTR = 0xE0;                /* eingeschaltet, schnellste Wandlung */
...
ADC_CONTR = 0xE8 | channel;      /* Power | Speed | START | Kanal */
for (settle = 0; settle < 8; settle++) ;
while (!(ADC_CONTR & 0x10)) ;    /* auf ADC_FLAG warten */
ADC_CONTR &= ~0x10;              /* Flag PER SOFTWARE löschen */
result = ((unsigned int)ADC_RES << 2) | (ADC_RESL & 0x03);   /* 10 Bit */
```

Bit-Belegung von `ADC_CONTR` (0xBC), stimmig zu den Konstanten oben:

| Bit | 7 | 6 | 5 | 4 | 3 | 2–0 |
|---|---|---|---|---|---|---|
| | ADC_POWER | SPEED1 | SPEED0 | ADC_FLAG | ADC_START | CHS2–CHS0 |

- `0xE0` = eingeschaltet, `SPEED = 11`, kein Start. `0xE8 | ch` = dasselbe plus `START` und
  ein Kanal.
- **`ADC_FLAG` wird per Software gelöscht, nie durch die Hardware** — das ist die Falle.
- **ADC-Kanal *n* liegt physisch auf P1.*n***. Es gibt keinen Multiplexer auf einen anderen
  Port; das Pseudocode-Frontend lehnt `ANALOG` außerhalb von P1 ab.
- ⚠ `SPEED1:SPEED0` → Wandlungsdauer in Takten: **die Tabelle im Datenblatt nachlesen.** Der
  Generator benutzt `11` und nennt das „schnellste“.
- ⚠ Ausrichtung des Ergebnisses: der Code oben nimmt an, `ADC_RES` hält die oberen 8 Bit und
  `ADC_RESL` die unteren 2. Prüfen, ob es auf diesem Teil ein Steuerbit für die Ausrichtung
  gibt und wie dessen Reset-Wert lautet.
- Für den Simulator: die Eingangsgröße ist eine **Spannung 0…VCC**, linear auf 0…1023
  abgebildet. Das ist die gesamte Kopplung zur Board-Schicht.

## 5. Der PCA-/PWM-Block — ⚠ nur Gerüst

Die Adressen sind gegengeprüft, die Bit-Belegungen nicht. Aus dem Datenblatt mit
Abschnittsangabe ergänzen. Beteiligte Register: `CCON` 0xD8, `CMOD` 0xD9, `CCAPM0` 0xDA,
`CCAPM1` 0xDB, `CL` 0xE9, `CH` 0xF9, `CCAP0H` 0xFA, `CCAP1H` 0xFB, `PCA_PWM0` 0xF2,
`PCA_PWM1` 0xF3.

Strukturell: ein frei laufender 16-Bit-Zähler (`CH`/`CL`) mit wählbarer Taktquelle (`CMOD`),
dazu Capture-/Compare-Module, deren Modus je Modul gesetzt wird (`CCAPM0/1`) und deren
8-Bit-Tastverhältnis aus `CCAPnH` kommt, mit den zusätzlichen Bits in `PCA_PWM0/1`.

Nichts in der Toolchain erzeugt bislang PCA-Code — PWM ist auf der Chipseite und in
`generateC` noch offen —, dieser Block ist also für *fremde* Images und die künftigen
PWM-Blöcke nötig, nicht um irgendetwas auszuführen, was wir heute erzeugen. Für den Simulator
ist ein PWM-Ausgang ein Pin, der mit einem Tastverhältnis schaltet; die LED-Helligkeit ist der
mittlere Strom, über etwa 20 ms integriert.

## 6. Reset und Pins — wo generisches 8051-Wissen falsch ist

- **Der Reset ist ACTIVE HIGH.** Unter 12 MHz ist ein einfacher 1 kΩ nach Masse die ganze
  Schaltung. Das Netzwerk aus 10 kΩ und 10 µF aus alten 8051-Schaltplänen gehört zu
  active-**low**-Teilen; nicht übernehmen.
- **Es gibt keinen `EA`-Pin.** STC hat ihn entfernt; Pin 31 ist `EX_LVD/RST2/P4.6`. Ebenso
  **kein PSEN**. Wer vom AT89C51 kommt, erwartet das Gegenteil — und die meisten Tutorials
  auch.
- PDIP-40: VCC 40, GND 20, RST 9, `P3.0/RxD` 10, `P3.1/TxD` 11, XTAL2 18, XTAL1 19. **P0 läuft
  absteigend**: Pin 32 ist P0.7, Pin 39 ist P0.0.
- 5-V-Teil (3,5–5,5 V). Der Geschwistertyp `STC12LE…` will 2,1–3,6 V, und 5 V zerstören ihn.

## 7. Was eine konforme Umsetzung nachbilden muss

Die Abnahmeleiter, nach steigendem Wert. Ehrlich berichten, welche Sprossen wirklich erklommen
sind.

1. Lädt ein Intel-HEX-Image und führt es aus.
2. Schreiben auf `PxM1`/`PxM0` ändert das Pinverhalten; Push-Pull gegen quasi-bidirektional
   ist beobachtbar.
3. **Timer 0 ist exakt**: mit `FOSC = 11059200` und `T0_RELOAD` von oben tickt es bei
   `AUXR.7 = 0` alle 1,000 ms — und nachweislich etwa 12× schneller bei `AUXR.7 = 1`.
4. Die ADC-Folge aus §4 läuft durch, setzt `ADC_FLAG` und verlangt dessen Löschen per
   Software, und liefert einen 10-Bit-Wert, der linear der Eingangsspannung folgt.
5. Der PCA-Block nach §5.
6. **Differenzielle Ausführung**: echte Images ergeben übereinstimmende Pin-/SFR-Spuren.
   Testimages: `src/01-blink` und `src/02-adc` in diesem Repo sowie alles, was `generateC()`
   von `sb3-creator` erzeugt (lokal bauen oder das C an
   `https://stc-compiler.vercel.app/compile` schicken, mit
   `{"language":"c","target":"stc12c5a60s2"}`).

## 8. Vorerst ausdrücklich außerhalb des Umfangs

UART1/UART2 und der BRT (Baudraten-Timer), EEPROM/IAP, der Watchdog (`WDT_CONTR` — der beim
STC89 an einer *anderen Adresse* sitzt, weshalb der Keil-Übersetzer dort nicht rät), SPI,
Timer 1 über das Standard-8051-Verhalten hinaus, Power-Down-/Idle-Modi und der LVD.

Ergänzen, wenn etwas sie braucht — und **zuerst** dieses Dokument erweitern.
