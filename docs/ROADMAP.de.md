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
5. **`generateC()` ergänzen** in `sb3Creator.js`, neben `generatePython` /
   `generateJavaScript`, mit denselben Round-Trip- und Ausführungstests wie die
   anderen Generatoren. C wird nur erzeugt, nicht gelesen (kein C-→-Pseudocode-
   Frontend) — die Invariante der beidseitigen Konvergenz muss es also nicht
   erfüllen.
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
