#!/usr/bin/env bash
#
# find-port.sh — work out which /dev/cu.* is the USB-TTL adapter.
#
# Run it once with the adapter unplugged and once plugged in; the device that
# appears is the one you want. Pass --watch to have it do that for you.
#
set -euo pipefail

list() {
  ls -1 /dev/cu.* 2>/dev/null | grep -Ev 'Bluetooth|debug-console|wlan-debug' || true
}

if [[ "${1:-}" == "--watch" ]]; then
  echo "Unplug the USB-TTL adapter, then press Return."
  read -r _
  before="$(list)"
  echo "Now plug it in, wait a second, then press Return."
  read -r _
  after="$(list)"

  new="$(comm -13 <(echo "$before" | sort) <(echo "$after" | sort))"
  if [[ -n "$new" ]]; then
    echo
    echo "Found it:"
    echo "$new" | sed 's/^/    /'
    echo
    echo "Use it like this:"
    echo "$new" | head -n1 | sed 's|^|    make flash PORT=|'
  else
    echo
    echo "Nothing new appeared. Either the adapter needs its vendor driver,"
    echo "or the cable is charge-only. See README section 2.1."
  fi
  exit 0
fi

echo "Candidate serial devices:"
found="$(list)"
if [[ -n "$found" ]]; then
  echo "$found" | sed 's/^/    /'
  echo
  echo "Not sure which one? Run:  ./tools/find-port.sh --watch"
else
  echo "    (none found)"
  echo
  echo "Plug in the adapter and try again. On macOS 11+ the CH340, CP210x and"
  echo "FT232 drivers are all built in, so no install should be needed."
fi

echo
echo "Note: always use /dev/cu.*, never /dev/tty.* — the tty node blocks on"
echo "carrier detect and the flash will hang."
