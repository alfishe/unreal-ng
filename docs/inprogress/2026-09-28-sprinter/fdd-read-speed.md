# Floppy read speed on the Sprinter: BIOS 3.06 and Estex DSS 1.71

**Status:** measured on unreal-ng 2026-11-16; patches built and tested on the emulator, not on a real board, not sent
upstream. **Sources:** [Sprinter-BIOS](https://zxgit.org/Tolik-Trek/Sprinter-BIOS) `c14a8c5` (3.06 Hotfix 2,
`src/bios/exp/EXTENDED/FDD_DRIVER.asm`), [Estex-DSS](https://zxgit.org/Tolik-Trek/Estex-DSS) `master` `ae08ad9`
(1.71.66: `DSS/FS/FAT.asm`, `DSS/drivers/media/fdd-drv.asm`, `DSS/DOS_Proc.asm`). The workload is a real program's
start: Nether Earth Sprinter Edition, which reads six files (230 KB) with `DSS_READ` in 16 KB calls.

A 1.44 MB disk gives 922 bytes a frame (18 sectors of 512 bytes a turn, 5 turns a second). The game's start
(230 KB, with its unpacking and the PLD reload in between) takes 1 012 frames from the EXE under the DSS on its disk
and 1 486 under DSS 1.71.66; from a hard disk 357. The CPU is not the limit: inside a track the BIOS
reads every sector on time. The time goes to three things, each in a different layer:

| # | Layer | What happens | Cost (this start) | Fix | Result |
|---|---|---|---|---|---|
| 1 | DSS 1.71 `MediaCheck` | every `DSS_READ` and every open resets the drive (the head back to track 0) and reports "disk changed"; DSS reads the boot sector, the FAT and the root directory again | 1 260 sector reads instead of 682 | [§3](#3-fix-1-dss-mediacheck-does-not-reset-the-drive) | **-527 frames (-35 %)**: recommended |
| 2 | DSS `BLOCK_READ` | one driver call per cluster: one sector on a 1.44 MB disk | 530 BIOS calls for 544 sectors | [§4](#4-fix-2-dss-reads-a-run-of-clusters-in-one-call) | only with fix 3a: +127 frames with the stock BIOS, 0 with 3a |
| 3a | BIOS `SEEK` | a `SEEK` command and a delay before every sector, even on the same track | in a long request every sector waits a turn after the previous one | [§5.1](#51-fix-3a-no-seek-on-the-track) | needed before fix 2 lands; harmless alone |
| 3b | BIOS `FDD_5x_LONG_READ` | it asks for sector N and waits for it: a call that comes late waits a whole turn (200 ms) | 30 lost turns, ~300 frames | [§5.2](#52-fix-3b-read-a-track-in-the-order-the-sectors-come) | **slower** by 8-104 frames on the emulator: not recommended as is |

Fix 1 stands alone and is the only large win. Fixes 2 + 3a together give the same time as fix 1 alone (958 against
959 frames): the turns they save at the end of each 16 KB call are lost again at the starts of files and calls.
Fix 3b removes half of the lost turns but pays a `READ ADDRESS` (about one sector) for every run, and on the
emulator a step already catches sector 1 once 3a is in. The measurements are in [§6](#6-measurements); hard disk
and CompactFlash in [§7](#7-hard-disk-and-compactflash): there the disk costs about 18 frames of the start and none
of this matters.

## 1. Method

- **Emulator:** unreal-ng, `SPRINTER`, the floppy at its real speed (300 rpm, 500 kbit/s; no fast disk), 21 MHz.
- **BIOS images:** built from the sources with `tools/machines/sprinter/bios-build/make-bios.py` and sjasmplus 1.21.1.
  The build of `c14a8c5` is **byte for byte** the shipped `data/rom/sprinter/sp2k-3.06-hf2.rom` (CRC32 `9aa7bb29`),
  so a patched build differs from the stock BIOS only by the patch. Selected per run with `POST /sprinter/bios`
  (`"bios": "<path>"`).
- **DSS kernels:** built from `ae08ad9` with sjasmplus 1.21.1 ([estex-dss-build.md](estex-dss-build.md)), all on
  the same day (the kernel stores its build date). The game's disk with `system.dos` replaced (in one piece) and the
  1.71.66 loader at LBA 1-3 and in sector 0 (a 1.62 loader cannot load a kernel over 16 KB).
- **What is measured:** frames from the reset to the game's EXE (`Loader`), to the title menu (`ScrEnter`), the
  section timer on the game's read loop; the port trace of the WD1793's command / track / sector writes and port
  `#FF` (drive, side) for every sector read and its time: `frame + (timestamp - frame x 71 680) / 430 080`.
- **What counts:** "EXE → menu" (the game's own start, without DSS's boot) and the reads. The game's own work in
  that span (unpacking about 120 frames, the PLD reload 199 frames) is the same in every run.

## 2. Where the time goes (stock BIOS, the game's DSS)

The gaps between two sector reads (from one `Read Sector` command to the next), 543 reads after the EXE starts:

| Gap | Reads | Meaning |
| --- | ---: | --- |
| 0.5 frame | 448 | the next sector, on time |
| 0.6-2 frames | 51 | the next sector after a side change, or after a step that caught its sector |
| 3.5-9 frames | 10 | the first sector of a file (directory, FAT) |
| 10 frames (one turn) | 30 | **the sector had passed: the disk turns once more** |
| 12-70 frames | 4 | the game unpacking between two files |

The lost turns fall at fixed places within each file: every 32 sectors (the end of a 16 KB `DSS_READ`) and every
36 sectors (a cylinder: 2 sides x 18, then a step). `SCREENS.PAK` (220 sectors): 122 frames of data plus 12 lost
turns of 10 frames = 242; measured 237.

Each DSS call reaches the BIOS as **one-sector** calls: the section timer on the BIOS's read entry counts 530 calls
for 544 sectors. On a 1.44 MB disk a cluster is one sector, and DSS calls the driver once per cluster.

### Why a step loses a turn

Standard 1.44 MB tracks start with sector 1 right after the index. On the emulator the read of sector 1 on the other
side comes 0.70 frame after the read of sector 18 (0.53 between two sectors): the index gap gives the BIOS about
3.4 ms more than between two sectors. The BIOS's `SEEK` spends more than that before it asks for sector 1:

```text
SEEK:  ...
       OUT  (FDC_93.DrvCTRL),A     ; drive, side
       IN   A,(FDC_93.Track)
       CP   C
       CALL NZ,P50ms               ; another track: a delay first (~2.3 ms at 21 MHz)
       ...
       LD   A,#18                  ; SEEK: the step itself (3-6 ms)
       CALL EXECOM
       ...
.STOL: CALL P50ms                  ; head settle (~2.3 ms)
```

Then it asks for sector 1, which has gone by. The same happens when a DSS call returns and the next one starts:
the program's loop and DSS's entry take longer than the gap between two sectors.

On the emulator a step takes about 3 ms (from the `SEEK` command to the next `READ SECTOR`: 0.27 frame with the
settle delay). With fix 3a the BIOS catches sector 1 after a step inside a long request. A real drive may step
slower (a WD1793 at 1 MHz steps in 6 ms at rate 0): **not checked on a board**, and the one place where fix 3b could
win on hardware where it loses on the emulator.

## 3. Fix 1: DSS `MediaCheck` does not reset the drive

### Was

`OPENDSK` (`DSS/DOS_Proc.asm`) runs on every open and every `READ` / `WRITE` call. For the drive already open it
asks the driver whether the medium changed:

```asm
OPENDSK:	 LD	B,A
		 LD	A,(CORE_BUFFERS.FS_Buffer.DRIVE)
		 CP	B
	 	 JR	NZ,.open
		 PUSH	BC
		 LD	C,Dss.DRV.MediaCheck
		 RST	ToDSS.DRV
		 POP	BC
		 JR	Z,.exit			; not changed: nothing to do
		 JR	C,.error
		 JR	.skip_open		; changed: read the BPB (and the FAT, the directory) again
```

The floppy driver (`DSS/drivers/media/fdd-drv.asm`) answers by resetting the drive and saying "changed":

```asm
MediaCheck:	CALL	Open			; BIOS DRV_RESET: seek, READ ADDRESS, RESTORE (track 0)
		RET	C
		IF NON_REMOVABLE_FDD		; 0 in defines.inc
		 XOR	A
		ELSE
		 LD	A,#FF			; always "changed"
		 OR	A
		ENDIF
		RET
```

So every 16 KB the head goes to track 0, DSS reads the boot sector, 16 FAT sectors and the root directory, and the
head seeks back to the file. Over the game's start: 34 boot-sector reads, 306 FAT reads, 266 directory reads
(1 260 reads in all; the game's own DSS reads 650). The released 1.71.57 has the same code; the DSS on the game's
disk (an older kernel) does not check at all.

### Should be

The WD1793 and the Sprinter's floppy port give no change line, so "changed" has to be inferred. What MS-DOS does
on drives without a change line: the medium cannot have changed while the motor still runs, i.e. within about two
seconds of the last access. Only after that the driver checks (here: the boot sector's serial number, or the BPB,
read once).

```asm
MediaCheck:	CALL	LAST_ACCESS_AGE		; frames since this drive was last read or written
		CP	100			; 2 s: the motor is still on, the disk is the same
		JR	NC,.check
		XOR	A			; not changed, the drive not touched
		RET
.check:		CALL	Open			; the old path: reset, and report "changed"
		RET	C
		LD	A,#FF
		OR	A
		RET
```

`LAST_ACCESS_AGE` needs a frame counter (DSS has the interrupt; a byte incremented there and stamped by `Read` /
`Write` is enough). A better check after the two seconds is to compare the boot sector's volume serial with the one
read at open, and to answer "not changed" when it matches: that costs one sector read instead of a reset, the BPB,
the FAT and the directory.

### Measured with

The experiment measures the upper bound: `MediaCheck` answers "not changed" without touching the drive.

```diff
-MediaCheck:	CALL	Open
-		RET	C
-		IF NON_REMOVABLE_FDD
-		 XOR	A
-		ELSE
-		 LD	A,#FF
-		 OR	A
-		ENDIF
-		RET
+MediaCheck:	XOR	A		; EXPERIMENT: the disk is never changed, the drive is not touched
+		RET
```

The two-second rule gives the same result here: the start reads the next file within a frame or two of the last.

### Difference

DSS 1.71.66, stock BIOS: EXE → menu **1 486 → 959 frames** (-527, -35 %), reads 1 260 → 682. Details in §6.

## 4. Fix 2: DSS reads a run of clusters in one call

### Was

`BLOCK_READ` (`DSS/FS/FAT.asm`) reads the part of the first cluster, then loops over the chain, one driver call
per cluster:

```asm
.loop4:		LD	HL,CORE_BUFFERS.FS_Buffer.SectorsPerCluster
		LD	A,B
		SUB	(HL)			; B - sectors left, C - this cluster's
		...
.BLOKRD7:	EX	DE,HL
		PUSH	BC
		CALL	READ_FROM_FAT		; the next cluster in the chain
		POP	BC
		JR	C,.ECL1
		EXX
		 EX	DE,HL
		 PUSH	HL
		EXX
		EX	DE,HL
		PUSH	HL
		PUSH	BC
		CALL	CLUSTER_TO_SECTOR
		LD	DE,(READ.PointerOnBuffer)
		LD	A,(CORE_BUFFERS.FS_Buffer.DRIVE)
		LD	B,C			; ONE cluster
		LD	C,Dss.DRV.Read
		RST	ToDSS.DRV
		...
		JP	.loop4
```

On a 1.44 MB disk (one sector a cluster) a 16 KB `DSS_READ` becomes 32 driver calls of one sector, each through
`RST #18`, the driver, `RST #08` into the BIOS, `SAVE_INTERRUPTS`, `S_FDD`, `SET_SPEED`, `NTRACK` (a division loop)
and `SEEK`.

### Should be

While the next cluster in the chain is the next one on the disk, it joins the run; the driver gets the run in one
call (the BIOS's `B` is up to 255 sectors, and `READ` never asks for more than 127):

```asm
.BLOKRD7:	EX	DE,HL
		PUSH	BC
		CALL	READ_FROM_FAT		; HL':HL - the cluster read last
		POP	BC
		JP	C,.ECL1
		EXX
		 EX	DE,HL
		EXX
		EX	DE,HL			; HL':HL - the run's first cluster
		...
		CALL	CLUSTER_TO_SECTOR
		LD	(.RunSector),IX		; the run starts here
		LD	(.RunSector+2),HL
		...
.ext:		LD	A,B			; sectors left after the run
		OR	A
		JR	Z,.run
		...
		CALL	READ_FROM_FAT		; DE':DE - the cluster after the run's last
		...
		PUSH	HL
		INC	HL
		AND	A
		SBC	HL,DE			; the next one on the disk?
		POP	HL
		JR	NZ,.run
		... (the high word likewise)
		INC	HL			; it joins the run: C += min(B, SectorsPerCluster)
		...
		JR	.ext
.run:		...
		LD	IX,(.RunSector)
		LD	HL,(.RunSector+2)
		LD	DE,(READ.PointerOnBuffer)
		LD	A,(CORE_BUFFERS.FS_Buffer.DRIVE)
		LD	B,C			; the whole run
		LD	C,Dss.DRV.Read
		RST	ToDSS.DRV
```

The full diff (72 lines added, 8 changed or removed) is in [§8](#8-the-patches). The rest of `BLOCK_READ` (the pointer advance, the
error exits, the stack frame `.Error` pops) is unchanged. It helps every driver: an IDE read of 32 sectors is one
ATA command instead of 32.

### Difference

With fix 1 in, EXE → menu:

| BIOS | Fix 1 only | Fix 1 + fix 2 | Lost turns (1 / 1+2) |
| --- | ---: | ---: | --- |
| stock | 959 | **1 085** (+126) | 33 / 46 |
| 3a | 959 | 958 | 33 / 32 |
| 3a + 3b | 951 | 990 | 34 / 15 |

With the stock BIOS fix 2 is **slower**: the BIOS's per-sector `SEEK` (a command and a delay before every sector)
makes each sector of a long request miss the next one now and then, and a step inside the run loses a turn.
With fix 3a the runs read a whole cylinder without a loss, but the time is the same as without fix 2: the lost
turns move to the starts of files and of `DSS_READ` calls (the first cluster of a call is read on its own, then the
run starts a sector too late), and the game's own small reads (file headers) stay one sector per call.

## 5. Fix 3: the BIOS's long read

### 5.1 Fix 3a: no `SEEK` on the track

`SEEK` issues the controller's `SEEK` (`#18`) before every sector, even with the head on the track, and waits before
a step as well as after it.

Was / should be:

```diff
 		 OUT	(FDC_93.DrvCTRL),A
 		 IN	A,(FDC_93.Track)
 		 CP	C
-		 CALL	NZ,P50ms
+		 RET	Z		; on the track already: only the side was selected, no SEEK
 		 LD	A,C
 		 OUT	(FDC_93.Data),A
-		 IN	A,(FDC_93.Track)
-		 CP	C
-		EX	AF,AF'	;R.TRACK==PHISICAL TRACK
-		LD	A,#18	;!HARDCODE CMD КР1818ВГ93
+		LD	A,#18		; SEEK (no delay before the step: the WD1793 needs none)
 		CALL	EXECOM
 		RET	C
-		EX	AF,AF'
 		 LD	A,C
 		 OUT	(FDC_93.Track),A
-		 RET	Z
-.STOL:		 CALL	P50ms
-		RET
+.STOL:		 JP	P50ms		; head settle after the step
```

Difference: the `SEEK` commands of the start go from 653 to 30 (1 296 to 85 under DSS 1.71.66). With one-sector
DSS calls the time does not change at all (1 012 = 1 012 frames on the game's disk, 959 = 959 with fix 1): the
reads wait for the disk, not for the BIOS. With fix 2 it is what makes runs work (1 085 → 958). Patch size: -9
lines, +3.

### 5.2 Fix 3b: read a track in the order the sectors come

#### Was

`FDD_5x_LONG_READ` (`FDD_DRIVER.asm`) handles each sector on its own, in order:

```asm
.DSK_LP:	LD	A,D
		EXX
		 PUSH	BC
		 CALL	SEEK			; for EVERY sector
		 POP	BC
		EXX
		PUSH	DE
		PUSH	BC
		PUSH	HL
		CALL	FDD_RW_SECTOR		; sector E+1: wait until it comes
		...
		LD	A,C
		INC	E
		CP	E
		JR	NZ,.NINC_T
		LD	E,0
		INC	D
.NINC_T:	DJNZ	.DSK_LP
```

If sector E+1 has just passed, `FDD_RW_SECTOR` waits a turn for it, then reads the rest of the track: a turn plus the
run.

#### Should be

For a read of two sectors or more on this track (and within the memory page), ask the controller which sector is
under the head (`READ ADDRESS`, `#C0`), start with the next one, and wrap round to the run's first sectors. Each
sector goes to its own place in the buffer. A run never costs more than a turn plus one sector, whatever came
before it (a step, a side change, a late call):

```asm
.DSK_LP:	LD	A,D
		EXX
		 PUSH	BC
		 CALL	SEEK
		 POP	BC
		 BIT	5,B			; a write: sector by sector, as before
		EXX
		JR	NZ,.IN_ORDER
		CALL	RUN_FITS		; two or more sectors of this track, in this page?
		JP	NC,.ARRIVE		;  yes: in the order they come under the head
.IN_ORDER:	PUSH	DE
		...

.ARRIVE:	PUSH	DE			; the same frame as the sector-by-sector path (.ERRDOS)
		PUSH	BC
		PUSH	HL
		LD	A,C			; the run: min(B, C - E) sectors
		SUB	E
		CP	B
		JR	C,.a_n
		LD	A,B
.a_n:		EXX
		 LD	E,A			; E' - the run's length
		EXX
		CALL	READ_ID			; A - the sector passing now (from 1)
		JR	C,.a_first		; no ID: from the run's first sector
		CP	C
		JR	C,.a_j			; A - the next sector's index (from 0)
		XOR	A
.a_j:		SUB	E			; its offset in the run
		EXX
		 CP	E
		EXX
		JR	C,.a_k
.a_first:	XOR	A			; not in the run: from its first sector
.a_k:		...				; D' - offset to read, C' - sectors left
.a_lp:		...				; HL = run address + offset * sector size
		ADD	A,E
		LD	E,A			; the sector's index on the track
		CALL	FDD_RW_SECTOR
		POP	DE
		JP	C,.ERRDOS		; (the whole run counts as not read)
		...				; next offset, wrapping at the run's end
		JR	NZ,.a_lp
		...				; HL, E, D, B past the run; CHANGE_MEM_BLK at a page end
		JP	NZ,.DSK_LP
		JP	.RETDOS

READ_ID:	LD	A,#C0			; READ ADDRESS
		OUT	(FDC_93.Command),A
		...				; wait for DRQ / INTRQ (timeout: FORCE INTERRUPT, CF=1)
.data:		LD	H,3			; track, side, SECTOR, size, CRC
.byte:		IN	A,(FDC_93.Data)
		DEC	H
		JR	NZ,.next
		LD	L,A
.next:		IN	A,(FDC_93.DrvCTRL)
		AND	%1100'0000
		JR	Z,.next
		JP	P,.byte
		IN	A,(FDC_93.Command)
		AND	%0001'1000		; RECORD NOT FOUND, CRC ERROR
		LD	A,L
		RET	Z
		SCF
		RET
```

Writes stay sector by sector (the same scheme works for them; not tested). The retry path in `FDD_RW_SECTOR` is
unchanged.

#### Difference

Alone (one-sector DSS calls) there is nothing to reorder: 1 012 → 1 003 frames on the game's disk, 959 → 951 with
fix 1. With fix 2 the lost turns halve (32 → 15) and the trace shows a track side read in one turn from wherever
the head was:

```text
frame   cmd  trk side sec
595.56  #18  17   0   18     step to track 18
595.82  #C0  18   0          READ ADDRESS: sector 1 is passing
595.84  #80  18   0    2     2, 3, ... 18 every 0.53 frame
604.79  #80  18   0   18
605.32  #80  18   0    1     sector 1 last: side 0 in 9.5 frames
606.04  #C0  18   1          other side
606.13  #80  18   1    3     3 ... 11, then 1, 2 (the call ends at sector 11)
```

Still it is **slower** (958 → 990 frames), for three measured reasons:

1. every run pays a `READ ADDRESS`: it waits for the next ID, and the first read then waits through that sector's
   data (0.96 frame to the first sector instead of 0.53). 48 runs after the EXE, about 25 frames;
2. with fix 3a a step already catches sector 1 on the emulator (§2), so the case 3b was made for does not occur;
3. a run that ends at the end of the track saves nothing: in order or wrapped round, the read ends when the run's
   last sector has passed. 3b wins only when the head is inside the run, and only the sectors after the head.

The calls that lose turns now are one-sector calls (the first cluster of a `DSS_READ`, the game's file headers),
which no BIOS ordering can help. 3b is worth another look only if a real drive steps slower than the gap (§2); then
`READ ADDRESS` should be used only after a step, not for every run.

## 6. Measurements

Floppy, 1.44 MB, the game's start, the floppy at its real speed. Frames (50 a second) from the reset. "EXE → menu"
is the game's own start; reads, `READ ADDRESS` commands and lost turns (gaps of 9-11 frames between two reads)
from the port trace. One run each: the emulator is deterministic, a repeat gives the same frame.

| Disk (DSS) | BIOS | EXE | Menu | EXE → menu | Sector reads | `READ ADDRESS` | Lost turns |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| the game's disk (its own DSS) | stock | 483 | 1 495 | 1 012 | 651 | 2 | 33 |
| | 3a | 483 | 1 495 | 1 012 | 651 | 2 | 33 |
| | 3a + 3b | 502 | 1 505 | 1 003 | 651 | 9 | 34 |
| 1.71.66 stock | stock | 673 | 2 159 | **1 486** | 1 260 | 36 | 18 |
| | 3a | 673 | 2 159 | 1 486 | 1 260 | 36 | 18 |
| | 3a + 3b | 745 | 2 335 | 1 590 | 1 260 | 113 | 30 |
| 1.71.66 + fix 1 | stock | 546 | 1 505 | **959** | 682 | 2 | 33 |
| | 3a | 546 | 1 505 | 959 | 682 | 2 | 33 |
| | 3a + 3b | 554 | 1 505 | 951 | 682 | 11 | 34 |
| 1.71.66 + fix 2 | stock | 673 | 2 286 | 1 613 | 1 260 | 36 | 31 |
| | 3a | 664 | 2 149 | 1 485 | 1 260 | 36 | 18 |
| | 3a + 3b | 745 | 2 413 | 1 668 | 1 260 | 162 | 18 |
| 1.71.66 + fixes 1, 2 | stock | 546 | 1 631 | 1 085 | 682 | 2 | 46 |
| | 3a | 537 | 1 495 | **958** | 682 | 2 | 32 |
| | 3a + 3b | 554 | 1 544 | 990 | 682 | 60 | 15 |

- DSS 1.71.66 boots 190 frames later than the game's DSS (EXE 673 against 483): its kernel is larger and its boot
  reads the disk the same way (the `READ ADDRESS` count is `MediaCheck`'s drive reset, 36 of them). Fix 1 brings
  the boot down to 546.
- The 2 `READ ADDRESS` on the other disks are the boot's own drive reset.
- Every run reached the menu (screenshot checked): no patch broke a read.

## 7. Hard disk and CompactFlash

The same game files on a 32 MB FAT16 hard disk image (MBR, partition at LBA 63, 2 KB clusters, the 1.71.66 loader
at LBA 1-3), on `ide0.master`, as a hard disk and as a CompactFlash card (`device: cf`), stock BIOS, two runs each:

| Kernel | Medium | EXE | Menu | EXE → menu | PLD reload |
| --- | --- | ---: | ---: | ---: | ---: |
| the game's DSS | HDD | 351 | 708 | 357 | 199 |
| | CF | 351 | 708 | 357 | 199 |
| 1.71.66 stock | HDD | 351 | 705 | 354 | 199 |
| | CF | 351 | 705 | 354 | 199 |
| 1.71.66 + fix 2 | HDD | 351 | 704 | 353 | 199 |
| | CF | 351 | 704 | 353 | 199 |

The game's read loop (section timer passes, frame of each `read` and the `unpack` after it): 351 / 353, 368 / 373,
412 / 420, 479 / 480, 485 / 485, 496 / 498, then the reload from 509 to 708.

On the hard disk the six files take about 18 frames (`GAME.PAK` 2, `SOUND.PAK` 5, `SCREENS.PAK` 8, the rest 3):
about 13 KB a frame, against 0.45 on the floppy. `MediaCheck` costs nothing measurable there (1.71.66 starts the game
in 354 frames, the game's own DSS in 357), and fix 2 saves 1 frame (one ATA command per run instead of one per cluster). The start is the game's
own work: the PLD reload (199 frames, the BIOS's ROM loader at 3.5 MHz after the reconfiguration reset: a program
cannot shorten it) and the unpacking (about 120). From a floppy the same start takes 958-1 012 frames, from an
IDE device 353-357.

## 8. The patches

In [fdd-read-speed/](fdd-read-speed/), each against the stated source revision:

| File | Against | Lines | Recommendation |
| --- | --- | ---: | --- |
| [dss-1-mediacheck-experiment.diff](fdd-read-speed/dss-1-mediacheck-experiment.diff) | Estex-DSS `ae08ad9` | -8 +1 | the measurement only; land the two-second rule or the serial check of §3 instead. The original has a `;!FIXIT` saying the check should ask the WD1793 "when (if) that becomes possible" |
| [dss-2-block-read-runs.diff](fdd-read-speed/dss-2-block-read-runs.diff) | Estex-DSS `ae08ad9` | +72 -8 | land together with 3a, never alone; no gain on this start, one ATA command per run on IDE |
| [bios-3a-seek.diff](fdd-read-speed/bios-3a-seek.diff) | Sprinter-BIOS `c14a8c5` | -9 +3 | land: simpler, harmless alone, needed by fix 2 |
| [bios-3b-arrival-order.diff](fdd-read-speed/bios-3b-arrival-order.diff) | Sprinter-BIOS 3a | +165 -2 | do not land: slower on the emulator; revisit after measuring a real drive's step time |

Build: the BIOS with `tools/machines/sprinter/bios-build/make-bios.py` (pages 0 and 8 change: page 0 carries a copy
of the driver), the DSS kernel per [estex-dss-build.md](estex-dss-build.md). A 1.71 kernel over 16 KB needs the
1.71 boot loader on the disk; the 1.62 loader stops after 16 KB.
