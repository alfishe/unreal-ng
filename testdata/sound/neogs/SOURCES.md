# NeoGS test fixtures

Fixtures for the NeoGS card (`docs/inprogress/2026-09-19-general-sound/neogs-tdd.md` §8).

## `mp3/` — decoder test streams

All three come from one recording: `testdata/loaders/sna/eyeache1.sna` (the EyeAche demo, AY music) on a Pentagon
128K, captured through the WebAPI audio capture (`/audio/capture`, 30 s, 44,100 Hz stereo, 16-bit) on 2026-09-27.
The music is copyrighted by the demo's authors and used here as test material only (see `testdata/NOTICE.md`).

| File | Format | Covers |
|---|---|---|
| `eyeache1-44k-128k-cbr.mp3` | MPEG-1 Layer III, 44.1 kHz stereo, 128 kbit/s CBR, 30 s. No ID3 tag, no Xing/LAME header (`lame -b 128 --cbr -t --noreplaygain`) | The main playback stream: frame parsing, DREQ pacing, DECODE_TIME, AUDATA/HDAT on a plain stream |
| `eyeache1-22k-mono-vbr-id3.mp3` | MPEG-2 Layer III, 22.05 kHz mono, VBR (`lame -V 4`), first 8 s. Starts with an ID3v2.3 tag, first frame is a Xing/LAME info frame | The frame parser skipping ID3v2 and handling the info frame; MPEG-2 (LSF) headers; mono |
| `eyeache1-44k-layer2.mp2` | MPEG-1 Layer II, 44.1 kHz stereo, 192 kbit/s, first 8 s (ffmpeg `mp2`, bit-exact flags) | Layer II decoding, which the MA8201 supports |

Encoders: LAME 4.0 (64-bit), ffmpeg 9.0.2 (Homebrew). The files are committed, not rebuilt, because encoder versions
change their output.

## SD card images — built at run time, not committed

The tests build their SD cards in `scratch/` when they run, and remove them afterwards:
`core/tests/_helpers/fatimagebuilder.h` (a deterministic FAT16/FAT32 builder, sparse files) and
`core/tests/_helpers/neogstestsdcard.h` (the three NeoGS cards). Each card holds, in its root directory:

- `NEOGS.ROM` — the NeoGS main ROM v1.11 (`tools/neogs/parts/neogs.rom`, 32 KB, identical to `full_ngs.rom` at
  `#10000`), for the loader's SD boot path;
- `NGS_ROM.UPD` — the NedoPC firmware update file (`tools/neogs/parts/ngs_rom.upd`), for the flasher;
- `EYEACHE.MP3` — `mp3/eyeache1-44k-128k-cbr.mp3`, for Neo Player Light and the DMA player;
- `EYE22K.MP3` — `mp3/eyeache1-22k-mono-vbr-id3.mp3`: a second MP3, because Neo Player Light v0.44 hangs on a
  card with exactly one (its FINDMP3 returns with the wrong memory page when it found fewer than two files; see
  `docs/inprogress/2026-09-19-general-sound/neogs-tdd.md` §14.2).

| Card | Size | Layout | Covers |
|---|---|---|---|
| `Fat16Mbr` | 8 MB | MBR, one partition at LBA 2048, type `#06` (strictly `#04` below 32 MB, but real cards carry `#06` and Neo Player Light accepts only 1/6/B/C/E); FAT16, 2 sectors per cluster | SD boot through a partition table, FAT16 |
| `Fat16NoMbr` | 8 MB | No partition table (FAT16 boot sector at LBA 0) | The loader's "bare boot sector" path, FAT type from the cluster count |
| `Fat32Mbr` | 36 MB | MBR, type `#0C`; FAT32 with 70,528 clusters (FAT32 needs at least 65,525, so it cannot be smaller) | SD boot on FAT32 |

The images are smaller than 2 GB, so the card model presents them as SDSC by default. SDHC is tested with the same
images through the `[NGS] SDType=sdhc` override (neogs-tdd.md §5.5).

The C++ builder was checked byte for byte against `tools/neogs/make_sd_image.py` (same layout), whose output was
checked with mtools (`mdir`, `mcopy`) and `fsck_msdos -n` (clean). `make_sd_image.py` stays for making cards by hand.

## `programs/` — NedoPC host programs (acceptance tests)

From the NedoPC `ngs` sources (`zx/` and `z80/flasher/`), treated as MIT like the rest of the NeoGS material. Used
by `core/tests/emulator/sound/chips/neogs/soundchip_neogs_acceptance_test.cpp`.

| File | Source | Program |
|---|---|---|
| `test_ngs.scl` | `zx/test_ngs/testngs.scl` | The authors' card test: detect, version, COM23 page count, write/read of every page, clock switching, SD, MP3 chip |
| `test_emu_ngs.scl` | `zx/test_emu_ngs/testngs.scl` | Test written for emulators: mailbox echo, 256 SD sector reads through its own card driver |
| `flasher.scl` | `z80/flasher/flasher.scl` | The flash updater (reads `NGS_ROM.UPD` from the SD card) |
| `neo_player_light.scl` | `zx/Neo_Player_Light/npl.scl` | Neo Player Light v0.60: MP3 player over SPI (no DMA) |
| `npl044.scl`, `npl044_dma.scl` | `zx/npl_044/npl044.scl`, `zx/npl_044_dma/npl044.scl` | Neo Player Light v0.44 without and with DMA. v0.44 needs two MP3 files on the card; the DMA build cannot play at all (CMD17 commented out before its SD DMA) - both are bugs of the player, neogs-tdd.md §14.2 |
| `altstd.fnt` | `zx/test_ngs/altstd.fnt` | The programs' 8x8 font (6x8 glyphs in the top six bits), used by the tests to read the screen |

The four SCL files marked below were published without the 4-byte SCL checksum; the copies here have it appended
(the data itself is unchanged): `test_emu_ngs.scl`, `neo_player_light.scl`, `npl044.scl`, `npl044_dma.scl`.
