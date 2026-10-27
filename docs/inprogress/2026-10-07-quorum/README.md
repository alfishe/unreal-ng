# Quorum (Kvorum) 128K / 1024K

**Created:** 2026-10-07 · **Status:** design only, nothing implemented · see [TODO.md](TODO.md)

## What this is

The Quorum (Russian "Кворум"; the ROM carries "1994 Quorum Ltd." and the menu "ROM-MENU QUORUM V.4.2 27.06.1997", and
the ZXMAK2 source calls the board "БК-04") is a Spectrum clone with a partial I/O decode of its own, its own 64K ROM (system menu, TR-DOS, 128 editor, 48 BASIC), a built-in
WD1793 floppy controller at its own ports (`#80..#85`), an extra 48-key keyboard matrix (`#7E`), and a control port
`#00` that maps RAM over the ROM, selects the ROM page and write-protects memory. It exists in several RAM sizes (128K
and 1024K in UnrealSpeccy; 64K, 256K, 128K and 1024K in the ZXMAK2 family).

unreal-ng already has the model slot and half of the plumbing, but the machine cannot be created:

| Piece | State today |
|:--|:--|
| `MM_QUORUM` in `MEM_MODEL`, model-table row `Quorum` / `QUORUM` / 1024K / `RAM_128 \| RAM_1024` | present (`core/src/emulator/config.h`, `platform.h`) |
| `[ROM] QUORUM=` (`config.quorum_rom_path`), ROM page roles SYS / DOS / 128 / 48 in `rom.cpp` | present |
| `Ports::_p00`, `_p80FD` latches | declared, never written |
| `data/rom/qu7v42.rom` (the factory ROM, 64K) | shipped, md5 `f559d4f0137b8ed062899afbc209f211` |
| Port decoder (`PortDecoder::GetPortDecoderForModel`) | **missing** - `IsModelSupported(MM_QUORUM)` is false, so the machine cannot be started |
| Memory mapping, video mode, config folder, TTD, snapshots, automation, Qt | missing |

**Can it be done correctly? Yes for 128K.** Two independent emulators (UnrealSpeccy, ZXMAK2) plus the Black_Cat port
table agree on the port decode, the ROM layout, the paging and the keyboard; the factory ROM itself confirms the FDC
ports and the `#00` usage. The 1024K paging bits, the video timing and a few `#00` bit semantics differ between sources
and are open questions with recommendations ([research-quorum-reference-consensus.md](research-quorum-reference-consensus.md)).

## Documents

| File | Contents |
|:--|:--|
| [requirements.md](requirements.md) | goals, requirements with confidence and evidence, acceptance |
| [design.md](design.md) | hardware facts (ports, paging, ROM, video, devices) and how they fit unreal-ng; the phased plan |
| [tdd-plan.md](tdd-plan.md) | concrete tests per phase (names, files, pass criteria) |
| [roms.md](roms.md) | the factory ROM, its pages, checksums, where it is |
| [research-quorum-reference-consensus.md](research-quorum-reference-consensus.md) | every disputed fact tabulated across UnrealSpeccy / ZXMAK2 / kozynax / Black_Cat / the ROM, with the verdict |
| [research-quorum-keyboard.md](research-quorum-keyboard.md) | the `#FE` and `#7E` matrices, NMI / RES keys, host key mapping |
| [TODO.md](TODO.md) | status marker, phase checklist, open questions each with a recommendation |

## Sources read

All facts in these documents come from sources that were read for this design:

- UnrealSpeccy (Quorum support added in the 0.3x line, "Добавлена поддержка quorum 128/1024" in its news file):
  [io.cpp](https://github.com/alfishe/unreal-speccy/blob/HEAD/io.cpp),
  [memory.cpp](https://github.com/alfishe/unreal-speccy/blob/HEAD/memory.cpp),
  [config.cpp](https://github.com/alfishe/unreal-speccy/blob/HEAD/config.cpp),
  [vars.cpp](https://github.com/alfishe/unreal-speccy/blob/HEAD/vars.cpp) (the `#7E` key table),
  [wd93cmd.cpp](https://github.com/alfishe/unreal-speccy/blob/HEAD/wd93cmd.cpp),
  [x32/quorum.ini](https://github.com/alfishe/unreal-speccy/blob/HEAD/x32/quorum.ini)
- ZXMAK2: [Quorum folder](https://github.com/zxmak/ZXMAK2/tree/HEAD/src/ZXMAK2.Hardware/Quorum) (`MemoryQuorum*.cs`,
  `UlaQuorum.cs`, `FddControllerQuorum.cs`, `KeyboardQuorum.cs`) and `machines.config`
- kozynax (a ZXMAK2 fork, 2025): [Quorum folder](https://github.com/kozynax/kozynax/tree/HEAD/src/ZXMAK2.Hardware/Quorum),
  which adds the 128K and 1024K memory classes and a Pentagon-timing bit
- Black_Cat, "Guide to the ZX Spectrum ports", BC Info Guide #4 (2008, www.zx.clan.su): the Quorum ("C") decode rows
- The factory ROM `qu7v42.rom` itself (byte scans of the four pages, see [roms.md](roms.md))

Not available: a Quorum schematic or manual (none in the local collection; its hardware, ROM and press folders hold
nothing about the Quorum), xpeccy (its reports and sources have no Quorum), ZX-M8XXX (ships the ROM, no machine). The
claims that rest on one source are marked as such.

## Relation to other work

- Same pattern as [Profi v3 / v5](../2026-10-01-profi-v3-v5/README.md): one decoder class, board facts in one place.
- The FDC sits at non-Beta ports and its system port needs a translation into the Beta128 `#FF` format that the
  existing WD1793 model understands ([design.md](design.md) section 4.4).
