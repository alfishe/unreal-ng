# ATM Turbo 2+ test files

## `nedoos-cdplay.scl`: NedoOS for ATM2 with its audio CD player

A floppy (SCL, 7 files, 50 KB) for the CD audio test on the ATM Turbo 2+
(`ATM710NedoOsCdplay_Test.PlaysPausesAndStopsATrack`, PLAN #83). TR-DOS runs
`boot`, which loads the NedoOS kernel; the kernel runs `autoexec.bat`, which
starts `cdplay.com`; the player reaches the CD drive on the IDE slave through
the ATM board's ports (`#FEEF` ... `#FF0F`).

| File | Origin |
|------|--------|
| `boot` (B), `code` (C), `reset.com`, `term.com`, `cmd.com` | `release/osatm2.trd` (the NedoOS ATM2 floppy kernel), byte for byte with their TR-DOS headers |
| `autoexec.bat` | ours: `cdplay` (the release's starts `wizcfg` and `nv`) |
| `cdplay.com` | `release/bin/cdplay.com` ("Audio CD Player"; source `src/kapps/cdplay/main.c`) |

Source: https://github.com/alfishe/NedoOS, revision `44049473`, folder `release/`.
The release floppy itself has no room for another file, so this one carries
only what the player needs.

## `ballq_gr.scl`: Ball Quest (Alone Coder, Shiru; 2006-2007)

A floppy (SCL, 32 files, 107 KB) with the game's two ZX builds: `BQ   ATM` for the ATM Turbo 2(+) (EGA 320x200,
`#FF77` mode 0) and `BQ   16C` for the Pentagon 1024SL (16 colours), plus the levels, pictures, music and the
readmes (`readme`, `readmezx`). Several BASIC programs and no `boot`: the autostart shows the commander.

`DiskAutostart_Atm710.BallQuestShowsAllFourEgaPlanes` runs `BQ   ATM` on the ATM710: a 128K program that pages with
`#7FFD = #1n`, which the TR-DOS cold start would send to RAM 24-31
([docs/inprogress/2026-10-11-atm-autostart-128k-paging](../../../docs/inprogress/2026-10-11-atm-autostart-128k-paging/README.md)).
The game does not support the ATM450 (no `#FF77` there).

Source: the owner's copy (file name lowered).
