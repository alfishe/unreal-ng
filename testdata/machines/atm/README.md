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
