/*
 * live-proto.h — the wire form of boundary D.
 *
 * docs/DEBUG-CONTROL-MODEL.md fixes the *semantics* of run control and says
 * explicitly that it is not a wire format. This file is the wire format for
 * the one target that needs one: the chip, reached over UART1.
 *
 * Nothing in here touches hardware, so it compiles with a host compiler too
 * and both ends of the link share one definition. See tests/frame_test.c.
 *
 * FRAME
 *
 *     0x7E   LEN   CMD   payload[LEN]   SUM
 *
 * SUM is chosen so that (LEN + CMD + payload + SUM) & 0xFF == 0.
 *
 * There is no byte stuffing, deliberately. Escaping costs code on a part
 * where code is the scarce thing, and buys little: a 0x7E inside a payload
 * can only cause a *false start*, which then fails its length or checksum
 * check and costs one dropped frame. The receiver resynchronises by hunting
 * for the next 0x7E. Retransmission is the host's job.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LIVE_PROTO_H
#define LIVE_PROTO_H

#define LIVE_PROTO_VERSION  1
#define LIVE_SOF            0x7E
#define LIVE_MAX_PAYLOAD    64

/* ---------------------------------------------------------------- commands
 * Host -> target. The reply carries the same code with bit 7 set, so a host
 * can always tell which request a frame answers.
 */
#define LIVE_CMD_HELLO      0x01   /* -> capability blob, see below          */
#define LIVE_CMD_READ       0x02   /* space, addr_hi, addr_lo, len           */
#define LIVE_CMD_WRITE      0x03   /* space, addr_hi, addr_lo, data...       */
#define LIVE_CMD_REGS       0x04   /* -> A B DPL DPH SP PSW bank R0..R7      */
#define LIVE_CMD_RUN        0x05
#define LIVE_CMD_HALT       0x06   /* takes effect at the next yield point   */
#define LIVE_CMD_STEP       0x07   /* kind, count                            */
#define LIVE_CMD_BPSET      0x08   /* kind, task, state_hi, state_lo         */
#define LIVE_CMD_BPCLR      0x09   /* handle                                 */
#define LIVE_CMD_POS        0x0A   /* -> Level 1 position, see below         */
#define LIVE_CMD_RESET      0x0B
#define LIVE_CMD_SYMS       0x0C   /* ntasks, then (space, hi, lo) entries   */

#define LIVE_REPLY(c)       ((unsigned char)((c) | 0x80))

#define LIVE_EVT_HALT       0xF0   /* unsolicited: cause, pos blob           */
#define LIVE_NAK            0xFF   /* cmd, errcode                           */

/* ----------------------------------------------------- address spaces (§6)
 * These share numeric addresses on an 8051 and are NOT interchangeable.
 * `sfr` and `iram` overlap at 0x80..0xFF and are different memories.
 */
#define LIVE_SP_CODE        0
#define LIVE_SP_IRAM        1
#define LIVE_SP_SFR         2
#define LIVE_SP_XRAM        3
#define LIVE_SP_BIT         4
#define LIVE_SP_COUNT       5

#define LIVE_SPMASK(s)      ((unsigned char)(1u << (s)))

/* ------------------------------------------------------------- step kinds
 * Bit positions in the HELLO capability bitmap, and the argument to STEP.
 * Only `block` is implementable here; see §4 of the model for why `insn`
 * needs the INT0 trick and why `line`/`over`/`out` need a line table.
 */
#define LIVE_STEP_INSN      0
#define LIVE_STEP_LINE      1
#define LIVE_STEP_BLOCK     2
#define LIVE_STEP_OVER      3
#define LIVE_STEP_OUT       4

/* ------------------------------------------------------- breakpoint kinds */
#define LIVE_BP_CODE        0
#define LIVE_BP_YIELD       1
#define LIVE_BP_WRITE       2
#define LIVE_BP_READ        3

/* ------------------------------------------------------------ halt causes */
#define LIVE_HALT_BREAKPOINT 1
#define LIVE_HALT_STEP       2
#define LIVE_HALT_USER       3
#define LIVE_HALT_RESET      4

/* -------------------------------------------------------------- run state */
#define LIVE_ST_RUNNING     0
#define LIVE_ST_HALTED      1

/* ------------------------------------------------------------ NAK reasons */
#define LIVE_ERR_BADCMD     1   /* unknown command code                      */
#define LIVE_ERR_BADLEN     2   /* payload length wrong for this command     */
#define LIVE_ERR_SPACE      3   /* no such address space, or not readable    */
#define LIVE_ERR_RANGE      4   /* address or length out of range            */
#define LIVE_ERR_REFUSED    5   /* would break the link — §6 write hazards   */
#define LIVE_ERR_UNSUP      6   /* capability this target does not have      */
#define LIVE_ERR_STATE      7   /* wrong run state for this command          */

/* ------------------------------------------------ HELLO capability blob
 * Byte-for-byte the on-chip row of the boundary D capability matrix. A host
 * MUST branch on this rather than assume; that is the whole point of §1.
 *
 *   0  protocol version
 *   1  max payload this target accepts
 *   2  step kinds bitmap        (1 << LIVE_STEP_*)
 *   3  breakpoint kinds bitmap  (1 << LIVE_BP_*)
 *   4  readable spaces bitmap   (1 << LIVE_SP_*)
 *   5  writable spaces bitmap
 *   6  flags, below
 *   7  max tasks the symbol table holds
 *   8  peripherals the monitor has taken, below
 */
#define LIVE_FLAG_TIME_FREEZES  0x01   /* halting stops *program* time       */
#define LIVE_FLAG_PC_VALID      0x02   /* regs() reports a meaningful PC     */
#define LIVE_FLAG_SFRS_ALL      0x04   /* all 256 SFRs, not a curated set    */

/* ------------------------------------------------ what the monitor consumed
 * A debugger is not free on a part this size: it takes real peripherals, and
 * a program that wanted one of them will not work under it. Reporting the set
 * lets a front end say WHY rather than leaving a feature silently dead.
 *
 * The case that forced this: a TONE pin is Timer 1 toggling a GPIO, because
 * no PWM path on this chip can make a pitch (STC12-PERIPHERAL-MODEL.md §5b).
 * The monitor also wants Timer 1, as the wall clock behind skewNs. So a
 * program with a buzzer cannot run under the monitor, and without this byte
 * the only symptom would be a buzzer that does not sound.
 *
 * The set differs by part, which is itself worth reporting: the STC12 takes
 * the dedicated BRT for its baud rate, the STC15 has no usable BRT and takes
 * Timer 2 instead (STC15-PERIPHERAL-MODEL.md §2.2).
 */
#define LIVE_RES_TIMER0         0x01   /* the millisecond tick               */
#define LIVE_RES_TIMER1         0x02   /* wall clock behind skewNs -> TONE   */
#define LIVE_RES_TIMER2         0x04   /* STC15 baud source                  */
#define LIVE_RES_BRT            0x08   /* STC12 baud source                  */
#define LIVE_RES_UART1          0x10   /* the link itself, and the ISP pins  */
#define LIVE_RES_PCA            0x20   /* not taken today; here so a future
                                        * monitor can say if it starts to be */

/* --------------------------------------------------- POS / EVT_HALT blob
 *   0     run state
 *   1     ntasks
 *   2..3  bw_ms          (program time, frozen while halted)
 *   4..5  skew_ms        (cumulative wall time lost to halts — never 0 here)
 *   then per task: state_hi state_lo until_hi until_lo
 *
 * EVT_HALT prefixes this with one byte: the halt cause.
 */

#endif /* LIVE_PROTO_H */
