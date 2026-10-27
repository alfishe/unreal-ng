# LSY256: what the references say

**Date:** 2026-10-07 · part of [README.md](README.md)

## 1. Who has the model

| Source | Has LSY / Orel? | Checked how |
|:--|:--|:--|
| Unreal Speccy NedoPC, <https://github.com/tslabs/zx-evo> | **yes**, `MM_LSY256`, since 2014-06-02 (commit message says "stub") | read `memory.cpp`, `io.cpp`, `emul.h`, `z80.cpp`, `config.cpp`, `vars.cpp`, `gui.cpp` |
| Unreal Speccy alfishe fork, <https://github.com/alfishe/unrealspeccy> | no (has `MM_KAY`, not `MM_LSY256`) | grep of all sources |
| UnrealSpeccyP, <https://github.com/djdron/UnrealSpeccyP> | no | grep |
| ZXMAK2, <https://github.com/zxmak/ZXMAK2> | no (Memory classes: Atm450/710, PentEvo, Lec528, Pentagon, Profi1024, Quorum, Scorpion, Plus3, 48, 128) | grep, class list |
| Xpeccy, <https://github.com/samstyle/Xpeccy> | no | grep |
| xpeccy-plus, <https://github.com/dotkoval/xpeccy-plus> | no | grep; the local report files do not mention it either |
| ZX-M8XXX, zesarux, Fuse, zxsp, pico-spec, Zero-Emulator, MAME | no | grep |

Result: the "consensus" is one emulator. The design takes it as the baseline and marks what cannot be cross-checked.

## 2. Unreal's model, line by line

All paths under `pentevo/unreal/Unreal/` of the zx-evo repository.

| Item | Unreal | File |
|:--|:--|:--|
| Model row | `{ "Orel' BK-08 (LSY)", "LSY256", MM_LSY256, 256, RAM_256 }` | `vars.cpp` |
| Latch | `u8 pLSY256`; `#define PF_DV0 0x01`, `PF_BLKROM 0x02`, `PF_EMUL 0x08`, `PF_PA3 0x10`; comment "LSY256 - BarmaleyM's Orel' extension" | `emul.h` |
| Write | on `OUT`, if the model is LSY256 and the **low address byte** is `#7B` (any high byte): latch = value, then `set_banks()`. No `return`: the decode continues | `io.cpp` |
| Read | none: `#7B` is write-only in Unreal | `io.cpp` |
| Reset | `pLSY256 = 0`; the reset mode is forced to the SYS ROM | `z80.cpp` |
| Paging | window 0 by `(latch & (EMUL\|BLKROM))`, window 3 = `page_ram((p7FFD & 7) \| (latch & PF_PA3))` | `memory.cpp` |
| ROM roles | page 0 = 128, 1 = 48, 2 = SYS (LSY-Setup), 3 = TR-DOS | `config.cpp` `apply_memory` |
| ROM path | `[ROM] LSY=rom\lsy256.rom` | `config.cpp`, `cfg/Unreal.ini` |
| Keyboard | table `zxk_bk08` (BK-08 keys on 8 half-rows, a 7th bit per row); added in version 0.37.1 (2008-09-14) "Orel' BK-08 keyboard added (Sergey V. Korop)" | `vars.cpp`, `doc/history.txt` |
| Video / timing | nothing model-specific: the `[ULA]` section of the INI decides (default 71680 T frame, 224 T line) | `cfg/Unreal.ini` |

## 3. Defects and gaps in Unreal's model (each is an open question in TODO.md)

1. **Window-3 page bit.** `latch & PF_PA3` is `0x10`, so the page index is `16 + (7FFD & 7)`, not `8 + (7FFD & 7)`.
   `page_ram()` does not mask, so Unreal reads pages 16..23, beyond the 256K (its RAM array is 4 MB, so it does not
   crash). The ROM's own RAM test ([research-rom-analysis.md](research-rom-analysis.md) section 3) treats the latch bit as
   page bit 3 of 16 pages. The design uses `8 + n`.
2. **The EMUL bit is never set by the shipped ROM's immediate writes.** Without it the machine shows only the SYS ROM at
   `#0000`. How the 128 / 48 / TR-DOS pages come into view on real hardware is not shown by Unreal.
3. **No TR-DOS entry from RAM at `#0000`.** In the RAM modes `set_banks()` does not arm the `#3Dxx` trap
   (`CF_LEAVEDOSRAM` and bank 0 in RAM).
4. **Bits 2, 5, 6, 7 of `#7B` are unused**, and `#7B` cannot be read. Real hardware may differ.
5. **The author called it a stub.**
6. **Pages 8..11 hold nothing at reset.** Unreal does not fill them. They are the "virtual ROM" the mode `EMUL|BLKROM`
   shows read-only; whoever fills them is the software (the SYS ROM has `LDIR` routines).
7. **The `7FFD` decode.** For this model Unreal uses its generic rule: A15 = 0 and A1 = 0 (so `#7FFD`, `#3FFD`, `#7FFF` ...
   all hit), bits 6 and 7 are ignored by paging, bit 5 locks.

## 4. Consensus table for the points where a second source could exist

| Point | Unreal | Second source | Taken |
|:--|:--|:--|:--|
| `#7B` decode | low byte | none | low byte `#7B`, high byte ignored |
| Window-0 modes | 4 modes | none | as Unreal |
| Window-3 page bit | `+16` (see 3.1) | the ROM's RAM test | `+8` |
| Frame / INT | none | none | Pentagon-class (71680 T), open (Q5) |
| AY / Covox / Kempston | generic Unreal sound setup | none | the Pentagon 128 set, open (Q6) |
