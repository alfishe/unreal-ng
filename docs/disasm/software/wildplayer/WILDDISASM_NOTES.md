# WildPlayer v0.333 pure-body extraction & annotated disassembly

Source: live-memory dumps of the emulator running ts_my.trd (Pentagon),
captured mid-play; see `wildplayer_dump_README.txt` for the raw dump layout.

## The pure body

`wildplayer_body_org5D3B.bin` = main RAM **0x5D3B-0xA8C2** (19336 bytes):

| range | content |
|---|---|
| 0x5D3B-0x5D52 | boot.B BASIC line-256 header (`CLEAR 24499 : RANDOMIZE USR 23891 : REM`) |
| 0x5D53-0x5D58 | loader stub: `DI; XOR A; OUT (#FE),A; JR STAGE2` |
| 0x5D59-0x5DB2 | WP SETUP menu text |
| 0x5DB3-0x5FFF | unpack/bootstrap code + workspace |
| 0x5FE0 | BANK_SWITCH helper (`AND 7 / OR #10 / OUT (#7FFD)`) |
| 0x6000-0x9FFF | the unpacked Wild Player: setup UI, file browser, format detectors (PT3/PT2/PSC/TFM/STP...), TR-DOS disk driver (0x9B04 poll loop), **TurboSound probe/gate/init/switch** |
| 0xA000-0xA8C2 | installed VTII PT3 r.7 engine instance + tables |

Excluded: 0x5B00-0x5D3A (TR-DOS workspace: catalog copy, typed-line buffer),
0xA8C3+ (the loaded *Bad Apple!!* PT3 module - content, not player).

## RAM-bank roles

| bank | role (evidence) |
|---|---|
| 0 | TR-DOS catalog cache + help screens (`WP_Help!`, `TW_MUZA`, `WP333hlp`, `WRD1`, `PlayHelp`, `txt` entries at 0xD802) |
| 1 | **VTII PT3 r.7 engine master** (identical head to the 0xA000 instance) + staged Bad Apple PT3 module at 0xC86E (second copy at 0xF131) |
| 4 | **UniPT2 engine** (`=UniPT2/-layer ` string at 0xCABB) - PT2 format support |
| 5 | data (music/aux buffers, long U/G/D runs) |
| 6 | table-driven data (repeating `ld (hl),.. / inc`-like patterns - screens/tables) |
| 7 | mostly empty + font-like bitmaps at 0xE9xx; was paged at 0xC000 at dump time |

## The TurboSound story (why chip 2 was silent - port-trace forensics)

1. **Probe** (intro overlay at 0x6090, code replaced by dump time):
   `OUT #FFFD,#F8 / OUT #FFFD,#00 / OUT #BFFD,#BF / IN A,(#FFFD); JP P,ts_ok`
   - reads back R0 of the #F8-latched chip; bit7==0 => "TS present"
2. On a classic 2xAY TurboSound the #BF marker survives => readback #BF =>
   **8EBF=0** ("no TS").
3. 8EBF gates every TS setup (`LD A,(8EBF); OR A; JP Z,skip` pattern) =>
   **TS_MODE_FLAG (6B02) stays 0** => DUAL_CHIP_INIT skipped, and the
   interrupt handler's switch block (0x9060/0x907D: #FE/#FF selects +
   CALL engine per chip) never runs.
4. Captured consequence: all 15,394 music writes went to ONE chip (pc
   C5B4/C5B7 = the VTII register-OUT loop inside a paged engine instance);
   chip 2 got exactly the 9 mute writes of MUTE_BOTH (0x8A61).

## Engine instances and install patches

The player copies the VTII master (bank 1) into place and **patches bytes per
instance** (chip-select value / module pointer / play trampoline) - see
`engine_install_patches.txt` (instance @0xA000 vs master @bank1) and the
A000-diff annotations in `wildplayer_body.asm`.

## Files

- `wildplayer_body.asm` - annotated disassembly of the pure body (READ THIS)
- `wildplayer_bank1_vtii.asm` - VTII engine master + module staging
- `wildplayer_bank4_unipt2.asm` - UniPT2 engine
- `engine_install_patches.txt` - per-instance install patches
- `wildplayer_body_org5D3B.bin` - the extracted pure body binary
