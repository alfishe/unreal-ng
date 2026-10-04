# Research: the Sprinter's CPU, the Zilog Z84C15

Status: research, 2026-10-01. Answers the open item in [technical-design.md](technical-design.md) §4
("where the wait rule comes from on the board") and the CPU row of
[hardware-reference.md](hardware-reference.md). No code was changed.

External sources are cited by URL plus page. Zilog's Z84C15 specification is a scanned PDF, so its
text was read through OCR. "p." means the page number printed on the page. Local copies of other
emulators and of the Sprinter firmware and hardware sources are cited as `emulators/...` (the corpus
next to this repository, see [materials.md](materials.md)). Sprinter code that exists only on branch
`sprinter-s1` is marked "(branch `sprinter-s1`)".

## Summary

- **The chip is a Zilog Z84C15, the 16 MHz grade (`Z84C1516FSC`/`PSC`), running at 21 MHz,
  about 31% above its rating.** It is not a Toshiba TMPZ84C015. MAME's `z84c015_device` is a
  Z84C15 model built on top of its Toshiba TMPZ84C015 class. That is the right chip family, but
  the model is approximate (§1, §6).
- **The CPU inside is a Zilog Z84C00, the standard CMOS Z80.** Compared with the NMOS Z80 that
  unreal-ng emulates today, it differs in two places only:
  - `OUT (C),0` writes `#FF` instead of `#00`;
  - `LD A,I` / `LD A,R` interrupted by an INT does not lose P/V.

  Instruction timings, the M1 and refresh cycle, the interrupt timings and the undocumented
  SCF/CCF flags are the same (§3).
- **Everything else that affects timing comes from outside the CPU core.** There are two
  sources:
  - **The chip's on-chip wait-state generator.** It inserts the maximum number of waits for the
    first 15 opcode fetches after power-on, then 1 memory wait while the PLD loader runs. BIOS
    3.04 then turns it off.
  - **The Sprinter's PLD.** It drives the CPU's `/WAIT` pin at 21 MHz.

  MAME's "phase mod 6" rule is MAME's own approximation of the PLD's wait logic. It has nothing
  to do with the Z84C15 (§4, §5, §8.3).
- **Verdict of this research: no separate CPU library** (two variant flags on the existing core,
  the Z84C15 features in the Sprinter-side package, §8.1). **Owner decision (2026-10-01): a
  separate library anyway**, `core/src/3rdparty/z84c15/`, for the Sprinter only (§8.0).

## Glossary

| Term | Meaning |
|:--|:--|
| **NMOS / CMOS** | Two chip manufacturing technologies. The original 1976 Z80 is NMOS; the later low-power Z84C00 is CMOS. A few undocumented details differ. |
| **T-state, clock** | One CPU clock period. At 21 MHz one T-state is 47.6 ns; at 3.5 MHz it is 285.7 ns. |
| **M1** | The opcode fetch cycle. It is 4 T-states: 2 to read the opcode, 2 for the DRAM refresh. |
| **Wait state** | An extra T-state inserted into a bus cycle while the `/WAIT` pin is low, or while the on-chip wait generator asks for one. |
| **INTA** | The interrupt acknowledge cycle. In IM2 the CPU reads the vector byte from the data bus during this cycle. |
| **Daisy chain** | Zilog's priority chain for interrupting peripherals (CTC, SIO, PIO). Each device watches the opcode stream for `RETI` (`ED 4D`) to know when its interrupt has been serviced. |
| **PLD** | The Altera ACEX chip that implements almost all of the Sprinter: ports, memory, video, waits. It is loaded at every power-on. |
| **Chip select (CS0/CS1)** | Two pins on the Z84C15 that go low for programmed address ranges. They select external memory chips. |
| **Q** | A hidden latch in the Zilog Z80's flag logic. It decides bits 3 and 5 of F after `SCF`/`CCF`. |

## 1. Which exact chip

### 1.1 Evidence from the board

| Evidence | What it says | Source |
|:--|:--|:--|
| Board photo (Sp2000 "light") | `ZiLOG Z84C1516FSC / Z80 EIPC / 0532 AT` | `emulators/zxgit/2000/2000light/sp2000light-1.jpg` |
| Replica board photo (Sp2016s) | `Z84C1516FSC` | `emulators/zxgit/2000/2016s/photos/black.jpg` |
| Bill of materials | `43 1 U3 Z84C1516PSC` | `emulators/zxgit/2000/2003s/docs/Sprin_3m.bom:77` |
| Schematic | `Z84C1516PSC`, QFP-100 footprint | `emulators/zxgit/2000/pcad_import/PAGE1.sch:38` |
| Clock wiring (schematic) | pin 69 CLKIN = net `CLK_Z80` from the PLD; XTAL1/XTAL2 = 14 MHz nets; EV = GND; CLKOUT goes to a separate net | `emulators/zxgit/2000/2000/docs/SPRINT_3.pdf` (CPU sheet) |
| User manual | "Z84C15", 21 MHz / 3.5 MHz; `#10..#1F, #EE, #EF, #F0, #F1, #F4` are "internal ports of the Z84C15" | `emulators/github/Sprinter200x/docs/sp2000_man.pdf` p. 4, p. 31 |
| PLD source | the PLD generates the CPU clock: `CLK_Z80 = CLK21` in turbo, a 42 MHz / 12 = 3.5 MHz toggle otherwise | `emulators/gitlab/sprinter-computer-hard/ACEX/DCP.TDF:275`, `:279` |

Decoding the part number `Z84C15 16 F S C`: `16` is the 16 MHz grade and `S` is the 0-70 °C range
(DC2992 p. 4). Reading `F` as the QFP package follows Zilog's naming scheme, but this was not checked
against a Zilog ordering table.

### 1.2 The three similar parts

| Part | Contents | Fixed ports | Speed grades | Source |
|:--|:--|:--|:--|:--|
| **Zilog Z84C15** (on the Sprinter) | Z84C00 CPU + CTC + SIO + PIO + clock generator + watchdog, **plus**: wait-state generator, `/CS0`/`/CS1`, power-on reset and reset output, 32-bit CRC on SIO A, clock divide-by-one option | `#10-#13`, `#18-#1F`, `#EE/#EF`, `#F0/#F1`, `#F4` | 6, 10, 16 MHz ("16 MHz operation for Z84C15 only"); 100-pin QFP/VQFP | [PS0182](https://www.zilog.com/docs/z80/ps0182.pdf) p. 293, p. 311 Table 1; [DC2992](https://www.zilog.com/docs/z80/dc2992.pdf) p. 4 |
| Zilog Z84C13 | the same as the Z84C15 but without the PIO | no `#1C-#1F` | 6, 10 MHz; 84-pin PLCC | PS0182 p. 293 |
| Toshiba TMPZ84C015 | CPU + CTC + SIO + PIO + clock generator + watchdog; **no** wait generator, chip selects or `#EE/#EF`; the clock is always crystal / 2 | `#10-#13`, `#18-#1F`, `#F0/#F1`, `#F4` | 10 MHz (datasheet seen) | [TMPZ84C015BF-10](https://archive.org/download/TMPZ84C015BF-10/TMPZ84C015BF-10.pdf) p. 9, p. 61 |

**Verdict.** The Sprinter carries a Zilog Z84C15-16, a 16 MHz part run at 21 MHz. There is no 20 MHz
Z84C15 grade (PS0182 p. 293); the Z84C00 alone does have one (`Z84C0020`,
[PS0178](https://www.zilog.com/docs/z80/ps0178.pdf) p. 1). The chip is clocked from outside through
CLKIN (§1.1), so the crystal divider bit (MCR D4) has no effect on the Sprinter (PS0182 p. 320-321:
"no effect when ... the external system clock is fed from CLKIN pin").

MAME's driver says "Z84C15" (`emulators/github/mame/src/mame/sinclair/sprinter.cpp:9`) and creates a
`Z84C015` (`:1957`). That class is "Zilog Z84C015" (`emulators/github/mame/src/devices/cpu/z80/z84c015.cpp:14`),
a subclass of the Toshiba `tmpz84c015_device` that adds the Zilog system registers `#EE/#EF` (`:16-21`)
and the chip selects (`:55-63`). The part is right; §6 lists what the model leaves out.

## 2. Documents

| Document | What it gives | Where |
|:--|:--|:--|
| Zilog PS0182 (PS018201-0602), "Z84013/Z84015/Z84C13/Z84C15 IPC/EIPC" | the full Z84C15 description (registers, port map, clocking, wait generator) | https://www.zilog.com/docs/z80/ps0182.pdf (PS0177 does not exist on zilog.com) |
| Zilog DC2992-06, Z84C15 electrical specification | speed grades, AC timing, errata (p. 27) | https://www.zilog.com/docs/z80/dc2992.pdf |
| Zilog UP0046 | Z84C15 errata: one cross-reference typo in the watchdog timing figure | https://www.zilog.com/docs/up0046.pdf |
| Zilog UM0080 (UM008011-0816), Z80 CPU User Manual | instruction timings, WAIT sampling, interrupt timing | https://www.zilog.com/docs/z80/um0080.pdf |
| Zilog PS0178, Z84C00 product specification | CMOS CPU grades, bus timing | https://www.zilog.com/docs/z80/ps0178.pdf |
| Zilog "Z80 Family Questions and Answers" (Product Specs Databook) | the LD A,I/R fix on CMOS | http://www.z80.info/zip/ZilogProductSpecsDatabook129-143.pdf, p. 3-130 |
| Toshiba TMPZ84C015BF-10 datasheet | the Toshiba part, for comparison | https://archive.org/download/TMPZ84C015BF-10/TMPZ84C015BF-10.pdf |

Copies were downloaded to the research session's scratch area, not to the repository.

## 3. The CPU core compared with the NMOS Z80

The Z84C15's CPU is "Z84C00 Z80 CPU" (PS0182 p. 293), so everything known about Zilog CMOS Z80s
applies.

| Behavior | NMOS Zilog Z80 | Zilog CMOS (Z84C00, so Z84C15) | Source |
|:--|:--|:--|:--|
| `OUT (C),0` (`ED 71`) | writes `#00` | writes **`#FF`** | not in Zilog documents; [ZXNet wiki, "Differences between NMOS and CMOS Z80s"](https://sinclair.wiki.zxnet.co.uk/wiki/Z80); [redcode Z80 wiki, Bugs](https://github.com/redcode/Z80/wiki/Bugs); redcode `Z80_MODEL_ZILOG_CMOS = OUT_VC_255 + XQ + YQ` (`emulators/github/pico-spec/src/GS/Z80_redcode.h:532-535`); ZXMAK2 `ED_OUTCR_CMOS` (`emulators/github/ZXMAK2/src/ZXMAK2.Engine.Cpu/Processor/Z80Cpu.OpcodesEd.cs:661-670`) |
| `LD A,I` / `LD A,R` followed at once by an accepted INT | P/V reads 0 (bug) | **fixed**: P/V = IFF2 | Zilog Q&A p. 3-130: "On CMOS Z80 CPU, we've fixed this problem"; UM0080 p. 94-95 still describes the bug without saying it is NMOS-only; redcode leaves `LD_A_IR_BUG` out of its Zilog CMOS model (`Z80_redcode.h:534`) |
| `SCF` / `CCF` X/Y flags (the Q rule) | `(A \| (F & ~Q)) & #28` | **the same** | Z80 XCF Flavor: "applies to all Zilog Z80 models, both NMOS and CMOS" (`emulators/github/Z80_XCF_Flavor/README.md:10`); skiselev z80-tests: Z84C0008PEC and Z84C0010PEG show the same XF/YF as Zilog NMOS. Toshiba and ST CMOS clones differ, so this is a Zilog-only fact. |
| Instruction T-states, M1 (4 T with refresh), memory cycle 3 T, I/O cycle 4 T (one automatic wait) | UM0080 tables | the same | PS0178 bus timing; no CMOS timing differences are documented |
| WAIT sampling | falling edge of T2 and of every TW | the same | UM0080 p. 7; PS0178 p. 26 |
| INT acknowledge | 2 automatic waits; IM1 13 T; IM2 19 T (7 fetch + 6 push + 6 vector) | the same | UM0080 p. 12, p. 19-20; PS0178 p. 27 |
| NMI | 11 T (5 T M1 + two 3 T writes) | the same | UM0080 timing figure p. 14. The 11 T is derived from the figure: the manual gives no explicit count. |
| HALT, block instructions, MEMPTR | – | no CMOS difference documented | – |

The Z84C15 adds three things of its own to these cycles. All of them are switched off unless the
wait-state register (WCR) asks for them (§4.1): extra INTA waits for the daisy chain, a longer
`RETI`, and an extra opcode-fetch wait. Undocumented behavior has not been tested on a Z84C15
itself. The table assumes the Z84C15 behaves like a Z84C00 because Zilog says its core is one.

**Worked example (`OUT (C),0`).** With `BC = #00FE`, `ED 71` on the Sprinter writes `#FF` to port
`#FE`: border 7 (white) on a Spectrum-style port, not 0 (black). unreal-ng today writes `#00`
(`cpu->out(cpu->bc, cpu->outc0)` with `outc0` never set,
[op_ed.cpp:561](../../../core/src/emulator/cpu/op_ed.cpp)).

**Worked example (LD A,I).** With `EI` set, `LD A,I` runs and an INT is accepted right after it.
The handler checks P/V to find out whether interrupts were on. NMOS: P/V = 0, "interrupts were off".
Z84C15: P/V = 1. unreal-ng clears P/V today
([z80.cpp:1615-1620](../../../core/src/emulator/cpu/z80.cpp)).

## 4. On-chip features that change timing or behavior

### 4.1 The wait-state generator (WCR, MWBR)

Registers are reached through pointer `#EE` (SCRP) and data `#EF` (SCDP). Pointer 0 = WCR,
1 = MWBR, 2 = CSBR, 3 = MCR (PS0182 p. 311, p. 318).

| WCR bits | Field | Values | Source |
|:--|:--|:--|:--|
| 7-6 | daisy-chain wait (INTA, between `/M1` and `/IORQ`) and RETI extension | 0/2/4/6 waits in INTA; 0/0/2/4 waits on `ED 4D`, and 1 wait after `ED` that is not followed by `4D` (settings 4 and 6 only) | PS0182 p. 319 |
| 5 | interrupt vector wait | +1 wait after `/IORQ` in INTA | p. 319 |
| 4 | opcode fetch extension | +1 wait in every M1, added to the memory waits | p. 319 |
| 3-2 | memory waits | 0-3, only inside the MWBR range | p. 319 |
| 1-0 | I/O waits | 0/2/4/6; "For the accesses to the on-chip I/O registers, no Wait states are inserted regardless of the programming of this field" | p. 319 |

MWBR: memory waits apply where `MWBR[7:4] >= A15-A12 >= MWBR[3:0]`. Its reset value `#F0` covers
the whole 64 KB (p. 320).

**Power-on window (important for emulation):** "the Z84C13/C15 inserts the maximum number of wait
states (set all bits of this register to one) for fifteen /M1 cycles after Power-on Reset. It
automatically clears ... on the trailing edge of the 16th /M1 signal unless software has programmed
a value. ... A read to WCR during this period will return FFh" (p. 318). MAME notes this in a comment
but does not implement it: `m_wcr = 0x00; // 0xff, then 0x00 on 16th M1` (`z84c015.cpp:100`).

External `/WAIT` (the PLD's) is sampled after the programmed waits (p. 309), so the two add up.

**Worked example (power-on window).** With WCR = `#FF`, a NOP costs 4 T + 3 memory waits + 1
opcode-fetch extension = 8 T. The first 15 M1 cycles from reset therefore take at least 60 T
longer than without waits.

### 4.2 Chip selects (CSBR, MCR)

- `/CS0` is active for `CSBR[3:0] >= A15-A12 >= 0`.
- `/CS1` is active for `CSBR[7:4] >= A15-A12 > CSBR[3:0]`.
- MCR D0 / D1 enable CS0 / CS1. MCR resets to `#01`; CSBR resets to `xxxx1111b`.
- The pins decode A15-A12 only, with no control signals (p. 310, p. 320).
- The chip selects only drive pins. They do not change the CPU's behavior; the board's wiring
  gives them meaning. On the Sprinter, CS0/CS1 split the address space between the PLD-decoded
  memory and the fast RAM while the PLD is loading (MAME `z84c015.cpp:55-63`, `sprinter.cpp:1160`;
  unreal-ng `z84systemregs.h` `Cs0End()` (branch `sprinter-s1`)).

### 4.3 MCR, clock, reset, power-down

| MCR bit | Meaning (PS0182 p. 320-321) | On the Sprinter |
|:--|:--|:--|
| D4 | clock divide-by-one (1) / divide-by-two (0) for the crystal input | no effect: the CPU is clocked through CLKIN (§1.1) |
| D3 | reset output disable (0: `/RESET` pin is driven low for 16 clocks by the reset logic) | the loader leaves it 0 |
| D2 | 32-bit CRC on SIO A | 0 |
| D1, D0 | CS1, CS0 enable | see §5 |

With an external clock on CLKIN only the RUN and IDLE2 halt modes apply (p. 322). The halt mode is
chosen in WDTMR bits 4-3 and is RUN after reset. Errata: STOP mode cannot be left by `/RESET`, and
the chip "will sometimes lock up in power cycling" (DC2992 p. 27). Neither matters to the emulator,
because the BIOS never selects STOP (§5).

### 4.4 Watchdog, interrupt priority, daisy chain

- **Watchdog.** WDTMR `#F0` resets to `#FB`, which means **enabled** with period `2^22` clocks and
  RUN mode. WDTCR `#F1` takes the commands `#B1` (disable, after WDTE is cleared), `#4E` (clear) and
  `#DB` (halt-mode unlock) (PS0182 p. 316-317).
- **Watchdog wiring on the Sprinter: unknown.** The `/WDTOUT` pin's connection on the board was not
  found. If it reaches `/RESET`, an enabled watchdog would reset the machine after
  2^22 clocks = 0.2 s at 21 MHz, unless software clears it. BIOS 3.04 never writes `#F0/#F1` (§5),
  and the machine does not reset itself. So either `/WDTOUT` is not connected, or something else
  is going on. Open question Q3.
- **Interrupt priority.** `#F4` resets to 0: CTC, then SIO, then PIO (p. 318).
- **Daisy chain.** The on-chip CTC/SIO/PIO interrupt through the internal daisy chain and supply
  their own IM2 vectors. They watch for `RETI` to clear their "under service" state. On the
  Sprinter they never interrupt under BIOS 3.04 (§5). The PLD's interrupts use vector `#FF` (the
  data bus floats high; MAME `sprinter.cpp:1961`).

### 4.5 Fixed ports and decoding

The on-chip registers "are fully decoded from A7-A0 and have no image" (PS0182 p. 310). So the
upper address byte is ignored and port `#xx1F` always reaches the PIO. The port list is in
PS0182 Table 1 (p. 311), and it matches [hardware-reference.md](hardware-reference.md) "Fixed
ports", MAME (`tmpz84c015.cpp:22-27`, `z84c015.cpp:19-20`) and unreal-ng `Z84C15::Owns`
(branch `sprinter-s1`). `#14-#17` are not Z84C15 ports. The Sprinter include file lists them under
"internal ports" (`emulators/zxgit/Shared_Includes/constants/SP2000.inc:1628-1650`, in CP866), but
neither PS0182 nor MAME claims them.

The PLD sees these cycles too. Its port-number rewrite of `OUT (#1F),A` explains the BIOS-TT comment
"PIO port B command: only through register BC, otherwise the Altera intercepts it"
(`SP2000.inc:2132`; the manual p. 21).

### 4.6 CTC clocking on the Sprinter (2026-10-02)

The CTC's CLK/TRG pins and ZC/TO outputs as the board wires them (MAME `sprinter.cpp:1993-2008`):

| Pin | Connection |
|:--|:--|
| TRG0, TRG1, TRG2 | X_SP / 48 = 42 MHz / 48 = **875 kHz**, from the board crystal: the same at 3.5 and 21 MHz |
| ZC/TO0 | SIO B receive and transmit clock (the serial mouse's baud rate) |
| ZC/TO1 | not connected |
| ZC/TO2 | TRG3 (cascade) |

The timer mode counts the CPU clock through the prescaler (Z80 CTC data sheet PS0181; MAME derives the
CTC's clock from the CPU clock, `tmpz84c015.cpp:249` `DERIVED_CLOCK(1,1)`, and `set_clock_scale` re-derives
it, `device.cpp:398-410`), so a timer runs six times faster at 21 MHz; the counter mode on TRG does not.

What software programs (seen live, BIOS 3.06 / DSS 1.71): CTC 0 `#55` (counter, rising edge) with 45, SIO B
in x16 mode: 875 000 / 45 / 16 = 1 215 baud for the 1 200 baud mouse. Bad Apple and deMarche's dontBlink:
CTC 2 `#57` with 112 (ZC/TO2 at 7 812.5 Hz), CTC 3 `#D7` with 160: an interrupt at 48.83 Hz = every
20.48 ms (one Sprinter frame), vector base `#00` -> `#06`, IM 2 with a table holding only that entry.
unreal-ng: `Z84Ctc::SetTrigger` in the library, the wiring in the `PortDecoder_Sprinter` constructor, the
time base the board's 42 MHz ticks in real time (base T-states x 12 + the in-frame CPU position / the clock
multiplier), the CPU clock 12 / multiplier ticks (`SyncChipClock` at a turbo switch and at the frame
boundary for the host speed control).

## 5. What the Sprinter firmware programs

| Step | Code | WCR | MCR | CSBR | Other |
|:--|:--|:--|:--|:--|:--|
| Power-on | – | `#FF` for 15 M1 (§4.1) | `#01` | `#xF` | WDTMR `#FB` |
| PLD loader (ROM page `#C`) | [bios304-pc-loader.asm:35-56](../../disasm/rom/sprinter/loader/bios304-pc-loader.asm); source `emulators/zxgit/Sprinter-BIOS/bios/loader/loader.asm:6-24` ("Memory waits set to 1") | **`#04`** = 1 memory wait | `#03` (CS0 + CS1; D4 = 0, no effect) | `#FE`: CS0 `#0000-#EFFF`, CS1 `#F000-#FFFF` | SIO A WR5 `#62`; PIO A mode 3, all bits out, `#EA` |
| Loader reload path | `loader.asm:45-52` | – | – | `#F0`: CS0 `#0000-#0FFF`, CS1 `#1000-#FFFF` (stream from fast RAM) | – |
| BIOS 3.04 start (page 8) | [bios304-p8-exp.asm:424-451](../../disasm/rom/sprinter/exp/bios304-p8-exp.asm) (`InitCpuPorts`); source `emulators/zxgit/Sprinter-BIOS/bios/exp/EXP.asm:333-347` ("set 0 Waits") | **`#00`** | **`#01`** (CS0 only) | unchanged | SIO A, PIO A again; POST codes on PIO A |
| SETUP interrupts | [bios304-setup.asm:66-83](../../disasm/rom/sprinter/rom/bios304-setup.asm) | – | – | – | `I = #80`, `IM 2`, vector `#FF` → handler pointer at `#80FF` |

Searches of BIOS-PP SETUP and of the DSS sources (`emulators/gitlab/sprinter-computer-bios/SETUP/`,
`emulators/gitlab/sprinter-computer-dos/`) found no other writes to `#EE/#EF` and none to `#F0/#F1/#F4`.
The DSS uses SIO A only to read the keyboard (`keyinter.asm`, see [materials.md](materials.md)).
A CTC/IM2 timer example in `SP2000.inc:2156-2177` is commented out. So in normal operation:

- the wait generator is **off** (WCR = 0) once the BIOS has started;
- the on-chip peripherals never raise interrupts (the firmware's own code; programs do - the demos above run
  their playback on a CTC 3 interrupt, and DSS 1.71 clocks SIO B from CTC 0, section 4.6);
- the watchdog is never cleared (Q3);
- every CPU wait after boot comes from the PLD's `/WAIT`.

**Worked example (the loader with 1 memory wait).** The stream loop (`bios304-pc-loader.asm:139-158`)
costs 113 T per bitstream byte and makes 29 memory cycles:

- `LD A,(HL)` 7 T;
- 8 x `LD (DE),A` = 56 T;
- 7 x `RRCA` = 28 T;
- `INC E` 4 T, `INC HL` 6 T;
- `JR` 12 T.

MAME runs it at 113 T: 59 215 bytes x 113 T = 6 691 295 T, which matches its measured last write
at t35 = 6 691 665 (`testdata/machines/sprinter/reference/loader.txt`, branch `sprinter-s1`). With
WCR = `#04` each of the 29 cycles gets 1 wait, so 142 T per byte: 8 408 530 T, which is 2.40 s at
3.5 MHz instead of 1.91 s.

Whether the loader really runs at 3.5 MHz is a separate open question. The ACEX, which generates
`CLK_Z80`, is not configured yet at that point. The small MAX CPLD drives a `CLKZZ` net while
configuration is off, and its comment says "14 MHz" (`emulators/gitlab/sprinter-computer-hard/MAX/SP2_MAX.TDF:255-257`).
At 14 MHz with the wait, the loader would take 0.60 s (Q1).

## 6. What other emulators do

| Emulator | CPU variant | Z84C15 features | Clock | Waits | Where its timings come from |
|:--|:--|:--|:--|:--|:--|
| **MAME** (`f43983b6`) | Zilog NMOS: `OUT (C),0` writes 0 (`z80.lst:5763-5765`); the LD A,I/R quirk is compiled out for **every** Z80 (`z80.inc:4` `HAS_LDAIR_QUIRK 0`), so in that one respect it acts like a CMOS part; Q emulated (`z80.lst:6427-6431`). Header comment lists the CMOS differences as TODO (`z80.cpp:7-20`) | `#EE/#EF` registers stored; CS0/CS1 applied as address spaces (`z84c015.cpp:55-63`); **WCR stored but never applied**, no power-on wait window (`:100`); watchdog runs but is unconnected on the Sprinter; on-chip writes are also forwarded to the PLD handler (`sprinter.cpp:1445-1458`) | 42 MHz / 12 = 3.5 MHz, x6 in turbo (`sprinter.cpp:178`, `:387`, `:1957`) | `do_mem_wait`: align to a 6-clock phase, then `6 - taken` (`:1720-1731`); RAM and ISA 3, ports 4 (`:1173`, `:1186`, `:581`, `:704`); none at 3.5 MHz, none for ROM or fast RAM; INT vector `#FF` (`:1961`) | the author's model of the PLD; no source given for the mod-6 rule |
| **ZXMAK2** | `NEC_NMOS` (the class default, `Z80Cpu.cs:59`; nothing sets the type for the Sprinter). It does have a `ZILOG_CMOS` type (`CpuType.cs:25-31`, `Z80Cpu.OpcodesEd.cs:661-670`, `:810-830`) | none (no `#EE/#EF`) | always 21 MHz: frame = `0x11800 * 6` T (`SprinterULA.cs:65`) | none | fixed frame length only |
| **SprintEm** | FUSE core (NMOS): `OUT (C),0` writes 0 (`emulators/gitlab/sprintem/z80/z_ed.hpp:353-355`) | none | "average speed" setting `mhz=12` (`sprintem.ini:32`) | none; the accelerator adds its byte count to the T counter (`sprintem.cpp:1913`) | high-level: it runs Sprinter EXE files without the BIOS |
| **unreal-ng S1** (branch `sprinter-s1`) | Zilog NMOS (`outc0 = 0`, LD A,I quirk on) | `Z84C15` package: CTC/SIO/PIO, system registers stored, CS0 used for the loader, watchdog stored but not run (`io/z84c15/z84systemregs.h`) | x6 ratio | MAME's rule, `memory/sprinter/sprinterwaits.h`; port waits at the I/O cycle start | MAME |

Searched with no Sprinter code found: Sprinter200x (only PLD, manual and software), Unreal Speccy,
UnrealSpeccyP, Xpeccy, zxsp, Zero, BizHawk.

**Consensus: there is none on the CPU.** No emulator models the Zilog CMOS core for the Sprinter,
nor the Z84C15 wait generator. All take their timings from their own guesses or from MAME. The
datasheet (§3, §4) and the PLD source (§8.3) are therefore the authorities. MAME is the only
reference with traces and is useful for checking the PLD-wait model.

## 7. unreal-ng today

- **Main Z80 core** (`core/src/emulator/cpu/`, the one the Sprinter uses):
  - `Z80State::outc0` exists ([z80.h:399](../../../core/src/emulator/cpu/z80.h)) and is used by
    `ED 71` ([op_ed.cpp:561](../../../core/src/emulator/cpu/op_ed.cpp)). Nothing sets it, so it
    stays 0, the NMOS value.
  - The LD A,I/R quirk is always on in `HandleINT` ([z80.cpp:1615-1620](../../../core/src/emulator/cpu/z80.cpp)).
    Only RZX playback can turn it off (`z80.cpp:835-837`).
  - The Q rule is the Zilog one (`op_noprefix.cpp`, SCF/CCF).
  - There is no CPU-type setting anywhere in the configuration.
- **Vendored `core/src/3rdparty/unreal-z80/`:**
  - A copy of the same core, extracted as a library. It is used **only** by the General Sound
    coprocessor ([README.md](../../../core/src/3rdparty/unreal-z80/README.md)).
  - It already has `Z80CpuSetOutC0Value` (`z80cpu.h:393`).
  - Its LD A,I/R quirk is always on (upstream README, `emulators/github/unreal-z80/README.md:174`).
- **Sprinter S1** (branch `sprinter-s1`):
  - `io/z84c15/` holds CTC, SIO, PIO and `Z84SystemRegs`. The system registers are stored, and CS0
    is used for the loader. WCR has no effect.
  - `memory/sprinter/sprinterwaits.h` holds MAME's rule as a `MemoryWaitOverlay`, plus port waits
    through `Z80::AddWaitStates` (`portdecoder_sprinter.cpp:400-422`).
  - The PLD loader is shortcut (`portdecoder_sprinter.cpp:199-207` replays the loader's register
    writes).
- **Gap:**
  1. CMOS `OUT (C),0` and the absent LD A,I/R quirk.
  2. The WCR power-on window and WCR waits (they matter only up to the BIOS start, unless the
     loader is ever run for real).
  3. The PLD's per-port I/O wait table (§8.3).
  4. The INT acknowledge writes its stack pushes and reads the IM2 vector through raw memory calls
     with the 19 T charged up front ([z80.cpp:1657-1683](../../../core/src/emulator/cpu/z80.cpp)).
     It is not checked whether the wait overlay sees these accesses, and at which clock. MAME
     charges PLD waits for them (they go through `ram_r`/`ram_w`).

## 8. Recommendation

### 8.0 Owner decision (2026-10-01): a separate library

The owner decided against the recommendation below: the Sprinter's CPU gets its own vendored
library, `core/src/3rdparty/z84c15/`, a fork of `unreal-z80` 0.5.0 with the CMOS core and the
on-chip block (wait generator, chip selects, MCR, watchdog, CTC / SIO / PIO, daisy chain, fixed
ports). Every other machine stays on the native interpreter, unchanged. Why:

- **Isolation of odd chips beats avoiding duplication.** The variant flags of §8.2 would put
  Z84C15 conditions into the CPU core every machine runs; a separate library keeps the shared core
  exactly as it is and lets the Z84C15 grow (wait generator, daisy chain) without touching it.
- **The on-chip block is CPU timing, not a board feature.** The wait generator stretches every bus
  cycle by kind (opcode fetch vs operand fetch, INTA, RETI); a library that owns the bus cycles
  models that directly instead of through host hooks added for one machine.
- **It is the first step of the main CPU's own move to a library.** Every place where the main CPU
  will follow is marked `CPU-LIBRARY-MIGRATION(<id>)` in the code.

Design and status: [2026-10-01-z84c15-cpu-library](../2026-10-01-z84c15-cpu-library/README.md).
The analysis below is kept as the research found it; its facts (§1-§7) are the library's input.

### 8.1 Verdict of the research: a variant option on the existing core, not a separate CPU library

The owner asked for a separate library if the Sprinter CPU differs from the plain Z80 in
instruction behavior, timing, cycle structure, wait generation or on-chip bus logic. Sorted by
where each difference lives:

| Difference | Kind | Where it belongs |
|:--|:--|:--|
| `OUT (C),0` writes `#FF` | instruction behavior, one opcode | CPU variant flag (the field exists) |
| LD A,I/R + INT keeps P/V | interrupt behavior, one condition | CPU variant flag (the boundary code exists) |
| Instruction timings, M1/refresh, INTA 2 waits, IM2 19 T, NMI 11 T, WAIT sampling, Q flags | **identical** | nothing to do |
| Wait generator (memory/M1/I/O/INTA/RETI waits, MWBR range, 15-M1 power-on window) | extra wait states inserted into normal cycles; the cycle structure is unchanged | Z84C15 package, through bus hooks |
| CS0/CS1 | address-decode pins | Z84C15 package + the machine's memory map (done) |
| Clock divider, halt modes, reset output | clock and pins; no effect on the Sprinter | package (stored) |
| CTC/SIO/PIO, daisy chain, IM2 vectors, RETI watching | peripherals | package + the existing `IInterruptSource` / `OnReti` hooks |
| 8-bit decoding of on-chip ports | port decoding | package (`Z84C15::Owns`, done) |

None of these needs a different instruction engine. The Z84C15's core *is* a standard Zilog CMOS
Z80, and everything the chip adds sits on the bus side. The existing core already has the right
hooks for that bus side: `MemoryWaitOverlay`, `Z80::AddWaitStates`, `IInterruptSource`, `OnReti`.

A separate library would also replace the Sprinter's **main** CPU. `unreal-z80` only drives the
General Sound coprocessor. The main CPU carries TTD, breakpoints, the debugger, contention and the
wait overlays, and all of that would have to be rewired or duplicated for one machine with two
one-line differences. **This report recommends against it (owner's case (c)).**

What would change the verdict: a hardware measurement that shows a Z84C15 instruction timing or
undocumented result differing from the Z84C00. None is known (open question Q4).

### 8.2 What the existing core needs (case (c))

1. **`Z80Variant` setting** per model: `ZilogNmos` (default, today's behavior) and `ZilogCmos`.
   Optionally `NecNmos` and `StCmos` later, which only change the SCF/CCF rule.
   - `ZilogCmos` sets `outc0 = #FF`.
   - `ZilogCmos` turns the LD A,I/R quirk off: `HandleINT` skips the P/V clear when the variant has
     no quirk. Fold this into the same check RZX uses (`z80.cpp:835-837`).
   - It is a CPU configuration field like `outc0`, which TTD already treats as configuration, not
     state (`core/src/debugger/ttd/ttdcheckpoint.h:25`). It is set when the model is created.
     MM_SPRINTER uses `ZilogCmos`.
   - Mirror it in `unreal-z80` for parity: `outc0` is there already, and the quirk needs a switch.
     Upstream that change rather than patching locally (its README says "Local patches: none").
2. **One more hook for the Z84C15 waits:** "this access is the opcode fetch (M1)" as distinct from
   an operand fetch. `MemoryWaitAccess::Code` currently covers both. The wait generator's
   opcode-fetch extension needs to tell them apart. INTA and RETI waits need an
   interrupt-acknowledge callback that can add waits; it could join `IInterruptSource::AcknowledgeInterrupt`.
3. **Check the INT acknowledge against the wait overlay** (gap 4 in §7), with a test.

What stays in the Sprinter / `Z84C15` package:

- The WCR model. It needs a counter of M1 cycles since power-on: the first 15 run with `#FF`
  unless the program writes WCR earlier (the loader does, at its 8th M1). Memory waits apply only
  in the MWBR range; I/O waits never apply to on-chip ports.
- CS0/CS1, the watchdog (stored; run it only if Q3 shows `/WDTOUT` is wired), the CTC/SIO/PIO and
  their daisy chain.
- The Z84C15 waits and the PLD waits add up (external `/WAIT` is sampled after the programmed
  waits, §4.1). The overlay rule becomes `Z84C15 waits + PLD waits`.

### 8.3 Where MAME's "phase mod 6" rule comes from

It is **not** the Z84C15. WCR is 0 after the BIOS starts (§5), and MAME does not apply WCR at all.
The rule models the **PLD's** `/WAIT` output (`DCP.TDF:572`: `/WAIT = /IO_WAIT & /MR_WAIT`), which
has two parts.

**Memory: `/MR_WAIT`** (`DCP.TDF:484`):

- It is active during a CPU memory cycle until the PLD's DRAM cycle ends (`MC_END`).
- It is never active for the fast RAM ("cache", `/CASH`).
- It is never active at 3.5 MHz unless the accelerator is on (`!TURBO & !ACC_ON`).
- The DRAM cycles follow a 42 MHz six-step counter `CT[2..0]` (`DCP.TDF:247-256`): one turn is
  6 x 23.8 ns = 143 ns = 3 CPU clocks at 21 MHz. A memory request is latched once per turn
  (`MC_BEGIN.ena = CT1 & CT2`, `:440`).
- So a CPU access waits for the next free slot, which is where MAME's "align to the next
  boundary" comes from.
- MAME uses a 6-clock boundary, which is two counter turns. That fits if CPU and video take turns
  on the DRAM, but this pass did not prove it from the PLD source (Q2).

**I/O: `/IO_WAIT`** (`DCP.TDF:537-565`):

- A wait counter is loaded per port from a 3-bit wait class in the port-decoder table entry
  (`MEM.q[14..12]`), and counts down on the 21 MHz enable.
- In turbo the classes give 2, 2, 4, 4, 7, 7, 7, 10 clocks. At 3.5 MHz they give 2, 2, 1, 1, 1, 2,
  7, 10 clocks, but those are 21 MHz ticks, i.e. less than 2 CPU clocks.
- An older PLD version uses 10 instead of 7 for class 6 (`emulators/github/Sprinter200x/Altera_1K30/Sp2000/DCP.TDF:558`).
- Ports `111x x1xx` in DOS mode at 3.5 MHz get no wait (`:539`).
- `/IO_WAIT` is gated off for INTA (`IO_RWM` uses `/M1`, `:490`).
- **MAME uses one rule for every port** (align, then 6 - 4 = 2 more clocks). The PLD instead
  gives each port its own length.

**Worked example (I/O).** A port of wait class 7 at 21 MHz gets a 10-tick count. MAME gives the
same access 2 to 7 extra clocks depending on the phase. The S1 port trace still matches MAME
within ±1-7 clocks per access (technical-design §4), and that agreement is with MAME, not with the
board.

**Recommended path:**

- Keep MAME's rule as the v1 reference while the tests compare against MAME traces.
- Write a second rule from `DCP.TDF`: a slot counter for memory, the per-class table for ports.
  Keep it behind the same `SprinterWaits::Rule` seam (technical-design D4).
- Switch to the second rule once a real-board measurement decides between the two (Q2, Q5).

### 8.4 Tests that prove it

| Test | Expected value | Reference |
|:--|:--|:--|
| `OUT (C),0` under `ZilogCmos` | port write of `#FF`; `#00` under `ZilogNmos` | §3 |
| `EI; LD A,I` with an INT accepted at the next boundary, `I` irrelevant, IFF2 = 1 | P/V = 1 (CMOS), P/V = 0 (NMOS) | Zilog Q&A p. 3-130 |
| SCF/CCF Q table | same results for both variants | z80test `z80ccf` (vendored in `emulators/github/unreal-z80/data/z80test`) |
| zexdoc / zexall / z80test `z80full`, `z80doc`, `z80flags`, `z80memptr` | pass unchanged under `ZilogCmos`: these suites do not test the two differences, so they only guard against regressions | unreal-z80 harness |
| Z84C15 power-on window | 20 NOPs from reset: the first 15 take 8 T each, then 4 T each; reading WCR in the window gives `#FF` | PS0182 p. 318-319 |
| WCR = `#04`, MWBR = `#F0` | `LD (DE),A` = 7 + 2 = 9 T; the loader byte = 142 T (§5) | PS0182 p. 319 |
| WCR I/O field, on-chip port | `OUT (#19),A` with WCR = `#03` keeps 11 T; `OUT (#FE),A` gets +6 | PS0182 p. 319 |
| PLD waits (current rule) | `SprinterReference_Test.Bios304_PortTraceMatchesMame` stays green | `testdata/machines/sprinter/reference/ports.csv` (branch `sprinter-s1`) |
| Loader timing | with the loader run for real: last stream write at 6 691 665 t35 matches MAME without WCR; with WCR modeled it moves to about 8.41 M. Document the divergence instead of forcing MAME's number. | `loader.txt` (branch `sprinter-s1`), §5 |

## 9. Open questions

1. **Q1, loader clock.** At what clock does the CPU run while the ACEX is unconfigured? The MAX
   CPLD's `CLKZZ` net is described as "14 MHz" (`SP2_MAX.TDF:255-257`). It is not traced to CLKIN
   on the schematic.
2. **Q2, memory slot length at 21 MHz.** Is it 3 or 6 CPU clocks per CPU memory slot? This needs
   `DCP.TDF`'s `MC_BEGIN`/`MC_END`/`RAS`/`CAS` logic worked through, or a board measurement.
3. **Q3, watchdog.** Where does `/WDTOUT` go? It is enabled at power-on (`#FB`) and the BIOS never
   clears it.
4. **Q4, undocumented behavior.** No Z84C15 has been run through z80test or XCF Flavor. The CMOS
   behavior in §3 is inferred from the Z84C00.
5. **Q5, PLD waits on on-chip port writes.** The PLD sees `IORQ` for `#19` and the other on-chip
   ports. Does it also stretch them, and does the Z84C15 sample external `/WAIT` during internal
   I/O? MAME's measured intervals show no wait; S1 adds one (technical-design §4).
6. **Q6, 21 MHz on a 16 MHz part.** Running 31% over the rating does not change the logic, but
   board-to-board instability at 21 MHz is plausible. This is a note for anyone comparing against
   real hardware.
