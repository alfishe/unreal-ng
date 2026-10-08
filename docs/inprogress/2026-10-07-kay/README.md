# Nemo's KAY (Kay-1024)

**Created:** 2026-10-07 · **Status:** research and design done, nothing implemented · see [TODO.md](TODO.md)

## What this is

Kay was a family of Spectrum 128K clones by Nemo (St. Petersburg): Kay-128 and Kay-256 (1994), Kay-1024/3SL/TURBO
(1998), the CPLD Kay-2006 NB and the four-slot Kay-2010. This folder designs the Kay-1024 as unreal-ng's `KAY`
machine. `MM_KAY` and its model row exist; the model is not creatable because no port decoder is registered.

**Can it be done correctly? Yes, for the Kay-1024.** The vendor passport (a full bit-by-bit port table), the
schematic and the designer's article agree on the ports, the RAM and ROM paging and the turbo rules. Unreal Speccy is the only
emulator with a Kay and differs in one decode (#7FFD). What stays open is listed in [TODO.md](TODO.md): the ROM
role table (settled by a boot test), the exact frame and INT numbers, the order of the two top RAM bits.

Key facts: 1024 KB RAM (64 pages of 16 KB), 64 KB ROM with four roles, `#1FFD` (A0 = 1, A1 = 0, A15 = A14 = 0) holds
RAM-at-0, turbo, ROM select and two RAM bits, `#7FFD` (A0 = 1, A14 = 1) the usual bits plus one RAM bit,
Kempston on every odd port, 3.5 / 7 MHz, three NemoBus slots, no contention at 3.5 MHz.

## Documents

| File | Contents |
|:--|:--|
| [requirements.md](requirements.md) | goals, requirements with confidence, acceptance |
| [design.md](design.md) | the machine, ports, memory map, video, turbo, how it fits unreal-ng (decoder, `PagingFamily`, snapshot capture, TTD blob, slots, Qt), phases with automation parity |
| [tdd-plan.md](tdd-plan.md) | concrete tests per phase |
| [roms.md](roms.md) | the firmware images, checksums, the role table question |
| [research-kay-reference-consensus.md](research-kay-reference-consensus.md) | every source, what each says, the comparison and the chosen answer |
| [TODO.md](TODO.md) | status, phase checklist, open questions with recommendations |

Related: [zx-bus-slots research](../2026-10-03-zx-bus-slots/research-machines.md) (NemoBus, IORQGE),
[contention research](../2026-09-28-m1-contention/contention-by-machine.md) (no contention at 3.5 MHz),
[Profi v3 / v5](../2026-10-01-profi-v3-v5/README.md) (the model for this folder),
[snapshot pipeline](../2026-10-02-snapshot-pipeline/TODO.md) (Kay named as "cannot be created yet").
