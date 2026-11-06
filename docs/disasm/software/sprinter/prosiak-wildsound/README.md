# prosiak.exe (Sprinter): Wild Sound XM player, its loader and its packer

`DEMOS/WILDSND/prosiak.exe` on the MAME pack's system disk plays one XM module ("Prosiak") on the
**ISA Wild Sound** card (Robus; STM32F405, four AY, AYX-32 compatible). On unreal-ng it prints
"Wild Sound ... ISA #0 Not Found", the same for #1, and exits, because the emulator has no such card.
This folder takes the file apart: the loader chain and its packer, the player's start-up, and the
part of the card's protocol the player uses.

| | |
|---|---|
| File | `DEMOS/WILDSND/prosiak.exe`, MAME pack `sp_hdd_sys.chd` (DSS 1.71); copy in `~/Downloads/zx-spectrum/sprinter/peripherals/developer-interest/disk-notes/` |
| Size / CRC32 | 150 003 bytes, `2114b9b9` |
| Banner | "Boot Loader For Sprinter By Rob F." (the same author as the card) |
| Listings | [loader.asm](loader.asm) (stage 1 and the depacker), [stage2-vm.asm](stage2-vm.asm), [player.asm](player.asm) |
| Tools | [tools/unpack_prosiak.py](tools/unpack_prosiak.py) (unpacks every block), [tools/make_listings.py](tools/make_listings.py) (rebuilds the listings), [tools/lzsa2.py](tools/lzsa2.py) (LZSA2 decoder) |
| How it was done | [walkthrough.md](walkthrough.md): the TTD session through MCP, step by step |

Checked 2026-10-08: BIOS 3.06 Hotfix 2, the file started from Flex Navigator 1.15, a TTD recording
from the Enter on. Every unpacked part was compared byte for byte with the emulated machine's memory
or with the official LZSA tool (below).

## The file

A DSS EXE with a **primary loader**: the header names a piece at the end of the file that DSS loads
and starts; DSS keeps the file open and passes its handle in `(IX-3)` (`CLP_FM` in the BIOS sources'
`dss_equ.inc`). The loader reads the rest itself.

| File offset | Content |
|---|---|
| `#0000` | header: `"EXE"`, 0; `+4` dword = loader offset `#24347`; `+8` dword = loader length `#6AC`; `+#10` load `#4327`; `+#12` start `#4409`; `+#14` SP `#40FF` |
| `#0100` | the VM script (below) |
| `#0180-#24346` | eight packed blocks, back to back |
| `#24347-#249F2` | the primary loader |

## Three stages

1. **Primary loader** (`#4327`, page of window 1). Prints the banner, takes three pages from DSS
   (`GetMem` `#3D`, then BIOS `GetMemBlkPages` `#C5` for their physical numbers), maps one into
   window 0, copies its LZSA2 depacker to `#3D4F`, unpacks the stream at `#44EA` (`#459` bytes) to
   `#0000` of that page and calls `#0100`. When stage 2 returns: close the file, "Done ...",
   free the pages, DSS `Exit`.
2. **Loader VM** (`#0000-#0606` of the window-0 page, `#607` bytes). RST `#08` / `#10` go through
   trampolines at `#3DDF` / `#3DE5` that map the DSS page back for the call. Stage 2 runs a byte-code
   script read from the file, `#40` bytes at a time.
3. **Player** (block 0, `#4500`, `#8D5` bytes). Called by the script: "XM Init ... OK", the card
   probe, then the loaders for the module parts.

## The packer

**LZSA2** (Emmanuel Marty, [github.com/emmanuel-marty/lzsa](https://github.com/emmanuel-marty/lzsa)),
raw blocks with the end-of-data marker, forward. The depacker at `#3D56` is a size-optimised LZSA2
decoder (nibble reader `#3DCE`, length extension `#3DB7`). `lzsa -d -r -f 2` (commit `15ee2df`) decodes
the stage-2 stream and all eight blocks; its output equals what the TTD shows in memory.

Each block record in the file is `[unpacked length, word][LZSA2 stream]`. The VM reads a record so
that it **ends** where the unpacked data will end and unpacks it in place, forward: the output start
is `record end - unpacked length`. The 32 bytes behind the record are kept at `#05A4` during the
unpacking and put back after it.

The VM can also XOR a block after reading it: a destination with bit 7 of the high byte set is XORed
with a key at `#05C6`, cycled to its zero byte (`ReadChunk`, `#04F0`). An op `#66` compares a script
word with `#05C4` before the key is used. **This file uses neither**: no record has the bit set,
`#05C4` stays `#FFFF` and the key is empty. So the "crypter" part of the format is present in the
loader but idle here; every block is plain LZSA2.

## The VM

The opcode is the displacement of a self-modified `JR` at `#0252`: handler = `#0254 + op`.

| Op | Handler | Operands | What it does | Seen |
|---|---|---|---|---|
| `#05` | `#0259` | `A, DE, HL, BC` | read `BC` bytes from file offset `#100 × (DE + 1) + A` to `HL`, unpack in place | traced |
| `#24` | `#0278` | `lo, hi` | `CALL hi lo`, with HL / BC = the last block's address / length | traced |
| `#00` | `#0254` | `A, DE, HL, BC` | read without unpacking | code only |
| `#1A`, `#1F` | `#026E`, `#0273` | record | write a record to the `#8000` window | code only |
| `#40`, `#47`, `#63` | `#0294`, `#029B`, `#02B7` | target | gosub, goto, return (Z80 stack) | code only |
| `#57` | `#02AB` | `fn, arg` | pages: map a tag into window 2 / 3, restore, remember, allocate | code only |
| `#66` | `#02BA` | `key, target` | XOR-key check, else jump | code only |
| `#E8`, `#EC` | `#023C`, `#0240` | `a, b, cond, target` | if a ⟨cond⟩ b: gosub / goto; cond `#C0` <, `#C6` ≤, `#CE` =, `#D4` ≥, `#DC` >, `#E2` ≠ | code only |

"Traced": every opcode fetch (`#024D`) found in the TTD history. "Code only": read from the
handler; this file's script does not use it.

The script of this file (`tools/unpack_prosiak.py` prints it):

```text
0100: 05 80 0000 487F 0556   block 0: file #00180 -> #4500, #08D5 bytes   the player
0108: 24 4500                call PlayerInit (probe; returns only when a card answers)
010B: 05 D6 0005 8A76 0E31   block 1: file #006D6 -> #4E00, #4AA7 bytes   track data
0113: 24 4755                call LoadTrack       "XM TRK Data"
0116: 05 07 0014 4EEE 00CB   block 2: file #01507 -> #4E00, #01B9 bytes   instrument headers
011E: 24 4796                call LoadInsHeaders  "XM INS HDR"
0121: 05 D2 0014 5474 798C   block 3: file #015D2 -> #4E00, #8000 bytes   samples
0129: 24 47AB                call LoadSamples     "XM INS"
012C: 05 5E 008E 587F 7581   block 4 (samples), 0134: 24 47AB
0137: 05 DF 0103 5A54 73AC   block 5 (samples), 013F: 24 47AB
0142: 05 8B 0177 5365 7A9B   block 6 (samples), 014A: 24 47AB
014D: 05 26 01F2 5631 5021   block 7: #5852 bytes (samples), 0155: 24 47AB
0158: 24 47C0                call Play            "XM PLAY", waits for a key
015B: 24 011C                call VmExit          back to the loader: "Done ..."
```

| Block | Unpacked | CRC32 | Content |
|---|---|---|---|
| 0 | 2 261 | `58ecc510` | the player code ([player.asm](player.asm)) |
| 1 | 19 111 | `3232abe3` | the module's track data in the player's own format (name "Prosiak"); no "Extended Module" header: the XM was converted beforehand |
| 2 | 441 | `66889e1d` | instrument headers |
| 3-7 | 32 768 × 4 + 22 610 | | 8-bit unsigned samples (centred on `#80`) |

The Z80 does not play anything: it uploads the parts to the card and tells it to play. The mixing
runs on the card's STM32.

## The card as the player sees it

Access path, as for every ISA card on the Sprinter ([ProPlay](../proplay/README.md#how-it-reaches-the-card)):
`#1FFD` = `#11`, window 3 = `#D4` (slot 0, ISA I/O) or `#D6` (slot 1), `#9FBD` = `#00` (A19-A14 = 0,
AEN off), then `LD (#C000+port),A` / `LD A,(#C000+port)`; afterwards `#1FFD` = `#01`. On exit the
player sets `#9FBD` = `#40` (AEN on). Two ISA I/O ports:

| Port | Use |
|---|---|
| `#0000` | command |
| `#0001` | data, both directions |

| Command | Sent | Answer |
|---|---|---|
| `#F0` | `"Wild Sound Enable "`, `#00`, `#00` | port 1: `#DF` = a card is there; then up to 32 bytes of its identification string (zero-terminated), printed after "ISA #n" |
| `#F1` | a 16-bit function number (low, high) | — |
| `#F2` | count n (1 byte), n data bytes | — |
| `#F3` | — | 32 bytes: a status frame, valid when bytes 29 / 30 are `#FA` / `#3D` and byte 31 equals the checksum of bytes 0-30 (`#45DD`); bit 7 of byte 1 goes back in Carry; byte 10 limits the length of the next `#F2` packet |

Functions (`#F1`) in the order the player sends them: `#00`, `#01`, `#02` before the first track
part, `#10` with the track data, `#20` with the instrument headers, `#21` with each sample block,
`#30` to play; `#00`, `#01` after the key. What each one means beyond the strings printed next to
it is not known: that is the card's firmware.

The probe, as the ISA journal of the emulator recorded it in slot 0 (`GET /state/isa/journal`):
`#F0` → port 0, `57 69 6C 64 20 53 6F 75 6E 64 20 45 6E 61 62 6C 65 20` ("Wild Sound Enable ") `00 00`
→ port 1, read port 1 = `#FF` (empty bus) → "Not Found".

## The author's sources

Robus published the firmware and the tools on GitHub:
[RobusAsmCoder/Wild-Sound](https://github.com/RobusAsmCoder/Wild-Sound) (one commit, 2021-05-23; no
license file). Local clone: `~/Downloads/zx-spectrum/sprinter/peripherals/wild-sound/Wild-Sound`.

- **Firmware** (STM32F405, Keil), `STM_AYX32_TSL/STM_AYX32_TSL_WILDSOUND/SRC`. The "commands" above
  are **virtual AY registers**: port 0 is the AY register select, port 1 the AY data (on a Spectrum the
  same code uses `#FFFD` / `#BFFD`). `ChipAY_Interrupt.c` has `LOCKSTRING[] = "Wild Sound Enable "`,
  the `#DF` answer, `#F1` "Stream CMD", `#F2` "Stream DATA", `#F3` "Stream STATUS" (`cmd ×2, size ×4,
  pos ×4, fiOUTfree, fiINsize, 0x3DFA, crcX, 0`), which matches what the player reads.
  The stream functions are in `ChipAY_StreamData.h`: `#00` NOP, `#01` CLEAR, `#02` ERASE, `#10` XMAST,
  `#11` XMADO, `#20` XMBST, `#21` XMBDO, `#30` DIGPLAY: the player's `#00 #01 #02` reset sequence,
  `#10` track, `#20` / `#21` instruments, `#30` play. `ChipAY_DigitalPlayer.c` is the XM player on the
  packed patterns and instruments. Prebuilt HEX files are in `HEX_SEND_FOR FRIENDS`.
- **Tools**, `STM_AYX32_TSL/TMT/MasonXM`: the XM converter (`MasonXM.pas`, `RobXM.pas`), the original
  `PROSIAK.XM` (`Music/`), the Z80 loader VM in ASAM source (`BOOT/loados.inc`, `loader.inc`, LZSA
  depackers) and the linker scripts (`BOOT/LINKER/prosiak.CMD`). The script's commands are the VM ops
  above: `U` load / unpack, `P` page, `E` exec, `G` / `g` goto / gosub, `I` / `i` conditional
  goto / gosub, `T` return, `K` / `k` start / end XOR coding of the output.
- That build targets a Pentagon with TR-DOS (`prosiak.trd`); the Sprinter EXE and the ISA bus side of
  the card are not in the repository.

## Emulating the card

A card model for this program: the virtual registers `#F0-#F3` (lock string, `#DF`, stream command,
data, status frame) on ISA I/O ports 0 / 1, the stream functions (`#10` track, `#20` instrument
headers, `#21` samples, `#30` play), and the XM player of `ChipAY_DigitalPlayer.c` on the uploaded
data. The firmware's AY emulation and the other modes are the rest of the card.

## Reproduce

```bash
python3 tools/unpack_prosiak.py prosiak.exe scratch/prosiak-unpacked   # loader, stage 2, blocks 0-7, script.txt
python3 tools/make_listings.py prosiak.exe                              # rewrites the three .asm files here
```

Both read only the file; `make_listings.py` needs `z80dasm` 1.2.0 on PATH.
