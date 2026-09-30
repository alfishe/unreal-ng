# Machine waits not modeled yet: technical design

**Date:** 2026-09-29 · **Requirements:** [requirements.md](requirements.md) · **Research:**
[research-zxevo.md](research-zxevo.md), [research-scorpion-turbo.md](research-scorpion-turbo.md) · **Status:** [TODO.md](TODO.md)

## 1. Scope after the research

| Item | Decision |
|:--|:--|
| ZX-Evo BaseConf at 14 MHz | **implement**: rule fixed by the RTL and a Verilator run of its own modules (high confidence) |
| Scorpion Turbo+ at 7 MHz | **implement** the SC15.1 firmware's rule (medium-high: decoded from the chip's JED file, traced on the schematic, simulated; not measured). SC15.3, the other original firmware, is documented as the alternative |
| ZX-Evo BaseConf 48K / 128K rasters, their emulated contention | **not now**: unreal-ng runs one ATM-style raster for the `ATM3` model (69888 T, paper 14395 T after INT); the BaseConf rasters are chosen with Scroll Lock through the AVR and are not modeled. The contention rule is ready in the research (the Ferranti ULA's, with two differences) for when the rasters exist (ZX-Evo, PLAN #55) |
| TS-Conf cache misses | not here: the TSConf machine (PLAN #41) |

## 2. The overlay: an opcode-fetch entry

Both rules need to tell an opcode fetch (M1) from an operand read: the ZX-Evo keeps separate code and data cache
words (filled by /M1 low and /M1 high reads), and the Scorpion's logic adds a forced wait on M1 only. A
`HostBusOverlay` today sees `onRead(addr, normal, isExecution, romPaged)`, where `isExecution` is true for
operand bytes too.

- `HostBusOverlay` gains `virtual uint8_t onReadM1(addr, normal, romPaged)`, defaulting to
  `onRead(addr, normal, true, romPaged)`; `HostBusOverlayChain` forwards it. Existing overlays (NeoGS ZX-DMA,
  `MemoryWaitOverlay`) keep their behavior.
- The overlay memory interfaces' `MemoryReadM1` (the opcode-fetch entry added for ULA snow) becomes
  `Memory::MemoryReadOverlayM1<Inner>`, which calls `onReadM1`; the contended overlay interfaces wrap it with
  the snow check as before.
- `HostBusOverlay` gains `virtual void onInterruptAcknowledge() {}`, called by the Z80 when it accepts an INT
  while an overlay is installed (one pointer test per interrupt): the ZX-Evo's I/O cycle invalidates its cache.
- Waits are added with `Z80::AddWaitStates` after the access (as `MemoryWaitOverlay`), from the access's start
  clock `Z80::AccessStartClock()`.

## 3. ZX-Evo BaseConf at 14 MHz (`EvoTurboOverlay`)

From [research-zxevo.md](research-zxevo.md) section A (clocks are 14 MHz T-states, the CPU clock at that rate):

| Access | Rule |
|:--|:--|
| M1 or data read, RAM window | if the address's 16-bit word (`addr >> 1`) matches the valid code word or the valid data word: no wait; else `2 + (t & 1)` T with `t` the access's start clock, and the word goes into the code word (M1) or the data word (read) |
| write, RAM window | no wait; the matching cache word(s) become invalid |
| any access to a ROM window | no wait; both words become invalid |
| I/O cycle, interrupt acknowledge | both words become invalid; an external port (low byte #FD with A15 = 1, or #1F / #3F / #5F / #7F in shadow mode) takes 3 T more |

- Installed by the `ATM3` port decoder (`updateTurboMode` -> `SyncTurboWaits`) while the clock select says
  14 MHz (`hw_turbo_ratio` 4), removed otherwise; it starts with an empty cache. The waits apply while the CPU
  runs at 14 MHz (`hw_turbo_ratio_applied` 4: unreal-ng applies the ATM3's clock select at the next frame, the
  hardware at the next fetch's refresh, research C.1) and the `contention` feature is on. The cache words are
  kept up to date either way, so switching the feature mid-run needs nothing more. The decoder invalidates the
  cache and adds the external port's 3 T on its own I/O path (before the IDE board's ports, which are I/O
  cycles too).
- RAM or ROM: the window's mapping (the ATM pager can put ROM in any window), from `Memory`.
- The frame origin: the rule's parity holds for a frame origin on the 3.5 MHz T grid; unreal-ng's clock at 14 MHz
  is the 3.5 MHz frame scaled by 4 (research A.3).
- Not modeled (research A.6): the TR-DOS ROM entry stall (derived, not simulated) and the AVR /WAIT ports (their
  length depends on the AVR firmware).
- TTD: the two cache words and their valid flags (6 bytes) are machine state that changes timing. They go into a
  new TTD peripheral (`EvoTurboCache` = 19), captured and restored with the other ATM state; a recording without
  it starts with an empty cache. The chipset state (the clock) is restored first, as a field copy that does not
  run the decoder, so the blob's restore also installs or removes the overlay (`SyncTurboWaits`).

## 4. Scorpion Turbo+ at 7 MHz (`ScorpionTurboOverlay`)

From [research-scorpion-turbo.md](research-scorpion-turbo.md), the SC15.1 firmware (clocks are 7 MHz T-states):

| Access | Rule |
|:--|:--|
| data read or write, RAM (any bank, RAM at #0000 too) | wait until the next CPU memory slot: slots every 4 T while the picture is drawn, every 2 T in the border (0-3 T in the picture, 0-1 in the border) |
| opcode fetch (M1), RAM | the read rule plus one forced wait (1-4 T in the picture, 1-2 in the border) |
| any I/O cycle | 2 T more |
| ROM | no wait |

- The slot phase comes from the pixel counter: the fetch window starts 14336 T (3.5 MHz) after INT, the same
  anchor as unreal-ng's floating bus, and the picture's slot phase is `(u + 3) mod 4` in 7 MHz edges `u` (research
  section 5). The picture area is the 128 T fetch window of each of the 192 lines; everything else is border.
- Installed by the Scorpion port decoder (`SyncTurboWaits`: the turbo strobe, reset, and the TTD restore of the
  `ScorpionProfROM` blob) while turbo is on; the waits apply with the `contention` feature on. The decoder adds
  the I/O cycle's 2 T when the cycle starts in turbo (the strobe that turns turbo off still pays them).
- Even M1 is already off in turbo (`Z80Step` tests the hardware clock ratio); the turbo M1 wait replaces it, as in the
  equations.
- Not modeled yet: the drop to 3.5 MHz while /INT is active (the length of /INT was not traced); SC15.3's rule
  (same slots for M1, 1 T per I/O, no Even M1 in normal mode) as a configuration option.

## 5. Tests

| Test | What |
|:--|:--|
| `EvoTurboOverlay_Test` | the research's worked examples at 14 MHz: a NOP stream in RAM 6, 4, 6, 4; a NOP at one address 4; `LD A,(HL)` with misses 12 / 10; ROM code without waits; a write invalidates; an OUT invalidates; `OUT (C),A` to #FFFD 18 T; nothing at 3.5 / 7 MHz or with the feature off |
| `ScorpionTurboOverlay_Test` | NOP stream 8 T in the picture, 6 in the border; `LD A,(HL)` / `LD (HL),A` 12 / 10; ROM without waits; nothing at 3.5 MHz, on other machines, with the feature off |
| fingerprints | every model's timing fingerprint unchanged (none of them runs in turbo) |
| TTD | the blob's round trip and the overlay's sync on restore (`EvoTurboOverlay_Test`); the TTD CI gate's `ATM3/idle` case records at 14 MHz (the BIOS's clock) and seeks |
| A/B | `BM_HostFrame_*` of the machines without the overlays (they never install one) |
