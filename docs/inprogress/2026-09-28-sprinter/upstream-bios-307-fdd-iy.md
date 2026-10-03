# Upstream note for the BIOS author: 3.07 BETA 1 FDD driver changes IY

**For:** Anatoly Belyansky (Tolik-Trek), [Sprinter-BIOS](https://zxgit.org/Tolik-Trek/Sprinter-BIOS), branch
`beta`. **Build:** `f546c4e` ("Firmware v3.07 BETA 1", 24.09.2026). **Found:** 2026-10-03, in unreal-ng, confirmed
in MAME 0.289. Background and evidence: [bios-versions.md](bios-versions.md) §5.2.

## Symptom

With DSS 1.71.57 (the system disk of the MAME pack, `sp_hdd_sys`) on BIOS 3.07 BETA 1, no program on a floppy
starts:

- Flex Navigator: "Invalid EXE file";
- the DSS prompt: "Bad command or file name";
- `copy b:\file c:\` makes a 0-byte file.

Directory listings of the floppy work. Drive A and drive B both fail, and so do 1.44 MB and 720 KB disks. The
same disk and floppy work on 3.06 Hotfix 2.

## Repro

1. Boot BIOS 3.07 BETA 1 with the DSS 1.71.57 hard disk on the primary master.
2. Put any FAT12 floppy with an EXE in drive A or B.
3. At the prompt type `b:\prog.exe`, or `a:\prog.exe`.

DSS reads the boot sector, the FAT and the directory, then stops. It never reads the file's first sector.

## Root cause

`bios/exp/EXTENDED/FDD_DRIVER.asm` now keeps one table per drive and selects it in `SELECT_FDD`
(`LD IY,SYS_PAGE.FDD_TABLE.A/B`). Six functions return with IY still pointing at that table:
`FDD_5x_RESET`, `FDD_5x_GET_PAR`, `FDD_5x_SET_PAR`, `FDD_5x_DETECT`, `FDD_5x_(LONG_)READ` and `(LONG_)WRITE`.
In `.Start` the `PUSH IY` / `POP IY` pair is commented out.

In 3.06 none of these functions changed IY. That driver used fixed addresses, and `.Start` saved IY.

DSS 1.71.57's floppy driver keeps a pointer of its own in IY across these calls, and it loses that pointer.
A trace of the BIOS entries shows the change: IY is `#31D0` going into RESET and `#C1E8` coming out on 3.07; on
3.06 it comes out unchanged. Current Estex-DSS (`drivers/media/fdd-drv.asm` on `master`) wraps every call in
`PUSH IY` / `POP IY`, and the DSS on the 3.07 recovery disk works. Every DSS 1.71.57 disk in the field still breaks.

## Suggested fix

Preserve IY in the six entry points, as 3.06 did. This patch, tested on unreal-ng, makes DSS 1.71.57 run programs
from a floppy on 3.07:

```asm
FDD_5x_GET_PAR:	PUSH	IY		; likewise FDD_5x_SET_PAR, FDD_5x_DETECT, FDD_5x_RESET
		CALL	FDD_5x_GET_PAR_IY
		POP	IY
		RET
FDD_5x_GET_PAR_IY:
		CALL	SELECT_FDD	; the original body
		...

.RW_Shared:	CALL	SAVE_INTERRUPTS.switch_off	; FDD_5x_LONG_READ: covers READ and WRITE
		PUSH	IY
		CALL	.Start
		POP	IY
		JP	SAVE_INTERRUPTS.restore
```

Do not simply uncomment `PUSH IY` at `.Start`. The `RET C` after `SELECT_FDD` would then return with IY still on
the stack.

## Confirmation

- **MAME 0.289** (`sprinter` driver, the beta image in place of `v3.06`, the same CHD, the floppy in drive A) shows
  "Invalid EXE file". MAME's own 3.06 runs the program.
- **unreal-ng:**
  - The controller delivers the beta's sector reads intact (`PortDecoderSprinter_Test.Fdc_Bios307SectorReadLoop_HdSide1`).
  - The env-gated `SprinterFloppyExe_Test` pins both BIOS versions.
  - A 3.07 build with the patch above runs the program.
