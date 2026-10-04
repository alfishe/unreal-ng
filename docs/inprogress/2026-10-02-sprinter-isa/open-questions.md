# Sprinter ISA slots: open questions for the owner

| | |
|---|---|
| **Date** | 2026-10-02 |
| **For** | [tdd.md](tdd.md) (the design follows each recommendation below until decided otherwise); facts in [research.md](research.md) |
| **Order** | most important first; Q1-Q4 are needed before phase I1 / I2 starts |

## Q1. Is the ISA bus Sprinter-only, and how much of a shared "ZX-bus" do we build now?

**Owner decision (2026-10-02): A** - the ISA bus is Sprinter-only with the one `ZxBusPresent()` seam. The shared ZX-bus (phase I5) moves into the bus and slot unification, [PLAN #82](../PLAN.md).

**Background.** No other emulated machine has ISA slots. The ZX-bus, on the other hand, is shared: the GS,
MoonSound and ZXNETUSB are ZX-bus cards used on most machines. They attach today through two different doors
(the exact port map that machine decoders call, and full-decode observers in the Z80 port funnel). The owner's
stated direction is "machine -> buses / extension slots -> devices".

| Option | Effect |
|---|---|
| **A (recommended)** | ISA bus, slots and cards Sprinter-only (`core/src/emulator/io/sprinter/isa/`). ZX-bus: one new shared virtual `PortDecoder::ZxBusPresent()` (default `true`, so no other machine changes); the adapter reaches the GS through the existing exact port map. The full ZX-bus seam (cards register on "a ZX-bus", the native port funnel and the adapter are two bus hosts) waits for phase I5 or the general slots work |
| B | Build the shared ZX-bus seam now, in I2: cleaner end state, but it touches GS, MoonSound and network attachment on every machine (A/B benchmarks, TTD fixtures of other machines) for no Sprinter program that needs more than the GS |
| C | A generic shared `isa8` bus | clutter: nothing else would use it |

**Recommendation: A.** It reuses the GS card unchanged and costs other machines nothing; B can follow without
undoing anything.

## Q2. Default slot population of the `SPRINTER` model

**Owner decision (2026-10-02):** slot 1 = ZX-bus adapter with the NeoGS, slot 2 empty.

| Option | Effect |
|---|---|
| **A (recommended)** | Slot 1 = ZX-bus adapter with the GS (personality `[SOUND] GSType=NGS`, as the Sprinter config has today), slot 2 empty. Matches MAME's default and the owner's MAME setup; ProPlay works out of the box; the TTD boot fixture keeps its GS blob |
| B | Both slots empty, as a stock Sp2000 left the factory | faithful to the stock board, but MOD playback needs a config edit; the GS is then not built (frame cost drops) |

**Recommendation: A** (the owner uses the adapter + NeoGS daily); B stays one INI line away (`Slot1=NONE`).

## Q3. Does ISA RESET (`#9FBD` bit 7) reset the General Sound behind the adapter?

**Owner decision (2026-10-02): yes** - ISA RESET reaches the GS through the adapter, as on the hardware; the difference from MAME (which ignores the bit) goes into the MAME comparison.

**Background.** RESET DRV is the only reset an ISA card receives, and on the Sprinter it is driven only by
software through `#9FBD`. No adapter schematic was found. ESSMIXER and the Wi-Fi library pulse it (`#C0`, 1 ms,
`#00`), and the pulse reaches **both** slots: running ESPT with a GS in the other slot would restart the GS on a
board wired that way. MAME ignores the bit.

| Option | Effect |
|---|---|
| **A (recommended)** | Pass it through: RESET held = GS card held in reset (the `#33` bit-7 "reset_card" path: CPU and banking reset, mailbox kept) |
| B | Ignore it, as MAME does | identical to MAME in every case; wrong if the real adapter wires RESET DRV to the ZX `/RESET` |

**Recommendation: A**, the most plausible wiring; it does not affect ProPlay (it never writes bit 7). A photo or
schematic of a real adapter would settle it - does the owner have one?

## Q4. Which NeoGS firmware for the comparison with MAME?

Our NeoGS flash image is v1.11 (`data/rom/neogs/full_ngs.rom`, CRC `9e14798b`); MAME's NeoGS defaults to
v1.10 fix2 (`neogs110_fix2.rom`, CRC `641a4976`) and the owner's MAME pack adds an SD card image (`neogs.chd`).
Firmware differences change the waveform, not pitch or tempo.

**Outcome (I2, 2026-10-04):** with the generated test MOD our NeoGS v1.11 and MAME's v1.10 fix2 (loaded into our
NeoGS with `UNREAL_SPRINTER_NGS_FLASH`) give the same waveform (correlation 0.9998 with MAME's) - the choice does not
matter for ProPlay ([i2-outcome.md](i2-outcome.md) §3).

**Recommendation:** the test suite asserts only firmware-independent facts (pitch from the MOD's note periods,
tempo from its speed / BPM) on both our personalities (classic GS `gs105a` and NeoGS v1.11). The one-off
waveform comparison with MAME (T-ISA-11) loads MAME's v1.10 fix2 image into our NeoGS (`[NGS] Flash=`, from the
owner's MAME ROM set, not committed) and uses an empty SD card on both sides.

## Q5. Should slots be refittable while the machine runs?

| Option | Effect |
|---|---|
| **A (recommended for v1)** | Population read at instance creation (like `GSType` today); the GS personality switch at run time keeps working |
| B | Insert / remove cards at run time from Qt and automation | needs TTD barriers for refits, a Qt settings page; natural with the general slots work |

**Recommendation: A now, B together with the general "buses / slots" work.**

## Q6. ISA wait states at 21 MHz

The PLD holds WAIT for about 4-5 periods of its 42 MHz clock on an ISA access (roughly 2-3 CPU clocks in
turbo; a reading of the AHDL, research §5). MAME charges an ISA access like a RAM access (align to 6 clocks).

**Recommendation:** MAME's rule in v1, so polling loops match MAME during the A/B; switch to the PLD counter
only after a measurement on the timing tests shows a difference that matters. It changes how many status polls
a loop makes, never what the GS plays.

## Q7. ZX-bus memory cycles and ZX-bus network cards through the adapter

The adapter as modeled passes ISA **I/O** cycles only (as MAME). A ZXNETUSB (W5300) uses a memory window and
the NeoGS ZX-DMA needs the Spectrum `/CSROM` signal; neither exists through ISA.

**Recommendation:** no memory cycles through the adapter; the Sprinter reports no native ZX-bus to the network
manager (`DescribeNetwork().zxBus = false`); the NeoGS ZX-DMA cannot install on the Sprinter. Network on the
Sprinter goes through ISA network cards (NE2000-class Ethernet, SprinterESP Wi-Fi, 3C509B, ISA modem), which real
Sprinter software (the 2026 network kits, ESPT, wterm, BC-Term) uses: designed in
[2026-10-02-sprinter-network](../2026-10-02-sprinter-network/tdd.md).

## Q8. Order of the cards after the GS

**Recommendation:** I3 ISA RAM (S; Shaos's TIMER), I4 16550 UART cards with the ESP Wi-Fi module (M; reuses
the network stack; ESPT, wterm, BC-Term), then deferred: I6 ESS688 / Sound Blaster Pro (L; only ESSMIXER found,
which only sets the mixer), I7 SprinterJoy (its board is "in development"), I8 Sprinter-FT (experimental FT812
card), I5 MoonSound on the adapter (no Sprinter program). Is that the owner's order?

*Update 2026-10-02:* the owner decided that NE2000-class Ethernet is built; the network cards have their own
design and phases ([2026-10-02-sprinter-network](../2026-10-02-sprinter-network/tdd.md) §16), and I4 keeps only
the PIO interrupt lines. Whether the network cards go before ISA RAM is that folder's open question Q7.

## Q9. Does a Sprinter machine reset reset the GS?

On the real board the `#9FBD` latch has no reset input, so pressing reset does not pulse ISA RESET and a GS
behind the adapter would keep running (and keep playing). The shared option `[SOUND] GSReset=1` (default)
reinitializes the GS on every machine reset.

**As built (I2):** `GSReset=0`. Found on the way: the BIOS itself pulses RESET DRV at POST (3.07 BETA 1: `#9FBD` <- `#FF`,
then `#00`), so the GS behind the adapter is reset on every boot through the ISA reset, not through the machine reset.

**Recommendation:** follow the hardware: `GSReset=0` in the Sprinter config, documented in the INI; the user can
set 1. (The faithful-hardware rule; it only affects what is heard right after a reset.)

## Q10. Test MOD files

The MAME pack's media disk has MOD files (`1974_1.MOD`, `MGL.MOD`, `XTD.MOD`), authors unknown.

**As built (I2):** `tools/machines/sprinter/test-mod/make-test-mod.py` (one 64-byte sine sample, C-3 / E-3 / G-3 for
16 rows each, 16 silent rows, speed 6 / 125 BPM); the test writes the same bytes into a session copy of the system disk.

**Recommendation:** the committed tests use a MOD **generated by a script** (one sample, three notes, fixed
tempo: the expected pitch and timing are computable); the pack's MODs serve the manual MAME comparison only and
are not committed.

## Q11. ISA RAM card contents at power-on

Real SRAM powers up with random contents; TTD needs a defined start.

**Recommendation:** `#FF` (what an empty ISA address reads, so a program that probes for RAM by reading first
sees "no change" until it writes). Alternative: 0.

## Q12. The old port-table code `#32` ("GS port redirected to ISA")

BIOS 3.04 maps native ports `#A3`, `#AB`, `#B3`, `#BB` ... (DOS off) to code `#32`, an Sp97 leftover; on the Sp2000
it reaches no slot (research §4.4) and no program uses it.

**Recommendation:** keep reading `#FF` / ignoring writes (today's behavior and MAME's) until a real-board check
says otherwise.

## Q13. Where the work is tracked

**Recommendation:** as phase S6b of PLAN row #59 (the Sprinter program), with this folder linked from the
Sprinter `TODO.md` and the roadmap's S6b row, rather than a new PLAN row. The later cards (I3-I8) appear in the
Sprinter `TODO.md` remaining list.
