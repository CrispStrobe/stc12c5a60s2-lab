#!/usr/bin/env bash
#
# setup-macos.sh — install everything needed to build and flash for the
# STC12C5A60S2 on macOS. Safe to run repeatedly.
#
set -euo pipefail

say()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m  %s\n' "$*"; }
die()  { printf '\033[1;31mxx\033[0m  %s\n' "$*" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || die "This script is for macOS."

command -v brew >/dev/null 2>&1 || die \
  "Homebrew not found. Install it from https://brew.sh and re-run."

# ---------------------------------------------------------------- compiler
if command -v sdcc >/dev/null 2>&1; then
  say "sdcc already installed: $(sdcc --version | head -n1)"
else
  say "Installing sdcc (compiler, packihx, makebin)..."
  brew install sdcc
fi

sdcc --version 2>/dev/null | head -n1 | grep -q mcs51 \
  || warn "sdcc does not list the mcs51 target — that is unexpected."

# ------------------------------------------------------------------ header
HDR="$(brew --prefix)/share/sdcc/include/mcs51/stc12.h"
if [[ -f "$HDR" ]]; then
  say "Register header present: $HDR"
else
  warn "Could not find stc12.h at $HDR — check your sdcc install."
fi

# ----------------------------------------------------------------- flasher
if command -v stcgal >/dev/null 2>&1; then
  say "stcgal already installed: $(stcgal --version)"
else
  command -v pipx >/dev/null 2>&1 || { say "Installing pipx..."; brew install pipx; }
  say "Ensuring pipx is on PATH..."
  pipx ensurepath >/dev/null 2>&1 || true
  say "Installing stcgal..."
  pipx install stcgal
  command -v stcgal >/dev/null 2>&1 \
    || warn "stcgal installed but not on PATH yet — open a new shell, or add ~/.local/bin to PATH."
fi

# ------------------------------------------------------------------- ports
say "Serial devices that could be a USB-TTL adapter:"
ls -1 /dev/cu.* 2>/dev/null | grep -Ev 'Bluetooth|debug-console|wlan-debug' \
  || echo "    (none — plug in the adapter and re-run './tools/find-port.sh')"

cat <<'EOF'

Done. Next:

    make            build the blink example
    make ports      confirm which /dev/cu.* is your adapter
    make info       talk to the chip (power-cycle it when prompted)
    make flash      build and flash

Remember: the STC bootloader only listens right after a COLD power-on.
Start the command first, then interrupt and restore VCC.
EOF
