# Handoff: 16550 serial cards on other buses (Sprinter ISA and the like)

**Date:** 2026-10-02 · **For:** the Sprinter ISA work (phase I4, the UART card,
[sprinter-isa tdd](../2026-10-02-sprinter-isa/tdd.md)) and any other machine that puts a
16550 + ESP / modem somewhere else than #xxEF · **From:** the network adapters work
([tdd-network.md](tdd-network.md))

The network stack already has everything a "16550 behind some other bus" card needs. The ATM2IOESP
card ([2026-10-02-atm2ioesp](../2026-10-02-atm2ioesp/README.md)) is the worked example: a TL16C550C on
the ATM Turbo 2+ INTERNAL I/O connector, reached through an address latch and a data port instead of
#xxEF. This note lists what to reuse and the pitfalls already paid for.

## 1. Reuse `ComPort` without its ports

`ComPort` (`core/src/emulator/io/serial/comport.h`) is the 16550 (`Uart16550`) plus its peer, its TTD
state and its status. It does not need #xxEF:

- construct it with a `RegisterOf` that maps your "port" value to the register: `port & 0x07`;
- do **not** call `AttachToPorts`;
- reach the registers with `portDeviceInMethod(reg)` / `portDeviceOutMethod(reg, value)` from your bus
  device;
- forward the machine reset to `ComPort::Reset()` (the UART to its reset values, the peer keeps its
  link) and the frame boundary to `ComPort::OnFrame()`.

`Atm2IoEsp` (`core/src/emulator/io/network/atm2ioesp.{h,cpp}`) is the whole adapter in 40 lines.

## 2. Pick the chip's parameters, not the AVR's

| Chip | Parameters |
|:--|:--|
| A real 16550 / TL16C550C (ISA cards, ATM2IOESP, ZX-WiFi) | `Uart16550::DefaultParams(Uart16550::Flavor::Chip16550)`: 1.8432 MHz clock (divisor 1 = 115200), 16-byte FIFOs, AFE in MCR bit 5, IER / IIR interrupts, no access wait |
| The ZX-Evo AVR's 16550 emulation only | `Flavor::EvoAvr` / `EvoAvrParams(firmware)`: no interrupts, no AFE, the AVR's /WAIT per access, firmware quirks |

The Sprinter ISA TDD currently says "Evo flavor parameters" for the UART card: for a real 16550 on ISA
that would drop the interrupts and AFE and add AVR waits that the card does not have. Use `Chip16550`.

Board wiring that differs from a full modem port has a parameter: `Params::ctsOnly` = only CTS' reaches
the chip, DSR' and DCD' tied asserted, RI' inactive (ATM2IOESP). Add a similar flag if your card ties
other lines.

## 3. The interrupt is yours to wire

`ComPort` never drives a CPU interrupt (ATM2IOESP has no INT line). A card with an IRQ (the Sprinter's
to PIO port B) reads `Uart16550::InterruptActive()` (IER / IIR state) after each access and at the frame
boundary and drives its own line.

## 4. Peers, ESP rate, settings

- Peers come from `NetworkManager::MakePeer(specText, espBaud)`: `NONE`, `LOOPBACK`, `TCP:host:port`,
  `SERIAL:device[,baud]`, `ESPNET[,baud]`, `AT[,baud]` (`ComPortSpec`).
- An ESP module ships at the rate its firmware was built for. Without `,baud` the port's own default
  applies: pass it as `espBaud` (`NetworkCapabilities::espBaud` for a machine's own port; 115200 for a
  card unless its firmware build says otherwise).
- The fitting pattern in `NetworkManager`: a `Plan` member (`atm2IoEsp`, its peer spec, its address),
  `MakePlan` (fit only where the machine has the bus, else a `not_fitted` note), `Fit...` /
  `Unplug` (detach from the bus, clear the `EmulatorContext` pointer), `OnFrame`, `UpdateStatus`.
  Copy `FitAtm2IoEsp`.
- Settings: an INI key in `[NETWORK]` (`config.cpp`), a runtime key in `NetworkManager::ParseChange`
  (validated there, used by every surface), the `Status::Settings` copy.
- The machine-bus direction (PLAN #82) will move these to slot-oriented keys; keep the per-card code
  behind one function so the move is mechanical.

## 5. The virtual network needs to know the peer (guest number)

The virtual network's TTD tables say which guest owns each socket. Today: 1 = the ZXNETUSB chip,
2 = the #xxEF port's peer, 3 = the machine's own serial port (ATM2 keyboard controller), 4 = the
ATM2IOESP card, 5 = the TS AVR's ZiFi line. A new card needs its own number, or its sockets are closed by a ZX-Bus reset and are not
handed back after a TTD seek:

- a field in `SerialGuests` (`core/src/common/network/nettypes.h`);
- `hasGuest` in `netstate::NetSocket` (`netstate.h` comment) and the mapping in
  `VirtualNetwork::SaveState` / `LoadState` / `Reset` (`virtualnetwork.cpp`);
- `ComPort::SerialNetGuests` fills it from the `EmulatorContext` pointer.

## 6. TTD

- `TTDSerialPort` (`core/src/debugger/ttd/network/ttdserialport.h`) takes a port getter, an id and a
  name: register the card's `ComPort` under your own id without copying the layout
  (`netstate::SerialPort`: the UART, the peer's link and received bytes by journal reference, an ESP
  module). See the `Atm2IoEsp` registration in `ttdmachineperipherals.cpp`.
- Bus-side latches (an address latch like ATM's #FB) go in their own small blob, not into an existing
  struct: a changed blob size makes older recordings skip the whole blob (`ttdperipheralregistry.cpp`,
  `sizeMismatches`).
- Ids: 36 `AtmIoBus`, 37 `Atm2IoEsp`, 38 `EvoMouse`, 39 `ZiFiLine` and 40 `ZiFi` are taken, the Sprinter
  reserved 32-35; the next free is 41.
  Duplicate values in `PeripheralId` compile silently - check `ttdserializable.h`, add the name to
  `ttdfileinfo.cpp`, the row to `ttdmodelstatecontract_test.cpp` and the entry to `ttd.ksy`.

## 7. Status and every surface

- `DeviceState::Network` (`core/src/emulator/state/devicestate.cpp`) has `peerFields` and `uartFields`
  helpers: a card block is a few lines (see `atm2ioesp`). Add a `machine.*` flag that says the machine
  has the bus.
- The same change touches CLI help (two places), the Python docstring, OpenAPI (config and state
  descriptions), the MCP text view, the four interface docs, the network recipe and the Qt Network
  window (`networkpanelmodel` + `networkwindow`, with a model test). Doing them in the same change is
  cheaper than chasing them later.

## 8. Testing an ESP behind a 16550: pace like the driver

- NedoOS and the AT tools pulse RTS (MCR 2, then 0) and read the FIFO out with RTS off. A test that
  holds RTS on for a whole frame lets the module send its reply back to back and overruns the 16-byte
  FIFO: a test error, not a model error.
- The echo of a loopback byte (and any peer reply) enters the receiver at the UART's next advance: run
  one more frame, or advance the Z80 clock between polls, before expecting it.
- A frame-deterministic test can still depend on what ran before it in the process (global `rand()` for
  the power-on RAM, keyboard phases); if a test passes alone and fails in its shard, reproduce the shard
  (`GTEST_TOTAL_SHARDS` / `GTEST_SHARD_INDEX`) before blaming the change.
- Worked examples: `core/tests/emulator/io/network/atm2ioesp_test.cpp` (register access, loopback,
  reset, TTD, the AT module answering through RTS pulses).
