# Three core defects found by fusetest: design

**Requirements:** [requirements.md](requirements.md) · **Research:** [research.md](research.md) · **Status:** [TODO.md](TODO.md)

## 1. The late floating-bus sample (R1)

`Z80::inFromBus` (`core/src/emulator/cpu/z80.cpp`) runs an I/O read in this order:

| Step | Clock | What |
|:--|:--|:--|
| 1 | T1 (the handler has counted it) | `IoWaitBeforeIorq`: the ULA's wait before IORQ (C:1 or N:1) |
| 2 | IORQ (T2) | the model's port decoder, full-decode cards |
| 3 | IORQ | the floating bus, if no device answered |
| 4 | | `IoWaitAfterIorq`: C:3 on a ULA port, three C:1 checkpoints when only the high byte is contended |
| 5 | | the IN observers (section 2) |

The CPU takes the data at the end of T3, after the waits of step 4. T3 is two T after IORQ when nothing waits, and
the floating-bus lookup (`UlaContention::FetchedByte`) is calibrated to IORQ in that case, so step 3 is right for
every port without late waits. The one case with late waits and no device is an undecoded port whose high byte is in
contended memory: there step 3 calls the cold `Z80::FloatingBusAfterLateWaits`, which counts the waits of step 4
first and then looks the byte up (the clock is again two T before T3), and step 4 is skipped. Every other IN runs the
same code as before (an A/B showed a reorder of steps 3 and 4 for all INs costing the 128K 5-9 % on an IN loop, a
code-layout effect).

Worked example (48K, fusetest): `IN A,(C)` with BC = #40FF, fetch at INT + 43046. T1 at 43054 (cell offset 7, no
wait), IORQ at 43055 (offset 0: 6 T), TW at 43062 (offset 7: 0), T3 checkpoint at 43063 (offset 0: 6 T), T3 at 43069.
The lookup now runs at 43067 and returns the attribute the ULA fetches at 43069: #5A0F. Before, it ran at 43055
(an idle phase: #FF).

## 2. The read-cycle latch (R2)

A latch clocked by read cycles needs the byte the CPU took, which exists only after step 3. `Z80` gains
`IReadCycleLatch* readCycleLatch` with its decode (mask, match), set with `SetReadCycleLatch`, and calls
`OnReadCycle(port, value)` last, for a matching port only. The port interceptor (ZX-Poly) and the latch share one
flag at the end of an IN (`_inResultHooks`, kept by `SetPortInterceptor` / `SetReadCycleLatch`), so a machine with
neither tests it once, as before. `PortDecoder_Spectrum128` (the 128K and the grey +2) implements it and sets it in
`reset()`: a port in its #7FFD decode goes through `Port_7FFD_Out` with the value's bits 0-5, which already ignores
everything while the lock bit is set. The +2A / +3 decoder does not set it.

`IsPort_7FFD` keeps its A2 term (A15 = 0, A2 = 1, A1 = 0). The HAL decodes A15 and A1 only; A2 was added so the
SounDrive ports (#F1 / #F9, A2 = 0) do not page. It applies to reads and writes alike.

The grey +2's later "fixed HAL" boards (research.md claim 2) are not modeled: every +2 has the bug, as FUSE and the
early boards.

## 3. #BFFD on the 128K (R3)

`PortDecoder_Spectrum128::DecodePortIn` no longer reads the AY for `(port & 0xC002) == 0x8000`; the port stays
undecoded and the Z80 serves the floating bus. The +2A / +3 decoder is unchanged: it reads the slot device for #BFFD,
and both the AY and the TSFM return the selected register there (the TSFM's status mode applies to #FFFD only and is
off after reset).

## 4. Tests

| Test | What |
|:--|:--|
| `Contention48K_Test` / `Contention128K_Test` `.FloatingBusOfAContendedHighBytePortIsSampledAfterTheWaits` | the worked example: 24 T, the planted attribute |
| `PortDecoderSpectrum128_Test` (128K, PLUS2) | an IN from #7FFD / #3FFD in the border writes #3F (RAM 7, ROM 1, locked); a locked latch ignores reads; other ports leave it; #BFFD undecoded with the AY fitted |
| `PortDecoderSpectrum3ReadCycle_Test`, `Spectrum3Paging_Test.BFFDReadReadsTheSelectedAyRegister` | the +3: no read-cycle latch; #BFFD reads the register |
| `FuseTest_Test` | every line on the 48K, 128K and +3; the AY fitted where the board has one (128K, +3) |

## 5. The standalone library (R5)

unreal-z80 (the CPU library extracted from this core) calls the port-read callback at IORQ and the
`Z80CpuAccessPortInPost` hook after it, the order this core had. Its IN calls the post hook first (one hook test for both
IN hooks), so the callback sees IORQ + the late waits; OUT keeps its order (the ULA takes the border colour at IORQ).
The contract comment in `z80cpu.h`, the T-trace golden, the interrupt/contention harness and the z80ex differential
(a contended IN is its late waits later than z80ex) change with it.
