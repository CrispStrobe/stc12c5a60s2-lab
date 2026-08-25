// SPDX-License-Identifier: MIT
//
// bweep — a serial-upload EEPROM burner for the BrickWright fleet.
//
// Burns a parallel EEPROM (28C16 / 28C64 / 28C256) from a ROM image sent
// over USB serial, so `bw flash` and the browser IDE can program the chip
// that goes into a 6502 / Z80 breadboard computer — the one family we
// emulate (eater6502) but could not flash, because a breadboard machine
// has no bootloader: the ROM is burned on a bench programmer and moved.
//
// HARDWARE is Ben Eater's EEPROM programmer, unchanged and unimproved:
// two 74HC595 shift registers drive the 15 address lines plus the
// EEPROM's ~OE, and eight Arduino pins are the data bus. That circuit is
// MIT (github.com/beneater/eeprom-programmer) and its shift-out / read /
// write routines below follow it faithfully — the hardware is his, and
// the wiring is a fact of the board, not ours to reinvent.
//
// What THIS sketch adds, and why it exists separately: a framed SERIAL
// UPLOAD PROTOCOL. Ben Eater's sketch bakes the data into a `const`
// array and you re-upload the sketch to change it; that is perfect for
// teaching and useless as a target for a compiler. Here the image
// arrives over the wire, page by page, verified, so the host tools drive
// it like any other flasher.
//
// PROTOCOL (host <-> programmer, 115200 8N1), every reply one byte:
//   0x79 ACK   0x1f NACK   (the AN3155 values, so one host log reads all
//                           the fleet's flashers)
//   'V'                     -> emits "BWEEP1\n" then ACK   (identify)
//   'E'                     -> ACK once the whole chip reads 0xFF is NOT
//                             done here; erase is implicit in page writes
//   'W' aHi aLo n d0..d(n-1) ck   -> write n bytes at addr; ck = XOR of
//                             (n, aHi, aLo, d0..); ACK when written AND
//                             read back identical, else NACK
//   'Q'                     -> ACK, end of session
// n is 1..64 (the 28C256 page-write window). The host paginates on page
// boundaries so a write never straddles two pages.
//
// SILICON STATUS: protocol-tested against a host mock; no chip has been
// burned with it yet. The wiring is Ben Eater's, proven by thousands of
// builds; the serial layer is new and owed a bench run.

#define SHIFT_DATA   2
#define SHIFT_CLK    3
#define SHIFT_LATCH  4
#define EEPROM_D0    5
#define EEPROM_D7   12
#define WRITE_EN    13   // ~WE, active low

#define ACK 0x79
#define NACK 0x1f

// Ben Eater's address+control shift-out: the low byte then the high
// byte, high byte's top bit carrying ~OE so a write leaves the output
// disabled. LSBFIRST both bytes, latch after.
void setAddress(int address, bool outputEnable) {
  shiftOut(SHIFT_DATA, SHIFT_CLK, MSBFIRST, (address >> 8) | (outputEnable ? 0x00 : 0x80));
  shiftOut(SHIFT_DATA, SHIFT_CLK, MSBFIRST, address);
  digitalWrite(SHIFT_LATCH, LOW);
  digitalWrite(SHIFT_LATCH, HIGH);
  digitalWrite(SHIFT_LATCH, LOW);
}

byte readEEPROM(int address) {
  for (int pin = EEPROM_D0; pin <= EEPROM_D7; pin += 1) pinMode(pin, INPUT);
  setAddress(address, /*outputEnable*/ true);
  byte data = 0;
  for (int pin = EEPROM_D7; pin >= EEPROM_D0; pin -= 1) data = (data << 1) + digitalRead(pin);
  return data;
}

void writeEEPROM(int address, byte data) {
  setAddress(address, /*outputEnable*/ false);
  for (int pin = EEPROM_D0; pin <= EEPROM_D7; pin += 1) pinMode(pin, OUTPUT);
  for (int pin = EEPROM_D0; pin <= EEPROM_D7; pin += 1) {
    digitalWrite(pin, data & 1);
    data = data >> 1;
  }
  digitalWrite(WRITE_EN, LOW);
  delayMicroseconds(1);
  digitalWrite(WRITE_EN, HIGH);
  delay(10);   // the 28C256's internal write cycle (data polling would be
               // faster, but 10 ms is safe on every part in the family)
}

// ---- the serial protocol -------------------------------------------------

// Block until n bytes are read, or timeout; returns how many arrived.
int readBytes(byte *buf, int n, unsigned long timeoutMs) {
  int got = 0;
  unsigned long start = millis();
  while (got < n) {
    if (Serial.available()) buf[got++] = Serial.read();
    else if (millis() - start > timeoutMs) break;
  }
  return got;
}

void setup() {
  Serial.begin(115200);
  digitalWrite(WRITE_EN, HIGH);
  pinMode(WRITE_EN, OUTPUT);
  digitalWrite(SHIFT_LATCH, LOW);
  pinMode(SHIFT_DATA, OUTPUT);
  pinMode(SHIFT_CLK, OUTPUT);
  pinMode(SHIFT_LATCH, OUTPUT);
}

void loop() {
  if (!Serial.available()) return;
  byte cmd = Serial.read();

  if (cmd == 'V') {
    Serial.print("BWEEP1\n");
    Serial.write(ACK);
  } else if (cmd == 'Q') {
    Serial.write(ACK);
  } else if (cmd == 'W') {
    byte hdr[3];
    if (readBytes(hdr, 3, 1000) != 3) { Serial.write(NACK); return; }
    int addr = (hdr[0] << 8) | hdr[1];
    int n = hdr[2];
    if (n < 1 || n > 64) { Serial.write(NACK); return; }
    byte data[64];
    if (readBytes(data, n, 1000) != n) { Serial.write(NACK); return; }
    byte ck[1];
    if (readBytes(ck, 1, 1000) != 1) { Serial.write(NACK); return; }
    byte want = n ^ hdr[0] ^ hdr[1];
    for (int i = 0; i < n; i++) want ^= data[i];
    if (want != ck[0]) { Serial.write(NACK); return; }
    for (int i = 0; i < n; i++) writeEEPROM(addr + i, data[i]);
    // verify
    bool ok = true;
    for (int i = 0; i < n && ok; i++) ok = (readEEPROM(addr + i) == data[i]);
    Serial.write(ok ? ACK : NACK);
  } else {
    Serial.write(NACK);
  }
}
