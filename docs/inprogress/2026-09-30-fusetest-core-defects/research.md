# fusetest differences vs FUSE 1.6.0: hardware research

Date: 2026-09-30. Scope: the three behaviors where fusetest reports that unreal-ng differs from FUSE 1.6.0
(ZEsarUX agrees with FUSE). Rule applied: hardware facts are decided by a consensus of independent sources.
Primary hardware documents and RTL/PAL dumps rank first, emulators second, and a source copied from another
source does not count as a second vote.

## Verdicts at a glance

| # | Claim | Verdict | Confidence |
|---|-------|---------|------------|
| 1 | Odd port with a contended high byte: the CPU samples the floating bus at the end of the stretched cycle (T3), not at IORQ | **Holds** | High (mechanism); medium-high (the exact byte, see below) |
| 2 | 128K / grey +2: an IN from the #7FFD decode (A15=0, A1=0) clocks the paging latch with the bus byte | **Holds** for the 128K and early grey +2s. The lock bit blocks it. The +2A/+3 do not have it | High (128K); medium (grey +2 revisions) |
| 3 | +2A/+3: reading #BFFD returns the selected AY register (mirror of #FFFD); on the 128K/+2 it floats | **Holds** | High (128K/+2 floats); medium-high (+2A/+3 mirror) |

## Glossary

- **T-state (T)**: one CPU clock period (3.5 MHz on the 48K, 3.5469 MHz on the 128K).
- **T1/T2/TW/T3**: the four T-states of a Z80 I/O cycle. IORQ and RD go active at the start of T2. TW is the
  wait state the Z80 always inserts. The CPU samples the data bus at the falling edge of T3.
- **Contention**: the ULA stops the CPU clock (it holds it high) while it fetches screen bytes. This stretches the
  current T-state. `C:1` means "a possible stall, then 1 T". An odd port whose high byte lies in #40-#7F follows
  the pattern `C:1, C:1, C:1, C:1`, which means the ULA can stall before every T-state of the I/O cycle.
- **Floating bus**: the byte a read returns when no device drives the data bus. On the 48K/128K it is the screen
  byte the ULA is fetching at that moment, or #FF between fetches and in the border.
- **o**: phase within the ULA's 8-T fetch group. In FUSE's frame, o = (t - first contended T) mod 8. The stall
  lengths for o = 0..7 are 6,5,4,3,2,1,0,0. The bus carries pixel(n), attr(n), pixel(n+1), attr(n+1) at
  o = 3,4,5,6 and #FF at o = 7,0,1,2.
- **HAL10H8 / PAL10H8**: the one-time-programmed logic chip (IC29) that decodes paging and the AY on the 128K and
  the grey +2.
- **74LS174**: a six-bit edge-triggered latch (IC31). It is the 128K's #7FFD register.
- **40077**: the Amstrad gate array in the +2A/+3. It contains the ULA, the paging logic and the AY decode.

---

## Claim 1: floating bus with a contended high byte (48K and 128K Ferranti ULA)

### Sources

| Source | Kind | What it does / says | Reference |
|---|---|---|---|
| Zilog Z80 CPU User Manual, I/O cycle timing | Documentation (CPU datasheet) | IORQ and RD go active at T2. Data is sampled on the falling edge of T3, after the automatic TW. Clock stretching lengthens the cycle, so the sample moves with it | Zilog UM0080, "Input or Output Cycles" |
| Sinclair Wiki, Floating bus | Documentation | "The Z80 samples the data bus during the final T-state of the I/O machine cycle." | https://sinclair.wiki.zxnet.co.uk/wiki/Floating_bus |
| WoS 48K / 128K reference, contended I/O table | Documentation (from hardware measurement) | High byte in #40-#7F with A0=1 gives `C:1, C:1, C:1, C:1`. The 128K reference says "Port 0x7ffd is not contended as of itself, but the high byte of the port address being 0x7f causes delays" | https://worldofspectrum.org/faq/reference/48kreference.htm, https://worldofspectrum.org/faq/reference/128kreference.htm |
| MiSTer ZX-Spectrum core, `ula.sv` (Chris Smith's ULA logic: transparent latches `mreqt23`, `ioreqtw3`) | RTL (reimplementation from die-level reverse engineering) | The stall is computed at every CPU clock while the address is contended and MREQ is high. For an odd port, `ioreq_n` stays high, so every T of the I/O cycle is stallable (C:1 x4). The stall holds `CPUClk` high | `/Volumes/TB4-4Tb/Projects/mister/cores/ZX-Spectrum_MISTer-alfishe/rtl/ula.sv:255-261` |
| MiSTer `ula.sv`, floating-bus register | RTL | `ff_data` takes the VRAM byte at hc 9/B/D/F (pixel, attr, pixel, attr), keeps the last attribute and is set to #FF at hc 1 | `rtl/ula.sv:215, 218`; `ZX-Spectrum.sv:376-388` (the `port_ff` mux into `cpu_din`) |
| MiSTer T80pa CPU wrapper | RTL | `DI_Reg <= DI` on the clock-low enable in TState 3, i.e. "latched at middle of T3". The ULA's stall delays that edge | `rtl/T80/T80pa.vhd:133, 177` |
| FUSE 1.6.0 `readport()` | Emulator | `ula_contend_port_early` (T1), `ula_contend_port_late` (three C:1 steps: before T2, TW, T3), then `readport_internal` (floating bus sampled here, at T3), then `tstates++` | `.../fuse-1.6.0/periph.c:271-290`, `peripherals/ula.c:260-286`, `spectrum.c:231-296` |
| ZEsarUX `lee_puerto_spectrum` | Emulator (**copy of FUSE**: same function names `ula_contend_port_early/late`, same comment "14338 (48K) and 14364 (128K)") | Same order as FUSE: all waits, then sample | `scratch/wt-m1/scratch/coemu-sources/zesarux/src/operaciones.c:6083-6091`, `contend.c:98` |
| fusetest `floatingbustest` | Test program (FUSE author) | Plants #53 at #5A0F and runs `IN A,(C)` with BC=#40FF at 43046 (48K) / 43584 (128K). Comment: "floating bus read at 43069 / 43607" | `scratch/wt-coemu/scratch/fusetest-repo/fusetest/tests.asm:207-250` |
| fusetest `hex3ffdreadtest` / `hex7ffdreadtest` | Test program | The same planted data gives #02 through #3FFD (uncontended high byte) and #04 through #7FFD (contended high byte). The contended read lands one fetch later, the same late-sample effect seen through the claim-2 latch | `tests.asm:406-545` |
| unreal-ng `Z80::inFromBus` | This emulator | Takes the floating bus after `IoWaitBeforeIorq` and before `IoWaitAfterIorq`, i.e. at IORQ (T2) | `core/src/emulator/cpu/z80.cpp:1045-1130`, `video/ulacontention.cpp:75-108` |

Independence: the Zilog datasheet, the WoS/Sinclair-wiki documents, the MiSTer RTL (derived from Chris Smith's
die work) and FUSE are independent lines of evidence. ZEsarUX is a copy of FUSE and does not add a vote.

### Exact timing: a worked example (48K, fusetest numbers)

48K: 224 T per line, first contended T = 14335, stall table by o = 6,5,4,3,2,1,0,0.
`IN A,(C)` starts at 43046 in uncontended RAM: ED M1 43046-43049, opcode M1 43050-43053, I/O cycle T1 at 43054.

| Step | T (FUSE frame) | o | Stall | What happens |
|---|---|---|---|---|
| T1 | 43054 | 7 | 0 | Address #40FF on the bus, MREQ high: contended, but o=7 has no stall |
| before T2 | 43055 | 0 | **6** | ULA holds the clock 43055-43060 |
| T2 (IORQ, RD active) | 43061 | 6 | - | **unreal-ng samples here in effect: at 43055, o=0, bus idle, #FF** |
| TW | 43062 | 7 | 0 | |
| before T3 | 43063 | 0 | **6** | ULA holds the clock 43063-43068 |
| T3 | 43069 | 6 | - | **CPU samples at the falling edge of T3 (43069.5): bus = attr of column 15 = (#5A0F) = #53** |
| next instruction | 43070 | | | matches fusetest's "ld hl,... 43070" |

Which byte is on the bus at 43069: 43069 - 14335 = 28734 = 128 x 224 + 62. That gives line 128 (character row 16,
pixel row 0) and 62 T into the line. Fetch group 62 / 8 = 7 covers columns 14 and 15, and o = 62 mod 8 = 6 is the
attribute of column 15. The address is #5800 + 16 x 32 + 15 = #5A0F, which holds the planted #53.

In the RTL the same point shows up as follows. The ULA's stall window is hc 4..15 of the 16-pixel group, and it
releases the clock with a falling edge at hc 0. At that edge `ff_data` still holds the attribute latched at hc F:
it is only cleared to #FF at hc 1. T80pa latches `DI` on that falling edge, so it gets attr(n+1), the same byte FUSE
returns. In other words, the late sample lands in the last half-T in which the ULA still presents the attribute it
fetched last. That is why a planted attribute is visible there while IORQ time (o=0) reads #FF.

128K: 228 T per line, first contended T = 14361, same stall table. `IN A,(C)` at 43584 gives T1 at 43592 (o=7) and
T3 after two 6-T stalls at 43607, which matches the fusetest comment. The byte is the same one.

### Consensus

Every independent source puts the sample at T3, after the contention stalls. The Z80 datasheet does it by
definition, the MiSTer RTL shows the stall landing on that edge, and FUSE and the fusetest comments agree. No
source samples at IORQ. **Confidence: high** that the sample is late.

The exact byte (attr(n+1) at o=6) is supported by two sources: FUSE's floating-bus table (calibrated against
games: Arkanoid, Sidewize) and the MiSTer RTL at its release edge. The byte there sits at the edge of the fetch
window, so it is sensitive to a half-T calibration error. I found no scope capture on disk. **Confidence:
medium-high** for the byte.

### What unreal-ng would have to change

`Z80::inFromBus` should take the floating-bus byte at the CPU's sample point: after `IoWaitAfterIorq` has added the
waits before TW and T3, i.e. at the start of T3 in FUSE's convention. At the moment it samples at IORQ. The
uncontended case must not move. Today's `FetchedByte` phase is presumably tuned for sampling at T2, so it has to be
shifted by the same fixed T2-to-T3 distance (2 T) so that uncontended reads (port #FF, Arkanoid/Sidewize-style sync)
return exactly what they do now. Only the contended high-byte case (and the ULA-port case, where the byte is
irrelevant) then changes. Existing floating-bus timing tests should pass unchanged. Add a new test for #40FF at
43046 expecting #53. The same late sample point also feeds claim 2, because the #7FFD latch takes the bus value at
the end of the cycle.

---

## Claim 2: an IN from the #7FFD decode writes the paging register (128K / grey +2)

### Sources

| Source | Kind | What it does / says | Reference |
|---|---|---|---|
| Sinclair ZX Spectrum 128 Service Manual (ed. B. Alford), bank register IC31 | Hardware documentation (Sinclair) | "The register is positive edge triggered and latches D5-D0 off the data bus on the negative (trailing) edge of the BANK output from the PAL IC29. BANK is decoded (set high) from /IORQ and RD/WR active low (**I/O read or write cycle**) and ZA1 and ZA15 low." | https://spectrumforeveryone.com/wp-content/uploads/2017/11/ZX-Spectrum-128-Service-Manual.pdf, section 1.x "Bank Register (IC31)" |
| J. L. (speccy.org trastero) PAL10H8 readout | **Hardware measurement** (the fuse-protected PAL was probed through a PC parallel port, all input combinations) | `BANK = /A15Z*/A1Z*/IORQ*/RDL + /A15Z*/A1Z*/IORQ*/WRL`; `PSG = A15Z*/A1Z*/IORQ*/RDL + A15Z*/A1Z*/IORQ*/WRL` | http://trastero.speccy.org/cosas/JL/Pal/PAL10H8.html |
| Velesoft "UMBRELLA" GAL16V8 replacement | CPLD/GAL equations (derived from the JL dump) | Original: `BANK = !WR&!A1&!IORQ&!A15 \| !RD&!A1&!IORQ&!A15`. Fixed: `BANK = !WR & RD & !A1 & !IORQ & !A15` with the note "Disable read from #7FFD port!" | https://velesoft.speccy.cz/zx/umbrella/umbrella.htm |
| Velesoft, WoS forum "Correction of +2 hardware errors" | Hardware report | "data of paging port 7FFD are rewrited also if CPU read of port 7FFD (write floating bus data)" (grey +2 thread) | https://worldofspectrum.org/forums/discussion/42171/correction-of-2-hardware-errors/p1 |
| Sinclair Wiki, Memory paging | Documentation (circuit description) | "a read or write from/to I/O port 0x7ffd ... trigger the bank register CLK input". Six flip-flops hold B0-B5. "The Q6 output connected to the CLK input of 74LS174 via a diode ... if Q6 become high (1) the CLK line fixed at high level (1) and the bank register cannot writen until a reset." | https://sinclair.wiki.zxnet.co.uk/wiki/Memory_paging |
| Sinclair Wiki, ZX Spectrum 128 | Documentation | "Reads from port 0x7ffd cause a crash, as the 128's HAL10H8 chip does not distinguish between reads and writes". "Later grey +2s were shipped with an updated HAL chip which corrects the issue". | https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_128 |
| Sinclair Wiki, ZX Spectrum +2 | Documentation | Board revision "0500" is listed with "Fixed HAL chip"; the Z70500/Z70700 boards are not | https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_%2B2 |
| Spectrum for Everyone, "Unrainer / IN 7FFD fix" | Hardware repair note (from Velesoft) | "PRINT IN 32765 in 128 BASIC causes a crash" on 128 and grey +2. After the GAL swap it "returns 255, and doesn't crash". +2A/+3 are unaffected | https://spectrumforeveryone.com/technical/applying-the-unrainerin-7ffd-fix-to-128grey-2-machines/ |
| WoS 128K reference | Documentation | 128K/+2 decode = A15=0, A1=0 only. +2A/+3 decode = A1=0, A14=1, A15=0. Says of reads: "Reading from 0x7ffd produces no special results" (**disagrees**, see below) | https://worldofspectrum.org/faq/reference/128kreference.htm |
| FUSE 1.6.0 `readport()` | Emulator | For LIBSPECTRUM_MACHINE_128 and _PLUS2: `if ((port & 0x8002) == 0) writeport_internal(0x7ffd, b)` with the byte just read (floating bus). It goes through `spec128_memoryport_write`, which returns early if `locked`. Not done for +2A/+3. ChangeLog 0.10.0: "Reading from the 128K's memory control port causes that byte to be written back to the port (thanks, Marat Fayzullin)" | `fuse-1.6.0/periph.c:271-290`, `machines/spec128.c:123-133`, `ChangeLog:864-866` |
| ZEsarUX | Emulator (**follows FUSE**) | For MACHINE_IS_SPECTRUM_128_P2 and `(puerto & 32770) == 0`: `out_port_spectrum_no_time(32765, valor_idle_bus_port)` (the lock is checked in the OUT path) | `zesarux/src/operaciones.c:7545-7553` |
| ZXMAK2 | Emulator (independent) | 128K: `SubscribeRdIo(0x8002, 0x0000, readPort7FFD)`; `if (!m_lock) CMR0 = value;` (bus value, lock honored). The +3 memory has no read hook | `ZXMAK2/src/ZXMAK2.Hardware/Spectrum/MemorySpectrum128.cs:26, 92-96`; `MemoryPlus3.cs:29-30` |
| MAME `spec128.cpp` | Emulator (independent) | Write-only map, with the comment "(A15 \| A1) == 0, note: reading from this port does write to it by value from data bus". The note documents the bug but does not implement it | `mame/src/mame/sinclair/spec128.cpp:276` |
| MiSTer ZX-Spectrum core | RTL (reimplementation) | `page_reg` is loaded only on `io_wr` (`~nIORQ & ~nWR`). **Does not reproduce the bug** | `ZX-Spectrum.sv:342, 497, 557-563` |
| ZX Spectrum Next (zxnext.vhd via jnext) | RTL (reimplementation) | `port_7ffd_wr` only (zxnext.vhd:2593, 2718). Does not reproduce the bug | `jnext/src/core/emulator.cpp:4050-4075, 4785` |
| Xpeccy, Unreal Speccy | Emulators | Write-only #7FFD. No read effect | `Xpeccy/src/libxpeccy/hardware/plus2.c:70-78`; `zx-evo-unreal/Unreal/io.cpp:699-758` |
| Karabas-Pro, ZX-Evo BaseConf | Clone RTL (Profi / Pentagon family) | Write-only (`cs_xxfd = '1' and cpu_wr_n = '0'`). Different machines, not evidence about the 128K | `karabas-pro/firmware/src/fpga/profi_plus3e/rtl/karabas_pro.vhd:1397` |

### The circuit, and what gets written

- **The decode**: BANK = IORQ & (RD | WR) & !A15 & !A1 (from the PAL readout). A read cycle clocks the latch like a
  write. The interrupt acknowledge cycle (IORQ with M1, no RD/WR) does not.
- **The instant**: the latch takes D0-D5 on the trailing edge of BANK. That edge comes when IORQ/RD go inactive at
  the falling edge of T3, which is the same instant the CPU samples its IN data. So the byte written is the byte the
  IN returns: the floating bus sampled late, as in claim 1. Example: #7FFD has a contended high byte and gets the
  late byte, which is why fusetest expects #04 for #7FFD and #02 for #3FFD from the same planted data. If another
  device drives the bus for that address, its byte is written.
- **Width**: the 74LS174 has six flip-flops, so only bits 0-5 are stored. Bits 6-7 do not exist in hardware.
  Emulators that store the full byte get the same paging.
- **The lock bit**: Q6 (bit 5) clamps the latch clock high through a diode. That blocks every clock edge, read or
  write. **A locked #7FFD is not changed by a read.** FUSE, ZEsarUX and ZXMAK2 all honor the lock on the read path.
- **Border example (the "PRINT IN 32765 crash")**: in the border the bus reads #FF, so #FF is written. That selects
  RAM 7 at #C000, the shadow screen and ROM 1 (48 BASIC), and sets the lock bit. The machine is stuck in that state
  until reset, which explains the crash.

| Source | Read clocks the latch? | Value written | Lock bit blocks it? |
|---|---|---|---|
| Service manual + PAL readout + Sinclair Wiki circuit | Yes | Data bus at the end of the cycle (floating bus / #FF) | Yes (diode clamp) |
| Velesoft / Spectrum for Everyone | Yes (fix required) | "floating bus data" | Implied (same latch) |
| FUSE 1.6.0 | Yes (128, +2) | The IN result (late floating bus) | Yes (`locked` check) |
| ZEsarUX | Yes (128, +2) | Floating-bus value | Yes (OUT path) |
| ZXMAK2 | Yes (128) | Bus value | Yes (`!m_lock`) |
| MAME | No (documented in a comment only) | - | - |
| MiSTer, Next, Xpeccy, Unreal | No | - | - |

### Grey +2 and +2A/+3

- **Grey +2 (Amstrad, 1986)**: it is the 128K design. Paging is not in the ULA (the 7K / Amstrad 40056 ULA only does
  video and contention). It is in the same HAL10H8 PAL + 74LS174. Early boards carry the original HAL and have the
  bug (Velesoft's report is about the grey +2). The Sinclair Wiki reports that a later revision ("0500") shipped a
  "Fixed HAL chip". So the grey +2 depends on its revision. FUSE and ZEsarUX treat all +2s as buggy.
- **+2A/+3**: the 40077 gate array decodes #7FFD only on writes, with a tighter decode (A1=0, A14=1, A15=0).
  Spectrum for Everyone, the WoS reference and FUSE (no write-back for +2A/+3) agree that the bug is gone.

### Consensus

The Sinclair service manual, a hardware readout of the PAL and repair-level reports (Velesoft, Spectrum for
Everyone) independently establish that a read clocks the latch. Three independent emulators (FUSE, ZXMAK2, MAME in a
comment) record it too; ZEsarUX follows FUSE. The WoS 128K reference sentence "Reading from 0x7ffd produces no
special results" is the only disagreement. It is contradicted by the PAL equations and the service manual, and was
probably written from a fixed-HAL machine or from the software's point of view. MiSTer, the Next and the other
emulators simply do not model the bug. They are silent on it rather than evidence against it. **Confidence: high**
for the 128K, including the lock-bit behavior. **Medium** for which grey +2 revisions carry the fixed HAL.

### What unreal-ng would have to change

For the 128K and the grey +2 port decoders only, an IN whose address matches the HAL decode (A15=0, A1=0) should,
after the final bus value has been formed, feed that value through the normal #7FFD write path. That path already
ignores the write when bit 5 is locked. Use the value after claim-1 late sampling (floating bus, #FF in the border,
or a device's byte). The +2A/+3 and all clones stay write-only. A grey +2 "fixed HAL" option could be a per-model
config flag, defaulting to buggy to match FUSE and the early boards.

Side note, one line: `PortDecoder_Spectrum128::IsPort_7FFD` masks A2 as well
(`portdecoder_spectrum128.cpp:289-296`), but the HAL decodes only A15 and A1.

---

## Claim 3: reading #BFFD on the +2A/+3 returns the selected AY register

### Sources

| Source | Kind | What it does / says | Reference |
|---|---|---|---|
| 128K Service Manual, AY decode (D26, D27) | Hardware documentation (Sinclair) | BC1/BDIR are decoded from PSG, A14 and /RD. Table: FFFD write → BDIR=1, BC1=1; BFFD write → 1,0; FFFD read → 0,1; PSG=0 → 0,0. So BDIR = PSG & /RD high, BC1 = PSG & A14. A **BFFD read** (A14=0, /RD=0) gives BDIR=0, BC1=0 = **inactive**: the AY does not drive the bus | Service manual (link above), section 1.7 PSG table |
| PAL10H8 readout | Hardware measurement | `PSG = A15 & !A1 & IORQ & (RD \| WR)`: the AY is selected on BFFD reads, but BC1 follows A14 | trastero.speccy.org (link above) |
| +2A/+3 schematic Z70830 | Schematic (Amstrad, redrawn) | AY BDIR comes from gate array pin 94, BC1 from pin 93 (the 40077 drives GICK on pin 92). The decode is **inside the 40077**, so the schematic cannot settle the question; only measurement can | https://zxnet.co.uk/spectrum/schematics/Z70830.pdf |
| FUSE 0.10.0 ChangeLog | Emulator changelog recording a **hardware report** | "Reading the AY data port on the +2A/+3 is the same as reading the register port (Philip Kendall; thanks, Mark Woodmass)." Mark Woodmass is the SpecEmu author and tests on real machines | `fuse-1.6.0/ChangeLog:867-868` |
| FUSE 1.6.0 AY port tables | Emulator | `ay_ports` (128K): `{0xc002, 0x8000, NULL, ay_dataport_write}`. `ay_ports_plus3`: `{0xc002, 0x8000, ay_registerport_read, ay_dataport_write}` | `fuse-1.6.0/peripherals/ay.c:63-80` |
| fusetest `hexbffdreadtest` | Test program | Selects R11, writes #55, reads #BFFD. Expects #FF on 48K, 128K/+2, Pentagon and #55 on +2A/+3 | `tests.asm:377-420` |
| Sinclair Wiki, AY-3-8912 | Documentation | 128/+2: "reading from BFFDh will return the floating bus value as normal for unattached ports". +2A/+3/+2B/+3B: "it will return the same as reading from FFFDh" | https://sinclair.wiki.zxnet.co.uk/wiki/AY-3-8912 |
| ZX Spectrum Next VHDL (via jnext) | RTL (reimplementation of +3 behavior) | `port_fffd_rd <= iord and (port_fffd or (port_bffd and machine_timing_p3) or port_bff5)`: BFFD reads alias FFFD **only in +3 timing** | `jnext/src/core/emulator.cpp:4654-4672` (cites zxnext.vhd:2771) |
| ZEsarUX | Emulator (follows FUSE; cites a port list "BFFD ... R Spectrum +2A/+3 Mirror of Port FFFDh (+2A/+3 only)") | Aliases on P2A/P3 only | `zesarux/src/operaciones.c:6897-6901` |
| MiSTer ZX-Spectrum | RTL (reimplementation) | `psg_rd = psg_sel & addr[14]`, so BFFD reads float on every model, +3 mode included | `ZX-Spectrum.sv:593-595, 385` |
| MAME `specpls3.cpp` | Emulator | `data_r` only on #C000 mirror #3FFD; a BFFD read on +3 is not handled | `mame/src/mame/sinclair/specpls3.cpp:323-324` |
| ZXMAK2, Xpeccy, Unreal Speccy | Emulators | No BFFD read handler on any model. Unreal returns #FF for `(p2 & 0xC0) != 0xC0` | `ZXMAK2/.../General/AY8910.cs:200-202`; `Xpeccy/.../plus3.c:36-44`; `zx-evo-unreal/Unreal/io.cpp:1365-1371` |

### Consensus

- **128K / grey +2 (BFFD read floats)**: the Sinclair service manual's BDIR/BC1 truth table, the PAL readout and
  every emulator agree. **Confidence: high.**
- **+2A/+3 (BFFD read = selected AY register)**: the decode is inside the 40077, so no schematic or netlist exists
  to check. The evidence is a hardware report (Mark Woodmass, via FUSE 0.10.0), the Sinclair Wiki and the Next's
  +3-timing decode. The Next is an independent implementation, but its provenance could be FUSE or the wiki. ZEsarUX
  copies FUSE. MiSTer, MAME, ZXMAK2 and Xpeccy do not alias, but none of them claims hardware testing on this point:
  they simply use the 128K decode for the +3. That is an omission, not a counter-measurement. A plausible gate-array
  implementation is BC1 = A14 OR read, which makes any AY read a register read. **Confidence: medium-high.** A
  single read on a real +3 (select R11, `OUT #BFFD,#55`, `IN #BFFD` in the border) would close it.
- **Value returned**: the same as an `IN` from #FFFD, i.e. the selected register's contents, with the usual AY
  masking of unused bits (R1/R3/R5 low nibble, R6 5 bits, R8-R10 5 bits, R13 4 bits). fusetest uses R11 (full 8
  bits) to avoid the masking.

### What unreal-ng would have to change

In `PortDecoder_Spectrum3::DecodePortIn` only (the +2A and +3 decoders), a read that matches the BFFD decode (A15=1,
A14=0, A1=0) should return the same value as the FFFD register read (the selected AY register, TurboSound chip
select respected). It should mark the port as decoded, so the gate array's floating bus does not apply. The 128K and
grey +2 decoders must keep BFFD reads undecoded (floating bus).

---

## Source independence map

| Group | Members | Counts as |
|---|---|---|
| Sinclair primary | 128K service manual | 1 (hardware documentation) |
| PAL readout lineage | JL trastero dump → Velesoft UMBRELLA → Spectrum for Everyone; Sinclair Wiki paging text | 1 hardware measurement (+ repair reports) |
| FUSE lineage | FUSE (credits Marat Fayzullin for claim 2, Mark Woodmass for claim 3) → ZEsarUX | 1 |
| Independent emulators | ZXMAK2, MAME, Xpeccy, Unreal Speccy | Each 1, weak (simplified models) |
| Reimplementation RTL | MiSTer (Chris Smith's ULA logic), ZX Next VHDL | Each 1. Strong for the claim-1 mechanism; they do not model the 128K PAL bug |
| Clones | Karabas-Pro, ZX-Evo BaseConf | Not evidence about Sinclair/Amstrad machines |

## Files and links consulted

- `/Volumes/TB4-4Tb/Projects/mister/cores/ZX-Spectrum_MISTer-alfishe/ZX-Spectrum.sv`, `rtl/ula.sv`, `rtl/T80/T80pa.vhd`
- `../../../scratch/wt-m1/scratch/coemu-sources/fuse-1.6.0` (`periph.c`, `peripherals/ula.c`, `peripherals/ay.c`, `spectrum.c`, `machines/spec128.c`, `ChangeLog`)
- `../../../scratch/wt-m1/scratch/coemu-sources/zesarux/src/operaciones.c`
- `../../../scratch/wt-coemu/scratch/fusetest-repo/fusetest/tests.asm`, `fusetest.asm`
- `/Volumes/TB4-4Tb/Projects/emulators/github/jnext/src/core/emulator.cpp`
- `/Volumes/TB4-4Tb/Projects/emulators/github/mame/src/mame/sinclair/spec128.cpp`, `specpls3.cpp`
- `/Volumes/TB4-4Tb/Projects/emulators/github/ZXMAK2/src/ZXMAK2.Hardware/Spectrum/MemorySpectrum128.cs`, `General/AY8910.cs`
- `/Volumes/TB4-4Tb/Projects/emulators/github/Xpeccy/src/libxpeccy/hardware/plus2.c`, `plus3.c`
- `/Volumes/TB4-4Tb/Projects/emulators/github/zx-evo-unreal/Unreal/io.cpp`
- `/Volumes/TB4-4Tb/Projects/emulators/github/karabas-pro/firmware/src/fpga/profi_plus3e/rtl/karabas_pro.vhd`
- `/Volumes/TB4-4Tb/Projects/Knowledge/zx/05_development/05_display_and_timing/floating_bus.md`
- Web: the service manual PDF, trastero PAL10H8 page, Velesoft UMBRELLA, Sinclair Wiki (ZX Spectrum 128, +2, Memory
  paging, Floating bus, AY-3-8912), WoS 128K reference and forum thread 42171, Spectrum for Everyone IN 7FFD fix,
  zxnet.co.uk Z70830 schematic.
