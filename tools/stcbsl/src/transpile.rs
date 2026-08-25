// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CrispStrobe
//
//! The embedded JS transpiler.
//!
//! Brickwright has exactly ONE transpiler and it is JavaScript: `SB3Creator`,
//! the same engine the browser and the `bw` node CLI run. This module runs it
//! *inside the Rust binary* by embedding QuickJS (via `rquickjs`, MIT) and
//! eval'ing a bundle built from `SB3Creator` (see `vendor/bw-transpiler.embed.js`,
//! produced by sb3-creator's `scripts/bundle-embed.mjs`). So a self-contained
//! Rust CLI turns pseudocode into C with no node subprocess, no network, and no
//! Python — and never drifts from the app, because it *is* the app's transpiler.
//!
//! What is NOT here: C -> binary (the flash image). That is compilation
//! (arm-none-eabi-gcc / sdcc / cc65), a toolchain step, not the transpiler.
//!
//! The bundle exposes three globals (see the sb3-creator embed entry):
//!
//! * `bwTranspileC(pseudocode, device)` -> C source
//! * `bwRetarget(pseudocode, device)`   -> retargeted pseudocode
//! * `bwDevices()`                       -> array of device ids
//!
//! Each throws a JS `Error` on refusal; we surface the message across the
//! boundary as [`TranspileError::Js`].

use rquickjs::{CatchResultExt, CaughtError, Context, Function, Runtime};

/// Pull the readable half out of a caught JS error: an `Error` instance's
/// `.message` (e.g. "retarget refused: ...") without the eval stack trace that
/// `Display` would tack on. Non-Error throws fall back to their string form.
fn js_message(err: &CaughtError<'_>) -> String {
    match err {
        CaughtError::Exception(exc) => exc
            .message()
            .filter(|m| !m.is_empty())
            .unwrap_or_else(|| exc.to_string()),
        other => other.to_string(),
    }
}

/// The transpiler bundle, compiled into the binary. Regenerate and re-vendor
/// with sb3-creator's `npm run bundle:embed` + `tools/stcbsl/sync-transpiler.mjs`.
const BUNDLE: &str = include_str!("../vendor/bw-transpiler.embed.js");

/// A transpiler backed by an embedded QuickJS runtime with the bundle loaded.
///
/// Construction runs the bundle once (defining the globals); each call then
/// re-enters the same context. The QuickJS runtime is not `Send`, so a
/// `Transpiler` is single-threaded — make one per thread that needs it.
pub struct Transpiler {
    // Order matters for drop: the context must outlive nothing that borrows it
    // here, and Runtime must outlive Context. rquickjs holds the runtime alive
    // internally via the context, but we keep it to be explicit.
    context: Context,
    _runtime: Runtime,
}

/// Why a transpile failed.
#[derive(Debug)]
pub enum TranspileError {
    /// The QuickJS engine could not be created or the bundle failed to load.
    Engine(String),
    /// The transpiler threw — e.g. a parse error or a refused retarget. The
    /// string is the JS `Error.message` (or a rendered exception).
    Js(String),
}

impl std::fmt::Display for TranspileError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            TranspileError::Engine(m) => write!(f, "transpiler engine: {m}"),
            TranspileError::Js(m) => write!(f, "{m}"),
        }
    }
}

impl std::error::Error for TranspileError {}

type Result<T> = std::result::Result<T, TranspileError>;

impl Transpiler {
    /// Build a transpiler: start QuickJS and load the bundle.
    pub fn new() -> Result<Self> {
        let runtime = Runtime::new().map_err(|e| TranspileError::Engine(e.to_string()))?;
        let context = Context::full(&runtime).map_err(|e| TranspileError::Engine(e.to_string()))?;
        context.with(|ctx| {
            // Run the IIFE; it hangs bwTranspileC / bwRetarget / bwDevices on
            // globalThis and sets bwEmbedReady. `.catch()` turns a thrown JS
            // value into a readable message instead of an opaque Exception.
            ctx.eval::<(), _>(BUNDLE)
                .catch(&ctx)
                .map_err(|e| TranspileError::Engine(format!("loading bundle: {}", js_message(&e))))?;
            let ready: bool = ctx
                .globals()
                .get("bwEmbedReady")
                .map_err(|e| TranspileError::Engine(e.to_string()))?;
            if !ready {
                return Err(TranspileError::Engine("bundle did not initialise".into()));
            }
            Ok(())
        })?;
        Ok(Transpiler { context, _runtime: runtime })
    }

    /// Transpile pseudocode to C for the device declared in the source (via a
    /// `DEVICE` header). When `device` is `Some`, the program is retargeted to
    /// that id first. Uses the proven `debug:true` codegen form.
    pub fn transpile_c(&self, pseudocode: &str, device: Option<&str>) -> Result<String> {
        self.call_string("bwTranspileC", pseudocode, device.unwrap_or(""))
    }

    /// Retarget a program's hardware declarations to another device id.
    pub fn retarget(&self, pseudocode: &str, device: &str) -> Result<String> {
        self.call_string("bwRetarget", pseudocode, device)
    }

    /// The list of retargetable device ids.
    pub fn devices(&self) -> Result<Vec<String>> {
        self.context.with(|ctx| {
            let f: Function = ctx
                .globals()
                .get("bwDevices")
                .map_err(|e| TranspileError::Engine(e.to_string()))?;
            f.call::<_, Vec<String>>(())
                .catch(&ctx)
                .map_err(|e| TranspileError::Js(js_message(&e)))
        })
    }

    /// Call a `(string, string) -> string` global, surfacing a JS throw as
    /// [`TranspileError::Js`] with the thrown Error's message.
    fn call_string(&self, name: &str, a: &str, b: &str) -> Result<String> {
        self.context.with(|ctx| {
            let f: Function = ctx
                .globals()
                .get(name)
                .map_err(|e| TranspileError::Engine(format!("no global {name}: {e}")))?;
            f.call::<_, String>((a, b))
                .catch(&ctx)
                .map_err(|e| TranspileError::Js(js_message(&e)))
        })
    }
}
