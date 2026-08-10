# Bench-Runbook — eine Sitzung, vier Antworten

Alles in diesem Projekt wurde unter Emulation verifiziert (Kategorie 2b) und
**nichts ist auf echtem Silizium gelaufen**. Dieses Dokument verwandelt
begrenzte Werkbank-Zeit in Kategorie-1-Evidenz. Folge der Reihenfolge; jeder
Schritt gibt entweder den nächsten frei oder scheitert billig.

**Was du brauchst:** ein STC12C5A60S2-Board (PDIP-40 oder LQFP-44), einen
USB-TTL-Adapter (CH340 oder CP2102), ein 10-kΩ-Potentiometer, zwei LEDs
(beliebige Farbe), zwei Widerstände (1 kΩ), ein Multimeter. Ein Oszilloskop
ist nützlich für BENCH-PWM, aber nicht nötig.

**Zeitbudget:** 30 Minuten reichen für BENCH-ADC und BENCH-PWM (gleiche
Verdrahtung). BENCH-CUBE braucht den Würfel-Bausatz. BENCH-UART braucht
einen zweiten Seriell-Pfad.

---

## Kollisionen — lies das, bevor du irgendetwas verdrahtest

Drei Pins haben Doppelfunktionen. Wenn du das falsch machst, ist die
Sitzung verloren.

| Pin | Chip-Pin-Nr. (PDIP-40) | Konflikt |
|---|---|---|
| **P1.3** | 24 | ADC-Kanal 3 UND PCA CCP0 (Servo). Nicht beides verdrahten. |
| **P3.0 / RxD** | 10 | UART1-Empfang UND ISP-Download. Der Bootloader startet nur bei **kaltem Einschalten** — ein Reset-Taster reicht nicht. VCC abziehen, wieder einstecken, dann spricht `stcgal`. |
| **P3.1 / TxD** | 11 | UART1-Senden UND ISP-Upload. Gleiche Einschränkung. |

**Flash-Regel:** Trenne alles von P3.0/P3.1, bevor du flashst. Nach dem
Programmstart wieder anschließen.

---

## 1. BENCH-ADC — der Analogpfad (dies zuerst)

**Warum zuerst:** `src/02-adc` ist fertig zum Flashen. Es testet das Flashen
UND den ADC in einem Schritt.

### Verdrahtung (nach Chip-Pin-Nummer, PDIP-40)

| Chip-Pin | Name | Verbinden mit |
|---|---|---|
| 40 | VCC | +5 V |
| 20 | GND | Masse |
| 24 | P1.3 (ADC3) | Poti-Schleifer |
| — | Poti-Ende A | +5 V |
| — | Poti-Ende B | Masse |
| 21 | P1.0 | LED1-Anode über 1 kΩ von VCC (active-low) |

### Flashen

```bash
make EXAMPLE=02-adc flash
```

Falls `stcgal` nicht verbindet: VCC abziehen, Befehl starten, dann VCC
einstecken. Der Bootloader braucht ein **kaltes** Einschalten.

### Vorab-registrierte Vorhersage (Kategorie 2b)

| Poti-Stellung | ADC-Wert | Blinkperiode | Quelle |
|---|---|---|---|
| Schleifer → GND | ~0 | ~100 ms (schnellstes) | `STC12-PERIPHERAL-MODEL.md` §4, `ucsim-stc` `f45ecc6` |
| Schleifer → VCC | ~1023 | ~2000 ms (langsamstes) | gleich |
| **Verhältnis** | — | **~20:1** | aus der Schleifenkonstante abgeleitet |

### Was beobachten

- Die Blinkperiode an beiden Extremen (zwei Zahlen in ms)
- Ob die Verfolgung gleichmäßig ist oder Sprünge/Plateaus hat
- Ob die Rate stabil ist, wenn das Poti nicht bewegt wird

### Entscheidungsregel

| Beobachtung | Bewertung |
|---|---|
| Verhältnis 10:1 – 30:1, gleichmäßig | **BESTANDEN** — Analogpfad funktioniert |
| Verhältnis 10:1 – 30:1, aber Plateau/Sprung | **UNKLAR** — möglicherweise ein feststeckender ADC-Bit |
| Verhältnis ~1:1 (konstantes Blinken) | **FEHLGESCHLAGEN** — ADC liest nicht |
| LED blinkt gar nicht | **FEHLGESCHLAGEN** — Programm nicht geflasht |

**Toleranz bei absoluten Perioden:** ±30 %. Das Verhältnis ist der stärkere Test.

### Was es aufwertet

- **ADC-Registersequenz** von 2b auf **Kategorie 1**.
- **ADC-Analogpfad** von „offen" auf „auf Silizium bestätigt".
- Die ADC-**Genauigkeit** (Linearität, Offset) bleibt offen.

---

## 2. BENCH-PWM — LED-Strom bei 50 % Tastgrad (gleiche Verdrahtung, Meter ergänzen)

### Verdrahtungsänderung

Poti von Pin 24 trennen. PWM-Testprogramm flashen:

```bash
make EXAMPLE=03-pwm flash
```

Pin 21 (P1.0) über ein **Multimeter im DC-mA-Modus** in Reihe mit dem
1-kΩ-Widerstand und der LED verbinden.

### Vorhersage

| Bedingung | Vorhergesagter Strom | Quelle |
|---|---|---|
| 50 % Tastgrad, 1 kΩ + LED, VCC=5V | **1,46 mA** DC-Durchschnitt | `bw-board` Helligkeitsmodell |
| 100 % an (stetig LOW) | **2,93 mA** | I = (5−2)/(1000+25) |

### Entscheidungsregel

| Messwert | Bewertung |
|---|---|
| 1,17 – 1,76 mA (±20 %) | **BESTANDEN** |
| 0,5 – 1,17 oder 1,76 – 2,5 mA | **UNKLAR** — Wert notieren |
| < 0,1 mA | **FEHLGESCHLAGEN** — PWM läuft nicht |

---

## 3. BENCH-CUBE — Voxel-Karte und Polarität

Braucht den 4×4×4-LED-Würfel-Bausatz.

```bash
sdcc --iram-size 256 -o build/ src/20-ledcube/probe.c
make EXAMPLE=20-ledcube flash
```

### Vorhersage

**Polarität: active-HIGH.** Bei `(FE, 01)` leuchtet **eine** LED.
**Bildwiederholrate: 124 Hz** (unsichtbar fürs Auge).

### Entscheidungsregel

| Beobachtung | Bewertung |
|---|---|
| Eine LED leuchtet bei `(FE, 01)` | **BESTANDEN** — active-high bestätigt |
| Eine LED DUNKEL, sieben leuchten | **BESTANDEN aber invertiert** — active-low |
| Sichtbares Flackern | **UNKLAR** — Verweilzeit kürzer als vorhergesagt |

---

## 4. BENCH-UART — der Monitor auf einem echten UART

### Verdrahtung

| Chip-Pin | Name | Verbinden mit |
|---|---|---|
| 10 | P3.0 / RxD | Adapter TXD |
| 11 | P3.1 / TxD | Adapter RXD |

**Kollision:** Das sind die ISP-Pins. Monitor-Adapter vor dem Flashen
trennen; danach wieder anschließen.

```bash
make EXAMPLE=10-live-firmware flash
python3 tools/live-monitor.py --port /dev/cu.usbserial-XXXX
```

### Vorhersage

| Befehl | Erwartet |
|---|---|
| `HELLO` | Gültige gerahmte Antwort |
| 500-ms-Halt | Wanduhr **500 ± 50 ms** |

### Entscheidungsregel

| Beobachtung | Bewertung |
|---|---|
| `HELLO` antwortet mit gültigem Frame | **BESTANDEN** |
| `HELLO` antwortet verstümmelt | **UNKLAR** — Baud-Fehlanpassung |
| Keine Antwort | **FEHLGESCHLAGEN** |

---

## Was mitbringen

Vier Ergebnisse, nach ID. Für jedes: die **rohe Beobachtung** (was das Meter
anzeigte, was die LED tat), nicht eine Schlussfolgerung.

| ID | Was aufzeichnen |
|---|---|
| BENCH-ADC | Zwei Blinkperioden (ms); gleichmäßig oder nicht |
| BENCH-PWM | DC mA durch die LED bei 50 % Tastgrad |
| BENCH-CUBE | 64-Zeilen-Tabelle `(select, bit) → (x, y, z)`; was bei `(FE, 01)` geschah |
| BENCH-UART | Ob `HELLO` antwortete; Halt-Versatz in ms |

**Wenn etwas mit den Emulatoren nicht übereinstimmt, ist das das wertvollste
Ergebnis der Sitzung.** Aufschreiben, bevor man es erklärt.
