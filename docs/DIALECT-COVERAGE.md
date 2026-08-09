# What the dialect can and cannot say — measured against a real corpus

Measured 2026-08-09 against **[treideme/stc89c52-demos](https://github.com/treideme/stc89c52-demos)**
(Apache-2.0), sixteen worked demos for the STC89C52 written to accompany a blog series on 8051
derivatives. Cited, not vendored: the corpus lives in `../stc-research`, which is never
published.

It is a good yardstick precisely because nobody wrote it for us. It is what an experienced
person actually does with this family, and the answer to "can our blocks say that" is a fact
about the dialect rather than an opinion about it.

**The headline: the gap was never "more blocks".** It was two dialect features — both since
built — and a parts library, which is still open. The sixteen demos split cleanly into three
tiers, and only the third is still a gap.

## Tier 1 — expressible today, and arguably better (5 of 16)

`00_hello`, `01_led_button`, `01_led_button_debounce`, `01_led_button_timer`, `01_led_buzzer`.

Blink, mirror a button to an LED, debounce, do something periodically, drive a buzzer.
`pseudocode/01-blink.bw` and `02-button.bw` are already these programs.

Worth noting where we come out *ahead* rather than merely level. `00_hello` is:

```c
P2_0 = 0;  delay(30000);  P2_0 = 1;  delay(30000);
static void delay(unsigned int t) { while (t--) ; }
```

A cycle-counted busy loop — which is exactly the construct that breaks when the same source
moves to a 1T part, running 6–12× fast (README §8.1). Everything this toolchain emits is timed
off Timer 0 at FOSC/12, which 12T and 1T parts count identically, so `wait 1 seconds` means a
second on all three families. The demo's own timing does not survive the port; ours does.

`01_led_buzzer` is the same story once more: its buzzer is a bit toggled at the timer-ISR rate,
so its pitch is whatever the tick happens to be. A `TONE` pin is Timer 1 with a computed reload
and gets an actual frequency (peripheral model §5b).

## Tier 2 — WAS blocked on exactly two features; now unblocked (5 of 16)

`01_led_matrix`, `01_button_led_matrix`, `01_led_74H595`, `02_7_segment`, `02_7_segment_dyn`.

Every one of these was blocked on the same two things, and on nothing else:

**1. Whole-port I/O.** The dialect addresses one pin at a time. These write eight at once:

```c
#define LED_DIGIT P0
LED_DIGIT = segment_map[i] | segment_dp;
```

Eight `turn on` statements are not a substitute — the whole point of a display is that the byte
lands at once. A `PORT` declaration alongside `PIN` would cover it.

**2. Indexed lookup tables.** A seven-segment font is a table, and there is no way to say one:

```c
const uint8_t segment_map[] = { 0b00111111, 0b00000110, /* ... */ };
```

These two are worth having for their own sake: a digit display and an LED matrix are the two
things people build after a blinking LED, and both were unreachable.

**Both landed on 2026-08-09** (`stc-compiler` `da0e1ca`). `TABLE` puts constants in code space,
`PORT` writes eight bits in one store, and `pseudocode/08-seven-segment.bw` is `02_7_segment`
said in the dialect — with the font asserted against a transcription of the original rather than
against our own output. The tokenizer gained binary literals on the way, because a seven-segment
font is written in binary or it is written wrong.

Two refusals came out of it that are worth more than the features. A `PORT` and a `PIN` on the
same port are rejected **in both directions**, each naming the other: a port write covers all
eight bits and would clobber the pin, and neither declaration looks wrong on its own. And a
computed table index is *clamped* rather than trusted — reading past a table means reading a
random byte of flash and, on a display, showing it, which looks like data rather than like a
fault. A constant index is checked when it compiles and costs nothing.

**Score is now 10 of 16 expressible.**

## Tier 3 — not programs, drivers (6 of 16)

`03_hd44780_lcd`, `04_st7920_lcd`, `04_st7920_graph`, `06_DS18B20_1wire`, `07_at24c02_i2c`,
`08_irda`.

These bit-bang a protocol against microsecond deadlines. From `06_DS18B20_1wire`:

```c
void delay(uint16_t t) { while (t--) /* more than 1us at 12MHz */ ; }
delay(480);                       /* the 1-Wire reset pulse */
j++;                              /* small delay */
DS18B20_DQ = (byte & 0x01);       /* math also introduces needed delay */
```

**This should not be expressible, and adding features until it is would be a mistake.** Timing
that depends on how the compiler happened to schedule an increment is the exact failure the
whole 1T/12T section of the README exists to warn about; it does not survive a recompile, let
alone a change of part. Blocks that emitted it would be blocks that sometimes work.

The right shape is the one Scratch and micro:bit already settled on: **you do not write an I2C
driver in blocks, you get a block backed by one.** A `read temperature from <pin>` block over a
hand-written, timing-audited DS18B20 driver is both safer and more useful than any amount of
dialect. That makes tier 3 an argument for a **parts library**, and these six demos are a good
specification for its first entries.

## Score, and what it means for the roadmap

| tier | count | status |
|---|---:|---|
| expressible from the start | 5 | — |
| needed whole-port I/O + lookup tables | 5 | **done, `da0e1ca`** |
| driver-backed blocks | 6 | needs a parts library |

**10 of 16 are expressible**, and the remaining 6 are not a dialect problem at all. That is the
whole of what this corpus asks of the dialect: it is now a question of parts, not of grammar.

One incidental finding: **not one of the sixteen uses the UART.** The serial console added in
`stc-compiler` `c0c1dec` is not something this corpus asked for, and its real justification is
the tethered link and diagnostics rather than parity with what people write.

⚠ Tier 1 is assessed from reading, and demonstrated only where an equivalent example already
exists (`01-blink`, `02-button`). Tiers 2 and 3 are blocked by construction, so their
classification is not in doubt.
