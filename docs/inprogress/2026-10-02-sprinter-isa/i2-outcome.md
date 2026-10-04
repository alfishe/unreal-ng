# ISA phase I2 outcome: the ZX-bus adapter with the General Sound / NeoGS, ProPlay against MAME

| | |
|---|---|
| **Date** | 2026-10-04 |
| **Branch** | `sprinter-isa-i2-neogs` (from master `2bab405c5`) |
| **Design** | [tdd.md](tdd.md) §6-7, as built in §14 (I2) |
| **Test MOD** | [tools/machines/sprinter/test-mod/](../../../tools/machines/sprinter/test-mod/README.md) (open question Q10) |

## 1. What was built

ISA slot 1 holds the ZX-bus adapter by default (owner decision Q2), and the General Sound of `[SOUND] GSType` sits
behind it (the NeoGS in the Sprinter config). A program reaches the card the way ProPlay does: `#1FFD` <- `#11`,
`OUT (#E2),#D4`, `#9FBD` <- `#00`, then `LD (#C0BB),A` / `LD A,(#C0BB)` (command / status) and `#C0B3` (data).

Worked example of one access: `LD A,(#C0BB)` -> `SprinterMemory` sees window 3 on page `#D4` -> `SprinterIsaBus`
I/O read, slot 1, ISA address `#000BB` -> the adapter (`IsaZxBusAdapter`) turns it into the Spectrum `IN` from
port `#BB` -> `PortDecoder::PeripheralPortIn(#BB)` -> the GS card catches up to this moment and answers its status.

## 2. ProPlay on the system disk

The test MOD (one 64-byte sine sample; C-3, E-3, G-3 for 16 rows each, then 16 silent rows; speed 6, 125 BPM)
replaces `DOCS\DISP.TXT` on a session copy of the MAME pack's `sp_hdd_sys` (same size: the tail is zeros), and
`SYSTEM.BAT` loses its last line `fn` so DSS stops at its prompt. Then, at the prompt: `proplay.exe \docs\disp.txt`.
Both emulators print the same screen:

```text
ProPlay - General Sound MOD player v0.5.91 (07.02.23)
by Miroshnichenko Aleksandr aka Sayman@SprinterTeam
General Sound found at slot: 0
Done.
C:\BIN>
```

(MAME's natural keyboard drops the `:` of `c:\docs\disp.txt`: type the path without the drive.)

## 3. The comparison with MAME

MAME 0.289 (`f43983b6`, the `zxsp` subset build), BIOS 3.06, the same disk as a CHD (`chdman createhd -chs
4096,16,32`), `-isa0 zxbus_adapter -isa0:zxbus_adapter:card neogs`, no NeoGS SD card, `-wavwrite` at 48 kHz
(`mame-zxsteps.sh`, `SPC_WAV`). MAME writes 8 channels; the NeoGS is channels 6 / 7. unreal-ng: the env-gated test
`SprinterProPlay_Test` with `UNREAL_SPRINTER_PROPLAY_WAV` (the GS row, mono, 44.1 kHz).

Compared with `compare-proplay.py` (second pass of the pattern, both aligned on its C-3):

| | MAME NeoGS v1.10 fix2 | unreal-ng NeoGS v1.11 | unreal-ng NeoGS v1.10 fix2 (MAME's flash, Q4) | unreal-ng classic GS (gs105a) |
|---|---|---|---|---|
| C-3 (expected 258.97 Hz) | 258.53 Hz (-3.0 cents) | 258.53 Hz | 258.53 Hz | 258.53 Hz |
| E-3 (expected 326.00 Hz) | 326.09 Hz | 326.09 Hz | 326.09 Hz | 326.09 Hz |
| G-3 (expected 387.55 Hz) | 387.60 Hz | 387.60 Hz | 387.60 Hz | 387.60 Hz |
| pitch vs MAME | - | 0.00 cents (all three) | 0.00 cents | 0.00 cents |
| note starts vs MAME | - | equal (20 ms resolution) | equal | equal |
| C -> E, E -> G, G -> silence, silence -> C | 1.94, 1.92, 1.96, 1.86 s | the same | the same | 1.96, 1.92, 1.94, 1.88 s |
| envelope correlation (20 ms RMS) | - | 0.9994 | 0.9994 | 0.9966 |
| waveform correlation (2 s of C-3) | - | **0.9998** | **0.9998** | 0.969 (another card) |

- The pitch follows the PAL clock (`3 546 895 / period / 64`); the -3 cents of C-3 is the firmwares' own period
  table, the same on both emulators and both cards.
- The pattern is 7.68 s on every run (64 rows of 120 ms). The 1.96 / 1.86 s split between the last note and the
  silent rows is the silence detector seeing a note's end late, on MAME as much as on unreal-ng.
- Our NeoGS v1.11 and MAME's v1.10 fix2 produce the same waveform for this MOD; the firmware choice (Q4) does not
  change the result.
- The NeoGS output is on the left only (channel 0 of the MOD = DAC channel 0, `StereoMode=separated`), as on MAME
  (its right channel holds a constant level).

Commands (paths outside the repo: the owner's MAME pack):

```bash
cd tools/machines/sprinter/mame-capture
MAME_BIN=<zxsp> MAME_ROMPATH=<pack>/roms SPC_HARD1=<sys.chd> SPC_WAV=<out>/mame-proplay.wav \
  ZXK_STEPS="5|kbdonly|ms_naturl;1100|keys|proplay.exe \\docs\\disp.txt{ENTER};2400|end|" \
  ./mame-zxsteps.sh proplay 120 -isa0 zxbus_adapter -isa0:zxbus_adapter:card neogs
UNREAL_SPRINTER_HDD=<sp_hdd_sys.img> UNREAL_SPRINTER_PROPLAY_WAV=<out> \
  [UNREAL_SPRINTER_NGS_FLASH=<pack>/roms/sprinter/neogs110_fix2.rom] \
  bin/core-tests --gtest_filter='*SprinterProPlay_Test*'
python3 tools/machines/sprinter/test-mod/compare-proplay.py <out>/mame-proplay.wav:6,7@32.5 <out>/proplay-neogs.wav@6.5
```

## 4. TTD

- **Port journals with the GS present.** They were off on the Sprinter since the NeoGS "ZX-DMA serves host memory
  reads without IN". The NeoGS ZX-DMA needs the host's memory cycles (`/MREQ`, `/CSROM`); the adapter passes I/O
  cycles only, so the module never sees a host access. Built as a property of the bus, not of the Sprinter:
  `PortDecoder::ZxBusMemoryCycles()` (default: `ZxBusPresent()`; the Sprinter: false). The NeoGS asks its ZX-bus
  (`NeoGSZxDma::Host::zxHostMemoryBus`) before it installs its overlay and re-checks once SoundManager attaches it
  (`GeneralSoundCard::onHostBusChanged`); `TimeTravelManager` turns the journals off for the NeoGS only where that
  bus carries memory cycles. Every host access to the card is then an ISA cycle of the machine's own state.
- **ProPlay recorded and replayed** (`SprinterProPlay_Test`, recording started before the command is typed, 700
  frames of playback): with the classic GS the replay ends with the GS blob (id 5, its RAM included) and the ISA blob
  (id 33) equal byte for byte. The sound of the replay plays the same notes at the same moments and correlates
  > 0.995 with the recording; it is not compared sample by sample, because the mixer's sample-count accumulator
  (903 / 904 samples a frame) and the GS output stage are host-side rendering state that the checkpoints do not
  carry (the replayed stream sits a fraction of a sample apart).
- **NeoGS: not bit-exact.** The NeoGS blob (id 12) leaves the card's 2 MB RAM and the flash out until the TTD v2
  memory regions (neogs-tdd §7.4); a restore keeps the live card RAM (here with the module and the firmware's
  variables of the recording's end), so the firmware runs a few T-states differently: 9 bytes of the blob differ
  after the replay (the card clock, the frame base, CPU registers). What it plays is the same music (asserted).
  Open item: the NeoGS RAM as a TTD v2 memory region.
- **Fixture.** `testdata/machines/sprinter/ttd/boot.ttd` re-recorded with the classic GS card (`"gs": "z80"` in
  `record_fixtures.py`, as the Pentagon corpus): the default population now builds a GS.

## 5. Findings on the way

- **The BIOS pulses ISA RESET DRV at POST** (BIOS 3.07 BETA 1: `#9FBD` <- `#FF` at PC `#0399`, `#00` at `#03AB`,
  frame 28 of a fast start). A machine reset does not reach the card by itself (the latch has no reset input, Q9),
  but the BIOS's own pulse resets the GS behind the adapter on every boot.
- The Sprinter config named `rom\bootgs.rom` (a NeoGS loader) as the classic card's firmware; a classic GS cannot
  run it (it waits for the NeoGS SD card at ports `#13` / `#14`). Now `rom/gs105a.rom`, as on every other model.
- The firmware's answer to GS command `#20` is the RAM left to modules: gs105a 112 KB of 128 KB, NeoGS v1.11
  2000 KB of 2048 KB (`SprinterGeneralSound_Test`).
- Side notes, not changed: the GS output stage resumes relative to its level after turbo or a TTD restore (a DC
  offset until the mixer's DC rejection removes it); the TTD analyzer does not know peripheral id 45
  (`EthernetNics`), so `validate` reports every checkpoint of the Sprinter fixture (also before this change).
