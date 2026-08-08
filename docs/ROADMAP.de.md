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
4. **Flashen braucht einen Kaltstart.** Der STC-Bootloader lauscht nur direkt
   nach dem Einschalten (siehe README §2.3). Im Browser heißt das: entweder man
   fordert die Nutzerin auf, VCC zu trennen, oder man setzt den
   DTR-schaltet-die-Versorgung-Trick voraus, damit
   `setSignals({dataTerminalReady})` das automatisch erledigen kann.

## Vorgeschlagene Reihenfolge

1. **Die C-Primitive in diesem Repository ausbauen** — `02-gpio-input`,
   `03-pwm`, `04-adc`, `05-uart`. Jedes davon ist ein Block aus der Tabelle oben,
   an echter Hardware nachgewiesen. *Das ist der aktuelle Schritt.*
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
   *C wird vorerst nur erzeugt, aber beide Richtungen sind das Ziel* — der Weg
   zurück wächst aus dem Keil-Übersetzer (`/translate`, 546/597) und dem
   Disassembler (`/disassemble`, 380/380 bytegenau) von `stc-compiler`. Bis
   dahin bleibt C von der Invariante der beidseitigen Konvergenz ausgenommen.
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
     billigste Weg, die ADC-Frage unten ohne Bank-Sitzung zu klären. Für den
     Browser: ucsim oder emu8051 nach WASM übersetzt — vorher die Lizenzen
     gegen die durchgängig permissive Regel von brickwright-lite prüfen.
* **Die Board-Beschreibung.** Derzeit kodiert `include/board.h` genau einen
  Zwei-LED-Aufbau fest. Blöcke brauchen eine Board-Beschreibung, die der
  Generator liest — vermutlich JSON, vermutlich geteilt mit der Live-Firmware,
  damit beide Varianten sich über die Pinnamen einig sind.
* **SDCC als WASM.** Das würde die Server-Abhängigkeit vollständig beseitigen
  und alles offline lauffähig machen, was für die Tauri-, iOS- und
  Android-Hüllen zählt. Aufwand unbekannt; vor einer Festlegung auf den
  Server-Weg einen Machbarkeitstest wert.
* **Lizenzen.** SDCC ist GPL, aber `mcs51/stc12.h` trägt die übliche
  Linking-Ausnahme — erzeugte Binaries sind also unbelastet. `stcgal` steht unter
  MIT, eine JS-Portierung ist für die durchgängig permissive Anforderung von
  BrickWright-lite folglich unproblematisch. Beides blockiert die
  store-taugliche Linie nicht — ein in die App gebündeltes SDCC-als-WASM
  *würde* dagegen GPL ins Bundle ziehen, also genau das, wogegen
  `brickwright-lite` überhaupt existiert. Serverseitiges Kompilieren umgeht das.
