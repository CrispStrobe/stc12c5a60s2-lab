// SPDX-License-Identifier: MIT
//! `stm32bsl` — flash an STM32 over its ROM serial bootloader (AN3155),
//! through the same serial layer `stcbsl` uses. One crate, two families.
//!
//!     stm32bsl --port /dev/cu.usbserial-X fw.bin
//!     stm32bsl --port ... --baud 57600 --base 0x08000000 fw.bin
//!     stm32bsl --list
//!
//! The line is **8E1 (even parity)** — set automatically, unlike the STC
//! side's 8N1. BOOT0 must be HIGH at reset for the ROM to answer; the
//! F030 breakout has a jumper for it. `.bin` only for now: a raw flash
//! image, vectors first (word 0 = SP), which is exactly what
//! stc-compiler's `format: "bin"` for `target: "stm32f030"` emits.

use std::process::ExitCode;

use serialport::Parity;
use stcbsl::an3155::{An3155, PID_STM32F03X};
use stcbsl::transport::SerialWire;

fn die(msg: &str) -> ExitCode {
    eprintln!("stm32bsl: {msg}");
    ExitCode::FAILURE
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut port: Option<String> = None;
    let mut baud: u32 = 115_200;
    let mut base: u32 = 0x0800_0000;
    let mut file: Option<String> = None;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--list" => {
                for p in SerialWire::list() {
                    println!("{p}");
                }
                return ExitCode::SUCCESS;
            }
            "--port" => {
                i += 1;
                port = args.get(i).cloned();
            }
            "--baud" => {
                i += 1;
                baud = match args.get(i).and_then(|s| s.parse().ok()) {
                    Some(b) => b,
                    None => return die("--baud needs a number"),
                };
            }
            "--base" => {
                i += 1;
                let s = match args.get(i) {
                    Some(s) => s.trim_start_matches("0x"),
                    None => return die("--base needs an address"),
                };
                base = match u32::from_str_radix(s, 16) {
                    Ok(a) => a,
                    Err(_) => return die("--base: not a hex address"),
                };
            }
            "--help" | "-h" => {
                println!(
                    "usage: stm32bsl --port <dev> [--baud N] [--base 0xADDR] <image.bin>\n       stm32bsl --list"
                );
                return ExitCode::SUCCESS;
            }
            other if !other.starts_with('-') => file = Some(other.to_string()),
            other => return die(&format!("unknown flag {other}")),
        }
        i += 1;
    }

    let (Some(port), Some(file)) = (port, file) else {
        return die("need --port <dev> and an image.bin (see --help)");
    };
    let image = match std::fs::read(&file) {
        Ok(b) => b,
        Err(e) => return die(&format!("{file}: {e}")),
    };
    if image.len() >= 4 && image[3] != 0x20 {
        // Little-endian word 0 is the initial SP; its high byte must be
        // 0x20 (SRAM). Refuse a wrong file with a sentence, not a brick.
        return die(&format!(
            "{file}: word 0 is not an SRAM stack pointer — not an STM32 flash image (vectors must come first)"
        ));
    }

    // 8E1 — AN3155's line format. This is the one place STM32 differs
    // from the STC wire config.
    let mut wire = match SerialWire::open(&port, baud, Parity::Even) {
        Ok(w) => w,
        Err(e) => return die(&format!("open {port}: {e}")),
    };

    let mut isp = An3155::new(&mut wire);
    eprintln!("stm32bsl: init (BOOT0 must be high, chip fresh out of reset)…");
    match isp.flash(&image, base, |done, total| {
        eprint!("\rstm32bsl: writing {done}/{total} bytes");
    }) {
        Ok((id, bytes)) => {
            eprintln!();
            let note = if id == PID_STM32F03X { " (STM32F03x)" } else { "" };
            println!(
                "flashed {bytes} bytes to 0x{base:08x}, product id 0x{id:03x}{note}; the application is running"
            );
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!();
            die(&e.to_string())
        }
    }
}
