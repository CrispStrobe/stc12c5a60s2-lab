# Fahrplan: ein STC12C5A60S2-Backend für BrickWright

[🇬🇧 English](ROADMAP.md) · 🇩🇪 Deutsch

Dieses Repository ist die Vorarbeit an Hardware und Toolchain. Das Ziel ist eine
**BrickWright-Erweiterung**, mit der man einen STC12C5A60S2 aus Scratch-Blöcken
heraus steuern — und anschließend dauerhaft programmieren — kann, über dieselbe
Kompilier- und Flash-Kette, die in der [README](../README.de.md) beschrieben ist.

## Wo das in den bestehenden Stack passt

BrickWright ist bereits ein Compiler mit vier Darstellungen
(**Pseudocode ⇄ Blöcke ⇄ Python ⇄ JavaScript**, `sb3-creator/src/utils/sb3Creator.js`),
auf den Hardware-Erweiterungen aufsetzen. Zwei Vorbilder sind hier einschlägig:

| Vorbild | Bauart | Was wir übernehmen |
|---|---|---|
| `legoev3direct`, `legonxt`, `legoboostunified` | **Live / angebunden** — Blöcke schicken Direktbefehle an eine Firmware, die schon auf dem Stein läuft | Das Muster der Laufzeit-Treiber (`RUNTIME_EXTENSIONS`-Registry) und die Trennung von Transport und Logik in `overlay/scratch-vm/src/extensions/crispstrobe/adapter.js` |
| `ev3lms` sowie der NBC-/LMSASM-→-NXT-/EV3-Bytecode-Pfad in `legacy-lego-compiler` / `brickwright-bridges` | **Kompiliert** — aus Blöcken wird Quelltext, daraus Maschinencode, der hochgeladen wird | Die Architektur „Compile-Server + Upload“ und die Einsicht, dass ein Zielsystem einfach ein weiterer Codegenerator neben `generatePython` / `generateJavaScript` ist |

Der STC12 will **beides** — aus demselben Grund, aus dem der EV3 beides hat.

## Zwei Betriebsarten

### Variante A — angebunden (`stc12live`)

Eine kleine, dauerhaft installierte Firmware sitzt auf dem Chip und lauscht auf
UART1 (`P3.0`/`P3.1`, dieselben Pins, die auch ISP benutzt). Blöcke schicken
Befehle in Rahmen, die Firmware führt sie aus und antwortet. Blöcke wirken
*sofort*, ohne neu zu flashen — der Bearbeitungszyklus ist damit so schnell wie
bei jeder anderen Scratch-Erweiterung.

* Transport: **Web Serial API** (Chrome/Edge auf dem Desktop), über dieselbe
  Adapter-Abstraktion, die heute schon BLE, Bluetooth Classic und
  WebSocket-Bridges kapselt.
* Preis dafür: der Chip ist nicht autonom — USB abziehen und er steht still.
* Das ist die Variante, die **zuerst** gebaut werden sollte. Hier entsteht und
  bewährt sich das Blockvokabular, und sie braucht keinerlei Compiler im Browser.

### Variante B — kompiliert (`stc12`)

Aus Blöcken wird C, aus C eine `.hex`, die geflasht wird. Danach läuft der Chip
eigenständig, an einer Batterie, ganz ohne Rechner.

```
  Scratch-Blöcke
        │  sb3Creator: die bestehenden Block-Walker
        ▼
  BrickWright-IR  ──▶ Pseudocode / Python / JavaScript   (existiert bereits)
        │
        │  NEU: generateC() — ein fünfter Codegenerator
        ▼
  C für SDCC + <stc12.h>
        │  NEU: Compile-Dienst (oder SDCC als WASM)
        ▼
  .ihx / .hex
        │  NEU: das STC12-ISP-Protokoll aus stcgal, neu über Web Serial
        ▼
  STC12C5A60S2
```

## Blockvokabular (erster Entwurf)

Bewusst klein gehalten und so gewählt, dass **jeder Block eine offensichtliche,
billige Übersetzung sowohl in einen Live-Befehl als auch in eine Zeile C hat**.
Dahinter steckt dasselbe Modell aus Senken/Speisen und Portmodi wie in
[PINOUT.de.md](PINOUT.de.md) — die Blöcke verbergen die Registerpaare, nicht die
Physik.

| Block | Live-Befehl | Kompiliert zu |
|---|---|---|
| `setze Pin [P1.0] Modus [Ausgang ▾]` | `MODE p m` | `P1M1 &= ~m; P1M0 \|= m;` |
| `setze Pin [P1.0] auf [low ▾]` | `WRITE p v` | `P1_0 = 0;` |
| `(lies Pin [P1.0])` | `READ p` | `P1_0` |
| `warte [1] Sekunden` | *(auf Host-Seite)* | `delay_ms(1000);` |
| `(lies analog [ADC0])` | `ADC ch` | ADC_CONTR-Sequenz → 10-Bit-Ergebnis |
| `setze PWM [CCP0] auf [50] %` | `PWM ch duty` | PCA-Modul im PWM-Modus |
| `(Millisekunden seit Start)` | `TICKS` | frei laufender Timer-1-Zähler |
| `wenn [P3.2] auf [low ▾] geht` | vom Host gepollt | externer Interrupt `INT0` |
| `serielle Ausgabe [hallo]` | — | UART1 mit fester Baudrate |

Ausdrücklich nicht in v1: Fließkomma (SDCCs Software-Float ist auf einem 8051
riesig), Rekursion über geringe Tiefe hinaus (248 Byte Stack) und dynamische
Speicherverwaltung.

## Warum die kompilierte Variante deutlich schwerer ist als die LEGO-Ziele

Das gehört ehrlich benannt, weil es die Reihenfolge der Arbeit bestimmt:

1. **Es gibt keine VM als Ziel.** NXT und EV3 haben beide einen
   Bytecode-Interpreter mit Variablen, Threads und Scheduling an Bord. Der STC12
   hat davon nichts — ein „Wenn die grüne Flagge angeklickt“-Skript muss zu einer
   `main()`-Schleife werden, und zwei parallele Skripte entweder zu kooperativem
   Round-Robin in dieser Schleife oder zu einer Timer-ISR. Diese
   Scheduling-Entscheidung ist das zentrale Entwurfsproblem.
2. **Die Ressourcen sind endlich und knapp.** 60 KB Flash sind großzügig, aber es
   gibt **248 nutzbare Byte Stack** und 256 B direkt adressierbaren RAM.
   Scratch-Variablen müssen statisch zugeteilt werden, und der Generator muss
   laut scheitern statt still überzulaufen.
3. **Der Compiler ist nativ.** SDCC ist ein C-Programm, keine JS-Bibliothek.
   Entweder er läuft serverseitig (so wie `legacy-lego-compiler` das für NBC und
   LMSASM längst tut — der ausgetretene Pfad) oder er wird nach WASM übersetzt.

   **Entschieden am 09.08.2026: WASM, für die 8051-Ziele.** Der Server-Weg kam
   zuerst und funktioniert, brachte aber zwei Probleme mit, die mit dem
   Übersetzen nichts zu tun haben. Das Deploy-Limit des kostenlosen Tarifs ließ
   den Dienst einen Tag lang rund 50 Commits veralten; und die glibc des Hosts
   nagelt ihn auf SDCC 4.0.0 fest, während dieses Repo mit 4.5.0 baut — beide
   erzeugen also unterschiedliche Firmware: `01-blink` ist entfernt 888 Bytes
   und lokal 996. WASM hat keine glibc und braucht kein Deployment, beseitigt
   also beides, statt um eines von beiden herumzuarbeiten. `gbdk-emscripten`
   liefert diese Bauform für die z80-Ports bereits mit ~1,3 MB WASM aus; es ist
   damit eine Portierung und kein Experiment. Der Build muss
   **einfädig** sein: GitHub Pages kann die COOP/COEP-Header nicht setzen, die
   `SharedArrayBuffer` und WASM-Threads voraussetzen.

   **AVR: entschieden am 10.08.2026 — das Übersetzen im Browser entfällt
   vorerst, bei Bedarf über einen kleinen gehosteten Dienst, und `avr-gcc` wird
   nur dann nach WASM portiert, wenn jemand danach fragt.** Gemessen statt
   geschätzt: das kleinste brauchbare `avr-gcc` sind 16,1 MB nativer Code
   (`cc1plus` plus `as`/`ld`/`objcopy`), wenn `lto1` entfällt und der
   Arduino-Kern zu `core.a` vorübersetzt wird; geschätzt 7–11 MB komprimiert,
   also grob das Achtfache der SDCC-Last — für ein Publikum, das bereits eine
   Toolchain hat. Ausschlaggebend ist nicht die Schwierigkeit, sondern die
   Mittelverteilung: dies ist ein 8051-Projekt, AVR ein Zusatzziel, und der
   größte Einzelposten gehört nicht dorthin. Die vollständige Begründung —
   samt der Korrektur, dass die GMP/MPFR/MPC-Abhängigkeit von `avr-gcc` in
   `math-stack-ios-builder` bereits weitgehend für WASM skriptiert ist — steht
   im README von `stc-compiler`, damit sie niemand ein zweites Mal herleitet.
4. **Flashen braucht einen Kaltstart.** Der STC-Bootloader lauscht nur direkt
   nach dem Einschalten (siehe README §2.3). Im Browser heißt das: entweder man
   fordert die Nutzerin auf, VCC zu trennen, oder man setzt den
   DTR-schaltet-die-Versorgung-Trick voraus, damit
   `setSignals({dataTerminalReady})` das automatisch erledigen kann.

## Vorgeschlagene Reihenfolge

1. ~~**Die C-Primitive in diesem Repository ausbauen** — `02-gpio-input`,
   `03-pwm`, `04-adc`, `05-uart`.~~ — **Gebaut und zwischen zwei Emulatoren
   gegengeprüft — nicht auf Silizium.** GPIO, ADC, PWM und UART existieren und
   stimmen zwischen emu8051 und ucsim überein; das ist **Kategorie 2b**.
   `BENCH-ADC`, `BENCH-PWM` und `BENCH-UART` sind die Bench-Sitzungen, die das
   anheben würden.

   > Hier stand bis zum 10.08.2026 „an echter Hardware nachgewiesen“. Das war
   > falsch, und zwar in genau der Richtung, gegen die dieses Projekt
   > angelegt ist. `VERIFICATION-LEDGER.md` beginnt mit dem Satz, dass nichts
   > davon je auf echtem Silizium gelaufen ist, führt den ADC als „2b, nur die
   > Registerfolge — der analoge Pfad ist offen“ und lässt `BENCH-ADC` genau
   > für die Messung offen, die ihn schließen würde. `DEBUG-CONTROL-MODEL.md`
   > sagt unmissverständlich, dass es noch keine Bench-Sitzung gab.
2. **Die residente Firmware schreiben** (`10-live-firmware`), die das
   Rahmenprotokoll implementiert, aufgebaut aus genau diesen Primitiven.
3. **`stc12live` veröffentlichen** als Erweiterung in `CrispStrobe/extensions`,
   mit Web Serial über die bestehende Adapterschicht. Ab hier steuern Blöcke
   echte Hardware.
4. **Das STC12-ISP-Protokoll in JavaScript nachbauen**, portiert aus
   `stcgal/protocols.py` (MIT). Gegen eine hier gebaute, bekannt gute `.hex`
   verifizieren — der Abgleich ist einfach zu testen, weil `make info` denselben
   Handshake zum Vergleich liefert.
5. ~~**`generateC()` ergänzen** in `sb3Creator.js`~~ — **ERLEDIGT, 08.08.2026.**
   Es steht neben `generatePython` / `generateJavaScript` und bringt
   `cRep`/`cCond`/`cStackBlock` (portiert `stmts_c`) sowie `cTaskBlock`
   (portiert `stmts_task`) mit; Scheduling, FOSC/12-Timing und Active-Low
   wurden unverändert aus `stc-compiler/stc_pseudocode.py` übernommen. Eine
   neue Blocksprache trägt die Hardware: `DEVICE` / `CLOCK` / `PIN` plus
   `turn on/off`, `set high/low`, `toggle`, `read` — exakt so geschrieben wie
   die `pseudocode/*.bw` dieses Repos, eine `.bw`-Datei *ist* also ein
   Brickwright-Programm. 37 Prüfungen ohne Netz, dazu 4 Live-Prüfungen, die
   jedes Beispiel über `POST /compile {"language":"c"}` wirklich bauen.
   Beschreibung: `sb3-creator/reference/c-target.md`; eingebunden in
   `brickwright` (`develop`) und `brickwright-lite` (`main`), jeweils mit einem
   nur lesbaren **🔌 C (STC12)**-Reiter.
   **C ist jetzt ZWEISEITIG** (`cToPseudocode.js`, 08.08.2026): es liest
   unseren eigenen C-Code exakt — über einen `@bw`-Markerkopf, den der
   Generator für alles ergänzt, was die flache C-Form verliert — und liest
   **handgeschriebene Firmware**, auch die `src/01-blink/main.c` dieses Repos,
   durch Herleitung: Pins aus `#define LED1 P1_0` / `sbit`, die Polarität aus
   dem Muster `LED_ON 0`, den Takt aus `#define FOSC_HZ` — und jede Herleitung
   wird gemeldet statt geraten. Die Register-Initialisierung sitzt jetzt in
   `bw_setup()`, damit Aufbau und Programm unterscheidbar bleiben.
   Nicht rückgerechnet wird die Scheduler-Form (dafür gibt es eine Warnung).
   `keil2sdcc` (C→C) verbreitert nun die Eingabe dieses Frontends, ein
   Keil-Projekt kann also über beide Stufen zu Blöcken werden; `stc_disasm`
   (HEX→Assembler) bleibt die eigene, schwierigere Spur. Ausführlich in
   `sb3-creator/reference/c-target.md`.
6. **Den Compile-Endpunkt aufsetzen** in `legacy-lego-compiler`, neben den
   bestehenden für NBC und LMSASM: C hinschicken, `.hex` zurückbekommen.
7. **`stc12` veröffentlichen** (kompilierte Variante), mit dem Flasher aus
   Schritt 4.

## Offene Fragen

* **Das Scheduling-Modell — ENTSCHIEDEN und prototypisch umgesetzt
  (2026-08-08).** Kooperatives Round-Robin, mit einem Kniff aus Scratchs
  eigenem Vertrag: Eine Timer-0-ISR zählt nur einen Millisekundenzähler hoch,
  und jedes `WHEN`-Skript wird zu einem Zustandsautomaten (Duff's Device),
  der bei jedem Warten **und an jeder Schleifen-Rückkante** abgibt — eine
  beschäftigte `FOREVER`-Schleife kann die anderen Skripte also nicht
  aushungern, genau wie in Scratch. Keine Präemption, keine Stacks pro Task,
  kein Register-Banking; Fristen sind überlaufsichere 16-Bit-Vergleiche.
  Implementiert im Pseudocode-Frontend von `stc-compiler`: mehrere
  `WHEN started:`-Blöcke kompilieren und laufen nebenläufig. Eine
  dokumentierte Grenze: Bei mehreren Skripten dürfen Prozeduren nicht warten
  — dieselbe Form wie in Scratch, wo eigene Blöcke durchlaufen. `generateC()`
  in sb3-creator übernimmt dieses Schema unverändert; `stc-compiler` ist
  Referenz und Orakel.
* **Chip-Familien — ebenfalls geklärt.** Derselbe Pseudocode erzeugt jetzt
  zeitkorrekten Code für `STC12C5A60S2`, `STC89C52(RC)` und `STC15F2K60S2`:
  Alles hängt an Timer 0 mit FOSC/12, den 12T- wie 1T-Kerne identisch zählen.
  Der Sockel-Tausch (STC12 ins STC89-Board) ändert damit nichts. Software-
  Warteschleifen liefen 6–12× zu schnell — genau deshalb erzeugt der
  Generator keine, und genau davor warnt der Keil-Übersetzer bei migriertem
  Code.
* **Wo das gegenüber dem Stand der Technik steht, und zwei Punkte, die KEINE
  Zugeständnisse sind (08.08.2026).** Die nächsten Vergleichspunkte:
  **Proteus VSM** (kommerziell) simuliert 8051 und Schaltung gemeinsam in
  Mixed-Mode-SPICE, mit Debugging auf Quelltextebene — das ist die Zielmarke;
  **SimulIDE** ist das nächstliegende Open-Source-Gegenstück und hat einen
  8051, steht aber unter **AGPLv3**, `brickwright-lite` darf also davon lernen
  und es niemals einbinden; **Wokwi** hat keinen 8051 und keinen Weg dorthin
  (seine Custom-Chips-API modelliert Peripherie, keine Kerne) — deshalb muss
  der Browser-Kern gebaut werden, und deshalb ist die Nachnutzung der
  MIT-lizenzierten `wokwi-elements` für die Oberfläche genau die richtige
  Tiefe. Nichts Gefundenes macht **Scratch-Blöcke → 8051 ohne Arduino-Runtime**
  — mBlock, MakeCode und eBlock setzen alle ein Framework voraus. Das, und zwei
  unabhängig geschriebene, gegen 349 echte Firmware-Images abgeglichene
  Emulatoren, sind das wirklich unbesetzte Feld.
  **Zwei Korrekturen an einer früheren Lesart dieses Vergleichs.** Erstens ist
  der Schaltungssimulator ein *Kernprodukt* und nichts, was man SimulIDE und
  Proteus überlässt: Er wird gerade gebaut (`bw-board`, `bw-circuit-ui`), das
  didaktische Ziel braucht einen echten Löser statt einer plausiblen Animation,
  und „wir schlagen SPICE nicht" ist kein Grund aufzuhören, sondern einer,
  ehrlich über die Genauigkeit zu sein. Zweitens ist **Peripherie-Breite ein
  Ziel und kein Wildwuchs.** Der Beleg lag schon vor und wurde falsch herum
  gelesen: **220 von 349** fremden Firmware-Images bestehen den differenziellen
  Lauf *strikt* — beide Ereignisströme vollständig identisch. Das *ist* die
  Fähigkeit, fremde Firmware auszuführen, und jede weitere Peripherie hebt die
  Zahl. Was der Codegenerator braucht, ist eine Untergrenze für das Modell, nie
  eine Obergrenze.
  ⚠ **Diese Zahl ist eine Korrektur.** Bis zum 09.08.2026 stand hier 275/349;
  dann verschärfte `ucsim-stc` sein eigenes Maß und stellte fest, dass die
  lockere Zählung 54 reine *Präfix*-Treffer mitgezählt hatte — Läufe, bei denen
  der kürzere Ereignisstrom als Präfix passte, die Längen aber auseinanderging
  en. Das ist keine Übereinstimmung. Das Argument überlebt die Korrektur, die
  Zahl nicht — und ein Maß, das sich selbst schmeichelt, ist weniger wert als
  ein kleineres ehrliches.
* **Die Beispielbündel sind der Integrationstest (08.08.2026).** `make examples`
  baut pro Pseudocode-Programm ein Bündel — Quelle, erzeugtes C, `.hex`,
  `pins.json` für Schnittstelle C, `symbols.json` für Schnittstelle D — und es
  wird eingecheckt, damit ein anderes Repository keine Toolchain braucht, um
  ein Beispiel zu öffnen. Jede Schicht hatte eigene Tests; dass sie
  *zusammenspielen*, hat nie etwas geprüft. Der Satz deckt alle vier
  `inferNetlist`-Zeilen ab, und der Generator schlägt fehl, wenn das nicht mehr
  gilt. **Die Lücke, die er sichtbar macht, ist die nächste Arbeit:** alles ist
  GPIO und ADC, weil der Generator nichts anderes ausgibt — `ledBrightness` und
  `buzzerTone`, beide in Schnittstelle B festgelegt, wurden also noch nie
  ausgeübt, weil nichts ein Tastverhältnis erzeugt. **PWM über die PCA ist die
  nächste Scheibe**, und dafür muss §5 des Peripheriemodells erst aus dem
  Datenblatt gefüllt werden.
* **Der STC15 ist ein Delta, der STC8H ein eigenes Projekt (entschieden am
  08.08.2026).** [`STC15-PERIPHERAL-MODEL.md`](STC15-PERIPHERAL-MODEL.md) (nur
  Englisch — interne Implementierungsvorgabe) beschreibt nur die Unterschiede zum
  STC12: 74 SFRs identisch, **kein Register behält seinen Namen an einer anderen
  Adresse**, und drei Fallen — `ADRJ` wandert von `AUXR1.2` nach `CLK_DIV.5`, die
  Baudraten-Bits in `AUXR` werden zu Timer-2-Bits, und aus `WAKE_CLKO` wird
  `INT_CLKO`. Für den STC15 ist Silizium vorhanden, also ist das die Familie, die
  am ehesten die **erste auf Hardware geprüfte** Peripherie-Aussage des Projekts
  liefert: die ADC-Kernregister sind identisch, eine Bestätigung der Sequenz auf
  einem STC15 ist damit auch ein echter Beleg für den STC12.
  **Der STC8H wird bewusst nicht begonnen.** Er ist keine Variante: 12-Bit-ADC mit
  anderen Steuerregistern, PWMA/PWMB statt einer bloßen PCA, anderer Taktbaum,
  Hardware-I²C, mehr Ports. Er braucht ein eigenes Handbuch, ein eigenes Modell,
  einen eigenen Header — SDCC liefert weder `stc15.h` noch `stc8h.h`. Zwei Fragen
  vorab: ob `stcgal` 1.10 mit `stc8`/`stc8d`/`stc8g` den STC8H überhaupt abdeckt
  (ein eigener Handler fehlt, `stc8prog` wäre die Alternative), und ob
  `program_eeprom_split` in `Stc8Option` — ein Bit ohne STC12-Entsprechung — dem
  IAP erlaubt, den Programmbereich zu beschreiben. Falls ja, sind **echte
  Code-Haltepunkte auf dem STC8 möglich**, die `DEBUG-CONTROL-MODEL.md` §5 für den
  STC12 als unmöglich abschließt — der STC8H wäre dann das bessere Debug-Ziel.
* **Das Peripheriemodell steht jetzt einmal geschrieben, für alle**:
  [`STC12-PERIPHERAL-MODEL.md`](STC12-PERIPHERAL-MODEL.md) (08.08.2026, nur Englisch —
  interne Implementierungsvorgabe). Ein
  ucsim-Fork (GPL-2, nur CI-Orakel), ein emu8051-Fork (**MIT**, also im Browser
  mitlieferbar) und die Board-Schicht des Simulators müssen sich einig sein, was
  dieser Chip tut; drei eigene Modelle hießen drei verschiedene Antworten. Die
  Registeradressen darin sind gegengeprüfte Tatsachen; noch aus dem Datenblatt zu
  lesende Bit-Belegungen sind markiert. **Der ADC-Abschnitt trägt den Hinweis, dass
  er auf Silizium unbestätigt ist** — ein aus dem Datenblatt geschriebener Emulator
  kann die Stimmigkeit der Folge zeigen, nicht ihre Richtigkeit.
* **Zwei Oberflächen, die noch fehlen (aufgeworfen 08.08.2026).** Beide gibt es
  noch nicht; beide ergänzen Scratchs Bühne und brauchen keine Änderung am
  Codegenerator.
  1. **Eine Hardware-Anzeige zum Mitschauen und Eingreifen** — das, was
     [S4A](https://s4a.cat/index_en.html) mit seinem Platinenbild macht, nur
     modern und für mehrere Geräte: LED-Zustände, Pin-Pegel, Poti-Werte,
     Motordrehzahlen, live neben der Bühne. Eine Anzeige, zwei Quellen:
     *simuliert* (aus dem Emulator oder dem neutralen Treiber-Shim) und *live*
     (spiegelt echte Hardware über die Tether-Verbindung). Sie setzt auf der
     vorhandenen `RUNTIME_EXTENSIONS`-Treiberkonvention von sb3-creator auf.
     Reihenfolge: LEGO-Hubs, dieses Board, später Arduino.
  2. **Eine Simulator-/Emulator-/Debugger-Ansicht** — ein `.hex` ohne
     angeschlossene Hardware laufen lassen: Einzelschritt, Haltepunkte, SFR-
     und Registeransicht, Speicher, Pin-Zustände. `emu8051` ist das Vorbild für
     die Bedienung (sein TUI zeigt genau die richtigen Bereiche —
     <https://reidemeister.com/blog/2022.07.03>); `ucsim`/`s51` ist die
     Maschine, gegen die `stc-compiler` schon differenziell ausführt. **Die
     bekannte Lücke: kein ucsim-Build bringt ein STC-Modell mit** (geprüft am
     Git-Stand 0.9.9) — die eigentliche Arbeit ist also, eines zu schreiben:
     der SFR-Satz, der ADC, die PCA, das 1T-Timing. Dieses Modell wäre auch der
     billigste Weg, die ADC-Frage unten ohne Bank-Sitzung zu klären. **Die
     Ablaufsteuerung selbst ist jetzt einmal festgeschrieben, als Schnittstelle D:**
     [`DEBUG-CONTROL-MODEL.md`](DEBUG-CONTROL-MODEL.md) (08.08.2026, wie das
     Peripheriemodell nur auf Englisch) — beide Emulator-Forks und der künftige
     On-Chip-Monitor implementieren das, nicht ihre eigene Lesart von
     „Einzelschritt". Für den Browser: ucsim oder emu8051 nach WASM übersetzt. **Lizenzen am 08.08.2026
     geprüft: emu8051 ist MIT, ebenso Wokwis avr8js und wokwi-elements — der
     Browserweg ist also wirklich offen; ucsim/QEMU/unicorn sind alle GPL-2 und
     bleiben CI-only.** Die vollständige Architektur, samt Schaltungssimulation und
     der Frage nach dem virtuellen Multimeter, steht in
     `sb3-creator/reference/simulation.md`.
* **Die Board-Beschreibung.** Derzeit kodiert `include/board.h` genau einen
  Zwei-LED-Aufbau fest. Blöcke brauchen eine Board-Beschreibung, die der
  Generator liest — vermutlich JSON, vermutlich geteilt mit der Live-Firmware,
  damit beide Varianten sich über die Pinnamen einig sind.
* ~~**SDCC als WASM.** Aufwand unbekannt; vor einer Festlegung auf den
  Server-Weg einen Machbarkeitstest wert.~~ **Beantwortet am 09.08.2026 — wird
  gemacht.** Beseitigt die Server-Abhängigkeit vollständig und läuft offline,
  was für die Tauri-, iOS- und Android-Hüllen zählt. Und eben doch kein
  Machbarkeitstest: `gbdk-emscripten` liefert ein Emscripten-SDCC für die
  z80-Ports bereits mit ~1,3 MB WASM aus — es ist also eine Portierung mit
  einem anderen `--port`-Schalter, keine Forschung. Einzelheiten und die beiden
  Randbedingungen (einfädig; 4.5.0 statt der gehosteten 4.0.0) stehen in
  §„Warum der kompilierte Modus wirklich schwerer ist", Punkt 3.
* **Lizenzen.** SDCC ist GPL, aber `mcs51/stc12.h` trägt die übliche
  Linking-Ausnahme — erzeugte Binaries sind also unbelastet. `stcgal` steht unter
  MIT, eine JS-Portierung ist für die durchgängig permissive Anforderung von
  BrickWright-lite folglich unproblematisch. Beides blockiert die
  store-taugliche Linie nicht — ein in die App gebündeltes SDCC-als-WASM
  *würde* dagegen GPL ins Bundle ziehen, also genau das, wogegen
  `brickwright-lite` überhaupt existiert. Serverseitiges Kompilieren umgeht das.
