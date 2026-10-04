# Profi+ ("Personal Computer PROFI Plus"): design

**Date:** 2026-10-04 · status in [TODO.md](TODO.md) · background:
[2026-10-01-profi-v3-v5/software-zoo.md](../2026-10-01-profi-v3-v5/software-zoo.md)

## 1. Goal

A machine variant `PROFI-PLUS` that starts Vadim's ROM BIOS Plus with every device of its board test "Ok", boots
PQ-DOS from a floppy and from a hard disk, and runs DOS Navigator. Today (BIOS Plus 0.32 on `PROFI` with
`[PROFI] ExtPorts=sys`) the FDC, both drives, the RTC and the AY pass; the parallel and serial interfaces fail
(not emulated); HDD fails (no image attached); DOS Navigator 2.0.16 refuses BIOS 0.32 ("Need BIOS version 0.40 or
higher").

## 2. What the board has (from BIOS Plus 0.32 code, `#1B82..#1D6A` of its SYS page)

| Unit | Chip | Ports (extended map, A1 A0 = 11) | What BIOS Plus does |
|:--|:--|:--|:--|
| Parallel port | KR580VV55 (8255 PPI) | `#87` A, `#A7` B, `#C7` C, `#E7` control | mode `#90` (A in, B out, C out); bit set / reset PC2 and reads C bit 2 back; writes B = 2 and reads it back; printer strobe on PC0, busy on PC7 (`IN #C7`, RLA) |
| Baud-rate timer | KR580VI53 (8253 PIT) | `#8F` counter 0, `#AF` counter 1, `#CF` counter 2, `#EF` control | counter 1 mode 3 (`#76`), loads `#0010`, reads it back (fails on `#FFFF`); counter 0 mode 3 (`#36`) = the serial baud divider |
| Serial port | KR580VV51 (8251 USART) | `#D3` data, `#F3` command / status | reset sequence (3 x `#00`, `#40`), mode and command words; status not `#FF`; TxRDY (bit 0), RxRDY (bit 1) polled |
| Control register | - | `#B3` (`#93`) | `OUT #B3,0/1` from the serial setup: D0 = COM interrupt enable (section 2.1) |
| FDC, RTC, IDE, AY | (existing) | `#83..`, `#BF/#DF`, `#xxCB/#xxEB`, `#FFFD` | already pass with `ExtPorts=sys` |

The same 8255 is the Profi's ordinary 8255 (Kempston joystick on port A, Covox on B / C): the extended addresses
are its aliases with A7 = 1 ([decoder-prom.md](../2026-10-01-profi-v3-v5/decoder-prom.md)). What the 5.06 /
Profi+ periphery CPLD decodes exactly is not documented (research agent, see TODO); Karabas Pro's VHDL is the
reference until then.

### 2.1 Facts from the documentation (research 2026-10-04, collection `profi/documentation/profi-plus/`)

- **Port list** (Concurrent BIOS manual, "Описание портов PROFI, PROFI+, PROFI 2+", `PLUSDOC/bios2.txt` pp. 13-14):
  RTC `#FF/#DF` address, `#BF/#9F` data; 8251 `#F3` control / `#D3` data; COM control `#B3/#93` (write D0 =
  interrupt enable; read D0 = RI, D7 = DCD); 8253 `#EF` control, `#CF/#AF/#8F` channels 2 / 1 / 0 (the COM baud
  clock); IDE `#EB/#CB/#AB/#8B`; 8255 `#E7` control, `#C7/#A7/#87` C / B / A; VG93 `#E3/#C3/#A3/#83`, system `#3F`;
  "Condor" modem `#FB/#DB/#BB/#9B`; an external VI53 timer `#F7/#D7/#B7/#97`.
- **Chips** (Profi+ user guide 1994, `PROFPLUS/guid.txt`; `COMPORT/aux2-13b.txt`): parallel port KR580VV55, serial
  KR580VV51A clocked by KR580VI53, RTC KR512VI1, FDC with PLL (4 drives), IDE for two drives.
- **When the extended map is decoded:**
  - the stock boards (5.0 PROM, 5.06 periphery CPLD EPM570 sources 2014-2015): CP/M and ROM14 only - never
    while the SYS ROM runs;
  - **Djoni's replacement port decoder PROM "V0.03"** (zx-pk.ru thread 23036, 22.03.2014; file
    `rt4-decoder-v0.03-coded.rar`): gives the TEST / SYS ROM the extended ports and TR-DOS the RTC, "for ROM-BIOS
    PLUS (beta) by Vadim / Q-DOS by Vadim". This is the real board `ExtPorts=sys` models;
  - Karabas Pro (2020-11-03, "fixed fdd, ide, rtc and serial IO decoders for PQ-DOS"): the same idea in its FPGA,
    `(cpm and rom14) or (dos and not rom14)`; Karabas has no 8255 (its SPI flash sits at #87..#E7).
- **ROM BIOS Plus versions** (collection `system-rom/later-v5/`): 0.241 (2014), 0.30-0.3F (Karabas Pro 2020-2021),
  0.3F2 (2022), **0.40h1 (2023), 0.41a, 0.41, 0.41h1 (2024-2026)**. DOS Navigator 2.0.16 needs 0.40 or later.
- **Disks** (collection `profi/dos/pq-dos/`): PQ-DOS boot floppies 2021-2023 and a 2 GB FAT16 HDD image (Karabas
  Pro, 2023) with PQ-DOS (`QDOS.SYS` 2023-09, CMD "PQ-DOS 1.45") and DOS Navigator 2.0.16.

## 3. Components

Each chip is its own class with a TTD blob, independent of the Profi (other machines may reuse them):

1. **`Ppi8255`** (`core/src/emulator/io/ppi/`): mode 0 complete (port directions, output latches read back, bit
   set / reset, input callbacks per port); modes 1 / 2 only as far as needed (none known). The Profi decoder routes
   both the normal (`#1F..#7F`) and the extended (`#87..#E7`) addresses to one instance; Kempston joystick = port A
   input, Covox = port B / C output (today's Covox behavior must stay bit-exact), printer = B data + C handshake to
   the existing printer / LPT sink if one exists, else a capture buffer.
2. **`Pit8253`** (`core/src/emulator/io/timer/pit8253.{h,cpp}`, done): three counters, modes 0-5, binary / BCD,
   LSB / MSB / both, the counter latch command, reads of the counting element; the 8254 read-back command is
   ignored, as on an 8253. **CLK = 1.5 MHz**: ROM BIOS Plus's baud table (SYS page `#2A3C`) gives divider x baud =
   1 500 000 for every rate (156 for 9600) with the 8251 at baud factor x1, so counter 0's output is the bit clock.
   Time: the emulator's base (3.5 MHz) T-states - `t_states` plus the in-frame CPU position scaled back from the CPU
   clock (`EmulatorState::CpuToBaseT`, right under turbo and the hi-res clocks); the counters advance lazily at each
   access by 3/7 of the elapsed base T (remainder kept), in closed form per mode - no per-instruction cost. The
   chip has no RESET pin: a reset leaves it counting. What counters 1 and 2 drive on the board is not documented:
   they are modeled as counters with nothing on their OUT (gates high). TTD blob `PeripheralId::Pit8253` (51).
3. **`Usart8251`** (`core/src/emulator/io/serial/usart8251.{h,cpp}`, done): asynchronous mode (mode word, sync
   characters after a synchronous mode word, command word, internal reset - the 3 x `#00`, `#40` sequence and
   BIOS Plus's 4 x `#01`, `#40` both work), status TxRDY / RxRDY / TxEMPTY / PE / OE / FE / DSR; a character takes
   (start + data + parity + stop) x the baud factor x counter 0's period. It connects to the existing `ISerialPeer`
   as the machine's own serial port (`NetworkCapabilities::SerialPort::Profi8251`, like the ATM Turbo 2+
   controller's RS-232): `[NETWORK] ComPort=` / `network set com_port=` picks loopback, a test plug, TCP, a host
   serial device, the Hayes modem or an ESP module; no peer = nothing connected (CTS / DSR / DCD inactive). The
   peer is saved in TTD as `MachineSerialPeer`, the chip as `PeripheralId::Usart8251` (52, with the `#B3` latch).
   Not modeled: the synchronous mode (words taken, no bytes move), break (SBRK / break detect), PE / FE (the peer's
   bytes are clean), the COM interrupt (below).
4. **Profi+ board in `PortDecoder_Profi`**: `[PROFI] ControllerBoard=none|plus` adds the 8253, the 8251 and `#B3`
   on the extended map; the 8255 aliases are decoded on every Profi with the extended map (a 5.0 board has the same
   8255 at those addresses when CP/M + ROM14).
5. **Machine variant `PROFI-PLUS`** (`machinevariants.cpp`): base `PROFI`, 1024K, ROM BIOS Plus 0.41h1 (the
   newest, in `data/rom/profi/`), the V0.03 decoder (`ExtPorts=sys`; to be checked against the V0.03 PROM table,
   P0), `ControllerBoard=plus`.

## 4. Phases

| Phase | Work | Check |
|:--|:--|:--|
| P0 | Decode Djoni's V0.03 PROM (`tools/machines/profi/profidecoder`) and compare with the `ExtPorts=sys` rule; fix the rule (or decode from the table) where they differ | the PROM's port map row by row |
| P1 | `Ppi8255` + the Profi routing (normal and extended addresses), Covox and joystick through it | unit tests per mode-0 rule; existing Covox / joystick tests unchanged; BIOS Plus "Parallel interface: Ok" |
| P2 | `Pit8253` (done 2026-10-04) | unit tests per mode, read-back, latch; BIOS Plus reads counter 1 back |
| P3 | `Usart8251` on the PIT, `ISerialPeer` hookup, `#B3` (done 2026-10-04; the COM interrupt open, section 5) | unit tests (reset sequence, async framing, flags); BIOS Plus "Serial interface: Ok"; a loopback peer round-trip |
| P4 | `PROFI-PLUS` variant, ROM in `data/rom/profi/`, HDD with PQ-DOS (variant done; board test done 2026-10-04) | `ProfiPlusBoot_Test`: board test all Ok (done: the BIOS result byte `(IY + 2)`, [software-zoo.md](../2026-10-01-profi-v3-v5/software-zoo.md) section 5), PQ-DOS boots from floppy and from an HDD image (open) |
| P5 | TTD blobs of the three chips (done: ids 50 / 51 / 52), automation (state reports for PPI / PIT / USART on all surfaces, the variant on every create path), recipe, docs | TTD round trip; parity checklist |
| P6 | DOS Navigator | needs BIOS Plus 0.40 or later (research); with it: DN starts |

A/B: the decoder change adds work only on Profi port accesses; the other machines are not touched (no hot path).

## 5. Open questions

- Settled 2026-10-04: the 8253 CLK is 1.5 MHz (BIOS Plus's baud table, section 3); the 8251's TxC / RxC is counter
  0's output.
- The COM interrupt: `#B3` D0 enables an interrupt controller that puts RST `#20` (receive) / RST `#28` (transmit)
  on the bus (PLUSDOC `comport.txt`, TESTCOM.COM). The Z80 model takes device INT lines with a `#FF` vector only
  (IM 0 beyond RST `#38` is not modeled), and the 8251 has no time events between accesses: the latch is kept, no
  INT is raised. BIOS Plus polls the 8251 from its frame interrupt instead (`#2853`), so it does not need it.
- The polarity of `#B3`'s RI (D0) / DCD (D7) reads (modeled 1 = asserted); its other bits (read `#7E`, floating).
- What the 8253's counters 1 and 2 drive on the board.
- Settled: the stock 5.06 CPLD does not open the extended map in the SYS ROM state; Djoni's V0.03 PROM does
  (section 2.1). `PROFI-PLUS` = a v5 with the V0.03 PROM and BIOS Plus.
