# CDPLAYER.FLX — the ATAPI path

| | |
|---|---|
| **Program** | Flex Navigator plugin `CDPLAYER.FLX`, "OSHAOS CD-Player v1.0 beta1", Alexander Shabarshin, 2002 |
| **File** | `C:\FN\FLX\cdplayer.flx` on the MAME-pack Sprinter system disk `sp_hdd_sys` (12 732 bytes, SHA-1 `144400e4a1d53702c3a18e13c5092f5aeb35bcdd`); the 2002-10-22 copy in Shabarshin's `cdplay.zip` (12 736 bytes) has the same routines and packets, one byte later |
| **Disassembly** | [ide-atapi.asm](ide-atapi.asm): C1BCh-C35Dh, origin BFF0h |
| **Investigated** | 2026-10-02, PLAN #83 follow-up ([cd-audio README §6](../../../../inprogress/2026-10-02-cd-audio/README.md)) |

Its README: "There are only 2 functions now: 1) Eject CD 2) Play CD from first track". It has no
track skip: "does not skip tracks" is the plugin, not the drive.

## Routines

| Address | What |
|---|---|
| C1BCh | wait until BSY (status bit 7) is clear |
| C1C5h | wait until DRQ (bit 3) is set |
| C1CFh | ERR (bit 0) into carry |
| C1D6h | select the slave (`B0h` to the device register) |
| C1EAh / C1F5h | write / read the byte count (#0154 / #0155) |
| C200h | wait BSY, wait DRQ, `INI` x 2048 from port #0050 with B counting down from 0 (A8 toggles: word, latch, word, ...), then swap the bytes of each word |
| C235h | send a packet: copy 12 bytes to C35Eh with the Sprinter accelerator (`ld d,d` / `ld l,l` / `ld b,b`), wait BSY, select the slave, wait BSY and DRDY, `A0h`, wait BSY, **retry from the top while ERR is set**, wait DRQ, `OUTI` x 12 to port #0150 (B decremented before the cycle: #0050 latch, #FF50 word, ...). It returns without reading the status: the packet's result is never looked at |
| C285h | detection: select the slave, fail if BSY; byte count 0; `ECh` IDENTIFY DEVICE (aborted with the ATAPI signature); read the byte count; `A1h` IDENTIFY PACKET DEVICE, read it (C200h); "CD-ROM is present" when word 0's device type is 5 |
| CC16h | **Play CD**: packet C352h |
| CC1Dh | **Eject CD**: packet C322h |

Packet table (C2FEh, 12 bytes each): TEST UNIT READY, REZERO UNIT (01h), START STOP UNIT start
/ eject / load, PAUSE, RESUME, and **PLAY AUDIO MSF `47 00 00 00 02 00 50 00 4A 00 00 00`:
00:02:00 to 80:00:74** - "from the first track to the end of any disc". Only Play and Eject are used.

## What went wrong, and the standard

- Our drive refused that PLAY with LBA OUT OF RANGE (05h / 21h) because the end lies past the
  lead-out. MMC-3 r10g 5.13 checks the **starting** address only ("If the starting address is not
  found ... LOGICAL BLOCK ADDRESS OUT OF RANGE") and plays "all contiguous audio sectors between the
  starting and the ending MSF address"; MAME `t10mmc.cpp` takes such ends too (its comment: BeOS sends
  99:59:71). Fixed in the shared drive: an end past the disc plays to the start track's session
  lead-out. Since the plugin never reads the status, every press failed silently.
- Track 1 must be audio: on a mixed-mode disc (data track 1) 00:02:00 is data and the drive refuses
  it (05h / 64h ILLEGAL MODE FOR THIS TRACK, as MMC-3 says) - the plugin's limit, also on a real drive.
- The plugin sends no TEST UNIT READY / REQUEST SENSE: the first command after a disc change gets the
  UNIT ATTENTION (06h / 28h, SPC) and nothing plays; the second press plays. A disc that is in the
  drive at boot has its unit attention taken by the BIOS's drive detection.
- The IDE handshake through the Sprinter adapter (A8 half latch, `OUTI` / `INI` with B counting down)
  works: the packet arrives intact (`ide_state` `last_packet`).

Regression test: `AtapiCdromAudio_Test.SprinterCdplayerFlxPlaysFromTheFirstTrack` (the plugin's
command sequence). Live check: the plugin's own routines (C1BCh-C35Dh) loaded into a running
Sprinter (BIOS port table), `call C285h` then `call C2FAh` with C352h: first call UNIT ATTENTION,
second call playing track 1 to LBA 4800 (the Enhanced CD test disc's audio lead-out).
