// SPDX-License-Identifier: MIT
//! The STM32 system-bootloader USART protocol (AN3155), over the same
//! [`Wire`] the STC driver uses — one serial layer, two chip families.
//!
//! Clean-room from ST's application note AN3155 ("USART protocol used in
//! the STM32 bootloader"): wire format only, no ST code. A JS twin lives
//! in bw-board (`src/stm32-isp.js`, mock-ROM-tested byte-exactly); this
//! module is the CLI/desktop side and keeps the identical frame rules so
//! a capture from either implementation reads the same.
//!
//! Entry ritual, which is NOT this module's job but is the part everyone
//! trips on: BOOT0 must be HIGH at reset for the ROM to listen, and the
//! line is **8E1 — even parity** (the STC side runs 8N1; the parity is a
//! per-open argument on [`SerialWire`], not a constant to fight over).
//!
//! SILICON STATUS: in-memory-wire tested only; the first real-chip run is
//! owed, same bar and same wording as the JS twin.

use std::io;
use std::time::{Duration, Instant};

use crate::driver::Wire;

pub const ACK: u8 = 0x79;
pub const NACK: u8 = 0x1f;

const CMD_GET: u8 = 0x00;
const CMD_GET_ID: u8 = 0x02;
const CMD_GO: u8 = 0x21;
const CMD_WRITE_MEMORY: u8 = 0x31;
const CMD_EXTENDED_ERASE: u8 = 0x44;

/// F030x4/x6's product id — the sanity value `flash` reports.
pub const PID_STM32F03X: u16 = 0x0444;

#[derive(Debug)]
pub enum An3155Error {
    Io(io::Error),
    /// The ROM answered NACK; the string names the command that drew it.
    Nack(&'static str),
    /// Nothing arrived in time; the string names the step.
    Timeout(&'static str),
    /// A byte that is neither ACK nor NACK where one was required.
    Garbage(&'static str, u8),
    /// Caller error caught before touching the wire.
    Usage(String),
}

impl std::fmt::Display for An3155Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            An3155Error::Io(e) => write!(f, "io: {e}"),
            An3155Error::Nack(what) => write!(f, "{what}: NACK"),
            An3155Error::Timeout(what) => write!(
                f,
                "{what}: no reply (is BOOT0 high, the chip fresh out of reset, and the line 8E1?)"
            ),
            An3155Error::Garbage(what, b) => {
                write!(f, "{what}: expected ACK/NACK, got 0x{b:02x}")
            }
            An3155Error::Usage(s) => write!(f, "{s}"),
        }
    }
}

impl std::error::Error for An3155Error {}

impl From<io::Error> for An3155Error {
    fn from(e: io::Error) -> Self {
        An3155Error::Io(e)
    }
}

fn xor(bytes: &[u8]) -> u8 {
    bytes.iter().fold(0, |a, b| a ^ b)
}

/// The protocol driver. Timeouts are per read; the erase gets its own
/// (a mass erase takes real time on silicon).
pub struct An3155<'w> {
    wire: &'w mut dyn Wire,
    pub timeout: Duration,
    pub erase_timeout: Duration,
}

impl<'w> An3155<'w> {
    pub fn new(wire: &'w mut dyn Wire) -> Self {
        An3155 {
            wire,
            timeout: Duration::from_millis(1000),
            erase_timeout: Duration::from_secs(30),
        }
    }

    /// Read exactly `n` bytes within `deadline`, polling the wire.
    fn read_exact(
        &mut self,
        n: usize,
        deadline: Duration,
        what: &'static str,
    ) -> Result<Vec<u8>, An3155Error> {
        let start = Instant::now();
        let mut out = Vec::with_capacity(n);
        let mut buf = [0u8; 64];
        while out.len() < n {
            if start.elapsed() > deadline {
                return Err(An3155Error::Timeout(what));
            }
            // Request no more than we still need: a Wire that hands back
            // everything buffered (the mock, and a real port under a
            // burst) would otherwise return surplus bytes this loop
            // discards — silently eating the reply's tail.
            let want = (n - out.len()).min(buf.len());
            let got = self.wire.read(&mut buf[..want])?;
            out.extend_from_slice(&buf[..got]);
        }
        Ok(out)
    }

    fn expect_ack(&mut self, what: &'static str, deadline: Duration) -> Result<(), An3155Error> {
        let b = self.read_exact(1, deadline, what)?[0];
        match b {
            ACK => Ok(()),
            NACK => Err(An3155Error::Nack(what)),
            other => Err(An3155Error::Garbage(what, other)),
        }
    }

    fn command(&mut self, cmd: u8, what: &'static str) -> Result<(), An3155Error> {
        self.wire.write_all(&[cmd, !cmd])?;
        self.expect_ack(what, self.timeout)
    }

    /// The 0x7F auto-baud byte. The ROM ACKs exactly once per reset; a
    /// NACK on a re-init of a live session is tolerated, same as the JS
    /// twin.
    pub fn init(&mut self) -> Result<(), An3155Error> {
        self.wire.write_all(&[0x7f])?;
        let b = self.read_exact(1, self.timeout, "init")?[0];
        match b {
            ACK | NACK => Ok(()),
            other => Err(An3155Error::Garbage("init", other)),
        }
    }

    /// GET (0x00): bootloader version and the supported command bytes.
    pub fn get(&mut self) -> Result<(u8, Vec<u8>), An3155Error> {
        self.command(CMD_GET, "GET")?;
        let n = self.read_exact(1, self.timeout, "GET length")?[0] as usize;
        let body = self.read_exact(n + 1, self.timeout, "GET body")?;
        self.expect_ack("GET tail", self.timeout)?;
        Ok((body[0], body[1..].to_vec()))
    }

    /// GET ID (0x02): the product id (F030x4/x6 answers 0x0444).
    pub fn get_id(&mut self) -> Result<u16, An3155Error> {
        self.command(CMD_GET_ID, "GET_ID")?;
        let n = self.read_exact(1, self.timeout, "GET_ID length")?[0] as usize;
        let body = self.read_exact(n + 1, self.timeout, "GET_ID body")?;
        self.expect_ack("GET_ID tail", self.timeout)?;
        let mut id: u16 = 0;
        for b in body {
            id = (id << 8) | u16::from(b);
        }
        Ok(id)
    }

    /// Extended erase (0x44), global: the F0 family's whole-array wipe —
    /// the two 0xFFFF special-erase bytes plus their XOR.
    pub fn global_erase(&mut self) -> Result<(), An3155Error> {
        self.command(CMD_EXTENDED_ERASE, "EXTENDED_ERASE")?;
        self.wire.write_all(&[0xff, 0xff, 0x00])?;
        self.expect_ack("global erase", self.erase_timeout)
    }

    /// WRITE MEMORY (0x31): one chunk of 1..=256 bytes at a word-aligned
    /// address. Frame: addr(4 BE)+xor, then N-1, data..., xor(N-1,data).
    pub fn write_chunk(&mut self, addr: u32, data: &[u8]) -> Result<(), An3155Error> {
        if data.is_empty() || data.len() > 256 {
            return Err(An3155Error::Usage(format!(
                "write_chunk: {} bytes (1..=256)",
                data.len()
            )));
        }
        if addr % 4 != 0 {
            return Err(An3155Error::Usage(format!(
                "write_chunk: address 0x{addr:08x} not word-aligned"
            )));
        }
        self.command(CMD_WRITE_MEMORY, "WRITE_MEMORY")?;
        let a = addr.to_be_bytes();
        self.wire.write_all(&[a[0], a[1], a[2], a[3], xor(&a)])?;
        self.expect_ack("write addr", self.timeout)?;
        let head = (data.len() - 1) as u8;
        let mut frame = Vec::with_capacity(data.len() + 2);
        frame.push(head);
        frame.extend_from_slice(data);
        frame.push(head ^ xor(data));
        self.wire.write_all(&frame)?;
        self.expect_ack("write data", self.timeout)
    }

    /// GO (0x21): jump to the application (stacked-vector start).
    pub fn go(&mut self, addr: u32) -> Result<(), An3155Error> {
        self.command(CMD_GO, "GO")?;
        let a = addr.to_be_bytes();
        self.wire.write_all(&[a[0], a[1], a[2], a[3], xor(&a)])?;
        self.expect_ack("go", self.timeout)
    }

    /// The whole ritual: init → id → global erase → chunked write → go.
    /// The image is padded to a word multiple with 0xFF (erased-flash
    /// value). Returns (product id, bytes written).
    pub fn flash(
        &mut self,
        image: &[u8],
        base: u32,
        mut progress: impl FnMut(usize, usize),
    ) -> Result<(u16, usize), An3155Error> {
        self.init()?;
        let id = self.get_id()?;
        self.global_erase()?;
        let mut padded = image.to_vec();
        while padded.len() % 4 != 0 {
            padded.push(0xff);
        }
        for (i, chunk) in padded.chunks(256).enumerate() {
            self.write_chunk(base + (i * 256) as u32, chunk)?;
            progress((i * 256 + chunk.len()).min(padded.len()), padded.len());
        }
        self.go(base)?;
        Ok((id, padded.len()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// An in-memory ROM speaking AN3155 — the same state machine as the
    /// JS twin's mock, validating every frame the way silicon does.
    struct MockRom {
        inbox: Vec<u8>,
        outbox: Vec<u8>,
        flash: std::collections::HashMap<u32, u8>,
        erased: bool,
        state: State,
        pending_addr: u32,
        pub log: Vec<String>,
    }

    #[derive(PartialEq)]
    enum State {
        Reset,
        Idle,
        ExtErase,
        WriteAddr,
        GoAddr,
        WriteData,
    }

    impl MockRom {
        fn new() -> Self {
            MockRom {
                inbox: Vec::new(),
                outbox: Vec::new(),
                flash: std::collections::HashMap::new(),
                erased: false,
                state: State::Reset,
                pending_addr: 0,
                log: Vec::new(),
            }
        }

        fn pump(&mut self) {
            loop {
                match self.state {
                    State::Reset => {
                        if self.inbox.is_empty() {
                            return;
                        }
                        let b = self.inbox.remove(0);
                        if b != 0x7f {
                            self.outbox.push(NACK);
                            continue;
                        }
                        self.outbox.push(ACK);
                        self.state = State::Idle;
                    }
                    State::Idle => {
                        if self.inbox.len() < 2 {
                            return;
                        }
                        let cmd = self.inbox.remove(0);
                        let comp = self.inbox.remove(0);
                        if !cmd != comp {
                            self.log.push(format!("badcomp:{cmd:02x}"));
                            self.outbox.push(NACK);
                            continue;
                        }
                        self.outbox.push(ACK);
                        match cmd {
                            CMD_GET_ID => {
                                self.outbox.extend_from_slice(&[1, 0x04, 0x44, ACK]);
                            }
                            CMD_GET => {
                                self.outbox
                                    .extend_from_slice(&[5, 0x31, 0x00, 0x02, 0x21, 0x31, 0x44, ACK]);
                            }
                            CMD_EXTENDED_ERASE => self.state = State::ExtErase,
                            CMD_WRITE_MEMORY => self.state = State::WriteAddr,
                            CMD_GO => self.state = State::GoAddr,
                            other => {
                                self.log.push(format!("unknown:{other:02x}"));
                                self.outbox.push(NACK);
                            }
                        }
                    }
                    State::ExtErase => {
                        if self.inbox.len() < 3 {
                            return;
                        }
                        let f: Vec<u8> = self.inbox.drain(..3).collect();
                        if f[0] ^ f[1] != f[2] {
                            self.outbox.push(NACK);
                        } else if f[0] == 0xff && f[1] == 0xff {
                            self.erased = true;
                            self.flash.clear();
                            self.log.push("global-erase".into());
                            self.outbox.push(ACK);
                        } else {
                            self.outbox.push(ACK);
                        }
                        self.state = State::Idle;
                    }
                    State::WriteAddr | State::GoAddr => {
                        if self.inbox.len() < 5 {
                            return;
                        }
                        let f: Vec<u8> = self.inbox.drain(..5).collect();
                        if f[0] ^ f[1] ^ f[2] ^ f[3] != f[4] {
                            self.log.push("badaddrcs".into());
                            self.outbox.push(NACK);
                            self.state = State::Idle;
                            continue;
                        }
                        let addr = u32::from_be_bytes([f[0], f[1], f[2], f[3]]);
                        if self.state == State::GoAddr {
                            self.log.push(format!("go:{addr:08x}"));
                            self.outbox.push(ACK);
                            self.state = State::Idle;
                        } else {
                            self.pending_addr = addr;
                            self.outbox.push(ACK);
                            self.state = State::WriteData;
                        }
                    }
                    State::WriteData => {
                        if self.inbox.is_empty() {
                            return;
                        }
                        let need = self.inbox[0] as usize + 1;
                        if self.inbox.len() < 1 + need + 1 {
                            return;
                        }
                        let f: Vec<u8> = self.inbox.drain(..1 + need + 1).collect();
                        let head = f[0];
                        let data = &f[1..1 + need];
                        let cs = data.iter().fold(head, |a, b| a ^ b);
                        if cs != f[f.len() - 1] {
                            self.log.push("baddatacs".into());
                            self.outbox.push(NACK);
                        } else {
                            for (i, &b) in data.iter().enumerate() {
                                self.flash.insert(self.pending_addr + i as u32, b);
                            }
                            self.outbox.push(ACK);
                        }
                        self.state = State::Idle;
                    }
                }
            }
        }
    }

    impl Wire for MockRom {
        fn write_all(&mut self, bytes: &[u8]) -> io::Result<()> {
            self.inbox.extend_from_slice(bytes);
            self.pump();
            Ok(())
        }
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            let n = buf.len().min(self.outbox.len());
            for (i, b) in self.outbox.drain(..n).enumerate() {
                buf[i] = b;
            }
            Ok(n)
        }
        fn set_baud(&mut self, _baud: u32) -> io::Result<()> {
            Ok(())
        }
        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
        fn baud(&self) -> u32 {
            115_200
        }
    }

    #[test]
    fn whole_ritual_writes_byte_for_byte_and_jumps() {
        let mut rom = MockRom::new();
        let image: Vec<u8> = (0..701u32).map(|i| ((i * 7 + 3) & 0xff) as u8).collect();
        let mut last = 0usize;
        let (id, bytes) = {
            let mut isp = An3155::new(&mut rom);
            isp.flash(&image, 0x0800_0000, |d, _t| last = d).unwrap()
        };
        assert_eq!(id, PID_STM32F03X);
        assert_eq!(bytes, 704, "padded to a word multiple");
        assert_eq!(last, 704);
        for (i, &b) in image.iter().enumerate() {
            assert_eq!(rom.flash[&(0x0800_0000 + i as u32)], b, "byte {i}");
        }
        assert!(rom.log.iter().any(|l| l == "global-erase"));
        assert!(rom.log.iter().any(|l| l == "go:08000000"));
        assert!(
            !rom.log.iter().any(|l| l.starts_with("bad")),
            "no framing rejections: {:?}",
            rom.log
        );
    }

    #[test]
    fn nack_names_the_command() {
        let mut rom = MockRom::new();
        let mut isp = An3155::new(&mut rom);
        isp.init().unwrap();
        // sabotage GO's complement at the wire level
        isp.wire.write_all(&[CMD_GO, 0x00]).unwrap();
        let e = isp.expect_ack("GO", Duration::from_millis(50)).unwrap_err();
        assert!(matches!(e, An3155Error::Nack("GO")));
    }

    #[test]
    fn refuses_misaligned_and_oversized_before_the_wire() {
        let mut rom = MockRom::new();
        let mut isp = An3155::new(&mut rom);
        assert!(matches!(
            isp.write_chunk(0x0800_0001, &[0; 4]),
            Err(An3155Error::Usage(_))
        ));
        assert!(matches!(
            isp.write_chunk(0x0800_0000, &[0; 300]),
            Err(An3155Error::Usage(_))
        ));
        assert!(rom.log.is_empty(), "nothing reached the ROM");
    }

    #[test]
    fn dead_rom_reads_as_the_helpful_error() {
        struct Dead;
        impl Wire for Dead {
            fn write_all(&mut self, _b: &[u8]) -> io::Result<()> {
                Ok(())
            }
            fn read(&mut self, _buf: &mut [u8]) -> io::Result<usize> {
                Ok(0)
            }
            fn set_baud(&mut self, _b: u32) -> io::Result<()> {
                Ok(())
            }
            fn flush(&mut self) -> io::Result<()> {
                Ok(())
            }
            fn baud(&self) -> u32 {
                115_200
            }
        }
        let mut dead = Dead;
        let mut isp = An3155::new(&mut dead);
        isp.timeout = Duration::from_millis(30);
        let e = isp.init().unwrap_err();
        assert!(e.to_string().contains("BOOT0"), "{e}");
    }
}
