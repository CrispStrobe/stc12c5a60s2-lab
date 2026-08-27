# HC6800-EM3 V3.0 — research notes (board not yet on the bench)

The "big brother" of the Prechin/PuZhong (普中科技) A2 the owner will connect
next. Same vendor family as the A2; the HC6800-ES is a smaller sibling whose
demo code is public and corroborates the pin conventions. **Nothing below is
measured by us yet** — when the board arrives, `src/05-discover89` maps the
real thing in one minute, and its output outranks every line of this file.

English-only: internal planning, per the bilingual rule.

## Identity

- Vendor: 普中科技 (PuZhong, sold as "Prechin"); board family HC6800,
  variants ES / EM3 V2.2 / EM3 V3.0 (V3.0 adds the 16×16 dot matrix).
- Stock MCU: STC89C52 or STC90C516RD+ (61 KB flash variant), DIP-40 socket,
  11.0592 MHz crystal, CH340 USB serial → same stcgal stc89 flow our tiny
  board just proved. Same cold-power-on ISP rule.
- Everything routes through jumper blocks (J-xx / JP-xx) — modules can be
  disconnected from ports, which the tiny board cannot do. Notably J-TXD /
  J-RXD sit between the CH340 and P3.0/P3.1.

## The structural fact first: modules are CABLED, not traced

cocus/hc6800em3-platformio's wiring notes ("Connections: JP11 (P2) to J12
using 8 PIN cable") settle how the EM3 works: each port ends in a header,
each module has a header, and 8-pin cables connect them. The pin map below
is therefore the *conventional* wiring the tutorials assume, not a fixed
fact of the PCB — any port can drive any module, and the discovery firmware
must confirm whatever cabling the physical board actually has.

## Pin map (sourced, unverified — cross-checked across five repos + wiki)

| peripheral | pins | notes |
|---|---|---|
| 8 LEDs (D11–D18) | one port via 74HC245 buffer, **ACTIVE HIGH** | opposite polarity to our minimum-system board — the buffer drives anodes, cathodes grounded (CSDN V2.2 writeup); dialect `ACTIVE HIGH` handles it |
| independent keys K1–K4 | P3.0–P3.3 region | ES demos literally use P3_0..P3_3 — K1/K2 SHARE THE UART PINS; jumpers decide. Verify with discovery firmware before trusting |
| 4×4 matrix keypad | P1 full port: rows P1.7–P1.4, cols P1.3–P1.0 | THREE sources agree (hongwenjun `GPIO_KEY P1`, cocus 8-keys demo, byr balance row/col sbits) |
| buzzer | P1.5 via J8, driver transistor | passive |
| relay | P1.4 via J2 | active low |
| DS18B20 | P3.7, socketed | remove it to free the pin |
| IR receiver | P3.2 via J1 | collides with K3/INT0 — J1 off when unused |
| DS1302 RTC | data P3.4, clk P3.6, ce P3.5 (JP1302) | battery-backed |
| AT24C02 EEPROM | I2C: SDA P2.0, SCL P2.1 | hard-wired — constrains P2 |
| PCF8591 ADC/DAC | same I2C bus | AIN0 trimpot, AIN1 NTC, AIN2 LDR, AIN3 header; AOUT LED |
| 8-digit 7-seg | data P0, digit select P2.2–P2.4 through 74HC138 | ES demos: `P2 = i<<2` |
| LCD1602 | data P0; RS P2.6, RW P2.5, E P2.7 | THREE sources agree (treideme ES, markchan3, byr balance) — the strongest line in this table |
| LCD12864 (ST7920) | same P0 bus, module socket | graphical |
| 16×16 dot matrix | 74HC595 chain: SER P3.4, RCLK P3.5, SRCLK P3.6 (JP595) | hongwenjun's sbits; V3.0 headline feature |
| 7-seg digit select | 74HC138 A/B/C on P2.2/P2.3/P2.4 | hongwenjun + treideme (`P2 = i<<2`) agree |
| 74HC165 input reg | P1.6/P3.6, JP165 | parallel-to-serial in |
| motors | ULN2003D (DC), UDN2916 (stepper) via IM/M headers | external wiring |
| RS232/RS485 | MAX232 / MAX485 | alternative serial paths |

## What matters for us

1. **Port conflicts are the curriculum.** I2C owns P2.0/P2.1 while the 7-seg
   select needs P2.2–P2.4 and the LCD control P2.5–P2.7 — P2 is fully spoken
   for. DS1302, the 595 chain and NE555 all contend for P3.4–P3.6. The
   jumper blocks are the resolution mechanism. Our pseudocode PART/PIN
   declarations can express exactly this, and the simulator's benches
   should model the conflicts, not idealize them away.
2. **LED polarity flips between boards of the same family** (EM3 active
   high vs. minimum-system active low). Reinforces: polarity is a per-board
   measured fact, never a family assumption — the exact lesson of the
   calculator's active-high keys.
3. **The discovery firmware ports as-is** (it only needs UART + quasi
   inputs), and on an EM3/A2 it can additionally find the DS18B20 (socketed
   here, so presence should actually fire, unlike our tiny board).
4. **PCF8591 gives the 8051 an ADC path over I2C** — the analog story the
   STC89 lacks natively, and a second real consumer for the sb3-creator I2C
   stack after the OLED/EEPROM work.
5. A2 first, EM3 later: the A2 is the same vendor's compact STC89 board;
   expect the same conventions, verify with discover89 in the first minute.
6. **The DS1302's crystal and battery are now simulable, and are two more
   things the bench should MEASURE rather than assume.** bw-board d85b08b
   gave the model X1/X2 and VCC1: no crystal means the clock never advances
   even with CH clear, and no cell means a power cut returns it halted at
   zero with its RAM gone. The table above says "battery-backed", but that
   line is research like everything else here — the header says so — and a
   coin cell that reads 1.5 V buys nothing, which is exactly what "it kept
   time for a year and then stopped" looks like. So: measure VCC1 with a
   meter, and confirm the clock still runs after a power cycle. Until then
   the simulator's PRECHIN-A2 preset wires neither pin, which the model
   treats as "crystal assumed, VCC2 only" — right for a bench that is not
   modelling them, and wrong the moment someone claims the preset shows
   what the board does over a power cut.

Sources: [SourceForge HC6800EM3 wiki](https://sourceforge.net/p/hc6800em3/wiki/Home/),
[treideme/stc89c52-demos](https://github.com/treideme/stc89c52-demos) (HC6800-ES
demo code — the sbit/#define lines are the pin authority above),
[cocus/hc6800em3-platformio](https://github.com/cocus/hc6800em3-platformio)
(EM3 + SDCC/PlatformIO — proof the modern toolchain runs this board, and the
source of the cable-wiring fact),
[hongwenjun/stc89c52](https://github.com/hongwenjun/stc89c52) (138/595/keypad
sbits), [markchan3/Uart_ReceiveSend_HC6800](https://github.com/markchan3/Uart_ReceiveSend_HC6800)
(LCD1602 pins), [byr-51-electronicbalance](https://github.com/bubiandexuanlv1998/byr-51-electronicbalance)
(LCD + keypad row/col sbits + HX711 on P3.2/P3.3 as an add-on pattern),
[WilliamRen/hc6800e](https://github.com/WilliamRen/hc6800e) (Keil-era HC6800E
lesson tree, incl. 18B20+1602 combo),
[CSDN V2.2 buzzer/LED writeups](https://blog.csdn.net/weixin_44475634/article/details/108019012),
[CSDN V3.0 materials index](https://blog.csdn.net/weixin_43143843/article/details/122689436).

Bonus fact from cocus: the EM3 builds with **SDCC via PlatformIO** today —
our exact compiler. Nothing about the board is Keil-bound.

The locally archived `hongwenjun_stc89c52`/`huang_daming_stc89c52` example
adds unusually direct conflict documentation: its `main.c` says that fitting
the LCD1602 conflicts with the numeric display, and that the P2 LED chase
conflicts specifically with LCD `EN`. Its `lcd1602a.h` defines data=P0,
RW=P2.5, RS=P2.6 and EN=P2.7. That predicts the A2 bench observations (the
7-segment `8.` changing to `I-`, and D7/D8 lighting) rather than merely
sharing the same conventional pin table.

Additional historical HC6800EM/8051 tutorial indexes retained for future
cross-checking: [HC6800EM3 WordPress archive](https://hc6800em.wordpress.com/tag/hc6800em3/)
and [Kasim Daniel's 8051 page](https://kasimdaniel.blogspot.com/2014/07/8051.html).

## Vendor DVD recovered locally (2026-08-24)

The owner's `hc6800em3-dvd.iso` (ISO label `HC6800EM3-DVD`, 469 MB) contains
the original English examples plus `A2. Chinese Examples/MCS51`. Its shipped
LCD1602 keypad demo independently confirms `data=P0`, `RW=P2.5`, `RS=P2.6`,
`EN=P2.7`; it selects 4-bit mode on P0.4–P0.7 and uses deliberately long
1 ms data-settle and 5 ms enable-pulse delays. The untouched 512-byte
`pro.hex` was programmed successfully on the A2's STC89C52RC. It ran (the
P1 keypad scan audibly clicked the A2's shared P1.5 buzzer) but the LCD still
showed only its uninitialized solid row. The cause was then bench-confirmed:
the LCD was seated end-for-end. Because the A2 connector supplies the LCD
logic/backlight GND and VCC symmetrically at both ends, it looked powered in
the wrong orientation. Rotating it 180 degrees—counterintuitively overlapping
the MCU somewhat—made the untouched DVD example 18 work immediately.
