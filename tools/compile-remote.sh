#!/usr/bin/env bash
#
# compile-remote.sh — build an example with the hosted compiler instead of a
# local SDCC install.
#
#   ./tools/compile-remote.sh                       # C: 01-blink -> 01-blink.hex
#   ./tools/compile-remote.sh 01-blink out.hex      # explicit
#   ./tools/compile-remote.sh -p 02-button          # pseudocode/02-button.bw
#   FOSC=12000000 ./tools/compile-remote.sh         # override the clock (C only)
#   API=http://localhost:3000 ./tools/compile-remote.sh
#
# The service compiles a single translation unit, so this inlines the repo's
# own headers first: it strips the `#include "..."` lines and concatenates
# include/board.h + include/delay.h + the example, in that order. System
# includes like <stc12.h> are left alone — the server has those.
#
set -euo pipefail

LANGUAGE=c
if [[ "${1:-}" == "-p" || "${1:-}" == "--pseudocode" ]]; then
  LANGUAGE=pseudocode
  shift
fi

EXAMPLE="${1:-01-blink}"
OUT="${2:-${EXAMPLE}.hex}"
FOSC="${FOSC:-11059200}"
API="${API:-https://stc-compiler.vercel.app}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

if [[ "$LANGUAGE" == pseudocode ]]; then
  SRC="$ROOT/pseudocode/$EXAMPLE.bw"
  [[ -f "$SRC" ]] || { echo "no such example: pseudocode/$EXAMPLE.bw" >&2; exit 1; }
  # Pseudocode is a single file already, and carries its own CLOCK.
  cp "$SRC" "$WORK/amalgamated.c"
  say "Using pseudocode/$EXAMPLE.bw ..."
else

SRC="$ROOT/src/$EXAMPLE/main.c"
[[ -f "$SRC" ]] || { echo "no such example: src/$EXAMPLE/main.c" >&2; exit 1; }

say "Amalgamating $EXAMPLE ..."
python3 - "$ROOT" "$SRC" "$WORK/amalgamated.c" <<'PY'
import pathlib, re, sys

root, src, out = (pathlib.Path(p) for p in sys.argv[1:4])

def clean(path):
    """Drop local includes only. Header guards are left alone — they behave
    correctly when the files are simply concatenated, and removing one half of
    a guard is how you get 'unterminated #ifndef'."""
    return re.sub(r'^\s*#include\s+"[^"]+"\s*$\n?', "", path.read_text(), flags=re.M)

parts = [clean(root / "include" / name) for name in ("board.h", "delay.h")]
parts.append(clean(src))
out.write_text("\n".join(parts))
PY
fi

say "Compiling via $API ($LANGUAGE, FOSC=$FOSC) ..."
python3 - "$API" "$FOSC" "$WORK/amalgamated.c" "$OUT" "$LANGUAGE" <<'PY'
import base64, json, pathlib, sys, urllib.error, urllib.request

api, fosc, source, out = sys.argv[1], int(sys.argv[2]), pathlib.Path(sys.argv[3]), sys.argv[4]
language = sys.argv[5]

payload = json.dumps({
    "code": source.read_text(),
    "language": language,
    "target": "stc12c5a60s2",
    # Pseudocode carries its own CLOCK line, which wins over this anyway.
    "fosc": None if language == "pseudocode" else fosc,
}).encode()

try:
    request = urllib.request.Request(api + "/compile", payload,
                                     {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=120) as response:
        data = json.load(response)
except (urllib.error.URLError, OSError) as exc:
    raise SystemExit(f"\033[31mcould not reach {api}:\033[0m {exc}")

if not data.get("success"):
    raise SystemExit("\033[31mcompile failed:\033[0m\n" + (data.get("error") or ""))

pathlib.Path(out).write_bytes(base64.b64decode(data["base64"]))
print(f"wrote {out} ({data['bytes']} bytes)")
if data.get("c"):
    generated = pathlib.Path(out).with_suffix(".c")
    generated.write_text(data["c"])
    print(f"      {generated} (generated C, for reference)")
for line in data.get("memory", "").splitlines():
    if "ROM/EPROM/FLASH" in line:
        print("  " + " ".join(line.split()))
PY

# Surface BW_STUB and BW_COLLISION warnings from the generated C.
# These mark blocks that compile but do nothing on hardware (stubs) or
# two drivers claiming the same pin/timer (collisions).  Do not fail the
# build — the program is 90% fine; the user just needs to know which 10%
# will be inert.
GENERATED="${OUT%.hex}.c"
if [[ -f "$GENERATED" ]]; then
    grep -n 'BW_STUB\|BW_COLLISION' "$GENERATED" | while IFS= read -r match; do
        printf '\033[1;33mwarning:\033[0m %s\n' "$match" >&2
    done
fi

cat <<EOF

Flash it with:
    stcgal -P stc12 -p /dev/cu.usbserial-XXXX -l 2400 -b 115200 $OUT
EOF
