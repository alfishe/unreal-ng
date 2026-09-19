# Fast Disk Loading — Reference Emulator Analysis and Gap Report

Findings from reading Unreal Speccy, UnrealSpeccyP, ZXMAK2 and Xpeccy(-plus), compared with the uncommitted
implementation in this repo (`diskfastload.cpp`, `wd1793.cpp/.h`, `z80.cpp`).
Reference paths are under `/Volumes/TB4-4Tb/Projects/emulators/github/`. Line numbers are as read.
Unreal's `.cpp` files are cp1251/CRLF, so grep needs `LC_ALL=C grep -a`.

## Glossary

- **Trap**: the emulator notices the CPU reached a specific ROM address and does the work itself instead of running the code.
- **DRQ**: the FDC's "a data byte is ready" line. **INTRQ**: the FDC's "command finished" line.
- **`$FF` port**: Beta 128 status port. Bit 7 = INTRQ, bit 6 = DRQ.
- **E flag**: command bit asking for a 15 ms head-settle delay.
- **Teleport**: jump the disk rotation to the wanted sector instead of waiting for it to come around.

## Summary table

| Emulator | ROM trap | FDC timing compression | Default |
|---|---|---|---|
| Unreal Speccy | Yes (`$3FEC` INI, `$3FD1` OUTI, seek/delay skips) | Yes (`wd93_nodelay`, teleport) | Both on |
| UnrealSpeccyP | No | Code present, hardwired off (`wd93_nodelay = false`, line 28) | Off |
| ZXMAK2 | No (tape traps only) | Yes (`NoDelay`), port of Unreal | Off, except Quorum/Sprinter |
| Xpeccy / xpeccy-plus | No | Yes (`fdcturbo`), per-step nanosecond compression | User option |

## 1. Unreal Speccy (the only ROM trap)

### Hook dispatch (`z80_main.inl:112-140`)
Runs in `step()` before each instruction fetch. `CF_SETDOSROM` plus `pch == 0x3D` enters TR-DOS. `CF_LEAVEDOSADR`
(PC > $3FFF) or `CF_LEAVEDOSRAM` leaves it. While the DOS ROM is paged, `if (conf.trdos_traps) comp.wd.trdos_traps();`
runs on every instruction. Config: `[beta128] Traps` (default 1, `config.cpp:513`) and `Fast` (default 1, line 514);
GUI checkboxes `IDC_DISK_TRAPS`, `IDC_DISK_NODELAY`.

### `WD1793::trdos_traps()` (`wd93cmd.cpp:939-1009`)
There is **no ROM version check and no `$3D13` signature**. It requires PC >= $3DFD and matches opcode bytes in the
currently mapped ROM page.

| Trap | Address / signature | Action |
|---|---|---|
| Seek to neighbour track | PC=$3DFD, `$3DFD`=$3E and `$3DFF`=$0E | Emulate RET, A=0, C=0 |
| Seek to arbitrary track | PC=$3EA0, `$3EA0`=$06 and `$3EA2`=$3E | Emulate RET, A=0, B=0 |
| Delay loop | PC=$3E01, `$3E01`=$0D (DEC C) | A=C=1, no pop |
| Sector read drain | PC=$3FEC, `$3FED`=$A2 (INI), state S_READ (or state2==S_READ with state==S_WAIT) | See below |
| Sector write | PC=$3FD1, `$3FD2`=$A3 (OUTI), DRQ set, rwlen>1, state S_WRITE (or S_WAIT with state2==S_WRITE) | Bulk write |

**Read drain at `$3FEC`.**
1. If DRQ is pending, write the byte in the data register to (HL); HL++, B--; clear DRQ.
2. Copy the remaining `rwlen` bytes from the track buffer to (HL), decrementing `rwlen` and `B`, incrementing HL.
3. `pc += 2` to skip the INI.

No `B==0` check. The FDC state is not touched: `rwlen` reaches 0, and the normal `process()` then does CRC and INTRQ.
No T-states are added.

**Write drain at `$3FD1`.** Loops while `rwlen > 1`, writing `t.write(rwptr++, rm(HL))`, HL++, B--; leaves one byte for the
FDC to finish; `pc += 2` skips the OUTI.

### FDC no-delay mode (`conf.wd93_nodelay`, `wd93cmd.cpp`)
- E flag 15 ms delay skipped (line 112).
- Header-skip and per-byte delays in write sector / write track skipped (246, 371, 464); read sector data uses `next = time + 1` (301-303).
- Step rate and seek noise skipped (528-532). Restore 21 µs skipped (546). Verify 15 ms skipped (597).
- `getindex()` does not wait for the index hole (715).
- `find_marker()` (644-678) teleports the head, picks the nearest sector ID, sets `tshift` so that ID is under the head,
  and waits 100 T-states. Comment: delay=0 makes the FDC search forever when no ID matches. No ID found: up to
  `end_waiting_am` (5 revolutions).
- `notready()` (700): in no-delay mode, while DRQ is pending and within a 600 ms budget, requeue and add `ts_byte`.
  Comment: "fdc is too fast in no-delay mode, wait until cpu handles DRQ". Prevents spurious lost-data.
- Motor stops when neither HLD nor system bit 0x20 is set; in idle after 15 index pulses or a timeout.

### Port `$FF` (`WD1793::in()`, ~line 726)
`if (port & 0x80) return rqs | (system & 0x3F);` — live `rqs`, INTRQ bit 7, DRQ bit 6. Reading `$1F` clears INTRQ.

### Compatibility
Only the opcode-byte signatures gate the traps. Non-matching ROMs (Profi, custom DOS) silently fall back to the slow path.

## 2. UnrealSpeccyP (`devices/fdd/wd1793.cpp`)
No ROM hook (no `trap`, `3FEC`, `0x3D` addresses). Same no-delay skeleton as Unreal but `const bool wd93_nodelay = false;`
(line 28); the guards at lines 162, 269, 295, 366, 444, 472, 501, 571, 594, 617, 633, 727 are dead code. Always full timing.

## 3. ZXMAK2 (`ZXMAK2.Hardware.Circuits/Fdd/Wd1793.cs`)
No ROM trap; only the tape device has traps (`TapeDevice.cs:31,210,267,274`, `UseTraps`, `$056B`).
Port of Unreal's WD1793 with `NoDelay` (default false, line 113). Enabled by XML `noDelay` (`FddController.cs:108`),
GUI `CtlSettingsBetaDisk.cs:29`, and defaulted on only for Quorum (`machines.config:167,179`) and Sprinter (line 220).
- Same skips as Unreal at lines 199, 408, 549, 621, 686, 782, 809, 835, 915, 943, 970-994; `find_marker` teleport with
  `tshift`, `wait = 100` (915-950); `notready()` DRQ hold (969); `getindex()` skips index wait.
- Motor: `if (motor > 0 || nodelay) motor = next + 2*Z80FQ;` (line 199).
- Quorum fix (line 435): `if (nodelay && (cmd & 0xF0)==0xC0) end_waiting_am = next + 1;` for ~1992 disks whose CP/M waits too little after `C4`.
- Line 345: "KLUDGE: motor emulation to fix SCORPION 128 TRDOS dead lock".
- Ports `$1F`/`$FF` (masked) active only when `DOSEN || SYSEN` (`FddController.cs:205-216`).

## 4. Xpeccy / xpeccy-plus (`libxpeccy/vg93.c`, `diskif.c`, `fdc.h`)
No ROM hook. "Fast disk access" (ini `fdcturbo`, `config.cpp:124,925`, flag `FDC_FAST`, macro `turbo`, `fdc.h:30`)
only compresses FDC timing. The FDC runs in nanoseconds; `fdcSync(fdc, ns)` (`diskif.c:16-27`) counts `fdc->wait` down.

- `VG_START` = 20000 ns is **kept in turbo**: code that waits for BUSY to rise after a command (Profi BIOS) must see it (`vg93.c:8-14`).
- `VG_TURBO_STEP` = 20000 ns replaces the 6/12/20/30 ms step rate.
- `TURBOBYTE` = 500 ns replaces `bytedelay` (32000 ns DD, 16000 ns HD, `fdc.h:29`) in `waitADR` (52), seek data (373),
  read-track index wait (533, 551) and stop (172). `seekADR` uses 1 ns (71); read/write ADR 1 ns (414, 473).
- `VG_SETTLE` = 15 ms **kept for E=1 even in turbo** (line 334, "loaders use the time"). Verify settle in `vgchk00` skipped (201).
- Data transfer (`vgGetByte` 107-137, `vgSendByte` 78-96): wait = 1 ns per byte. If DRQ is still set, the FDC holds until
  `tns > bytedelay + hold` before flagging lost data. `hold` = `bytedelay * trklen` (one revolution) per command
  (`vgExec` ~560). Reason: the turbo disk spins far faster than a loader expects, so a loader that reads late (e.g. after
  playing music) would miss every byte. Unreal's fast mode does the same.
- No teleport: the head still rotates, only ~1000x faster. Index pulses are counted and the 5-9 revolution limits still apply.
- `$FF` (`diskif.c:78-79`): `(irq ? 0x80 : 0) | (drq ? 0x40 : 0)`. Write: `val&3` drive, `val&4` master reset, `val&8` block, `val&0x10` side (inverted), `val&0x40` MFM. Ports decoded only when DOS is on.
- Changelog `CHANGELOG.md:276`: Profi BIOS needs BUSY visible; CHORDOUT-style loaders work again; loaders waiting on E=1 settle keep working. uPD765 (`upd765.c`) has the same `turbo` macro.

None of the three shorten CPU T-states; traps just change registers/PC.

## 5. Actual TR-DOS ROM read loop (bundled ROMs)

Identical in `trdos.rom`, `trdos503.rom`, `trdos504t.rom`:

```
3FE5: DB FF       IN A,($FF)
3FE7: E6 C0       AND $C0
3FE9: 28 FA       JR Z,$3FE5     ; wait for INTRQ or DRQ
3FEB: F8          RET M          ; bit 7 (INTRQ) -> command done
3FEC: ED A2       INI            ; read data reg ($7F), (HL)=byte, HL++, B--
3FEE: 18 F5       JR $3FE5
```

Bytes at `$3D10`: `00 18 E7 00 18 E7 00 C3 69 2F`, i.e. `$3D13` = `00 18 E7 00`; the `00 C3 69 2F` sequence is at `$3D16`.

## 6. Gap report: current uncommitted implementation

1. **Trap never arms.** `CheckROMSignature` requires `00 C3 69 2F` at `$3D13`; bundled ROMs have `00 18 E7 00` there, so
   `IsArmed()` is always false. Unreal needs no such signature — only the `INI` bytes at `$3FEC`/`$3FED`.
2. **Byte-count gate is wrong.** `getBytesToRead() != 256` rejects the trap: when the loop reaches `INI`, DRQ is already set
   and the first byte is latched in the data register, so `_bytesToRead` is 255. `drainSectorRead` also never emits the
   data-register byte. No check that the FDC is in its read state.
3. **Drain breaks the FDC state machine.** It forces `_state = S_IDLE`, clears status bits and raises INTRQ itself. This skips
   CRC and the `CMD_MULTIPLE` follow-up queued in `_operationFIFO`. Unreal instead sets `rwlen = 0` and lets the FDC finish.
4. **`B == 0` gate is fragile.** Unreal has no such check and decrements `B` per byte.
5. **Timing compression is partial.**
   - `rotationalDelayToData` returns 100 and step/verify use 1 T-state, but per-byte pacing (`_tstatesPerByte`) is untouched, so a
     sector still costs a full 256-byte transfer.
   - No DRQ hold, so a slow CPU can lose data (Unreal `notready()`, Xpeccy per-command revolution budget).
   - No preserved BUSY window (`VG_START`) or E=1 settle, though the plan claims both.
   - `isFastDiskEnabled()` does not consult `IsArmed()`; compression applies to any loader whenever the feature is on.
6. **Missing traps** present in Unreal: seek/delay skips (`$3DFD`, `$3EA0`, `$3E01`) and the `OUTI` write drain (`$3FD1`).
7. **Synthetic time:** `cpu.t += 256` in the trap is arbitrary; references add none (fine, but be consistent).

## 7. Suggested direction (not yet implemented)

- Trap keyed on `INI` bytes at `$3FEC` plus FDC read state; emit the data-register byte first, then the rest of the buffer,
  decrementing `B` and advancing `HL`; leave the FDC to finish via its own CRC/INTRQ path.
- Compress `_tstatesPerByte` (Xpeccy: 500 ns/byte) rather than only rotational latency; keep BUSY window and E=1 settle.
- Add DRQ hold (one-revolution budget) so late readers do not get lost-data.
- Optionally add the write drain and the seek/delay-loop skips.
