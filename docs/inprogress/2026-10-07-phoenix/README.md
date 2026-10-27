# ZXM-Phoenix (`PHOENIX`, 1024K / 2048K) as a creatable machine

**Created:** 2026-10-07 · **Status:** design only, nothing implemented · see [TODO.md](TODO.md)

## What this is

`MM_PHOENIX` ("ZXM-Phoenix v1.0", short name `PHOENIX`, 1024K or 2048K) has been in the `MEM_MODEL` enum and the model table
for a long time, and `[ROM] PHOENIX=` is read from the INI, but the model cannot be created. `PortDecoder::GetPortDecoderForModel`
throws `std::logic_error` for it, `IsModelSupported` answers false, and the snapshot capture lists it among the models with no
layout (`capture_unsupported`). The firmware image `data/rom/ZXM-Phoenix_bios.bin` is already in the repository.

The ZXM-Phoenix is a Russian Spectrum clone with 1 or 2 MB of RAM, a Beta Disk (TR-DOS) interface and a NemoBus v1.1m slot
(the version table is in [the bus-slots research](../2026-10-03-zx-bus-slots/research-machines.md) section 4.1). The emulators
page it like the Kay (Nemo) line: `#1FFD` and `#7FFD` each supply some bits of one RAM page number, and four 16K ROM pages sit
behind `#1FFD` bits and `#7FFD` bit 4. No source in the local collection gives the board's date or authors; that is not needed
for the emulation.

This folder designs the machine. It is **small**: one port decoder, one paging function, one ROM layout, one TTD blob. Almost
everything else (Beta Disk, AY / TurboSound, Covox, Kempston joystick and mouse, General Sound, the Pentagon timing) is
reused from the Pentagon decoder unchanged.

## What the research found (and did not find)

| Finding | Where |
|:--|:--|
| **No manual, schematic or PROM dump of the board is in the local collection.** The three emulator sources below are the evidence. Every fact in the design carries the sources that state it, and anything only one source states is an open question | [research-reference-consensus.md](research-reference-consensus.md) |
| Three emulators model the machine: Unreal Speccy (the zx-evo / NedoPC line), Xpeccy and Xpeccy+. UnrealSpeccyP, UnrealSpeccy 0.39 and ZXMAK2 have **no** Phoenix | same |
| **They disagree on one thing that matters: which port bits are RAM page bits 3 and 4.** Xpeccy and Xpeccy+ agree with each other and with a "real" comment in the Xpeccy source; Unreal swaps the two. The design takes the Xpeccy order and keeps the choice in one table | [research-reference-consensus.md](research-reference-consensus.md) section 2 |
| `#1FFD` and `#7FFD` decode: Unreal and the Black_Cat port table (for the KAY-1024SL) agree, Xpeccy decodes A2 as well | same, section 3 |
| The ROM image holds **an erased SYS page** (16K of `#FF`), TR-DOS 5.03, a 128K editor ROM and 48 BASIC. Xpeccy+ labels the image "BIOS 5.03", which names the TR-DOS version, not a BIOS | [roms.md](roms.md) |

**Can it be done correctly? Yes for the memory, ports, ROM and timing a program can see; four details are open** (the SYS
page contents, the `#00F7` "version" port, an RTC, the 7 MHz switch). None blocks phase 1; each has a recommendation in
[TODO.md](TODO.md).

## Documents

| File | Contents |
|:--|:--|
| [requirements.md](requirements.md) | goals, non-goals, requirements with confidence and evidence, acceptance |
| [design.md](design.md) | hardware facts (ports, paging, ROM, video, devices) and how they fit unreal-ng: factory, decoder, ROM roles, config, TTD, snapshots, state transfer, media, automation, Qt; the phase plan |
| [tdd-plan.md](tdd-plan.md) | concrete tests per phase, with names and files |
| [roms.md](roms.md) | the ROM file, its page contents, checksums, where it lives |
| [research-reference-consensus.md](research-reference-consensus.md) | the three emulators and the Black_Cat table compared line by line; the consensus and the dissent |
| [TODO.md](TODO.md) | status marker, phase checklist, open questions with recommendations |

## Rules this folder follows

- Simple design, no new framework: a Phoenix is a Pentagon with a different pager.
- Real-hardware variants are faithful: 1024K and 2048K are two board builds, the wrap of the unused page bit is hardware behavior.
- Naive first, measure later: nothing here touches a hot path.
- Every phase ends in a verified `.recipe` and starts its verification with a TTD recording ([design.md](design.md) section 9).
