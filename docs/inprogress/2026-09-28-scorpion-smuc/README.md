# Scorpion SMUC: how hard is full integration?

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Question** | What does it take to integrate the Scorpion **SMUC** card fully and make the Scorpion ZS-256 (and the Scorpion with ProfROM) work with it? |
| **Code state analyzed** | branch `ide-atapi` (IDE rollout 1: the ATA disk core, ATAPI, slots, image formats, IDE TTD), on `master` since `f5fc5f05` |
| **Status** | analysis and plan. Update 2026-10-04: the clock moved onto the shared `Ds12887` (emulated time, TTD) with PLAN #60(c); the rest is open - the readiness table in [TODO.md](TODO.md) is the current state |

## Answer in short

**Moderate, and the hard part is already done.** SMUC is an IDE port plus a clock, a 2 KB
settings memory, a "virtual floppy" register and an optional interrupt controller. The IDE part,
which is most of the work for any hard-disk card, landed with IDE rollout 1 and already works
under the real ProfROM: it asks the disk for its identity and goes on to read sectors. What is
left:

1. **Five small bus fixes** (S): answer only while the TR-DOS ports are on, the real decode mask,
   the IDE reset bit the right way round (the ProfROM firmware proves "0 = reset", answering
   question Q3 of the IDE design), the interrupt bit, the virtual-floppy readback.
2. **One presence rule** (S): "fitted" from the config, the same on both Scorpion models.
3. **A real clock and settings memory** (M): saved between runs (today the ProfROM reformats its
   settings on every cold boot, ~4 s) and driven by emulated time so time travel replays.
4. **Time travel for the card** (M).
5. **Acceptance on the real ProfROM** (L, mostly test-image work): create a partition, mount a
   TR-DOS image from the disk on drive A, run a program from it.
6. Reports, Qt option, docs (M). The 8259 interrupt controller is optional (L); every other
   emulator treats it as absent.

The shipped Scorpion configs keep the card **not fitted**, as MAME and UnrealSpeccy do.

## Documents

| File | Content |
|---|---|
| [hardware-reference.md](hardware-reference.md) | **What SMUC is.** Sub-devices, the full port map with decode masks, TR-DOS gating, every `#FFBA` and `#7FBA` bit, the 8259, version registers, the IDE window; UnrealSpeccy / Xpeccy / MAME / ZXMAK2 / ports guide / ProfROM firmware tabulated with the consensus; Q3 answered |
| [current-state-and-gaps.md](current-state-and-gaps.md) | What unreal-ng has on master (IDE rollout 1), what works, what is stubbed, why the card is absent by default, the gap list G1-G13 |
| [software-and-boot.md](software-and-boot.md) | Which software uses the card, how the ProfROM boot changes with it, what is on the disk (partition types, SMFS, FAT), test images, levels of "works" |
| [integration-plan.md](integration-plan.md) | Phases S1-S8 with files, tests, TTD, automation / Qt, the default decision, risks, open questions, effort and verdict |
| [TODO.md](TODO.md) | Status marker |

## Glossary

| Term | Meaning |
|---|---|
| **SMUC** | the Scorpion add-on card described here ("Scorpion & MOA Universal Controller") |
| **ProfROM** | the Scorpion's large replacement firmware (4.01 ships in `data/rom/`); it contains the SMUC drivers, a hard-disk utility and a patched TR-DOS. Its 512 KB are split into 16 KB **pages**; each 64 KB **plane** has a BASIC 128, BASIC 48, service and TR-DOS page |
| **TR-DOS ports** | the Beta 128 floppy-controller ports (`#1F` ... `#FF`). On the Scorpion they, and SMUC, answer only while the TR-DOS ROM is paged in |
| **`#3D2F` trick** | a program that needs the TR-DOS ports jumps to address `#3D2F` in the TR-DOS ROM with the address of an `IN A,(C) : RET` (`#3FF3`) or `OUT (C),A : RET` on the stack; the ROM pages in, the instruction runs with the ports on, the ROM pages out |
| **IDE / ATA** | the PC hard-disk interface: eight registers (data, error / features, count, sector, cylinder low / high, drive-head, status / command) plus a **control block** (device control / alternate status) |
| **Task file** | those eight registers |
| **INTRQ** | the drive's "command finished" interrupt line |
| **SRST** | software reset: a bit in the device control register |
| **Latch (high byte)** | the Z80 moves 8 bits, the IDE data register 16: the card keeps the other half in a latch (`#D8BE`) |
| **RTC / CMOS** | the battery-backed clock chip (DS1685) and its small RAM |
| **EEPROM / NVRAM** | the 2 KB 24C16 settings memory, read and written bit by bit over **I²C** (two wires: clock SCL, data SDA; the device answers each byte with an **ACK**, a 0 on SDA) |
| **8259 / PIC** | Intel's programmable interrupt controller; **IMR** = its interrupt mask register |
| **ISA** | the 8-bit PC expansion bus; SMUC maps PC I/O `#200-#3FF` into Z80 ports |
| **Virtual FDD** | the ProfROM feature that makes drive A or B read a disk image stored on the hard disk; the `#7FBA` register says which drives are virtual |
| **SMFS / MFS** | the ProfROM's own hard-disk volume type: a collection of TR-DOS disk images |
| **Fitted / absent** | whether the card is on the bus in the emulated machine |
| **TTD** | time-travel debugging: the emulator records state blobs per frame and replays; a device must put all of its state into its **blob** |
| **Consensus** | the value most references agree on; the rule for hardware facts in this project |
