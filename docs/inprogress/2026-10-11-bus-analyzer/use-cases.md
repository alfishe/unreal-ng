# Bus analyzer: use cases

**Date:** 2026-10-11 · part of [README.md](README.md)

Each use case names who asks, what they do today, what the analyzer gives them, and the phase that delivers
it ([TODO.md](TODO.md)). "Agent" means an AI agent driving the emulator over MCP; the same calls exist on
WebAPI, CLI, Lua and Python ([requirements.md](requirements.md) §3).

## 1. Driver writers

### UC-1. "Does my SD driver race the SPI master?" (TS-Conf, ZX-Evo)

The question that started this. On TS-Conf a CPU-only race is impossible: a byte takes 17 fclk
(0.61 us, 8.5 T at 14 MHz) and two Z80 port accesses are at least 12 T apart. The real races are elsewhere:

- the CPU touches `#57` / `#77` while an SPI DMA runs (DMA and ports share one master, `top.v:1168-1189`):
  the driver did not wait for DMA status before reading the CRC or deselecting;
- the driver sends the next command while the card is still busy after a write, or before it read the
  whole answer of the previous one: the emulator's card forgives both silently, a real card does not;
- the driver deselects in the middle of a 6-byte command (an interrupt handler that touches the bus).

**Today:** nothing; the driver works in the emulator and fails on the board.
**With the analyzer:** `bus_analyzer {"action":"enable","bus":"zc.spi","checks":true}`, run the driver,
`bus_analyzer {"action":"findings"}` lists each violation with frame, T, PC and the rule. **B2.**

### UC-2. "My driver works in the emulator and fails on a real card" (all SD hosts)

The emulator's card answers after 1 NCR byte, delivers the data token after 8 bytes, stays busy for 64 bytes
after a write and leaves idle after 4 ACMD41 polls. Real cards take up to 8 NCR bytes, up to 100 ms to the
data token, up to 250 ms of write busy and up to a second to initialize. A driver with a 255-poll timeout
or no busy wait passes here.

**With the analyzer:** `bus_analyzer {"action":"sd_timing","bus":"zc.spi","profile":"slow"}` (or
`realistic` with a seed); the driver now times out where a real card would make it time out, and the trace
shows how many polls it made before giving up. **B4.**

### UC-3. "What did my driver actually send?" (all SD hosts)

**Today:** the port trace shows bytes on `#57`; the command, its argument and the R1 have to be decoded by
hand; on the Next a separate SD journal exists, on TS-Conf / Evo / DivMMC / NeoGS nothing.
**With the analyzer:** one event list per bus mixing the transaction level (select, bytes) and the protocol
level (`CMD17 arg=0x00000800 R1=00`, `read block 2048`, `write block 7 accepted`), filterable by kind,
slot, frame range and sequence number. **B1 + B3.**

### UC-4. FT812 (VDAC2) display list and command traffic (TS-Conf)

The FT812 shares the Z-Controller bus with the SD card (`#77` D2). The analyzer shows which device was
selected for each byte and flags a byte sent with both selected. Decoding FT8xx host commands and memory
writes is a protocol decoder of its own. **B1 (wire level), B6 (FT8xx decoder).**

### UC-5. NeoGS: MP3 decoder and SD on the card's three masters

A GS program that streams MP3 data restarts the SDI master before a byte finished, or reads SD before the
byte completed. NeoGS already models the restart; the analyzer reports it as a finding with the GS CPU's PC
and shows the SCI register traffic. **B1 + B2.**

### UC-6. Serial and ESP (later buses)

ZiFi / Next ESP drivers talk AT commands over a UART; today the bytes are invisible. A UART tap plus a
line decoder shows `AT+CIPSTART=...` / `OK` with time and the overrun check. Same for the Next I2C RTC (start
without stop, NACK) and ATA (register access while BSY). **B6.**

## 2. Emulator developers

### UC-7. Verify a bus model against the hardware

Compare the emulator's trace with a logic-analyzer capture from the board: the VCD export synthesizes SCK /
MOSI / MISO / CS edges from the transactions and the bus's byte time, so both open side by side in PulseView,
whose own SPI and SD decoders then run on the emulator's trace. **B5.**

### UC-8. Stop rewriting journals

A new machine's SD, I2C or UART gets the trace, the checks, the query syntax and every automation plane by
registering a bus and calling the tap at its existing exchange point; no new ring, no new endpoint, no new
MCP aspect. The Next SD journal becomes a view over the bus trace. **B1 + B7.**

### UC-9. Regression tests on protocol behaviour

A test drives a machine, enables the bus trace and asserts on decoded events ("the BIOS sends CMD0, CMD8,
ACMD41 until ready, CMD58, then CMD17 of block 0") instead of on port bytes. **B1.**

## 3. Agents (MCP)

### UC-10. One-shot diagnosis of a failing loader

An agent asked "why does this .spg not start from SD" runs: enable the bus with checks, run N frames,
read findings, read the protocol events of the last frames. The text of each answer carries the data
(MCP content[] rule), one line per event, so an agent that sees only text can answer. **B3.**

### UC-11. Hardening a driver in a loop

An agent iterates over profiles (ideal, realistic seeds 1..5, slow) and reports which one breaks the
driver and at which command. **B4 + recipe `sd-driver-debugging.md`.**

## 4. What changes for the user, in one table

| Before | After |
|:--|:--|
| SD protocol log on the Next only | on every SD host, same query, same output |
| No race detection outside Next / NeoGS internals | findings with PC on every SPI bus, plus DMA collisions on TS-Conf |
| A card that forgives everything | realistic and worst-case timing on demand, deterministic by seed |
| Bytes in the port trace | transactions and decoded protocol per bus, VCD export for PulseView |
| A new journal per device | register a bus, call the tap |
