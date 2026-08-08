# STC12C5A60S2 — build and flash
#
#   make                      build the default example (01-blink)
#   make EXAMPLE=01-blink     build a specific example
#   make ports                list serial ports that look like a USB-TTL adapter
#   make flash                build, then flash over ISP (power-cycle when asked)
#   make erase                erase the chip
#   make info                 read back the chip's ID and settings, flash nothing
#   make clean                remove build/
#
# Every variable below can be overridden on the command line, e.g.
#   make flash PORT=/dev/cu.usbserial-1420 FOSC=12000000

EXAMPLE   ?= 01-blink
FOSC      ?= 11059200

# Which chip. The two parts share a core, port modes and an ADC block, but not
# their RAM size or their baud-rate source — see docs/STC15-PERIPHERAL-MODEL.md.
#
#   make PART=stc15f2k60s2 EXAMPLE=10-live-firmware
#
# On an STC15 the internal RC is trimmable to 0.3%, and 11.0592 MHz is one of
# the values the datasheet names, so FOSC is a known quantity with no crystal
# fitted:  stcgal -t 11059
PART      ?= stc12c5a60s2

# ------------------------------------------------------------------ toolchain
SDCC      ?= sdcc
PACKIHX   ?= packihx
STCGAL    ?= stcgal

# --------------------------------------------------------------------- target
# STC12C5A60S2: 60 KB flash, 256 B internal RAM + 1024 B auxiliary (XRAM).
# STC15F2K60S2: 60 KB flash, 256 B internal RAM + 1792 B auxiliary (XRAM).
ifeq ($(PART),stc15f2k60s2)
  XRAM     := 1792
  PARTDEF  := -DPART_STC15F2K60S2=1
  PROTOCOL ?= stc15
else ifeq ($(PART),stc12c5a60s2)
  XRAM     := 1024
  PARTDEF  := -DPART_STC12C5A60S2=1
  PROTOCOL ?= stc12
else
  $(error unknown PART "$(PART)" - try stc12c5a60s2 or stc15f2k60s2)
endif

SDCCFLAGS ?= -mmcs51 --std-c99 \
             --iram-size 256 --xram-size $(XRAM) --code-size 61440 \
             -I include -DFOSC_HZ=$(FOSC)UL $(PARTDEF)

# ---------------------------------------------------------------------- flash
# PROTOCOL already defaults per PART above (stc12 / stc15); override it here
# only if stcgal's auto-detection disagrees with the part you actually have.
BAUD      ?= 115200
HANDSHAKE ?= 2400

# Best guess at the adapter: CH340 shows up as cu.wchusbserial*, CP210x as
# cu.SLAB_USBtoUART or cu.usbserial-*, FTDI as cu.usbserial-*.
PORT      ?= $(firstword $(wildcard \
               /dev/cu.usbserial-* \
               /dev/cu.wchusbserial* \
               /dev/cu.SLAB_USBtoUART* \
               /dev/cu.usbmodem*))

# ----------------------------------------------------------------------- paths
SRC       := src/$(EXAMPLE)/main.c
HEADERS   := $(wildcard include/*.h)
# Build output is per-part: the same example compiled for two chips must not
# share a .hex, or you eventually flash an STC12 image to an STC15.
BUILD     := build/$(PART)/$(EXAMPLE)
IHX       := $(BUILD)/main.ihx
HEX       := $(BUILD)/$(EXAMPLE).hex

.PHONY: all clean flash erase info ports size check-port test

all: $(HEX)

# ------------------------------------------------------------------- tests
# The framing codec in include/live-frame.h is plain C on purpose, so the
# parser that runs on the chip is the one tested here. tools/live-monitor.py
# implements the same wire format independently, and the two are diffed
# against each other — one format, two implementations, which is the only
# way to find out whether it is written down clearly enough.
CC        ?= cc
TESTBIN   := build/tests/frame_test

$(TESTBIN): tests/frame_test.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) -Wall -Wextra -O2 -I include -o $@ $<

test: $(TESTBIN)
	@echo "== C codec (the one that runs on the chip) =="
	@./$(TESTBIN)
	@echo
	@echo "== host codec, and C-vs-Python agreement =="
	@./tools/live-monitor.py --selftest --vectors-from ./$(TESTBIN)

$(IHX): $(SRC) $(HEADERS)
	@mkdir -p $(BUILD)
	$(SDCC) $(SDCCFLAGS) -o $(BUILD)/ $(SRC)

$(HEX): $(IHX)
	$(PACKIHX) $< > $@
	@echo "built $@"

size: $(IHX)
	@grep -E "^(Name|Area|CODE|DATA|XRAM)" $(BUILD)/main.mem 2>/dev/null || cat $(BUILD)/main.mem

ports:
	@echo "Likely USB-TTL adapters:"
	@ls -1 /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem* \
	    2>/dev/null | sed 's/^/  /' || true
	@ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem* \
	    >/dev/null 2>&1 || echo "  (none - plug in the adapter, or run ./tools/find-port.sh --watch)"
	@echo
	@echo "All other serial devices (Bluetooth pairings, debug consoles - not these):"
	@ls -1 /dev/cu.* 2>/dev/null \
	    | grep -Ev 'usbserial|wchusbserial|SLAB_USBtoUART|usbmodem' | sed 's/^/  /' || true
	@echo
	@echo "PORT is currently: $(if $(PORT),$(PORT),<none - pass PORT=/dev/cu.xxx>)"

check-port:
	@test -n "$(PORT)" || { \
	  echo "No serial port found. Plug in the USB-TTL adapter, then:"; \
	  echo "  make ports"; \
	  echo "  make flash PORT=/dev/cu.usbserial-XXXX"; \
	  exit 1; }

flash: $(HEX) check-port
	@echo "Flashing $(HEX) via $(PORT) ..."
	@echo ">>> Power-cycle the MCU now (unplug/replug its VCC) <<<"
	$(STCGAL) -P $(PROTOCOL) -p $(PORT) -l $(HANDSHAKE) -b $(BAUD) $(HEX)

erase: check-port
	@echo ">>> Power-cycle the MCU now <<<"
	$(STCGAL) -P $(PROTOCOL) -p $(PORT) -l $(HANDSHAKE) -b $(BAUD) -e

info: check-port
	@echo ">>> Power-cycle the MCU now <<<"
	$(STCGAL) -P $(PROTOCOL) -p $(PORT) -l $(HANDSHAKE) -b $(BAUD)

clean:
	rm -rf build/
