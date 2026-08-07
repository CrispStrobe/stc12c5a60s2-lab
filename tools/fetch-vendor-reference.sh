#!/usr/bin/env bash
#
# fetch-vendor-reference.sh — download the ICStation 4x4x4 LED cube package
# (product 4681), the most complete first-party STC12C5A60S2 example in
# circulation: a Keil uVision project, STC's own register header, the assembly
# guide, and the schematic.
#
# It lands in ./vendor-reference/, which is gitignored. That is deliberate: the
# package carries NO LICENCE, so it can be read for reference but not
# redistributed or vendored into this repo.
#
# Note the filenames inside are GBK-encoded Chinese, which /usr/bin/unzip
# mangles on macOS — hence the Python extractor below.
#
set -euo pipefail

# The URL in rgm3/ledcube444's README (/ebay/IC/All data modules/4681.zip) has
# been dead since at least 2018. ICStation moved it here:
URL="https://www.icstation.com/product_document/Download/4681.zip"
# Wayback fallback, captured 2018-01-08:
FALLBACK="https://web.archive.org/web/20180108212426id_/http://www.icstation.com/product_document/Download/4681.zip"

DEST="$(cd "$(dirname "$0")/.." && pwd)/vendor-reference"
ZIP="$DEST/4681.zip"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

mkdir -p "$DEST"

if [[ -f "$ZIP" ]]; then
  say "Already downloaded: $ZIP"
else
  say "Downloading from icstation.com ..."
  if ! curl -fsSL --max-time 180 -o "$ZIP" "$URL"; then
    say "Live site failed; trying the Wayback Machine ..."
    curl -fsSL --max-time 300 -o "$ZIP" "$FALLBACK"
  fi
fi

say "Extracting (decoding GBK filenames) ..."
python3 - "$ZIP" "$DEST/4681" <<'PY'
import pathlib, sys, zipfile

src, out = sys.argv[1], pathlib.Path(sys.argv[2])
with zipfile.ZipFile(src) as z:
    for info in z.infolist():
        try:
            name = info.filename.encode("cp437").decode("gbk")
        except (UnicodeDecodeError, UnicodeEncodeError):
            name = info.filename
        dest = out / name
        if info.is_dir():
            dest.mkdir(parents=True, exist_ok=True)
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(z.read(info))
        print(f"  {name}")
PY

cat <<EOF

Done. Worth looking at:

  vendor-reference/4681/Code/main.c            1773 lines of Keil C51
  vendor-reference/4681/Code/stc12c5a60s2.h    STC's own register header
  vendor-reference/4681/Code/main.hex          a known-good image to flash
  vendor-reference/4681/4x4x4 light cube welded Guide.pdf
  vendor-reference/4681/Circuit Diagram.zip    schematic (Protel/Altium + PDF + JPG)

Two things to know before borrowing from it:

  1. That header is Keil syntax ("sfr P0 = 0x80;", "sbit"). SDCC will not
     compile it. Use SDCC's own <stc12.h> instead -- the addresses agree.
  2. No licence, so nothing here can be copied into this repo.
EOF
