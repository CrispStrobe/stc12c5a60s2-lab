// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CrispStrobe
//
//! The embedded transpiler: prove the Rust binary really runs the one JS
//! transpiler (SB3Creator) in-process and gets correct, current output.
//!
//! These are the Rust-side twin of sb3-creator's test/embed-bundle.test.mjs:
//! that one proves the bundle runs in a bare JS engine and matches node; this
//! one proves rquickjs is that bare engine and the vendored bundle carries the
//! current codegen (the STM32 TIM3 fix among it).

#![cfg(feature = "transpile")]

use stcbsl::transpile::Transpiler;

const STM32_BLINK: &str = "DEVICE STM32F030\n\
                           PIN led = PA0 OUTPUT\n\
                           PIN pot = PA5 ANALOG\n\n\
                           WHEN flag clicked:\n\
                           \x20 forever:\n\
                           \x20   set led to HIGH\n\
                           \x20   wait 500 ms\n\
                           \x20   set led to LOW\n\
                           \x20   wait 500 ms";

#[test]
fn transpiles_stm32_pseudocode_to_c() {
    let t = Transpiler::new().expect("start embedded transpiler");
    let c = t.transpile_c(STM32_BLINK, None).expect("transpile");
    assert!(c.contains("@bw device stm32f030"), "targets STM32F030");
    assert!(c.contains("RCC") || c.contains("GPIOA"), "emits ARM GPIO setup");
    assert!(c.contains("ADC"), "emits the ADC for the analog pin");
}

#[test]
fn stm32_uses_tim3_not_the_pico_timebase() {
    // The vendored bundle must carry the fix: STM32 forces the TIM3-tick
    // scheduler and never the RP2040 TIMER (BW_TIMER_TIMELR). A stale vendor
    // would fail here, which is exactly what sync-transpiler --check guards.
    let t = Transpiler::new().unwrap();
    let c = t.transpile_c(STM32_BLINK, None).unwrap();
    assert!(c.contains("TIM3"), "STM32 timebase is TIM3");
    assert!(!c.contains("BW_TIMER_TIME"), "must not leak the pico microsecond timer");
}

#[test]
fn retarget_moves_pins_to_another_device() {
    let t = Transpiler::new().unwrap();
    let out = t.retarget(STM32_BLINK, "pico").expect("retarget to pico");
    assert!(out.contains("DEVICE PICO"), "header switched to the pico");
    assert!(out.contains("GP"), "pins renamed to the pico's GPn scheme");
}

#[test]
fn a_refused_retarget_surfaces_the_js_message() {
    let t = Transpiler::new().unwrap();
    let err = t.retarget(STM32_BLINK, "nonsuch").unwrap_err();
    let msg = err.to_string();
    assert!(msg.contains("nonsuch"), "message names the bad device: {msg}");
    // The clean message, not a raw eval stack trace.
    assert!(!msg.contains("eval_script"), "no stack trace leaked: {msg}");
}

#[test]
fn devices_lists_the_retarget_pool() {
    let t = Transpiler::new().unwrap();
    let ids = t.devices().expect("device list");
    assert!(ids.iter().any(|d| d == "stm32f030"), "stm32f030 present");
    assert!(ids.iter().any(|d| d == "pico"), "pico present");
    assert!(ids.len() >= 8, "the full pool, got {}", ids.len());
}
