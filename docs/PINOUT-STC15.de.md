# STC15F2K60S2 — Pinbelegung PDIP-40

Ergänzung zu [`PINOUT.de.md`](PINOUT.de.md), das den STC12C5A60S2 beschreibt. Die Unterschiede
auf Registerebene stehen in [`STC15-PERIPHERAL-MODEL.md`](STC15-PERIPHERAL-MODEL.md) (nur
Englisch — interne Implementierungsvorgabe); hier geht es darum, wo die Beine hingehören.

Quelle: [STC15-Datenblatt](https://www.stcmicro.com/datasheet/STC15F2K60S2-en.pdf) §1.1.3
(S. 19). **Nicht an einem physischen Bauteil überprüft.**

> [!CAUTION]
> **Dieser Baustein ist NICHT pinkompatibel zum STC12C5A60S2.** Nicht ungefähr, nicht
> größtenteils — die Versorgungsspannung wandert. Beim STC12 liegt VCC auf Pin 40 und Pin 18
> ist XTAL2. Beim STC15F2K60S2 liegt **VCC auf Pin 18**, und Pin 40 ist `P4.5/ALE`.
>
> Wer einen STC15F2K60S2 in eine für den STC12 verdrahtete Fassung steckt, legt die
> Versorgungsspannung auf einen GPIO-Pin und treibt die Quarzpins mit dem, was die alte
> Schaltung dort hatte. Man sollte den Chip als zerstört betrachten.
>
> Die Geschichte vom „Tausch in derselben Fassung" in [README §8.1](../README.de.md) betrifft
> **STC12 ↔ STC89**, die sich die klassische 8051-Belegung teilen. Der STC15 tut das nicht.
> Die Platine muss neu verdrahtet werden.

## PDIP-40

```
                      ┌─────────∪──────────┐
            AD0/P0.0 ─│  1              40 │─ P4.5/ALE
            AD1/P0.1 ─│  2              39 │─ P2.7/A15/CCP2_3
            AD2/P0.2 ─│  3              38 │─ P2.6/A14/CCP1_3
            AD3/P0.3 ─│  4              37 │─ P2.5/A13/CCP0_3
            AD4/P0.4 ─│  5              36 │─ P2.4/A12/ECI_3/SS_2
            AD5/P0.5 ─│  6              35 │─ P2.3/A11/MOSI_2
            AD6/P0.6 ─│  7              34 │─ P2.2/A10/MISO_2
            AD7/P0.7 ─│  8    STC15     33 │─ P2.1/A9/SCLK_2
  RxD2/CCP1/ADC0/P1.0 │  9   F2K60S2    32 │─ P2.0/A8/RSTOUT_LOW
  TxD2/CCP0/ADC1/P1.1 │ 10              31 │─ P4.4/RD
      ECI/SS/ADC2/P1.2│ 11              30 │─ P4.2/WR
        MOSI/ADC3/P1.3│ 12              29 │─ P4.1/MISO_3
        MISO/ADC4/P1.4│ 13              28 │─ P3.7/INT3/TxD_2/CCP2_2
        SCLK/ADC5/P1.5│ 14              27 │─ P3.6/INT2/RxD_2/CCP1_2
 XTAL2/RxD_3/ADC6/P1.6│ 15              26 │─ P3.5/T1/T0CLKO/CCP0_2
 XTAL1/TxD_3/ADC7/P1.7│ 16              25 │─ P3.4/T0/T1CLKO/ECI_2
    SS_3/MCLKO/RST/P5.4│17              24 │─ P3.3/INT1
                  VCC ─│ 18              23 │─ P3.2/INT0
                 P5.5 ─│ 19              22 │─ P3.1/TxD/T2
                  GND ─│ 20              21 │─ P3.0/RxD/INT4/T2CLKO
                      └────────────────────┘
```

38 I/O-Pins in diesem Gehäuse. Pin 1 liegt an der Seite mit der Kerbe.

## Die vier Unterschiede, die weh tun

| | STC12C5A60S2 | STC15F2K60S2 |
|---|---|---|
| **VCC** | Pin 40 | **Pin 18** |
| **GND** | Pin 20 | Pin 20 *(das Einzige, was passt)* |
| **RST** | Pin 9, eigener Pin | **Pin 17**, mehrfach belegt: `SS_3/MCLKO/RST/P5.4` |
| **P0** | Pins 32–39, **absteigend** (32 = P0.7) | Pins 1–8, **aufsteigend** (1 = P0.0) |
| **P1** | Pins 1–8 | Pins 9–16 |
| **XTAL2 / XTAL1** | Pins 18 / 19, eigene Pins | **Pins 15 / 16**, geteilt mit `P1.6`/`P1.7` |
| **UART1 RxD/TxD** | Pins 10 / 11 | **Pins 21 / 22** |

### Ein Quarz kostet zwei ADC-Kanäle

`XTAL1`/`XTAL2` liegen auf `P1.6`/`P1.7`, und das sind zugleich `ADC7`/`ADC6`. Ein Quarz nimmt
beide weg. Beim STC12 haben die Quarzpins keine Zweitfunktion und kosten nichts.

Das wiegt weniger schwer, als es klingt, denn bei diesem Baustein **sollte man gar keinen Quarz
bestücken**: Der interne RC-Oszillator hat ±0,3 % und lässt sich mit `stcgal -t 11059` auf
11,0592 MHz trimmen (`STC15-PERIPHERAL-MODEL.md` §5). Das Datenblatt sagt es selbst — „No need
external crystal and reset". Also P1.6/P1.7 als ADC-Eingänge behalten.

### UART1 lässt sich verlegen, und das Datenblatt empfiehlt es

`P3.0`/`P3.1` (Pins 21/22) ist die Voreinstellung und **das Pinpaar des ISP-Bootloaders**, genau
wie beim STC12. UART1 lässt sich aber über `P_SW1` (0xA2) auf `P3.6`/`P3.7` (Pins 27/28) oder
`P1.6`/`P1.7` (Pins 15/16) umlegen — für die kleineren Gehäuse empfiehlt das Datenblatt das auf
seiner eigenen Pinbelegungsseite.

**Für `src/10-live-firmware` ist das eine echte Chance.** Beim STC12 streiten sich der
Debug-Monitor und der ISP-Bootloader um dieselben Pins, man kann also kein Terminal offen haben
während man flasht (`DEBUG-CONTROL-MODEL.md` §9). Auf einem STC15 könnte der Monitor auf den
umgelegten Pins sitzen und `P3.0`/`P3.1` für ISP frei lassen. ⚠ Ungetestet, und es kostet ein
ADC-Kanalpaar oder zwei GPIOs.

## Reset

**High-aktiv, genau wie beim STC12** — unter 12 MHz ist ein simpler 1-kΩ-Widerstand nach Masse
die ganze Schaltung, und das Netzwerk aus 10 kΩ + 10 µF aus low-aktiven 8051-Schaltplänen ist
auch hier falsch.

Der Unterschied: RST ist kein eigener Pin, sondern `P5.4`, und zugleich `MCLKO` (Ausgang des
Haupttakts) und `SS_3`. Nach dem Einschalten ist es der Reset-Eingang; ihn zu GPIO zu machen ist
eine ISP-Option, gesteuert von `stcgal`s `reset_pin_enabled`. **Diese Option beim einzigen
vorhandenen Chip nicht abschalten** — ohne Reset-Pin und ohne Quarz wird die Rettung mühsam.

`RSTOUT_LOW` auf Pin 32 (`P2.0`) ist ein Ausgang, den der MCU während des Resets auf Low zieht,
um Peripherie im Reset zu halten. Beim STC12 gibt es das nicht.

## Versorgung

**5,5 – 4,2 V** beim `STC15F2K60S2`. Der 3,3-V-Bruder heißt `STC15L2K60S2` (3,6 – 2,4 V), und
*den* zerstören 5 V — also den Aufdruck prüfen.

Zu beachten ist das **Minimum von 4,2 V**, enger als die 3,5 V des STC12. Eine USB-Schiene, die
unter LED-Last einbricht und die dem STC12 nichts ausmacht, kann diesen Baustein unter die
Spezifikation drücken. Am Chip messen, nicht am Adapter.

## Was dieses Gehäuse nicht hat

`P0`–`P3` sind vollständig. Von `P4` gibt es nur `P4.1`, `P4.2`, `P4.4`, `P4.5`; von `P5` nur
`P5.4` und `P5.5`. `P6`/`P7` gibt es auf diesem Baustein in keinem Gehäuse ⚠ (fehlt in der
Merkmalsliste §1.1.1; diese Pinbelegung bestätigt es für PDIP-40).

Ebenfalls nicht vorhanden, wie beim STC12: **kein `EA`-Pin und kein `PSEN`** — der Chip läuft
immer aus dem internen Flash, weshalb der Weg von Keils Monitor-51 auch hier nicht baubar ist
([`DEBUG-CONTROL-MODEL.md`](DEBUG-CONTROL-MODEL.md) §5).

## Andere Gehäuse

LQFP-44 (42 I/O), LQFP-32 (30 I/O), SOP-28/SKDIP-28 (26 I/O), TSSOP-20 (18 I/O). Datenblatt
§1.1.3 S. 19–20 zeigt alle; dieses Repository geht durchgehend von PDIP-40 aus.
