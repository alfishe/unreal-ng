# NedoOS General Sound / NeoGS software: host protocols and uploaded card code

Input for the GS debugger firmware profiles ([design.md](design.md) §8A.9-§8A.10, requirements §4.15-§4.16:
M1-M8, the F4 anomaly kinds, scenarios V-1..V-6). Read-only research over the NedoOS sources at HEAD `44049473`
(2026-09-27).

**NedoOS path prefix.** Every citation written as `N/<path>:<line>` means
`/Volumes/TB4-4Tb/Projects/emulators/github/NedoOS/src/<path>:<line>`. Release binaries are cited as
`N/../release/<path>`.

**Stock firmware prefix.** `FW/<file>` means
[`../2026-09-19-general-sound/materials/gs/gs-firmware/firmware/src/<file>`](../2026-09-19-general-sound/materials/gs/gs-firmware/firmware/src/).

**Notation.**

| Mark | Meaning |
|------|---------|
| WC | the host polls `#BB` until bit0 (command flag C) is clear |
| WD | the host polls `#BB` until bit7 (data flag D) is clear (the card took the byte) |
| WN | the host polls `#BB` until bit7 is set (the card wrote a reply) |
| no wait | the host reads or writes the port without polling |
| pre-param | data byte written to `#B3` before the command byte to `#BB` |
| strobe | a write to `#BB` used as a handshake, not as a command (the card only waits for C) |

Hardware facts used below: host `#BB` write = command (sets C), read = status (bit0 C, bit7 D); host `#B3` = data
(host write sets D, host read clears D); host `#33` = GSCTR (`#80` = card reset). Card side: a write to OUTRG/ZXDATWR
sets D, a read of DATRG/ZXDATRD clears D, a write to RSCOM/CLRCBIT clears C. D is one flip-flop for both directions,
so "the card wrote a reply" and "the host wrote a byte" are indistinguishable in the status bit.

Lines in files with CR-only line endings (`N/gp/ngsdec/gsports.asm`) are counted by CR. Lines in
`N/kapps/ngsplay/gs/*.a80` are the real file lines (CRLF). Comments in `ngsbios.a80` and `page2.a80` are CP866
text that is no longer decodable (bytes were lost in a UTF-8 round trip); command meanings below are taken from the code
and from the C host comments in `ngs_host.c`.

---

## 1. Stock GS firmware reference (what the NedoOS hosts talk to)

The stock ROM (gs105a/b, and the same low ROM bytes in `neogs.rom`) is the target of V-1, V-2 (GS .mod), V-6 and the
kernel upload part of V-5.

### 1.1 Dispatcher

| Item | Value | Source |
|------|-------|--------|
| Main loop | `COMINT` (sets SP = ISTACK), `COMINT_` polls FLAGS | FW/COM_L.a80:2-3 |
| Fetch | `COMINT1 IN A,(COMRG)` | FW/COM_L.a80:24 |
| Low range | `< #20` -> `COMLOW`: `ADD A,A`, index into `COMTAB` (word table, `DEFW COM00..`) | FW/COM_L.a80:31, 72 |
| `#F0-#FF` | `COMINT2 CP #F0`; `SUB #D0` gives index `#20-#2F` in the same `COMTAB` | FW/COM_L.a80:27-30 |
| `#20-#EF` | `COMHIGH`: pushes `COMINT_` as return address, split low/high byte table `COMTABH` | FW/COM_L.a80:41-50, FW/TABLES_H.a80:10 |
| Unknown / unused | `COMZ OUT (RSCOM),A` then back to the loop | FW/COM_L.a80:80 |
| `#1E` | `COM1E EQU COMZ` (unused in stock; NeoGS SD driver patches this slot) | FW/COM_L.a80:560 |
| Data wait | `WTDTL` waits for `FLAGS & #81`; if only C is set it jumps to `COMINT1`: a new command aborts a parameter wait | FW/COM_L.a80:54-60 |
| Second wait | `WTDTG` (same, used by high commands) | FW/COM_L.a80:61 |
| Known addresses (gs105a) | COMINT `#026B`, COMINT_ `#026E`, WTDTL `#02BC`, COMTAB `#0300`, COMZ `#0360`, COM13 `#0537`, COM14 `#0545`, LOAD `#0C09`, COM30 ROM1:`#C3DF`, COMTABH ROM1:`#D720` | gs105a.map (previous research step) |

`COMTAB[#1E]` = `#0360` (COMZ) in gs105a, gs105b and `neogs.rom` (checked byte-wise in the previous step).
The kernel's `COMINT_ = #026E` (`N/kernel/ngssddrv.asm:7`, `N/kernel/ngsinst.asm:6`) relies on that address.

### 1.2 Commands used by NedoOS software

| Cmd | Name (comment) | Params / reply | Handshake | Source |
|-----|----------------|----------------|-----------|--------|
| `#00` | reset flags | reads DATRG, RSCOM | barrier: both flags cleared | FW/COM_L.a80:84-86 |
| `#13` | jump | pre-param = addr low (read before RSCOM), addr high via WTDTL, `JP (HL)` | host WC then one data byte; the jump happens without a reply | FW/COM_L.a80:372-379 |
| `#14` | upload | pre-param = length low (read, then RSCOM); length high, addr low, addr high via WTDTL; bytes loop on `FLAGS & #81`, a command aborts to `COMINT1` | host WC or WD after the command both work (pre-param is consumed before C clears) | FW/COM_L.a80:384-427 |
| `#20` | get RAM free | 3-byte reply; first byte written before RSCOM, each next after the host read (HSEND waits for D clear) | host reads 3 bytes; a WN is optional for the first | FW/COM_H.a80:9, 62-80 |
| `#23` | get page count | `OUT OUTRG NUMPG` before RSCOM; does not read DATRG | 1 reply | FW/COM_H.a80:114-117 |
| `#30` | open module stream | `OUT OUTRG handle`, then `IN DATRG` (the card clears its own reply: flagless), RSCOM, `CALL LOAD`, then PLAYMD | WC only; a WN for the handle hangs | FW/COM_H.a80:231-245 |
| `#D1` | (in LOAD) | treated as an ignored command: RSCOM and continue | WC | FW/LOAD_L.a80:48-70 |
| `#D2` | close stream | LOAD3: end of data | WC | FW/LOAD_L.a80:99 |
| `#F3`, `#F4` inside LOAD | abort | accepted as aborts of the stream | - | FW/LOAD_L.a80:48-70 |
| `#31` | play module | pre-param = module (0 = current), reply module number via `OUT OUTRG` before RSCOM | WC; long C hold on large modules | FW/COM_H.a80:253-319 |
| `#32` | stop module | reply module number, then `IN DATRG` (flagless) | WC or none | FW/COM_H.a80:321-327 |
| `#33` | continue module | same pattern as `#32` | WC or none | FW/COM_H.a80:329-345 |
| `#60` | get song position | `OUT OUTRG MTSNGPS` before RSCOM | WC then read (WN optional) | FW/COM_H.a80:841-844 |
| `#F3` | warm reset | RSCOM, `JP INITVAR` | WC | FW/COM_L.a80:575 |
| `#F4` | cold reset | RSCOM, `JP #0000` | WC; host then polls the page count value | FW/COM_L.a80:578 |
| `#FA` | test mode | RSCOM, enter `TCOM` | - | FW/COM_L.a80:614-618 |

Boot handshake: INIT writes `OUT OUTRG,0` early and the page count after the RAM test (FW/INIT_L.a80:1-5,
206-207). D is set both times, so a host cannot tell the two bytes apart by flag; V-1 polls the value instead (§2).

Test mode: `TCOM` polls FLAGS, reads COMRG (FW/TEST_H.a80:2-5), dispatches `#02-#15` through `TCOMTB`
(FW/TABLES_H.a80:91); `#20` and above leave test mode via `COMINT2`. `TCOMB` (FW/TEST_H.a80:91-131) is the beep
on channels 1-4: a 64-step volume sweep with inner loops, about 5 s per command at 12 MHz (inferred from the loop
counts). The command flag is checked only between sweeps, so back-to-back test commands are each held about 5 s.

---

## 2. V-1 gstest

### 2.1 Program

| Item | Value | Source |
|------|-------|--------|
| Source | `N/kapps/gstest/main.c`, module `N/kapps/gstest/booter.c` | |
| Ports | `datareg 179` (`#B3`), `cmdreg 187` (`#BB`) | N/kapps/gstest/main.c:7-8 |
| Docs | `N/../release/doc/gstest.txt` (CP866) | |
| Release | `N/../release/bin/gstest.com`, 33,915 B, sha256 `48d2ca3e3bd768c2b7a9230a45f0e18ee98be1be0a5f529bd60c69700b2acacd` | |
| Testdata copy | [`testdata/machines/p2666/software`](../../../testdata/machines/p2666/software/README.md) `gstest.com`, 33,860 B, sha256 `3189aeda0f5b3bbae28219846e49cebfa46be2a0a60270ef1003369b3bdeec07` (NedoOS `d1d011f8`, older) | |
| Embedded module | `rawData[27862]`, name "booter", `M.K.` at 1080 (ProTracker), sha256 of the 27,862 bytes `79de75eb47d0876a064c779319f8c7865496031564c5e86cca5a4adc0cf5865b` | N/kapps/gstest/booter.c:1 |
| Uploaded card code | none; stock firmware only | |

Helpers: `getDat` reads `#B3` with no poll (main.c:19-25); `sendDat` waits for D clear, then writes (27-36);
`sendDatnv` writes with no wait (38-42); `sendCmd` writes `#BB` and waits for C clear (44-53).

### 2.2 Host sequence

| # | Host action | Wait | Card expectation | Source |
|---|-------------|------|------------------|--------|
| 1 | `getDat()` stale drain | no wait | clears a stale D | main.c:96 |
| 2 | read status `& 129`, print | - | | main.c:105-123 |
| 3 | `#F4` cold reset | WC | `JP #0000` | main.c:126 |
| 4 | loop: read `#B3` until non-zero | value poll, no flag poll | INIT writes 0, then NUMPG | main.c:129-133 |
| 5 | `#20`, then 3 x `getDat` | WC; reads without WN | 3-byte reply via HSEND | main.c:63-67 |
| 6 | `#FA`, then `11`, `12`, `13`, `14` | WC each | test mode beeps channels 1-4 (each ~5 s, C held) | main.c:138-146 |
| 7 | `#30`, then `getDat` handle | WC; read with no WN | flagless handle (card already cleared D) | main.c:150-151 |
| 8 | `#D1` | WC | ignored inside LOAD | main.c:152 |
| 9 | 27,862 x `sendDat` | WD per byte | LOAD loop | main.c:153-156 |
| 10 | `#D2` | WC | LOAD3 | main.c:157 |
| 11 | `sendDatnv(handle)`, `#31` | no wait, then WC | play | main.c:160-161 |
| 12 | wait for key, `#F3` | WC | warm reset | main.c:164-167 |

Step 6: `11..14` are `#0B-#0E`, inside `#02-#15` (FW/TEST_H.a80:2-18). Step 7 reads
the handle without waiting for D; the stock `#30` clears D itself (`IN DATRG` after `OUT OUTRG`), so a WN here would
hang. gstest reads the value the card latched in OUTRG, which stays readable after D is cleared (inferred from the
host doing exactly that and printing a sane handle).

### 2.3 Quirks against F4

- Value poll instead of a flag poll for the boot page count (step 4): a correct firmware profile must not flag the host
  reading `#B3` with D clear as an anomaly during the boot window.
- Flagless replies (`#30`) read without WN (step 7).
- `#D1` is not a stock command; the stock LOAD ignores it (RSCOM only).
- Test-mode commands hold C for seconds; the host WC loops through them (expected long C, not a stall).
- `sendDatnv` before `#31`: the pre-param can be written while D is still set by the last stream byte only if the card
  has not consumed it; in practice the `#D2` WC makes it safe (inferred).

### 2.4 Headless run

Any machine with a GS or NeoGS and NedoOS. `gstest.com` is on the P2666 testdata card, but P2666 has no GS (§8). Run
it on ZX-Evo (zxevo-ers ships a NeoGS) or on a Pentagon with NedoOS and the shipped `zxbus.1 = neogs`. Command line:
`gstest` (no args). Media: none (embedded module).

---

## 3. V-2 / V-3 GP (gp.com + gp.plr): GS .mod and NeoGS MP3

### 3.1 Program

| Item | Value | Source |
|------|-------|--------|
| Player | `N/gp/mp3.asm`: "Player for NeoGS decoder chip (also GS/NeoGS .mod player)" | N/gp/mp3.asm:1 |
| Port macros | `N/gp/ngsdec/gsports.asm` (CR endings) | |
| Card code | `N/gp/ngsdec/gscode.asm`, `gscodedefs.asm` | |
| Release | `N/../release/bin/gp.com` 9,429 B, sha256 `2e2682fd7cde58db249139ec866d6ca3419612373add1fb964d6b6a8bc15a9d3`; `N/../release/bin/gp/gp.plr` 84,687 B, sha256 `1bc62a722ffbc272c693f12efc662e29849b64130cb3fbf3eaf41038e058f159`. P2666 testdata copies are identical. | |
| Config | `gp.ini` embedded (N/gp/startup.asm:941), optionally read from `gp/gp.ini` (N/gp/startup.asm:490); release has `ModDevice=0` | |
| Command line | `gp [folder\|file]`: `processcommandline` sets the folder and finds the file, then `startplaying` | N/gp/main.asm:174-199 |
| Main loop | `playloop`: `musicplay` when playing, then `OS_GETKEY` and message tables | N/gp/main.asm:135-153 |

Macros (N/gp/ngsdec/gsports.asm): `SC` = `OUT (GSCOM)` (184-187); `WC` waits C clear (190-197); `WD` waits D clear
(207-214); `GD` = `IN (GSDAT)` (217-219); `WN` waits D set (223-230). `YIELD` gives up the rest of the 50 Hz
quantum (N/_sdk/sys_h.asm:161-170).

### 3.2 Init and detection

| Step | Host action | Wait | Source |
|------|-------------|------|--------|
| gshardreset | `GSCTR #80`, 3 x YIELD, `#F3`, up to 50 x (YIELD, poll C) | C poll | N/gp/mp3.asm:651-670 |
| gsstartcode | data = length low, `#14`, WC, length high WD, addr low WD, addr high WD, bytes `OUTI` + WD; then data = addr low, `#13`, WC, addr high WD, 3 x YIELD | WC after `#14` | N/gp/mp3.asm:672-705 |
| chip id | `#02`, WC, WN, GD -> vsversion | | N/gp/mp3.asm:169-173 |
| reset code | `#00`, WC (gscodereset) | | N/gp/mp3.asm:736-739 |
| GS-only mode | vsversion >= VS1103+1 (meaning: no decoder answer) selects GS-only (.mod) | | N/gp/mp3.asm:178-206 |
| .mod device | `ModDevice` `'0'` or `'2'` enables the GS .mod player | | N/gp/mp3.asm:115-125 |
| MoonSound .mod | needs `moonsoundstatus` 2 | | N/gp/moonmod.asm:45-48 |

### 3.3 GS .mod (V-2) host sequence (stock firmware)

| Step | Host action | Wait | Source |
|------|-------------|------|--------|
| open | `#30` | WC; the handle reply is never read | N/gp/mp3.asm:328-329 |
| start | `#D1` | WC | N/gp/mp3.asm:331-332 |
| data | bytes `OUT` + WD | WD per byte | N/gp/mp3.asm:345-352 |
| close | `#D2` | WC | N/gp/mp3.asm:367-368 |
| play | data = 1, `#31` | WC | N/gp/mp3.asm:370-373 |
| cancel | `#F4` cold reset, comment "cold reset because warm reset hangs randomly if stream is not closed" | WC | N/gp/mp3.asm:389-391 |
| poll | each pass: YIELD, `#60`, WC, WN, GD; end of song = position decreased | | N/gp/mp3.asm:590-606 |
| unload | `#F3` | WC | N/gp/mp3.asm:402-420 |

The `#30` handle is left in OUTRG; the stock card clears D itself, so nothing stays pending (FW/COM_H.a80:231-245).

### 3.4 NeoGS MP3 (V-3) host sequence (GP card code)

| Step | Host action | Wait | Source |
|------|-------------|------|--------|
| load | gshardreset, gsstartcode (gscode.bin to `#4000`, jump `#4000`) | | N/gp/mp3.asm:211-267 |
| pump | `#01`, WC, WN, GD = free 256-byte pages; if >= 6: bursts of 512 bytes `OUT` + WD; poll once per 512 bytes | | N/gp/mp3.asm:451-467 |
| info | `#06`, WC, WN, GD = stream info; YIELD | | N/gp/mp3.asm:422-480 |
| tail | 150 padding frames of 417 B after the file | | N/gp/mp3.asm:436-440, 741-755 |
| unload | MP3: `#00` (gscodereset); MIDI: `GSCTR #80` | | N/gp/mp3.asm:402-420 |

### 3.5 Uploaded blob: gscode.bin

| Item | Value | Source |
|------|-------|--------|
| Size, hash | 990 B, sha256 `9770df659e6981a824e13e8dd17618a4c9b3e1880675da2c184460291724fc8a` | |
| Location | embedded in `gp.plr` at offset 43098; identical copy in `ngsplay.com` at offset 39695 and in `N/kapps/ngsplay/gscode_bin.c` | |
| Upload / entry | `#14` to `GSPROGSTART = #4000`, `#13` to `#4000` | N/gp/ngsdec/gscodedefs.asm:2 |
| Stack | `GSSTACKADDR = #5D00` | gscodedefs.asm:3 |
| IM2 | vector `#5EFF`, `I = #5E` | gscodedefs.asm:4, N/gp/ngsdec/gscode.asm:64-101 |
| Clocks | 10 MHz (77-78), decoder reset, 12 MHz (87-88); `.cmdreset` sets 20 MHz before `JP 0` (419-422) | gscode.asm |
| NeoGS check | `checkifngs`: if GSCFG0 reads `#FF` (no NeoGS), loop answering commands forever | gscode.asm:377-385 |
| Ring buffer | MPAG page 2 mapped at `#8000-#FFFF` | gscode.asm:100-101 |

Code map (computed by assembling offsets in the previous step):

| Label | Address |
|-------|---------|
| begin | `#4000` |
| startnewstream | `#4046` |
| preload `ld de,#8000` | `#4050` |
| `ld hl,#8000` / `ei` | `#406D` |
| mainloop | `#4071` |
| interrupthandler | `#40A0` |
| checkifngs | `#4336` |
| processcommand | `#4355` |
| `.commandtable` (SMC jr offset) | `#4362` |
| cmdgetstreaminfo | `#43C1` (operand `#43C2`) |
| noper | `#43CB` |
| end | `#43DE` |

Dispatcher `processcommand` (gscode.asm:399-462):

| Step | Detail |
|------|--------|
| Fetch | `IN ZXCMD`; `CP CMDCOUNT (7)`; `JR NC,.cmdreset` **without** `OUT CLRCBIT` |
| Table | `ld (.commandtable),a`: SMC writes the jr offset at `.commandtable=$+1` (406), then `OUT CLRCBIT` |
| Reply | `ATOZX` (26-31): `OUT ZXDATWR`, then waits until the host reads it (D clear), with interrupts off |
| Caller | `mainloop` (117-132): if C set, DI, processcommand, EI; `LOAD256` also accepts commands inline (39-61) |

| Cmd | Name | Reply | Source |
|-----|------|-------|--------|
| 0 | reset (`.cmdreset`, 20 MHz, `JP 0`) | none | gscodedefs.asm:7, gscode.asm:419-422 |
| 1 | get free buffer space | `(H-D) & #7F` pages (CALCFREEBUFFERSPACE) | gscodedefs.asm:8, gscode.asm:33-37 |
| 2 | get chip id | vsversion | gscodedefs.asm:9 |
| 3 | restart stream | none | gscodedefs.asm:10 |
| 4 | volume up | none | gscodedefs.asm:11 |
| 5 | volume down | none | gscodedefs.asm:12 |
| 6 | get stream info | streaminfobyte | gscodedefs.asm:13 |
| >= 7 | treated as reset, C stays set | none | gscode.asm:399-406 |

Data structures and register variables:

| Name | Kind | Detail | Source |
|------|------|--------|--------|
| ring write pointer | register `DE` | starts `#8000` | gscode.asm:108 |
| ring read pointer | register `HL` | starts `#8000`, set before EI | gscode.asm:115 |
| preload limit | `H = #70` during preload, preload until `D = #DF` | | gscode.asm:102-116 |
| volumevalue | SMC operand `volumevalue=$+1`, operand at `#4286` | | gscode.asm:283 |
| vsversion | SMC operand `vsversion=$+1`, at `#4296` | | gscode.asm:290 |
| streaminfobyte | SMC operand `streaminfobyte=$+1`, at `#43C2` | | gscode.asm:459 |
| interrupt | if ring holds >= 3 pages and DREQ (SSTAT bit0): 32 bytes to MD_SEND; `updatestreaminfo` on wrap | | gscode.asm:134-162, 164-233 |
| MCRDY | not polled; `WDC` calls `noper` (18 NOPs); TODO comment | | gscode.asm:11-18 |

### 3.6 Quirks against F4

- `#14` followed by WC (not WD); works because the stock `#14` reads the pre-param before RSCOM.
- `#30` reply never read and flagless.
- `#D1` not a stock command (ignored).
- GP card: command >= 7 does not clear C and resets the card (`JP 0` into the stock ROM, which then sees the same
  pending command).
- `ATOZX` stalls the card with interrupts off until the host reads: the decoder feed stops while a reply is pending.
- `noper` instead of an MCRDY poll: a profile should treat "MC_SEND write while MCRDY = 0" as an expected GP quirk.
- Host reads GSCFG0 from the card side to detect NeoGS (`checkifngs`).

### 3.7 Headless run

V-2: GS or NeoGS, `gp <file.mod>`, `gp.ini` `ModDevice` `0` (release default). V-3: NeoGS required (the decoder).
Media in the repo: [`testdata/sound/neogs/mp3/`](../../../testdata/sound/neogs/mp3/) (`eyeache1-44k-128k-cbr.mp3`,
`eyeache1-22k-mono-vbr-id3.mp3`), [`testdata/media/audio/`](../../../testdata/media/audio/)
(`tone-440-660-44k-stereo.mp3`, `tone-1000-48k-mono.mp3`). .mod media: the GS firmware materials contain
[`test_mods.zip`](../2026-09-19-general-sound/materials/gs/gs-firmware/test_data/test_mods.zip) (5 test modules, 4-332 KB)
and [`music.zip`](../2026-09-19-general-sound/materials/gs/gs-firmware/test_data/music.zip); nothing extracted on a
NedoOS card yet.

---

## 4. V-4 ngsplay / NeoTracker driver

### 4.1 Program

| Item | Value | Source |
|------|-------|--------|
| Host | `N/kapps/ngsplay/main.c`, `ngs_host.c`, `ngs_mp3.c`, `ngs_mod.c`, `gs_io.asm` | |
| Usage | `ngsplay <file>` autostarts the file (`.mp3`, `.mod`, `.s3m` by name) | N/kapps/ngsplay/main.c:1197-1262, 1432-1496 |
| UI loop | MP3 pump every pass; .mod `ngs_mod_pump` every 32 polls; S3M `#E6` every 32 polls; YIELD each pass; Space toggles `#E4`/`#E5`; `stop_current` | main.c:232-243, 1340-1426 |
| Card build | `N/kapps/ngsplay/gs/build_gs.a80`: `ngsdrv.bin` from `_BIOS` (PHASE `#0000`), `ngsldr.bin` from `_loader` (PHASE `#5100`), `neopg2.bin` from `_BIOS2` (ORG `#D000`) | |
| Release | `N/../release/bin/ngsplay.com` 50,682 B, sha256 `bc99e4f165abbdac5d26ff048d007a3891e23babda55f44a2a2337f50a81ed2d`; not on the P2666 testdata card | |

### 4.2 Host library (ngs_host.c)

| Function | Sequence | Wait | Source |
|----------|----------|------|--------|
| waits | busy loops of 30000 spins; timed variants with YIELD | | ngs_host.c:55-132 |
| gs_drain | up to 32 reads of `#B3` while D set | | ngs_host.c:156-166 |
| ngs_upload | data = len low, `#14`, **WD** (not WC), len high WD, addr low WD, addr high WD, then `gs_send_bytes` (`OUT` + WD with timeout) | WD | ngs_host.c:168-196, gs_io.asm:20-52 |
| ngs_jump | data = addr low, `#13`, WC, addr high, best-effort WD | | ngs_host.c:198-212 |
| ngs_download | `#00`, data = len low, `#15`, WD, len high WD, addr low WD, addr high, short WD, then `gs_recv_bytes` (WN + IN) | | ngs_host.c:214-251 |
| ngs_set_page2 / 3 | `#E0` / `#E1` with page pre-param | | ngs_host.c:253-263 |
| ngs_peek_nt | reads `#BC00` in page 1, expects `'N','T'` | | ngs_host.c:269-288 |
| ngs_reset_fw | drain, `GSCTR #80`, 3 x YIELD, `#F3`, timed WC, drain | | ngs_host.c:291-304 |
| ngs_start_load | data = slot, `#E8`, WC, WD, 256 header bytes, then `#00` (C stays set until Load_s3m finishes) | | ngs_host.c:405-424 |
| ngs_get_next_block | `#E9`, WC, 5 bytes each with WN: type, ofs lo, ofs hi, len lo, len hi | | ngs_host.c:426-452 |
| ngs_save_block | data = pages, `#EA`, WC, WD, pages x 256 bytes | | ngs_host.c:454-469 |
| ngs_init_sample | data = module, `#E2`, WC, WD, smp, WD | | ngs_host.c:471-479 |
| ngs_start_play | data = module, `#E3`, **WD**, order, WD | | ngs_host.c:481-490 |
| ngs_stop_play | `#E4`, WC; comment: "during PlayCykl only #E4/#E6 are accepted" | | ngs_host.c:492-497 |
| continue / status | `#E5` WC; `#E6` WC WN read | | ngs_host.c:499-513 |
| ngs_load_s3m | needs `SCRM` at `#2C`; slot 0; block loop (type 0 done; 255 and 123 errors), up to 64 pages per `#EA`; ends with `init_sample(0,255)` | | ngs_host.c:540-733 |

### 4.3 Bootstrap

| # | Host action | Source |
|---|-------------|--------|
| 1 | ngs_reset_fw (stock firmware) | ngs_host.c:311-403 |
| 2 | upload ngsldr.bin to `#5100` | ngs_host.c:311-403 |
| 3 | jump `#5100`; comment "Loader stub only handles #13/#14 - do NOT send #00 here (C-bit sticks)" | ngs_host.c:340 |
| 4 | upload ngsdrv.bin to `#0000` (through the loader at `#5100`) | ngs_host.c:311-403 |
| 5 | jump `#0000`, 30 x YIELD, `#00` sync | ngs_host.c:311-403 |
| 6 | `#E1` page 2, upload neopg2.bin to `#D000`, peek "NT" | ngs_host.c:311-403 |

The ngsdrv upload overwrites `#0000-#300F` while the CPU runs in the loader at `#5100-#518F`; no running code is
overwritten (inferred from the addresses).

MP3 path (`ngs_mp3.c`) reuses GP's gscode: `#14` upload with WC (comment "GP uses WC after SC #14 (not WD)",
ngs_mp3.c:141-176), `#02` retried 40 times (276-286), `#03` restart stream (297), pump on `#01` (< 6 = wait, else
512-byte bursts, 323-363), `#00` on stop (248).

.mod path (`ngs_mod.c`, stock firmware): `mod_hw_reset` = `GSCTR #80`, 3 x YIELD, `#F3`, up to 50 C polls
(ngs_mod.c:85-96); `#30`, `#D1`, 16 KB chunks, `#D2`, a wait for D clear that times out (the stock firmware does not
read data in `COMINT_`), data 1, `#31` with `wait_cmd_busy_long` (500 x 2000 spins with YIELD, comment "Play Module
(#31) can hold C while GS inits patterns") and a `#60` fallback; pump on `#60` with WN; stop with `#F3`
(ngs_mod.c:36-48, 140-302).

### 4.4 Blobs

| Blob | Size | sha256 | Offset in ngsplay.com | Upload | Entry |
|------|------|--------|-----------------------|--------|-------|
| ngsldr.bin | 143 B | `1ef08a9faba171e382ca7656980c5e169b77c4b2ebe7b0514efd93931d9d549e` | 32276 | `#5100` | `#5100` |
| ngsdrv.bin | 12,303 B (`#300F`, ends with " end NeoPlayer ") | `543eceaa43f1fe9ddccb8c388f769fb3eab1d4bab25f3251b05286d853027e9a` | 19971 | `#0000` | `#0000` (`di; jp initngs`) |
| neopg2.bin | 7,272 B | `5bd768e03fe4ba33126677018317404bcf50228f27f9b84811135b59209889cb` | 32421 | `#D000` in page 2 (after `#E1`) | none (overlays copied by the BIOS) |
| gscode.bin (MP3) | 990 B | `9770df659e6981a824e13e8dd17618a4c9b3e1880675da2c184460291724fc8a` | 39695 | `#4000` | `#4000` |

The `.bin` files equal the `*_bin.c` arrays.

### 4.5 ngsldr dispatcher (LOADER)

`N/kapps/ngsplay/gs/ngsbios.a80:5985-6082`, PHASE `#5100`. `COMHZ` (5988): DI, 24 MHz, clear C. `COMINT_`
(5999) polls ZXSTAT; `COMINT1` (6004) reads ZXCMD; `COMINT2 CP #14` (6007): only `#13` (6013) and `#14` (6024) are
accepted. Any other command is never cleared, so it spins with C set: the reason for the `#00` warning in step 3.

### 4.6 ngsdrv (NeoTracker BIOS) dispatcher

| Item | Value | Source |
|------|-------|--------|
| Header | `di; jp initngs`; NUMPG; LoopPat | ngsbios.a80:1-12 |
| INT8 | 8-channel DAC read from `#60xx-#67xx`; `Faza equ $-1` SMC | ngsbios.a80:16-48 |
| initngs | 12 MHz then 24 MHz, EXPAG, 8CHANS, RAM probe (NUMPG `#7E` or `#3E`), page 1 `#BC00 = "TN"` (`ld (#BC00),hl`, read as "NT"), clears `#6000-#7FFF` to `#80`, clears SamplTabl, builds Tab64, `JR COMINT` | ngsbios.a80:59-155 |
| Loop | `COMHZ xor a; OUT (CLRCBIT),A`; `COMINT LD SP,ISTACK`; `COMINT_ IN A,(ZXSTAT); RRCA; JR C,COMINT1` | ngsbios.a80:194-201 |
| Fetch | `COMINT1 IN A,(ZXCMD); CP #20; JR C,COMLOW`; `COMINT2 CP #E0; JR C,COMINT_; SUB #C0` | ngsbios.a80:202-207 |
| Table | `COMLOW ADD A,A; LD H,HIGH COMTAB; OR LOW COMTAB; LD L,A; LD A,(HL); INC L; LD H,(HL); LD L,A; JP (HL)` | ngsbios.a80:208-216 |
| `#20-#DF` | jump back to `COMINT_` **without** clearing C: the command stays pending forever | ngsbios.a80:205-206 |
| Unused | entries point to `COMZ OUT (CLRCBIT),A` -> `COMINT_` | ngsbios.a80:4989, 5613-5656 |
| COMTAB | 64 words (index 0-#1F = cmd `#00-#1F`, #20-#3F = cmd `#E0-#FF`) | ngsbios.a80:5658 |

Addresses computed from ngsdrv.bin: COMHZ `#01F5`, COMINT `#01F8`, COMINT_ `#01FB`, COMINT1 `#0203`, COMLOW `#020F`,
COMTAB `#2E80`, COMZ `#2600`, EndBIOS `#3000`.

| Cmd | Handler | Meaning | Params / reply | Source (ngsbios.a80) |
|-----|---------|---------|----------------|----------------------|
| `#00` | `#06DE` | reset flags (barrier) | reads ZXDATRD, clears C | 901-905 |
| `#01` | `#06E5` | silence | | 907-922 |
| `#02` | `#06FA` | low volume | | 925-935 |
| `#03` | `#0710` | high volume | | 938-948 |
| `#05` | `#0727` | volume of channel E (inferred) | pre-param | 954-963 |
| `#13` | `#021A` | jump | as stock | 220-227 |
| `#14` | `#0565` | upload | as stock; `COM14_JPLOAD` / `COM14_JP` / `COM14_JP16bit` variants with `sign1-3` SMC `add 0` operands | 657-758 |
| `#15` | `#05F9` | download (card -> host) | replies, no CLRCBIT until the first byte | 764-822 |
| `#16` | `#0663` | poke | | 824-838 |
| `#17` | `#06B4` | peek | | 875-885 |
| `#1E` | `#2923` | GSDDRV SD driver (same protocol as the kernel's, §5) | | 5192-5548 |
| `#E0` | `#073A` | SetPage2 | pre-param page | 967-972 |
| `#E1` | `#074E` | SetPage3 | pre-param page | 981-986 |
| `#E2` | `#0762` | init sample: H module, D smp or 255 = all; module < 12 | | 997-1030 |
| `#E3` | `#0F55` | start play: module, then order; ends `IM 1; EI; JP PlayCykl`; copies "sf" overlay | | 2083-2310 |
| `#E4` | `#1166` | pause | | 2372-2385 |
| `#E5` | `#1183` | continue | | 2387-2400 |
| `#E6` | `#11A0` | status: reply PROCESS, then CLRCBIT; resets `nxtDTZX` | | 2402-2411 |
| `#E7` | `#11B3` | reinit volumes / BPM | | 2413-2486 |
| `#E8` | `#029D` | start load: slot, 256-byte header (COM14_JPLOAD), checks `SCRM`, copies "s3" overlay | | 287-329 |
| `#E9` | `#033D` | next block: 5-byte reply (first before CLRCBIT, each next after the host read) | | 369-404 |
| `#EA` | `#039E` | load block: pages pre-param, SkipByte, 16-bit or paged load | | 413-459 |
| `#EB` | `#04E7` | delete module: 5-byte reply | | 584-605 |
| `#EC` | `#10FC` | play note | | 2319 |
| `#ED` | `#114A` | note 254 (inferred: stop note) | | 2357 |
| `#EE` | `#1155` | play without row processing (inferred) | | 2364 |
| `#EF` | `#067C` | peek long: no CLRCBIT up front, reply then CLRCBIT, resumes the player if PROCESS set | | 840-873 |
| `#F0` | `#03FA` | save start: reply 255 or 0 | | 461-518 |
| `#F1` | `#046C` | save block: 6-byte reply, then COM15IN | | 520-582 |
| `#F2` | `#0EA0` | record note | | 1999-2059 |
| `#F3` | `#0F28` | play pattern | | 2060-2078 |
| `#F4` | `#0D6B` | insert pattern | | 1825 |
| `#F5` | `#0228` | new module (no CLRCBIT; `jp New.Create` in page 2 replies with ZXDATWR + CLRCBIT) | | 229-234, page2.a80:2430-2432 |
| `#F6` | `#0238` | load sample: smp, module, 256 bytes, checks "RIFF"/"WAVE" | | 236-268 |
| `#F7` | `#0AA1` | delete | | 1434 |
| others | `#2600` | COMZ | | |

Note: in NeoTracker `#F3` and `#F4` are **not** resets. A host that sends the stock `#F3` warm reset to a running
ngsdrv starts "play pattern" instead (inferred from the table); ngsplay always resets through `GSCTR #80` first.

Load error replies: `_ErrorMem` 123, `NoMiModul` 255 (ngsbios.a80:341-360).

**PlayCykl** (ngsbios.a80:2488-2513) accepts only `#E4`, `#E6`, `#EF`, `#F2`; any other command stays pending until
playback stops. During playback `DataToZX` (5922-5952, called per channel from page2.a80:182) pushes a 4-byte cycle
to ZXDATWR whenever both flags are clear: 255 (sync), MODULE, ORDER, ROWSCUR, with the index in `nxtDTZX equ $-1`
(5929). The card writes data without any host command (unsolicited D).

### 4.7 Data structures

Variables (ngsbios.a80:1399-1431): PROCESS, MODULE, HEADADR, SAMPLADR, ORDER, PATERN, PATERNADR, ROWSCUR, LENROWS,
CHNPAT, ROWSADR, SPED, SPEDTIC, TEMP, BPMBYTE, BPMCUR, MsVol, GlVol, MODES3IT.

Page 1 globals: KolModule `#BC04`, CurModul `#BC05`, LastAdr `#BC06`, LastPage `#BC08`, AllPage `#BC0A`, FlagPlay
`#BC0B`; signature `#BC00` "NT"; per-module 16-byte entries at `#BC10 + 16*k` (DeleteModul, ngsbios.a80:607-650);
module header at `#8000 + n*#500` in page 1, n < 12 (ngsbios.a80:310-329; `cp 12` in COME2). LoadBufer `#C000`,
DepakBufer `#8000`.

Memory map: Tab64 `#3E00`, SamplTabl `#4F00` (4919-4920), ISTACK `#5FFE`, DAC0 `#6000` (5611-5612), EndBIOS
`#3000` (overlay area).

Page and port shadows (5181-5186): Cur_MPAG, Cur_MPAGEX, NGS_MPAG, NGS_MPAGEX, finetune, Loop512.

SMC operands: `Faza equ $-1` (20), `LoadModul equ $-1` (316), `oldfz equ $-1` (526), `sign2`/`sign3`/`sign1`
(702, 712, 752), `To_cl_row equ $-2` (2492), `To_tikeff equ $-2` (2515), `nxtDTZX equ $-1` (5929).

`MODULE smp` channel record (ngsbios.a80:4922-4968), `lenHead = 64`, instances at `SamplTabl + 64*k`, up to CHNPAT
channels (32 maximum inferred; init clears `#FBF` bytes):

| Off | Field | Off | Field | Off | Field |
|-----|-------|-----|-------|-----|-------|
| 0 | Vol_L | 19 | LastSmp | 36 | Argum1 (w) |
| 1 | Vol_R | 20 | Length (w) | 38 | Argum2 (w) |
| 2 | SamplAdr (w) | 22 | ChnVol | 40 | Argum3 (w) |
| 4 | CurPage | 23 | ChnPan | 42 | Argum4 (w) |
| 5 | Nota | 24 | EffJmp (w) | 44 | SaveFrc (3) |
| 6 | CurFrac (w) | 26 | LastEfD | 47 | SaveVL |
| 8 | CurAdr (w) | 27 | LastEfE | 48 | SaveVR |
| 10 | PerFrac (w) | 28 | LastEfH | 49 | FlgEFF |
| 12 | PerInt (w) | 29 | LastEfI | 50 | LastEfP |
| 14 | PgLoop | 30 | LastEfJ | 51 | LastEfQ |
| 15 | SmplVol | 31 | LastEfN | 52 | CelevFrc (3) |
| 16 | LoopSt (w) | 32 | LastEfO | 55 | GlChVol |
| 18 | PgLeng | 33 | LastEfS | 56 | Argum5 (w) |
| | | 34 | LastEfT | 58 | Argum6 (w) |
| | | 35 | LastEfW | 60 | LopPat (w), 62 LopCikl (w) |

`MODULE s3m` EQU block (sample / module header offsets, `N/kapps/ngsplay/gs/gs_defs.a80:4-37`): Length `#010`,
LengthInPg `#024`, LoopBeg `#014`, LoopBegInPg `#02C`, LoopEnd `#018`, LoopEndInPg `#02E`, PgLength `#013`, PgLoopBeg
`#017`, PgLoopEnd `#01B`, Typ `#01D`, InsNum `#022`, PatNum `#024`, ModFlags `#026`, SmpFlags `#01F`, OrdNum `#020`,
Int_Gp `#028`, MemSeg `#0D`, Chen_pat `#03C`, Row_Def `#03D`, inspd `#031`, intmp `#032`, GlVol `#030`, Vol `#01C`,
GVl `#01D`, Int512 `#02B`, memNGS `#01E`, C2Spd `#020`, FineTune `#050`, TabVol `#040`, TabPan `#080`, HeadSampl
`#D800`, PaternTab `HeadSampl-768-512`. Several names share offsets (header vs sample record views).

neopg2 (page2.a80, `_BIOS2`, ORG `#D000`): overlays PHASEd to `BIOS.EndBIOS` (`#3000`), tagged by their first word:
`Sample.SoftSmpl` "sf" (11-272), `Load.s3mload` "s3" (274-1067), `Save.s3mSave` "3S" (1069-1891), `LoadSMP.wavload`
"wa" (1893-2290); `New` module (2293-2447). Load variables (285-296): NextBlock, LastBlock, NextOfs (w, 256-byte
units), NextLen (w), NextAdr (w), NextPage, SkipByte (w), BitSmp, ChenSmp, Modul, Head.

### 4.8 Quirks against F4

- `#14` with WD (ngsplay) vs WC (GP, kernel): both valid against stock and NeoTracker handlers.
- `#E3` uses WD after the command, not WC.
- `#00` as barrier: after `#E8` (C stays set through Load_s3m) and after the jump to `#0000`.
- Loader stub: commands other than `#13`/`#14` stick (C never cleared).
- NeoTracker: `#20-#DF` never cleared; PlayCykl leaves all but `#E4/#E6/#EF/#F2` pending.
- Unsolicited D from `DataToZX` during playback: a host poll on D sees bytes it never asked for; ngsplay drains them
  (`gs_drain`).
- `#F3`/`#F4` change meaning after the upload (not resets).
- `.mod` path waits for D clear after `#D2` and times out by design (stock firmware).

### 4.9 Headless run

NeoGS required (ngsdrv pages, NeoGS ports). `ngsplay` is not on the P2666 card: copy `N/../release/bin/ngsplay.com`
to the card. Command line `ngsplay <file>`. S3M media:
[`testdata/sound/wildsound/src/`](../../../testdata/sound/wildsound/src/) (`kc-techrockloop.s3m`,
`kj_jose_-_a_new_frontend.s3m`, `test.s3m`); `N/kapps/ngsplay/testdata/{min,bad}.s3m` are stubs that do not play.
MP3 media as V-3.

---

## 5. V-5 kernel NeoGS SD driver

### 5.1 Build wiring

| Item | Value | Source |
|------|-------|--------|
| Define | `NGSSD` only in `mkevo.bat:9` (atm=1, `release/sd_boot.$C`, no upload path) and `mkpe26sd.bat:12` (atm=3, `release/osp26sd.$C`, upload path) | N/kernel/mkevo.bat:9, N/kernel/mkpe26sd.bat:12 |
| Device init | `devices_init` device 3 calls `GS_INIT` under `ifdef NGSSD` | N/kernel/fatfsdrv.asm:116-121 |
| I/O | `readsectorsGS` (204), `writesectorsGS` (260), includes (891-892) | N/kernel/fatfsdrv.asm |
| Ports | GSCOM `#BB` (22), GSDAT `#B3` (33), GSCTR `#33` (36), GSCFG0 `#0F` (105) | N/kernel/portsngs.asm |
| Mount | drives A-O mounted at boot | N/kernel/idle.asm:97-127 |
| Images | `sd_boot.$C` 22,033 B, sha256 `36bfc8109712e89300d024d51a4f67c050fbc8c0e78b8a5645d203173e92e90c`; `osp26sd.$C` 22,545 B, sha256 `4c1afde7584dcb31466d9b646c6ed05f9d1fec7bb91b77b9802bf896e0c18279` | N/../release |
| Testdata | P2666 `SD_BOOT.$C` sha256 `2ca7ea22d1d36bbca5f2fb7420ffd6e78ef160d9b60f0f618c3ebc43659aeb68` (`d1d011f8`); zxevo `SD_BOOT.$C` sha256 `4fbee5d81b85ad8a854d85271ee3071521962a9e9b96e1ad57294cefac6dc0a3` | [`testdata/machines/zxevo/nedoos/`](../../../testdata/machines/zxevo/nedoos/README.md) |

### 5.2 Init and upload (host side, ngssddrv.asm)

| # | Step | Wait | Source |
|---|------|------|--------|
| 1 | GS_INIT: data = 0, `#23`, HALT, HALT, `IN GSDAT` with no poll; `inc; cp 4` rejects < 3 pages and `#FF` | 2 HALTs | N/kernel/ngssddrv.asm:121-136 |
| 2 | `if atm != 1`: INSTSDD; on Evo (atm=1) the comment says the ERS already installed the driver | | ngssddrv.asm:121-136 |
| 3 | INSTSDD: host `IN (C)` of GSCFG0 `#0F`; `#FF` = old GS -> SD_NO | | ngssddrv.asm:202-273 |
| 4 | NEOGSINIT: `GSCTR #80`, 3 HALTs, `#F3`, up to 48 HALTs polling C; one `IN GSDAT` drain | | ngssddrv.asm:202-273 |
| 5 | upload 768 B (`#0300`) to SETUPSD `#5B00`: data = E, `#14`, WC_, D WD_, L WD_, H WD_, 768 x `OUTI` + WD_ | WC after `#14` | ngssddrv.asm:14, 202-273 |
| 6 | jump: data = L, `#13`, WC_, H with no WD; 2 HALTs; `IN GSDAT` with no poll; expects `#77` (`#CC` = old GS); `LD B,3` / `DEC B` dead code | | ngssddrv.asm:202-273 |
| 7 | GSDINIT: COMM2SD(0), WN_, IN, expects `#77` | | ngssddrv.asm:139-148 |
| blob | `UKLAD1 incbin ngssd.bin` | | ngssddrv.asm:275-276 |

### 5.3 Uploaded installer and the COMTAB[#1E] patch (ngsinst.asm)

| Item | Value | Source |
|------|-------|--------|
| Origin | `ORG #5B00`; `COMINT_ = #026E` | N/kernel/ngsinst.asm:4-6 |
| Installer | `SCTRL #9C`; AVTODET writes GSCFG0 with bits 5-4 cleared (24 MHz) and reads it back | ngsinst.asm:35-73 |
| Reply | `#77` (NeoGS) or `#CC` on ZXDATWR **before** CLRCBIT; `#CC` -> `JP COMINT_` | ngsinst.asm:35-59 |
| Patch | `RES RAMRO`; `LD (#0300+#1E*2),#1D00` (= `#033C`); LDIR the driver from UKLAD2 (`#5B44`) to `#1D00`, 618 B (to `#1F6A`); `SET RAMRO`; `JP COMINT_` | ngsinst.asm:35-59 |
| Blob | `ngssd.bin` 686 B, sha256 `7e6ac5c61eb27260f97cee80a97ba24020b71047f614006b8d90ef61907db30c`; `SAVEBIN` | ngsinst.asm:451 |
| Upload size | the kernel sends 768 B, so 82 bytes after the blob come from the kernel image (inferred) | ngssddrv.asm:202-273 |

The patch writes the stock `COMTAB` at `#0300` in RAM (the ROM image copied to RAM; RAMRO lifted), so it relies on the
stock layout: COMTAB at `#0300`, COMINT_ at `#026E`. A different firmware (e.g. NeoTracker) breaks it; NeoTracker
ships its own `#1E` (GSDDRV at `#2923`).

### 5.4 Driver GSDDRV (DISP `#1D00`)

| Item | Value | Source |
|------|-------|--------|
| Entry | `IN ZXDATRD` sub-command, CLRCBIT, push COMINT_, `IYH = #77` status | ngsinst.asm:87-120 |
| Table | TABLSDG: 0 SDINIT, 1 SDCMP, 2 RDMUL, 3 WRMUL | ngsinst.asm:122-126 |
| Params | after the table lookup: WDY + IN for B, C, D, E (LBA bits 31-0) and the sector count, then `JP (HL)` | ngsinst.asm:87-120 |
| CCS | SECM20G handles byte addressing (CCS bit) | ngsinst.asm:247-294 |
| Write | WRSECTG: send token, wait for the C strobe, CLRCBIT, 512 x (WDY, IN) | ngsinst.asm:311-329 |
| Read | RDSECTG: wait for the C strobe, CLRCBIT, 512 x (IN SD, OUT ZXDATWR, WDN) | ngsinst.asm:332-348 |
| Lost | ERR_WR_LOST / ERR_RD_LOST drain 512 bytes | ngsinst.asm:378-406 |
| RDMULG | CMD18, token `#FE`, CMD12 | ngsinst.asm:409-428 |
| Status | OK_WORK / OUTSTAT writes status, then WDN | ngsinst.asm:430-440 |
| Waits | WDY (443-446); end GSDDRVE (447) | |

Status codes:

| Code | Meaning | Source |
|------|---------|--------|
| `#77` | OK | ngsinst.asm:87-120 |
| `#88` | card failed the presence check | ngsinst.asm:142 (SDCMPG) |
| `#EE` | card not found | ngsinst.asm:145 (ZAW003G) |
| `#99` | card lost | ngsinst.asm:265 (SECM20G); host `CP #99` with the jump commented out, ngssddrv.asm:61-118 |

### 5.5 Sector handshake (host side)

COMM2SD (ngssddrv.asm:154-180): data = sub-command, `#1E`, WC_; then `RRA` + `bit 6,a` tests the original D bit (still
set = driver absent, the byte was not consumed); then B, C, D, E each with WD_; the count with no WD, then `ds 9` NOPs.

| Op | Per sector | End | Source |
|----|-----------|-----|--------|
| write | COMM2SD(3); strobe `OUT (GSCOM)`, WC_, 512 x `OUTI` + WD_ | WN_, `IN` status | ngssddrv.asm:30-57 |
| read | COMM2SD(2); strobe `OUT (GSCOM),A` (arbitrary value), wait C clear, 512 x (wait D set, `INI`) | wait D, `IN` status, `CP #99` | ngssddrv.asm:61-118 |

WD_ / WN_ / WC_ macros: ngssddrv.asm:183-198.

### 5.6 Quirks against F4

- Strobe writes to `#BB` that are not commands: the driver only waits for C; a profile must not decode them as
  command bytes (the stock dispatcher never sees them because the driver owns the loop).
- Count parameter written with no WD, followed by 9 NOPs: relies on card timing.
- `#13` high byte written with no WD, reply read with no poll after 2 HALTs.
- GS_INIT reads the `#23` reply with no poll after 2 HALTs.
- Host reads GSCFG0 (`#0F`) directly to detect NeoGS.
- 768-byte upload of a 686-byte blob: 82 trailing bytes of kernel image land at `#5DAE-#5DFF` (inferred).
- RAM write into the firmware COMTAB (`#033C`) with RAMRO lifted: an M-kind "firmware code modified" event that is
  intended.

### 5.7 Headless run

`NGSSD` builds only for atm=1 (Evo) and atm=3 (Pentagon 2.666 SD kernel, `osp26sd.$C`). The upload path runs only in
atm=3; Evo relies on the ERS to install the driver (not verified). Machine: ZX-Evo (zxevo-ers ships a NeoGS in
`zxbus`, [`.recipe/machines/atm/atm3-zxevo-baseconf.md`](../../../.recipe/machines/atm/atm3-zxevo-baseconf.md):62-92), NeoGS
SD image in `sd.ngs`, NedoOS card in `sd.zc`. Boot test pattern: `ZXEvoErs_Test.NedoOsBootsFromAHostFolder`
([`core/tests/emulator/machines/zxevo/zxevo_ers_test.cpp`](../../../core/tests/emulator/machines/zxevo/zxevo_ers_test.cpp):783-812,
Tap 5, wait for `M:/bin>`). For the upload path: a machine that boots `osp26sd.$C` with a NeoGS fitted
([`.recipe/machines/slots.md`](../../../.recipe/machines/slots.md), `slots_plug`).

---

## 6. V-6 Moon Rabbit (mrf / mrfua / mrfue)

| Item | Value | Source |
|------|-------|--------|
| Driver | `N/mrabbit-fusion/drivers/general-sound.asm` (the moon-rabbit-zx copy is the same, wrapped in `ifdef GS`) | |
| Build | `-DGS` for mrf (NEDONET), mrfua (UARTATM), mrfue (UARTEVO) | N/mrabbit-fusion/Makefile:24-30 |
| Release | 19,272 B each; mrf sha256 `6fe722f6a71e06bd41490b9bcca7e35a13a250bd6df2f720fa31e88538ae6ba9`, mrfua `3326ca0a82e1b0e8aeb2b73362c6bbfacb489a6336537d54b9edd13b5359817e`, mrfue `5195725d10ca7f69bdbd01e96984460988fbd95c448ab4aa7b01a9253c66d0c5`; testdata mrf and mrfue identical | N/../release/bin |
| Card code | none (stock firmware) | |

Macros: `GS_WaitCommand` (1-6), `GS_WaitData` (8-13), `GS_SendCommand` with no wait (15-17); commands `#F3`, `#F4`,
`#30`, `#31`, `#32`, `#33`, `#D1`, `#D2` (25-32).

| Step | Host action | Wait | Source |
|------|-------------|------|--------|
| init | `#F3` or `#F4` | no wait | general-sound.asm:35-41 |
| loadModule | `#30` WC (reply not read), `#D1` WC | WC | general-sound.asm:44-49 |
| sendByte | `OUT` + WD | WD | general-sound.asm:52-55 |
| finish | `#D2` WC, data 1, `#31` WC | WC | general-sound.asm:58-66 |
| stop | `#32` | no wait | general-sound.asm:69-72 |
| continue | `#33` | no wait | general-sound.asm:74-77 |
| loader | `Gopher.loadMod`: init(0), request, loadModule, then `Wifi.getPacket` loop, every byte via sendByte, finishLoadingModule | stream stays open across network packets | N/mrabbit-fusion/gopher/gopher.asm:60-85 |
| poll | every frame: HALT, `#60`, WC, `IN (DATA)` with no WN; end = position decreased; stopModule | | N/mrabbit-fusion/player/mod-processor.asm:34-63 |
| PT3 | sends `#32` when PT3 starts | | N/mrabbit-fusion/player/vortex-processor.asm:36-38, vortexnedoos.asm:15 |

Quirks against F4: reset sent with no wait (the next command can land while C is still set); `#32`/`#33` with no
wait; open stream across slow network gaps (long idle inside LOAD, not a stall); `#60` read without WN (relies on
the stock reply being written before RSCOM); `.mod` dispatch only for `MIME_MOD` (`ifdef GS`,
gopher/engine/history/controler.asm:87-89).

Headless run: GS or NeoGS plus a network path (NEDONET for mrf, ATM/Evo UART for mrfua/mrfue). The home page is
`browser/index.gph` on a local `file` server (controler.asm:93-99); a local gopher menu pointing at a `.mod` file
would avoid the network (inferred; not checked).

---

## 7. Blob summary

| Blob | Program | Size | sha256 | Upload | Entry | Dispatcher |
|------|---------|------|--------|--------|-------|-----------|
| gscode.bin | GP, ngsplay MP3 | 990 | `9770df65...24fc8a` | `#4000` | `#4000` | processcommand `#4355`, SMC jr at `#4362`, 7 commands |
| ngsldr.bin | ngsplay | 143 | `1ef08a9f...d549e` | `#5100` | `#5100` | LOADER, `#13`/`#14` only |
| ngsdrv.bin | ngsplay | 12,303 | `543eceaa...27e9a` | `#0000` | `#0000` | COMTAB `#2E80`, 64 words |
| neopg2.bin | ngsplay | 7,272 | `5bd768e0...889cb` | `#D000` page 2 | none | overlays to `#3000` |
| ngssd.bin (+82 B) | kernel V-5 | 686 (768 sent) | `7e6ac5c6...db30c` (686 B) | `#5B00` | `#5B00` | installs GSDDRV `#1D00` into COMTAB[#1E] |

---

## 8. Run environment notes

- The P2666 testdata card (NedoOS `d1d011f8`,
  [`testdata/machines/p2666/software/README.md`](../../../testdata/machines/p2666/software/README.md)) has gstest, gp,
  gp.plr, mrf/mrfua/mrfue and modplay, but not ngsplay. The README says "General Sound: modplay" needs hardware the
  board does not have; [`data/configs/p2666/unreal.ini`](../../../data/configs/p2666/unreal.ini):350-353 ships no
  ZX-BUS card, and the refdata lists one ZX-BUS slot with "no card is shipped"
  ([`core/src/emulator/slots/refdata/machines.cpp`](../../../core/src/emulator/slots/refdata/machines.cpp):670-677).
  A NeoGS has to be fitted with `slots_plug` for V-1..V-6 on P2666.
- ZX-Evo (zxevo-ers) ships a NeoGS; the testdata zxevo card is minimal (SD_BOOT.$C, term, cmd, autoexec), so the
  V-programs must be added.
- Pentagon ships `zxbus.1 = neogs` ([`.recipe/machines/slots.md`](../../../.recipe/machines/slots.md)).

## 9. Gaps

- ngsbios.a80 / page2.a80 comments are unreadable (lost CP866 bytes); names for `#05`, `#ED`, `#EE` are inferred.
- The sha256 of the 768 bytes the kernel uploads depends on the kernel build (82 bytes past ngssd.bin); the blob is
  not found verbatim in the release kernel images (inferred: compressed).
- The Evo path (ERS installs the SD driver, no upload) was not verified against ERS sources.
- The drive letter of kernel device 3 (NeoGS SD) was not traced.
- No `.mod` on any NedoOS test card; .mod media exist only in the GS firmware materials zips.
- The `smp` instance count (32) is inferred from the init clear size.
- The stock gs105a map addresses come from the previous research step (map file), not re-cited here by line.
