// SPDX-License-Identifier: MIT
//! `bwc` — the Brickwright compiler front end, self-contained.
//!
//! Turns Brickwright pseudocode into C by running the ONE JS transpiler
//! (`SB3Creator`) inside an embedded QuickJS engine — no node, no network, no
//! Python. It is the same transpiler the browser and the `bw` node CLI run,
//! compiled into this binary as a bundle (see `src/transpile.rs`).
//!
//!     bwc <file.bw>                 pseudocode -> C on stdout
//!     bwc <file.bw> -o out.c        ... to a file
//!     bwc <file.bw> --device pico   retarget first, then emit C
//!     bwc --devices                 list the retargetable device ids
//!     bwc --retarget <file.bw> --device z80    emit retargeted pseudocode
//!
//! C -> binary (the flash image) is a separate toolchain step
//! (arm-none-eabi-gcc / sdcc / cc65), not this front end; flashing is
//! `stcbsl` / `stm32bsl`. A later slice unifies them under one `bw`.

use std::process::ExitCode;

use stcbsl::transpile::Transpiler;

fn die(msg: &str) -> ExitCode {
    eprintln!("bwc: {msg}");
    ExitCode::FAILURE
}

fn usage() -> ExitCode {
    eprintln!(
        "usage: bwc <file.bw> [--device ID] [-o out.c]\n\
         \x20      bwc --retarget <file.bw> --device ID [-o out.bw]\n\
         \x20      bwc --devices"
    );
    ExitCode::FAILURE
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.is_empty() {
        return usage();
    }

    let mut file: Option<String> = None;
    let mut device: Option<String> = None;
    let mut out: Option<String> = None;
    let mut list_devices = false;
    let mut retarget_only = false;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--devices" => list_devices = true,
            "--retarget" => retarget_only = true,
            "--device" | "-d" => {
                i += 1;
                match args.get(i) {
                    Some(v) => device = Some(v.clone()),
                    None => return die("--device needs an id (try --devices)"),
                }
            }
            "-o" | "--out" => {
                i += 1;
                match args.get(i) {
                    Some(v) => out = Some(v.clone()),
                    None => return die("-o needs a path"),
                }
            }
            "-h" | "--help" => return usage(),
            s if s.starts_with('-') => return die(&format!("unknown flag: {s}")),
            s => {
                if file.is_some() {
                    return die(&format!("unexpected extra argument: {s}"));
                }
                file = Some(s.to_string());
            }
        }
        i += 1;
    }

    let transpiler = match Transpiler::new() {
        Ok(t) => t,
        Err(e) => return die(&format!("could not start the embedded transpiler: {e}")),
    };

    if list_devices {
        match transpiler.devices() {
            Ok(ids) => {
                for id in ids {
                    println!("{id}");
                }
                return ExitCode::SUCCESS;
            }
            Err(e) => return die(&e.to_string()),
        }
    }

    let path = match &file {
        Some(p) => p,
        None => return die("no input file (a .bw pseudocode file), or --devices"),
    };
    let source = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => return die(&format!("cannot read {path}: {e}")),
    };

    if retarget_only {
        let dev = match &device {
            Some(d) => d,
            None => return die("--retarget needs --device ID"),
        };
        match transpiler.retarget(&source, dev) {
            Ok(pseudo) => return emit(&pseudo, out.as_deref(), path),
            Err(e) => return die(&e.to_string()),
        }
    }

    match transpiler.transpile_c(&source, device.as_deref()) {
        Ok(c) => emit(&c, out.as_deref(), path),
        Err(e) => die(&e.to_string()),
    }
}

/// Write `text` to `out` (or stdout when `None`). `_src` names the input in the
/// "wrote" line so a shell log reads which file produced which output.
fn emit(text: &str, out: Option<&str>, _src: &str) -> ExitCode {
    match out {
        Some(path) => match std::fs::write(path, text) {
            Ok(()) => {
                eprintln!("bwc: wrote {path} ({} bytes)", text.len());
                ExitCode::SUCCESS
            }
            Err(e) => die(&format!("cannot write {path}: {e}")),
        },
        None => {
            print!("{text}");
            ExitCode::SUCCESS
        }
    }
}
