// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CrispStrobe
//
//! `stcbsl` — command-line front end.
//!
//! The CLI surface is this crate's own design, per `STC-ISP-CLEANROOM.md`:
//! subcommands rather than a positional filename, verbs that say what they do
//! to the chip, and no flag-for-flag resemblance to any existing tool.
//!
//! The loudest thing it does is print what it is about to send. Every failure
//! mode in this protocol looks like silence (§3.3, §5.1.2), so a log that
//! shows the last frame that went out and what was expected back is the
//! difference between a bench session and a guessing game.

use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

use stcbsl::ihex;
use stcbsl::protocol::stc89::{Stc89, ERASE_BLOCK, WRITE_BLOCK};
use stcbsl::protocol::Job;

#[cfg(feature = "serial")]
use std::time::Duration;
#[cfg(feature = "serial")]
use stcbsl::driver::{self, DrainConfig, DrainMode, Log};
#[cfg(feature = "serial")]
use stcbsl::frame::{hex, Frame};
#[cfg(feature = "serial")]
use stcbsl::protocol::{ProtocolFamily, SessionOptions, TargetInfo};
#[cfg(feature = "serial")]
use stcbsl::session::Session;

const USAGE: &str = "\
stcbsl — clean-room STC serial bootloader flasher (STC89 family)

USAGE:
  stcbsl [OPTIONS] <COMMAND>

COMMANDS:
  identify                  handshake, report what the chip says, let it run
  erase [--blocks N]        erase code flash (whole chip unless --blocks)
  write <FILE.hex|FILE.bw>  compile pseudocode if needed, erase, program, run
  flash <FILE.hex|FILE.bw>  alias for `write`
  explain <FILE.hex>        offline: show the block plan for an image; no port
  ports                     list serial ports on this machine

OPTIONS:
  -p, --port <DEV>          serial device, e.g. /dev/cu.usbserial-1110
      --baud <N>            transfer baud                 [default 115200]
      --handshake-baud <N>  handshake baud                [default 2400]
      --parity <none|even>  wire parity                   [default none]
      --wait <SECONDS>      how long to pulse for the BSL [default 30]
      --drain-mode <M>      at each 8F/8E baud switch: tcdrain (tcdrain(2)
                            the wire) | wire (flush only)  [default tcdrain]
      --drain-margin <MS>   settle ms after the drain, before switching baud
                            (matches stcgal's sleep(0.1)) [default 100]
      --blocks <N>          erase block count (256 B each), `erase` only
      --keep-options        write the option byte back (default for `write`)
      --skip-options        do not send the option frame at all
  -q, --quiet               only print the outcome
  -h, --help                this text

THE ONE THING TO KNOW:
  The bootloader runs only after a COLD power-on and listens for a few
  hundred milliseconds. So: switch the board OFF, start this command, then
  switch it ON. A reset button does not work; DTR autoreset does not work on
  these boards, and this tool deliberately does not pretend otherwise.

  Every retry is a power cycle.
";

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    match run(&args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(msg) => {
            eprintln!("stcbsl: {msg}");
            ExitCode::FAILURE
        }
    }
}

struct Args {
    port: Option<String>,
    transfer_baud: u32,
    handshake_baud: u32,
    parity_even: bool,
    wait_s: u64,
    blocks: Option<u8>,
    write_options: bool,
    quiet: bool,
    drain_mode: DrainMode,
    drain_margin_ms: i64,
    command: Option<String>,
    positional: Vec<String>,
}

fn parse_args(argv: &[String]) -> Result<Args, String> {
    let mut a = Args {
        port: None,
        transfer_baud: 115200,
        handshake_baud: 2400,
        parity_even: false,
        wait_s: 30,
        blocks: None,
        write_options: true,
        quiet: false,
        drain_mode: DrainConfig::default().mode,
        drain_margin_ms: DrainConfig::default().margin_ms,
        command: None,
        positional: Vec::new(),
    };
    let mut i = 0;
    macro_rules! next {
        ($what:expr) => {{
            i += 1;
            argv.get(i)
                .cloned()
                .ok_or_else(|| format!("{} needs a value", $what))?
        }};
    }
    while i < argv.len() {
        let arg = argv[i].clone();
        match arg.as_str() {
            "-h" | "--help" => {
                print!("{USAGE}");
                std::process::exit(0);
            }
            "-q" | "--quiet" => a.quiet = true,
            "-p" | "--port" => a.port = Some(next!("--port")),
            "--baud" => {
                a.transfer_baud = next!("--baud")
                    .parse()
                    .map_err(|_| "--baud: not a number".to_string())?
            }
            "--handshake-baud" => {
                a.handshake_baud = next!("--handshake-baud")
                    .parse()
                    .map_err(|_| "--handshake-baud: not a number".to_string())?
            }
            "--drain-mode" => {
                let v = next!("--drain-mode");
                a.drain_mode = match v.as_str() {
                    "tcdrain" => DrainMode::TcDrain,
                    "wire" => DrainMode::ComputeWireTime,
                    other => return Err(format!("--drain-mode: expected tcdrain|wire, got {other:?}")),
                };
            }
            "--drain-margin" => {
                a.drain_margin_ms = next!("--drain-margin")
                    .parse()
                    .map_err(|_| "--drain-margin: not an integer (ms, may be negative)".to_string())?;
            }
            "--parity" => {
                let v = next!("--parity");
                a.parity_even = match v.as_str() {
                    "none" => false,
                    "even" => true,
                    other => return Err(format!("--parity: expected none|even, got {other:?}")),
                }
            }
            "--wait" => {
                a.wait_s = next!("--wait")
                    .parse()
                    .map_err(|_| "--wait: not a number".to_string())?
            }
            "--blocks" => {
                a.blocks = Some(
                    next!("--blocks")
                        .parse()
                        .map_err(|_| "--blocks: not a byte".to_string())?,
                )
            }
            "--keep-options" => a.write_options = true,
            "--skip-options" => a.write_options = false,
            other if other.starts_with('-') => return Err(format!("unknown option {other:?}")),
            other => {
                if a.command.is_none() {
                    a.command = Some(other.to_string());
                } else {
                    a.positional.push(other.to_string());
                }
            }
        }
        i += 1;
    }
    Ok(a)
}

fn run(argv: &[String]) -> Result<(), String> {
    if argv.is_empty() {
        print!("{USAGE}");
        return Ok(());
    }
    let args = parse_args(argv)?;
    let cmd = args.command.clone().ok_or("no command; try --help")?;

    match cmd.as_str() {
        "ports" => cmd_ports(),
        "explain" => cmd_explain(&args),
        "identify" | "info" => cmd_session(&args, Job::Identify),
        "erase" => cmd_session(&args, Job::Erase { blocks: args.blocks }),
        "write" | "flash" => {
            let path = args
                .positional
                .first()
                .ok_or("`write` needs a .hex/.ihx or .bw file")?
                .clone();
            let image = load_flash_input(&path, args.quiet)?;
            cmd_session(
                &args,
                Job::Flash { image, write_options: args.write_options },
            )
        }
        other => Err(format!("unknown command {other:?}; try --help")),
    }
}

fn load_flash_input(path: &str, quiet: bool) -> Result<Vec<u8>, String> {
    if Path::new(path)
        .extension()
        .is_some_and(|ext| ext.eq_ignore_ascii_case("bw"))
    {
        compile_pseudocode(path, quiet)
    } else {
        load_image(path, quiet)
    }
}

/// Compile Brickwright pseudocode with the transpiler embedded in this Rust
/// binary, then feed SDCC's Intel HEX output into the ordinary flash path.
/// Node is deliberately not involved: `stcbsl flash program.bw` is the whole
/// source-to-silicon operation.
#[cfg(feature = "transpile")]
fn compile_pseudocode(path: &str, quiet: bool) -> Result<Vec<u8>, String> {
    use stcbsl::transpile::Transpiler;

    let source = std::fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
    let device = declared_device(&source)
        .ok_or_else(|| format!("{path}: no DEVICE declaration"))?;
    if !device.starts_with("stc89") {
        return Err(format!(
            "{path}: DEVICE {device} is not an STC89 target; this flasher currently implements the STC89 wire protocol"
        ));
    }

    let c = Transpiler::new()
        .map_err(|e| format!("could not start the embedded transpiler: {e}"))?
        .transpile_c(&source, None)
        .map_err(|e| format!("{path}: {e}"))?;
    let build = TempBuild::new()?;
    let c_path = build.path.join("main.c");
    let ihx_path = build.path.join("main.ihx");
    std::fs::write(&c_path, c).map_err(|e| format!("{}: {e}", c_path.display()))?;

    let output = Command::new("sdcc")
        .args([
            "-mmcs51",
            "--iram-size",
            "256",
            "--xram-size",
            "256",
            "--code-size",
            "8192",
            "-o",
        ])
        .arg(&ihx_path)
        .arg(&c_path)
        .output()
        .map_err(|e| format!("could not run sdcc (install SDCC and ensure it is on PATH): {e}"))?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();
        let detail = if !stderr.is_empty() { stderr } else { stdout };
        return Err(format!("sdcc failed for {path}: {detail}"));
    }
    if !quiet {
        println!("compiled: {path} -> Intel HEX with embedded Brickwright + SDCC");
    }
    load_image(
        ihx_path
            .to_str()
            .ok_or_else(|| "temporary build path is not UTF-8".to_string())?,
        quiet,
    )
}

#[cfg(not(feature = "transpile"))]
fn compile_pseudocode(_path: &str, _quiet: bool) -> Result<Vec<u8>, String> {
    Err("this stcbsl build has no `transpile` feature; rebuild with default features".into())
}

fn declared_device(source: &str) -> Option<String> {
    source.lines().find_map(|line| {
        let mut words = line.split_whitespace();
        if words.next()?.eq_ignore_ascii_case("DEVICE") {
            Some(words.next()?.to_ascii_lowercase().replace('_', "-"))
        } else {
            None
        }
    })
}

struct TempBuild {
    path: PathBuf,
}

impl TempBuild {
    fn new() -> Result<Self, String> {
        let nonce = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map_err(|e| format!("clock error while making build directory: {e}"))?
            .as_nanos();
        let path = std::env::temp_dir().join(format!("stcbsl-{}-{nonce}", std::process::id()));
        std::fs::create_dir(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        Ok(Self { path })
    }
}

impl Drop for TempBuild {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}

#[cfg(test)]
mod source_tests {
    use super::declared_device;

    #[test]
    fn device_header_is_case_insensitive_and_normalized() {
        assert_eq!(
            declared_device("# comment\ndeViCe STC89C52RC\nWHEN flag clicked:\n"),
            Some("stc89c52rc".into())
        );
        assert_eq!(
            declared_device("DEVICE stc89_c52rc\n"),
            Some("stc89-c52rc".into())
        );
        assert_eq!(declared_device("WHEN flag clicked:\n"), None);
    }
}

fn load_image(path: &str, quiet: bool) -> Result<Vec<u8>, String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
    let img = ihex::parse(&text).map_err(|e| format!("{path}: {e}"))?;
    if !quiet {
        println!("image: {} — {} bytes", path, img.bytes.len());
        if img.lowest_addr != 0 {
            println!(
                "  note: lowest address is 0x{:04x}, not 0 — the 8051 starts at 0x0000",
                img.lowest_addr
            );
        }
        if img.gap_bytes > 0 {
            // See ihex.rs: gaps are zero-filled, matching the capture.
            println!("  note: {} bytes are gaps in the hex, filled with 0x00", img.gap_bytes);
        }
    }
    Ok(img.bytes)
}

/// Offline: what would be programmed where, and what each block should ack.
/// Needs no port and no chip, so it is the cheap way to sanity-check an image
/// before a bench session.
fn cmd_explain(args: &Args) -> Result<(), String> {
    let path = args.positional.first().ok_or("`explain` needs a .hex file")?;
    let image = load_image(path, args.quiet)?;
    let blocks = Stc89::erase_blocks_for(image.len());
    let region = blocks * ERASE_BLOCK;
    let mut padded = image.clone();
    padded.resize(region, 0xFF);

    println!(
        "erase: {blocks} blocks x {ERASE_BLOCK} B = {region} B  (rule: NN = 2 x ceil(size/512))"
    );
    println!("write: {} blocks x {WRITE_BLOCK} B", region / WRITE_BLOCK);
    for (i, chunk) in padded.chunks(WRITE_BLOCK).enumerate() {
        let addr = i * WRITE_BLOCK;
        let all_ff = chunk.iter().all(|b| *b == 0xFF);
        println!(
            "  0x{addr:04x}  ack 0x{:02x}{}",
            Stc89::block_ack(chunk),
            if all_ff { "   (padding)" } else { "" }
        );
    }
    println!();
    println!(
        "The baud, link-test and option frames depend on the live status packet\n\
         (measured frequency, chip id, current option byte), so they are not shown\n\
         here: this tool never invents an option byte."
    );
    Ok(())
}

#[cfg(not(feature = "serial"))]
fn cmd_ports() -> Result<(), String> {
    Err("built without the `serial` feature".into())
}

#[cfg(feature = "serial")]
fn cmd_ports() -> Result<(), String> {
    let ports = stcbsl::transport::SerialWire::list();
    if ports.is_empty() {
        println!("no serial ports found");
    }
    for p in ports {
        // /dev/tty.* blocks on carrier detect on macOS; /dev/cu.* is the one
        // to use (this repo's README says so at length).
        let hint = if p.contains("/dev/tty.") { "   (prefer the /dev/cu.* twin)" } else { "" };
        println!("{p}{hint}");
    }
    Ok(())
}

#[cfg(not(feature = "serial"))]
fn cmd_session(_args: &Args, _job: Job) -> Result<(), String> {
    Err("built without the `serial` feature: no port support in this build".into())
}

#[cfg(feature = "serial")]
fn cmd_session(args: &Args, job: Job) -> Result<(), String> {
    use serialport::Parity;
    use stcbsl::transport::SerialWire;

    let port = args
        .port
        .clone()
        .ok_or("--port is required (try `stcbsl ports`)")?;
    let opts = SessionOptions {
        handshake_baud: args.handshake_baud,
        transfer_baud: args.transfer_baud,
        ..SessionOptions::default()
    };
    let family = Stc89;
    let parity = if args.parity_even { Parity::Even } else { Parity::None };

    let mut wire = SerialWire::open(&port, opts.handshake_baud, parity)
        .map_err(|e| format!("{port}: {e}"))?;
    let mut log = CliLog { quiet: args.quiet, noise: 0 };

    if !args.quiet {
        println!(
            "waiting up to {} s for the bootloader at {} baud on {port}",
            args.wait_s, opts.handshake_baud
        );
        println!("  >>> POWER-CYCLE THE BOARD NOW (off, then on) <<<");
    }

    let info = driver::handshake(
        &mut wire,
        &family,
        &opts,
        Duration::from_secs(args.wait_s),
        &mut log,
    )
    .map_err(|e| e.to_string())?;
    report_target(&info, opts.handshake_baud);

    let steps = family.plan(&info, &job, &opts).map_err(|e| e.to_string())?;
    if !args.quiet {
        println!("plan: {} frames", steps.len());
    }
    let mut session = Session::new(steps);
    let drain = DrainConfig { mode: args.drain_mode, margin_ms: args.drain_margin_ms };
    driver::run(&mut wire, &mut session, drain, &mut log).map_err(|e| e.to_string())?;

    match job {
        Job::Identify => println!("done — chip released to its application"),
        Job::Erase { .. } => println!("done — code flash erased"),
        Job::Flash { .. } => println!(
            "done — image programmed and the chip released to it.\n\
             There is no read-back on this part, so 'programmed' means every block's\n\
             checksum was acknowledged by the MCU, not that the flash was read back."
        ),
    }
    Ok(())
}

#[cfg(feature = "serial")]
fn report_target(info: &TargetInfo, handshake_baud: u32) {
    println!("target:");
    println!(
        "  model         {}",
        info.model.unwrap_or("unknown (chip id not in our table)")
    );
    println!("  chip id       {}", hex(&info.chip_id));
    println!("  BSL version   {}", info.bsl_version);
    if let (Some(c), Some(e)) = (info.code_flash, info.eeprom_flash) {
        println!("  code flash    {} KB", c / 1024);
        println!("  EEPROM flash  {} KB", e / 1024);
    }
    // §7.1: this is a property of the handshake, not of the board. Say so.
    println!(
        "  clock         {:.3} MHz  (MEASURED by the BSL this handshake, word 0x{:04X} \
         at {} baud — not a stored constant, expect it to move run to run)",
        info.measured_hz as f64 / 1e6,
        info.freq_words[0],
        handshake_baud
    );
    println!(
        "  option byte   {}  (read only — the bit map is not known, so stcbsl only ever \
         echoes it back)",
        hex(&info.option_bytes)
    );
}

#[cfg(feature = "serial")]
struct CliLog {
    quiet: bool,
    noise: usize,
}

#[cfg(feature = "serial")]
impl Log for CliLog {
    fn step(&mut self, label: &str, bytes: &[u8]) {
        if self.quiet {
            return;
        }
        if bytes.len() > 24 {
            println!("-> {label}\n   {} … ({} bytes)", hex(&bytes[..16]), bytes.len());
        } else {
            println!("-> {label}\n   {}", hex(bytes));
        }
    }

    fn reply(&mut self, frame: &Frame) {
        if self.quiet {
            return;
        }
        println!("<- cmd 0x{:02x}, {} payload bytes", frame.cmd, frame.payload.len());
    }

    fn note(&mut self, msg: &str) {
        if !self.quiet {
            println!("   {msg}");
        }
    }

    fn discarded(&mut self, bytes: &[u8]) {
        self.noise += bytes.len();
        if self.quiet {
            return;
        }
        // §5.1.2: on these boards the application prints on the ISP UART, so
        // noise while waiting is normal and expected. Report it as such.
        println!(
            "   discarded {} non-protocol byte(s) [{} total] — normal if the chip is still \
             running an app that prints",
            bytes.len(),
            self.noise
        );
    }
}
