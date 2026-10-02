# TDD: ATM2IOESP

**Date:** 2026-10-02 · **Reference:** [reference-atm2ioesp.md](reference-atm2ioesp.md)

## 1. The board side: ATM 7.10 decode and the INTERNAL I/O bus

| Part | Choice |
|:--|:--|
| #FE | A2..A0 = 110 (the ATM 7.10 ports doc, Xpeccy, MAME). It was A0 alone: `IN (#FA)` became a keyboard controller command with a Z80 wait, `OUT (#FA)` a border write |
| #FB (A2..A0 = 011) | `OUT` latches the bus address CT0..CT7 (`PortDecoder_ATM710::IoBusAddress`); the write goes on to the Covox DAC, which shares the latch. An `IN` is the printer status (not the latch): left undecoded |
| #FA (A2..A0 = 010) | `IN` / `OUT` go to the first device whose `IAtmIoDevice::Matches(latch)`; an `IN` with none reads #FF (the bus floats) |
| Reset | the connector's RS: every device's `Reset()` (the board reset) |
| Devices | `IAtmIoDevice` (`Matches`, `Read`, `Write`, `Reset`), `AttachIoDevice` / `DetachIoDevice`; only the v7.10 board (`v710Board`), never the ZX-Evo |
| TTD | the latch in its own blob `AtmIoBus` (28): `AtmPagingState` stays as it was, old recordings keep loading |

## 2. The card

`Atm2IoEsp` (`core/src/emulator/io/network/atm2ioesp.h`): matches `(latch & #F8) == base` (#F0 Rev 1.5 / 2.0,
#F8 Rev 1.0), the register is `latch & 7`. The UART and its peer are a `ComPort` built with the plain
`Chip16550` parameters (1.8432 MHz, AFE present, no access wait, INTRPT not wired) and a register map
`port & 7`, reached through the bus instead of #xxEF. TTD: the same `TTDSerialPort` serializer under
its own id `Atm2IoEsp` (29). On the virtual network its peer is guest 4.

## 3. Configuration and surfaces

```ini
[NETWORK]
Card=ATM2IOESP          ; with others: ZXNETUSB,ATM2IOESP
Atm2IoEsp=AT            ; the ComPortSpec values; AT = the shipped firmware, ESPNET for the NedoOS kernel
Atm2IoEspAddress=0xF0   ; 0xF8 for Rev 1.0
```

Runtime keys `atm2ioesp`, `atm2ioesp_address` (CLI, WebAPI + OpenAPI, MCP, Lua, Python, Qt Network
window). The network state: `machine.internal_io`, `atm2ioesp` (fitted, address, peer, the 16550
registers). A machine without the connector lists the card under `not_fitted`.

## 4. Tests

`Atm2IoEsp_Test` (10): #FA is not the keyboard port (no controller command, no wait, no border write),
the #FB latch and its mirrors, the address match for both revisions, divisor 1 = 115200, a loopback byte
with an RTS pulse, the reset, TTD, no connector on a Pentagon, the AT firmware answering through RTS
pulses. `PortDecoder_ATM710_Test.IsPort_FE` and `FullDecodeClaim_ATM710_Test` follow the corrected #FE.
`NetworkPanelModel_Test` for the Qt form.

## 5. NedoOS end to end

`osatm2esp.trd` with `espcom.ini` comType 3, registers 0xF0..0xF7, divider 1 (in place of `net.ini`,
the floppy is full) and `espcfg -S` in `autoexec.bat`; `Card=ATM2IOESP`, `Atm2IoEsp=ESPNET`;
`wget example.com/` at 7 MHz (turbo, with the RAM waits): see TODO for the series.
