# TDD: the ATM Turbo 2+ keyboard controller (network step N4)

Status: design, 2026-10-01. Facts and sources: [reference-atm2-kbc.md](reference-atm2-kbc.md).
Parent plan: [network TDD](../2026-09-30-nedoos-integration/tdd-network.md) §7.3, §15 (N4).

## 1. Goal

On an ATM Turbo 2+ (v7.xx, model `ATM710`) a microcontroller sits behind
`IN #FE`: an i8031 (or an AT89S52 with firmware v4.x) that

- answers every `IN #FE` and holds the Z80 on /WAIT while it does;
- turns the PC (AT) keyboard into a Spectrum matrix, CP/M codes or scan codes
  (keyboard modes 0..3);
- keeps a 50 Hz clock (time and date);
- **is the RS-232 port**: its own UART, reached through the escape `#55` +
  command (`#02` / `#03` / `#42` / `#43` / `#82` / `#83` / `#C2` / `#C3`).

NedoOS's ATM2 ESP kernels and apps (comType 1), Moon Rabbit's `atm-uart` and
NedoOS `time2` use it. The emulator has none of this today: `#FE` on ATM710 is
the plain matrix port.

## 2. Decisions

| # | Decision | Why |
|---|---|---|
| D1 | **Emulate the MCS-51 and run the real firmware images** (not a C++ port of the firmware) | The images exist for every version with a COM port (2.2, 3.1, 3.2m, 4.0, 4.1); timing, buffer behavior and every quirk come from the code itself. User decision 2026-10-01 |
| D2 | **Default firmware v4.1** (AT89S52, 11.0592 MHz) | The newest; the one NedoOS's drivers expect (exact 38400 / 115200). User decision 2026-10-01 |
| D3 | Presets: `V22-7`, `V22-11`, `V22-12`, `V31-7`, `V31-11`, `V32-7`, `V32-11`, `V40`, `V41`, plus `NONE` (no controller: the plain matrix port, as on boards without the chip) | Every released image; the crystal is part of the image (version bytes 3..4) |
| D4 | The controller is an `ATM710` board device, owned by its port decoder. `ATM450` and `ATM3` (ZX-Evo) never get it | v4.50 and v6.x boards have no 8031; the ZX-Evo has the AVR |
| D5 | Its UART is the machine's own serial port (`PortDecoder::DescribeNetwork`: serial port `Atm2Kbc`): `[NETWORK] ComPort=` plugs a peer into it, exactly as on the ZX-Evo | Same slot model as the AVR's 16550; the ZX-WiFi card (#xxEF) still fits on ATM710 |
| D6 | The host's ZX key matrix is the board's **native keyboard port** (D45: the mechanical Spectrum keyboard, tape in), PC keys go to the controller as a PS/2 keyboard | That is the hardware: mode 0 answers `native AND controller`; automation typing through the matrix keeps working |

## 3. The board around the MCU (v7.10 schematic `cp7_2`)

| Signal | MCU pin | Model |
|---|---|---|
| Z80 `IN #FE` (A0=0, A1=A2=1) | INT1 (P3.3, falling edge) | each read: buffer A15..A8 (D108), set the WAIT flip-flop D71 (if W_ON = 0), pulse INT1 |
| A15..A8 buffer D108 (D23 drives the mechanical matrix) | `MOVX A,@DPTR` with P2.0 = 0 | the high byte, while the Z80 waits |
| Native port D45 (matrix AND, tape, joystick) | `MOVX A,@DPTR` with P2.0 = 1 | host ZX matrix for the latched high byte, tape-in bit D6, D5 |
| Data to the Z80 (D102) | `MOVX @DPTR,A` with P2.0 = 1 (/VWR) | the byte the Z80 reads; /VWR clears the WAIT flip-flop |
| VE1 (`#FF77` bit 6) | P3.4 | 1 = controller off: the firmware answers `#FF` once and parks |
| W_ON | P1.7 | 1 = no WAIT generation (the read takes the native port) |
| INT_T | P1.5 | low = Z80 /INT (wired-AND with the ULA INT; v4.1 RX overflow) |
| /RES | P1.6 | low = Z80 reset (power-on, Ctrl+Alt+Del, command `#0D`) |
| PS/2 clock / data | P3.2 (INT0) / P3.5 | open-collector, both sides can pull low (§6) |
| RXD / TXD | P3.0 / P3.1 | the UART (§7) |
| CD, CTS, RI in | P1.0, P1.1, P1.2 | from the peer, inverted by the 170UP2 receivers |
| DTR, RTS out | P1.3, P1.4 | to the peer, inverted by the 170AP2 drivers |
| Crystal | XTAL | 7 MHz (F0 = 14 MHz / 2) or a fitted crystal; the preset fixes it |

The data bus value when the read is not served (VE1 = 1 after the parked
answer, or W_ON = 1): the native port D45 [reference §1.2, to be confirmed on
the schematic during K2].

## 4. MCS-51 core (`core/src/emulator/cpu/mcs51/`)

A cycle-exact 8051 / 8052, Qt-free, reusable:

- All 255 opcodes, machine-cycle timing (1 or 2 cycles, MUL / DIV 4), 12
  oscillator clocks per machine cycle.
- 128 bytes internal RAM (8031 / 8051) or 256 (8032 / 8052: upper half by
  indirect addressing only), SFR space, bit space, register banks, stack.
- Timers 0 / 1 (modes 0..3), Timer 2 (8052: auto-reload, capture, baud-rate
  generator), the UART (modes 0..3; mode 1 / 3 bit rate from Timer 1 or Timer
  2), PCON.SMOD, IDL / PD.
- Interrupts: INT0, T0, INT1, T1, serial, T2; edge / level, two priority
  levels, the polling order, the "one more instruction after RETI / IE / IP
  write" rule, latency 3..9 machine cycles.
- Quasi-bidirectional ports: a pin reads `latch AND external`, so an open
  collector line driven low from outside reads 0.
- External bus through callbacks: `MOVX` read / write (with P2 / DPTR as the
  address), port pins in / out, serial byte out / line state. Program memory
  is the ROM image (2 KB here; up to 64 KB).
- `Run(untilClock)`: executes until a target oscillator clock, stops early on
  a callback request (the Z80 release). State save / load as a fixed blob.

Tests (`core/tests/emulator/cpu/mcs51/mcs51core_test.cpp`): every opcode's
result, flags (CY, AC, OV, P) and cycle count from the Intel MCS-51 manual;
the tricky ones (DA A, SUBB, MUL, DIV, CJNE, bit addressing of SFRs, MOV to
P-latch read-modify-write) cross-checked with MAME's `mcs51.cpp`
(consensus rule: the manual first, MAME as the second reference); timers,
UART and interrupt priority behavior; each firmware image boots and answers
`#55` with `#AA` and its version bytes.

## 5. Time and the Z80 wait

The MCU runs on emulated time: its oscillator clock is tied to the base
T-state counter (`Fosc / base Z80 clock`), not to the host. It catches up
lazily:

- at every `IN #FE` (to the start of the I/O cycle),
- at a `#FF77` write (VE1 changes),
- at a peer event (a byte or a line change that must land at the right time),
- at the frame end (so its clock, keyboard and UART run without Z80 accesses).

An `IN #FE` with the controller active: catch up, latch A15..A8, set the WAIT
flip-flop, raise INT1, run the MCU until it writes D102 (/VWR). The Z80 waits
from the access to that write, converted to CPU clocks at the current CPU
speed (`AddWaitStates`), and reads the byte written. A watchdog bound (e.g.
50 ms of MCU time) ends a wait the firmware never answers, with a log line:
the hardware would hang there.

Nothing else is clocked per instruction: the controller costs the Z80 loop
nothing between `#FE` reads.

## 6. The PC keyboard

A PS/2 (AT, scan code set 2) keyboard on the clock / data lines:

- Host keys arrive as `PcKey` events (the same journaled input the ZX-Evo's
  AVR uses: `Keyboard::SetPs2Sink`). The front end posts the ZX key
  (`MC_KEY_*`) and the physical key (`MC_PCKEY_*`) as separate messages with
  separate handlers; `[INPUT] HostKeyboard=` (AUTO | MATRIX | PS2 | BOTH,
  runtime: `key route`, `POST /keyboard/route`, Qt Machine > Host Keyboard)
  gates each side. AUTO = both on ATM710 with the controller (user decision
  2026-10-01): mode 0 ANDs the two (the native port and the controller's
  keys), CP/M modes read only the controller. The keyboard model turns them into set-2
  make / break codes (with `E0` / `E1` prefixes) and clocks them out bit by
  bit at a PS/2 rate (~12.5 kHz; start, 8 data, odd parity, stop).
- No firmware (2.2 .. 4.1) drives the clock or data line: the controller
  only receives (no LED or reset commands to the keyboard), so the model
  sends and never listens.
- The firmware keeps one received scan code at a time (INT0 handler: one
  register): bytes of two keys sent in the same instant lose the first if the
  main loop is busy. A person never presses two keys within a millisecond;
  automation presses the PC keys of a chord one frame apart.
- Typematic: the last key made repeats while held (500 ms, 10.9 / s).
- Ctrl+Alt+Del (reset), Pause (WAIT mode), Ctrl+Alt+Ins / Home (v4.x block /
  unblock - the code, not the release notes, which have them the other way
  round; keypad 0 and 7) are firmware behavior. The board's part: a blocked
  controller (W_ON = 1) holds no read, so its INT1 handler runs after the
  Z80's cycle and its MOVX reads of the address buffer and the native port
  see the floating bus (#FF): a `#55` poll cannot arm the command mode
  (`Atm2Kbc::MovxRead`, reference (b) item 8).

## 7. The UART and its peer

- The MCU's UART pins are bit-exact in time: a byte written to SBUF leaves on
  TXD at the bit rate the MCU's timers set (Timer 1 or Timer 2), and reaches
  the peer when its stop bit ends; a byte from the peer is shifted into RXD at
  that rate, so RI rises when a real one would.
- The bit rate is computed from the MCU registers whenever they change and
  passed to the peer (`ISerialPeer::OnLineSettings`). A rate the ESP does not
  share produces garbage on both sides, as on hardware (36458 baud on v3.2 at
  7 MHz).
- Modem lines through the inverting drivers: RTS / DTR to the peer (the ESP
  honors RTS as its CTS), CTS / CD / RI from it. DSR is not wired.
- The peers are the existing ones: `NONE`, `LOOPBACK`, `TCP:`, `SERIAL:`,
  `ESPNET`, `AT` (network TDD §7.2).

As built (K4):

- The controller does not own the peer: `NetworkManager` builds it from
  `ComPort=` (the same factory as for the #xxEF port) and plugs it in through
  `NetworkCapabilities::attachSerialPeer`; `EmulatorContext::pMachineSerialPeer`
  points at it. A V22-* firmware has no RS-232: the port is not offered and
  `not_fitted` says why.
- Receive: when the peer has a byte (and RTS is asserted, for a peer that
  honors it) the frame starts on RXD; `SerialIn` hands it to the MCU when the
  middle of its stop bit is sampled (one frame time later, at the bit rate of
  the receiving timer). The next frame cannot start before the stop bit ends.
  A frame that arrives while RI is still set is counted as `lost`.
- Transmit: the MCU's `serialOut` (at the end of the frame on TXD) hands the
  byte to the peer and ends the MCU's run slice, so an echo or a module's
  answer starts at once; so does a change of RTS / DTR.
- Modem inputs: CD, CTS, RI from the peer on P1.0..P1.2 (asserted = 0); with
  nothing plugged in they read deasserted.
- The peer's clock is the machine's (`SetClock` with the emulated T-state
  counter); a byte from outside makes the controller catch up to its arrival
  time first (`onReceive`).
- On the virtual network the peer is guest 3 (`SerialGuests::machine`; the
  card's chip is 1, the #xxEF port's peer 2): its sockets survive a ZX-Bus
  reset and come back to it after a TTD seek.
- Measured on V41 (2026-10-01, the emulated MCU): the INT1 answer takes 34..83
  machine cycles, the timer 0 tick 36, the serial ISR 24; one frame at
  115200 baud is 80 cycles. INT1 and TF0 come before the UART in the polling
  order, so a Z80 that reads `#FE` while RTS is on (a program polling the
  keyboard, a driver reading the buffer out) loses a received frame now and
  then (`lost` in the state), and reads back to back with no gap starve the
  UART completely. That is the firmware on the real chip, not an emulation
  artifact; it is why the drivers pulse RTS (reference §2.5). The tests pace
  their reads like a driver (`DriverIn`, RTS off while reading out).
- An ESP module on this port ships at 38400 (`ComPort=ESPNET` / `AT` without
  `,<baud>`; `NetworkCapabilities::espBaud`): NedoOS's ESPNET firmware is
  built for 38400 on the ATM2 COM (`src/kapps/common/espnet/pins.h`
  "ATM2COM - 38400", `release/ini/espcom.ini` "1 ATM2 COM 38400").

### 7.1 NedoOS over the ATM2 COM (checked 2026-10-02)

NedoOS `osatm2esp.trd` on ATM710 with `ComPort=ESPNET`, `wget example.com/`,
the model unchanged (Z80 released at the /VWR strobe):

| Z80 | Line | received / sent / lost | Result |
|---|---|---|---|
| 7 MHz (NedoOS turns turbo on) | 115200 | 4 / 16 / 14 | stops after SOCKET |
| 7 MHz | 38400 | 6 / 16 / 12 | stops after SOCKET |
| 3.5 MHz | 115200 | 10 / 16 / 8 | stops after SOCKET |
| 3.5 MHz | 38400 | 1274 / 334 / 0 | DNS, CONNECT, HTTP request, 1027-byte reply |

Why (from the v4.1 source and the v7.10 schematic `cp7_2`):

- The Z80 is released by the asynchronous preset of D71 on the falling edge
  of /VWR; D102 drives the data bus only while /VWR is low. A later release
  cannot be: the model matches the board within about 1 us per read.
- INT1 answers take 27..60 machine cycles to /VWR plus 9 to RETI (no loops),
  timer 0 ticks every 8.89 ms (39-40 cycles), INT0 (PS/2) is the only high
  priority. The serial interrupt runs only when the poll after RETI finds no
  new /KEYRD edge: the Z80 needs a gap of more than about 10 machine cycles
  (38 T at 3.5 MHz, 76 T at 7 MHz) after a read.
- The NedoOS receive loop (`_sdk/espnet.asm` `esp_fill1`) reads `#FE` with
  20..58 T gaps. At 7 MHz the empty poll leaves no serial window at all; at
  3.5 MHz it leaves one per iteration, enough for 38400 (240 cycles a frame,
  one byte per ~167 us RTS pulse) and not for 115200.
- So the floppy as shipped cannot work on the real board either: it runs
  `wizcfg` (W5300), not `espcfg`, and the kernel's default is divisor 1
  (115200). A working setup sets `/ini/espcom.ini` `comType = 1`,
  `divider = 3` and runs at 3.5 MHz while it polls (or with I/O waits in
  turbo - WAIT_H not traced yet).

Resolved the same day: the real board has no I/O waits in turbo, but it
stretches every RAM access by 2-3 T at 7 MHz (the video arbiter D68 / D69.1,
[2026-10-02-atm710-turbo-waits](../2026-10-02-atm710-turbo-waits/README.md)).
The NedoOS loop runs from RAM, so its gaps grow into the serial window in
part of the iterations. With those waits modeled, 7 MHz and 38400:

| Z80 | Line | received / sent / lost | Result |
|---|---|---|---|
| 7 MHz with the RAM waits | 38400 | 1319 / 379 / 1 | DNS, CONNECT, HTTP request, 1028-byte reply |

115200 still loses bytes, as on the board. Open: an end-to-end fixture needs
a NedoOS image with an `espcom.ini` for the ATM2 COM (the shipped floppy is
full and runs `wizcfg`).

## 8. Configuration and surfaces

```ini
[ATM]
Kbc=V41            ; NONE | V22-7 | V22-11 | V22-12 | V31-7 | V31-11 | V32-7 | V32-11 | V40 | V41
[ROM]
ATM2KBC=           ; an own image instead of the preset's (the preset still gives the crystal)
[NETWORK]
ComPort=NONE       ; what is plugged into the controller's RS-232 port (ATM710)
```

- Runtime: `kbc_firmware` in `network_configure` / `POST /network/config`
  (like `avr_firmware`; a new image restarts the controller).
- State: `GET /state/atm2kbc` (and the MCP aspect, CLI `atm2kbc`, Lua / Python
  `atm2kbc_state()`): firmware, crystal, mode, VE1, W_ON, command state,
  keyboard registers and LEDs, clock, RX / TX counts, baud, modem lines, and
  the MCU (PC, registers, timers) for debugging. The network state shows the
  controller's port as `machine_serial` (`flavor` `atm2kbc`, beside a ZX-WiFi
  card's `com_port`); `machine.serial_port` is `atm2-kbc`.
- Qt: the Network window offers the controller firmware where the machine has
  it, like the AVR firmware on the ZX-Evo.

## 9. TTD

A new blob `PeripheralId::Atm2Kbc` (next free id): the MCU state (internal
RAM, SFRs, PC, oscillator clock, interrupt state), the board latches (A15..A8,
D102, WAIT flip-flop), the PS/2 keyboard model (queue, bit position, LEDs) and
the UART line state (`SerialLineState`: the frame on RXD, counters). The peer
has its own blob, `PeripheralId::MachineSerialPeer` (27): the peer part of
`netstate::Com` (loopback queue, stream link with its received bytes as
journal references, ESP module), shared code with the SerialPort blob
(`ComPort::SavePeer` / `LoadPeer`). Host input is
already journaled (`PcKey` events, NetEvents). Contract-test row and
`ttd.ksy`.

## 10. Performance

The MCU exists only on ATM710 with `Kbc != NONE`. It runs in bursts at the
four catch-up points: ~920 k machine cycles a second at 11.0592 MHz, ~18 k per
frame, mostly the firmware's idle loop. Naive first (memory: "naive first,
then measure"): an A/B benchmark of ATM710 with and without the controller,
other models untouched (performance guidelines). Fast-forwarding a provably
idle loop is an idea for the backlog, not v1.

## 11. Phases

| Phase | Content | Done when |
|---|---|---|
| K0 | Reference, this TDD, the firmware images with provenance | docs reviewed |
| K1 | MCS-51 core (8051 / 8052, timers, UART, interrupts) and its tests | every opcode test green, MAME cross-check notes |
| K2 | Board glue on ATM710: `IN #FE` through the MCU with WAIT, native port, VE1, W_ON, /RES, INT_T; presets and `[ATM] Kbc=` | each image boots; `#55` -> `#AA`, version bytes; ATM BIOS still starts and reads the keyboard; timing tests |
| K3 | PS/2 keyboard model; modes 0..3; clock commands; NedoOS `time2` | typing in the BIOS / CP/M / NedoOS on ATM710 through the controller |
| K4 | UART + peers, NetworkManager (`Atm2Kbc` serial port), automation, Qt | NedoOS ATM2 ESP kernel on ATM710 reaches the network (zxdb) |
| K5 | TTD blob, state report everywhere, docs, A/B benchmark | replay bit-exact; benchmark recorded |

## 12. Open questions

Second pass 2026-10-02; evidence in [reference §(b)](reference-atm2-kbc.md#b-open-questions-sources-do-not-answer).

1. **The data bus value of an unserved `IN #FE`** (VE1 = 1 after the parked
   answer, W_ON = 1, v4.x block, MCU cold start) - **answered**: the native
   port. D45 is enabled by `/KRD` = NAND(KEYRD, /VWR) on every `IN #FE`.
   It drives D0-D4 = KD1-KD5 for the read's own A15..A8 (D23 is always
   enabled), D5 = Z and D6 = tape in, and does not drive D7 [inferred: idle
   bus]. No WAIT. D102 (the MCU's answer) reaches the bus only while /VWR = 0,
   VA8 = 1 and /KEYRD = 0, so a late answer is lost. Correction to §3: the MCU
   reads A15..A8 through the buffer D108 (555AP5, /ACS), not a latch, and /ACS
   also needs /KEYRD = 0. A handler that runs after the Z80 cycle has ended
   reads #FF (the floating VD bus with its 1 kOhm pull-ups). **K2 follow-up:**
   in the `waitOff` branch `Atm2Kbc::ReadPort` must give the MCU #FF for the
   high byte and the native port, not `port >> 8`.
2. **Does the front-panel reset also reset the MCU** - **answered: yes on
   v7.10** (and 7.18 by its PCB strings): `/RS` -> D80 -> R85 / C11 -> D100
   pin 9 RST. The MCU's own P1.6 pull of `/RS` resets the MCU too (2.2 / 3.1
   wait for exactly that). So any reset is a cold start: COM buffers flushed,
   divisor 6, mode 0, `stat_rs` 0, DTR / RTS off; v4.1 keeps the clock. The
   current model (`BoardReset()`, P1.6 -> `RequestReset`) is right. v8.01
   decouples it (jumper X5); not needed for ATM710.
3. **Real-hardware wait timing** - **partly**: circuit and Karimov's numbers
   (WAIT release -> end of read 0.4-0.7 us at 3.5 MHz; MOVX /VWR 0.54 us at
   11.0592 MHz; a hand strobe 1.1 us). No scope trace was found. The model
   (wait until the firmware's /VWR, plus the answer always delivered) stays.
   Known deviation: MOVX images (2.2, 3.2m, 4.0) are marginal on hardware.
4. **XT-keyboard images** - **answered: already in the collection**.
   `keyboard-controller/xt-keyboard-v1.06/extracted/RF2VE31.rom` (1408 bytes,
   v1.06, with `RFXT710.asm`) has SHA-1
   `adcf14758fab8472cfa0167af7e8326c66416416`, which equals MAME's `rf2ve3.rom`.
   `at-keyboard-v1.00/extracted/RFAT710.rom` (SHA-1
   `6cb6311727fad9bc4ccb18919c3c39b37529b8e6`) equals MAME's `rfat710.rom`
   ([atm.cpp](https://raw.githubusercontent.com/mamedev/mame/master/src/mame/sinclair/atm.cpp)
   :568-570). These are the MicroART 1.0x images
   ([Rf2ve31.zip](http://atmturbo.nedopc.com/download/shems/roms/Rf2ve31.zip),
   [Rfat710.zip](http://atmturbo.nedopc.com/download/shems/roms/Rfat710.zip)).
   The [schemes page](http://atmturbo.nedopc.com/atmshem.htm) lists no other
   XT image. An XT preset needs an XT keyboard model (a different serial
   protocol from PS/2 set 2) and has no COM port (RS-232 starts at v3.0), so
   it stays out of N4.
5. **v4.x block keys** (new) - the code is the reverse of `at40.txt`:
   **Ctrl+Alt+Ins blocks, Ctrl+Alt+Home unblocks** (`atm_at41.asm`
   :523-549). §6 and every surface's docs must say so.
