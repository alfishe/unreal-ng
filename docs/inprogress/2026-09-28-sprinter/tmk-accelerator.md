# The TMK accelerator configuration

**Status:** module done 2026-10-18 (`SprinterPldTmk`, `SprinterPldTmk_Test`). It follows the author's sources. Nothing
was checked against a board or the author's MAME setup yet (§5).

The source is tmk79's K30 project of 2026-02-25. It has the AHDL sources, the built `SP2_ACEX.ttf/.hex/.pof/.sof` and
the test program `TmKonf.exe` (sjasmplus sources `asm/_sprinter`). The base is BIOS-TT's `sprinter-computer-hard` 1K30
design, the one with the 2022 changes, which Standard already models (keyboard INT on ALL_MODE bits 0 and 3, the ZX
keyboard always on, the ZX screen shadow off with ALL_MODE bit 0). Against that base the author changed
`ACCELER.TDF` and the accelerator's registers in `SP2_ACEX.TDF`. Everything else in this list needs no emulation:

- Shift+F12 toggles `VGA_SWITCH`, which drives the video DAC's `V_CS1`, so it only switches the TV / VGA output.
- `NMI_SW` is wired to `DCP.TDF`, but `NMI_ENA` there stays `VCC`.
- In `VIDEO2.TDF` the author only commented out nodes.

## 1. Identification

`SP2_ACEX.ttf` holds the 59 215 bytes of an `.ACX`. Over the loader's 473 720 writes (`sprinterpldconfig.h`) the
hashes are full `#84BA5A9A`, head `#E1BA76FB` (CRC32 of the bytes `b5418b41`). The same script gives BIOS 3.07's
`K30.ACX` the known `#29641AB3` / `#49861031`. A program that loads this stream (LDConf, a reload through fast RAM),
or a BIOS built with it on ROM page `#C`, gets the TMK module.

## 2. What it adds

| Register / part | Hardware (`ACCELER.TDF`, `SP2_ACEX.TDF`) | Emulation |
|---|---|---|
| Buffer | two `LPM_RAM_DP` 1 024 x 4 (was one 256 x 16): **1 024 bytes**, `ACC_CNT[9..0]` | cells `#000-#0FF` in `SprinterAccelState::buffer` (Standard's blob unchanged), `#100-#3FF` in the module's `SprinterAccelTmkState::bufferHigh` |
| Start of the counter | `(0,1,RGACC)` for RGACC = 0 (256), `(0,0,RGACC)`; with ACC_CTRL bit 5 `(RGACC,0,0)` = RGACC x 4 (0 = 1 024) | `StartCounter`, `OperationAccesses` |
| Alternate addressing (code `#C7`) | `RAM_ADR = (0,0,XCNT)`: only the first 256 cells | the same |
| Code `#80` ACC_CTRL (write) | bit 0 clip by PORT_Y bit 7 and bit 1 the clipped value; bit 2 clip by address bit 7 and bit 3 the clipped value; bit 4 rectangle; bit 5 length x 4; bit 6 the address runs down (`AA - 1`); cleared by /RESET | `SprinterPldTmk::WriteCode`, `OnReset` |
| Code `#81` ACC_RECT_W (write) | bytes per rectangle row - 1; `ACC_RECT_W_ENA = DFF(DCPP == #81)` loads `RECT_COUNTER`, whatever the direction | a write sets width and counter; a read reloads the counter |
| Rectangle | `RECT_ENA = CTRL4 & ACC_DIR1 & !START_ACC & store`: the counter steps with the address; at 0 the address goes back to `AA_0` (the operation's first address), `AGR` (PORT_Y) + 1, the counter reloads | `Advance(addr, start, store)` |
| Clip | `GFX_CLIP = (CTRL0 & CTRL1 == AGR7) or (CTRL2 & CTRL3 == AO7)`, ORed into `BLK_MR` and `GLISSER` of the graphics pages `#50-#5F`: neither the RAM store nor the video RAM store happens. `AO` follows the CPU's address when no operation runs, so the CPU's own stores are clipped as well. Reads are not | `SprinterAccelerator::ClipsGraphicsWrite`, called from `SprinterMemory::OnWrite` beside the transparency test |
| Ports | codes `#80` / `#81` are free in Standard; the program enters them into the port table (page `#40`) itself. `TmKonf.exe` `accLib.openPorts`: `#C49D/#C69D` <- `#80`, `#C49E/#C69E` <- `#81` (ports `#7785` / `#7786`, DOS off, both directions) | the port table is RAM: nothing to add |

TTD: the module's state (772 bytes) is the module state of the PLD blob. The PLD blob is variable-size: its room is the
active module's state, at least 16 bytes (the room of every blob before TMK). Standard and Game sessions keep the same
bytes, older `.ttd` files load, and only a session on TMK carries the 772 bytes.

## 3. Worked example (TmKonf.exe)

`OUT (#7786),31 : OUT (#7785),#30` sets the width to 32 bytes per row, with the rectangle and length x 4. Then
`LD D,D : LD A,0` sets the length to 1 024 bytes. `LD L,L : CP (HL) : LD B,B` reads the sprite into the buffer.
`LD L,L : LD (DE),A : LD B,B` then stores it as 32 rows of 32 bytes from DE, with PORT_Y + 1 per row. With bit 0 also
set, the rows whose PORT_Y has bit 7 equal to bit 1 are not written, so the program clips a sprite at the top or bottom.

## 4. Where the description and the AHDL differ (the AHDL is followed)

1. **No rectangle for fills.** The description says "copy into memory / fill". `RECT_ENA` needs `ACC_DIR1`, and only the
   copy modes have it (`LD L,L` and, without effect, `LD A,A`). A fill (`LD C,C`) with bit 4 stays one line.
2. **The row counter is not restarted per operation.** It reloads only on an access to `#81` and at a row end, so a
   program writes `#81` before every rectangle (TmKonf does).
3. **One more step after the last store.** The model makes one address step per access, the last access included, as
   the vertical modes do for PORT_Y. A rectangle whose length is a whole number of rows ends with PORT_Y one row past
   the last row and the counter reloaded.

## 5. Open

- Check 4.3 and the row length (`ACC_RECT_W` + 1) against a board or the author's MAME build. The AHDL timing
  (`AA_ENA` once per access) was read, not simulated.
- A Sprinter session with this firmware: there is no switch to start configured with TMK. The module comes only from a
  load of the stream.
