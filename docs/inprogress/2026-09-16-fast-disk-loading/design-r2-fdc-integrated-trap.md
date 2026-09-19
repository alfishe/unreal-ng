# Fast Disk Loading — Design r2: FDC-Integrated Trap

| | |
|---|---|
| **Status** | Proposal r2, 2026-09-18. Supersedes the Layer A/B mechanics in `design.md` r1 and `implementation_plan.md`. |
| **Basis** | [reference-emulators-analysis.md](reference-emulators-analysis.md): Unreal Speccy trap, Xpeccy+ timing compression, ZXMAK2/UnrealSpeccyP |
| **Feature** | `fastdisk` (alias `fdisk`), unchanged |

## 1. Why r2

The r1 implementation does not work (see the gap report in the analysis document). The root causes are structural:

- The trap lives in `DiskFastLoad` and reaches into the FDC from outside, so it makes assumptions about the FDC's internal state
  (`_bytesToRead == 256`) that are wrong, and it overwrites the state machine (`S_IDLE`, own INTRQ), breaking CRC and multi-sector follow-up.
- Arming depends on a ROM signature (`00 C3 69 2F` at `$3D13`) that does not match the bundled ROMs.
- Timing compression is spread over ad-hoc edits, applies when merely enabled (not armed), and leaves per-byte pacing untouched.

r2 keeps the same two layers but changes who owns what: **the FDC owns all state changes; the trap only tells it "the CPU is about to drain the sector".**

## 2. Glossary

- **Armed**: fast loading is enabled *and* the machine is currently running standard TR-DOS code, so shortcuts are safe.
- **Drain**: hand over all remaining bytes of the current sector at once instead of one byte per DRQ.
- **DRQ / INTRQ**: FDC "data byte ready" / "command finished" lines. Beta 128 port `$FF`: bit 6 = DRQ, bit 7 = INTRQ.
- **BUSY window**: short period after a command during which the FDC status shows BUSY; some loaders wait for it to appear.
- **E flag**: command bit requesting the 15 ms head-settle delay.

Worked example: a `LOAD` reads a 256-byte sector. Normally the ROM loop waits for DRQ, executes `INI` (one byte), and repeats 256 times,
each byte arriving ~224 T-states apart. With the trap, at the first `INI` the FDC hands all 256 bytes over immediately, then finishes the
command through its normal CRC/INTRQ path; the ROM sees INTRQ on its next poll and returns.

## 3. The ROM loop being hooked

Identical in `trdos.rom`, `trdos503.rom`, `trdos504t.rom`:

```
3FE5: IN A,($FF) / AND $C0 / JR Z,$3FE5    ; wait for INTRQ|DRQ
3FEB: RET M                                 ; INTRQ -> done
3FEC: INI                                   ; byte from $7F to (HL), HL++, B--
3FEE: JR $3FE5
```

The loop is not counted by `B`; it runs until INTRQ. `B` is only the port high byte and wraps.

## 4. Components

### 4.1 Arming — `DiskFastLoad::IsArmed()` (single gate)

Armed when all hold:
1. `FeatureManager::isEnabled(kFastDisk)` (already false during TTD recording).
2. `CF_TRDOS` set and `pBetaDisk` present.
3. Bank 0 is ROM.

No `$3D13` signature. Per-trap opcode checks (below) are the only ROM-content gate, as in Unreal Speccy.
`WD1793::isFastDiskEnabled()` is replaced by a call to `IsArmed()`, so software running outside TR-DOS keeps authentic timing.

### 4.2 Read trap (Layer B)

Hook stays in `Z80::Z80Step()`: `pc == 0x3FEC && pDiskFastLoad`. `DiskFastLoad::HandleSectorDrainTrap(Z80&)`:

1. Check armed and opcode bytes at `$3FEC`/`$3FED` are `ED A2` (already the case of the existing `CheckROMSignature` second half).
2. Ask the FDC: `fdc->drainSectorRead(sink)` — succeeds only if a Read Sector command is in the byte-transfer phase.
3. For each byte from the sink: `cpu.wd(HL, byte)`, `HL++`, `B--`.
4. `PC += 2` (skip the `INI`). No `cpu.t` adjustment; `m1_pc` handling as today.

`B == 0` and `getBytesToRead() == 256` are **not** required.

### 4.3 FDC side — `WD1793::drainSectorRead`

Preconditions: command is Read Sector (single or multiple), FSM is in the byte-transfer phase (`S_READ_BYTE` pending), `_rawDataBuffer` valid.

Actions, in order:
1. If DRQ is pending: emit `_dataRegister`, `clearDrq()`, set `_drq_served = true`. (This is the byte the ROM loop was about to `INI`.)
2. Emit the remaining `_bytesToRead` bytes from `_rawDataBuffer`, advancing the pointer.
3. Set `_bytesToRead = 0`.
4. **Do not** change `_state`, do not touch status bits, do not raise INTRQ.

`processReadByte()` gets one guard: if `_bytesToRead <= 0` on entry, `transitionFSMWithDelay(S_READ_CRC, shortDelay)` and return without reading another byte. The existing `S_READ_CRC` handler then performs CRC verification, multi-sector follow-up from `_operationFIFO`, and INTRQ. This is the equivalent of Unreal's "`rwlen = 0`, let `process()` finish".

Consequence: `CMD_MULTIPLE` (used by TR-DOS 5.04T COPY paths), lost-data flags, and status semantics all stay correct.

### 4.4 Timing compression (Layer A) — one function

All delay decisions go through one helper used by `transitionFSMWithDelay` and the per-byte scheduling:

```
size_t WD1793::fastDelay(DelayKind kind, size_t authentic)
```

Active only when `IsArmed()`. Policy (derived from Xpeccy+ / Unreal):

| Kind | Fast behaviour | Reason |
|---|---|---|
| Command start (BUSY window) | Keep a minimum (~20 µs equivalent, Xpeccy `VG_START`) | Loaders/Profi BIOS wait to see BUSY rise |
| Step / restore | Small constant (Xpeccy 20 µs) | Head movement dominates load time |
| E=1 settle | Keep 15 ms | Loaders use this time (Xpeccy comment) |
| Verify | Skip | Unreal/Xpeccy skip |
| Rotational latency to sector | ~100 T-states (Unreal `find_marker`) | Optional teleport; alternatively rely on fast rotation |
| Per-byte cell (`_tstatesPerByte`) | Small constant (Xpeccy 500 ns) | Currently untouched — main missing piece |
| Index-hole wait | Skip on no-disk/unformatted paths only; keep revolution limits for Record-Not-Found | Keep error semantics |

**DRQ hold.** In `processReadByte` (and the write equivalent), when armed and `!_drq_served`, do not set lost-data immediately: reschedule
by one byte cell until a per-command budget of one authentic revolution has elapsed (Xpeccy `hold`, Unreal `notready()`). This protects
loaders that read late (e.g. after playing music) and any read that misses the `$3FEC` trap.

### 4.5 Optional follow-ups (same structure)

- **Write drain at `$3FD1`** (`ED A3` OUTI): symmetrical `WD1793::drainSectorWrite`, leave one byte for the FDC to finish, `PC += 2`.
- **Seek/delay-loop skips** (`$3DFD`, `$3EA0`, `$3E01`): only if profiling shows the compressed FDC timing still leaves visible delay.
  Each needs its own opcode-byte signature; keep in a small table in `DiskFastLoad`.

## 5. What changes versus the current code

| Area | Current | r2 |
|---|---|---|
| Arming | Requires `00 C3 69 2F` at `$3D13` (never matches) | Feature + `CF_TRDOS` + ROM bank; `INI` opcode check per trap |
| Read trap gate | `B == 0` and `_bytesToRead == 256` | FDC in byte-transfer phase |
| Drain result | Forces `S_IDLE`, raises INTRQ | Sets `_bytesToRead = 0`; FDC finishes via `S_READ_CRC` |
| First byte | Skipped (already in data register) | Emitted first |
| Timing | Rotational latency, step, verify only; when merely enabled | All delays via `fastDelay`, incl. per-byte; only when armed |
| DRQ hold / BUSY window / E settle | Missing (claimed in plan) | Implemented |
| `cpu.t += 256` | Synthetic | Removed |

Files touched: `diskfastload.{h,cpp}`, `wd1793.{h,cpp}` (drain, `processReadByte` guard, `fastDelay`), `z80.cpp` hook unchanged.
Feature registration, CLI/WebAPI/Qt surfaces from r1 stay as they are.

## 6. Test plan

1. **Arming**: armed with bundled `trdos.rom`, `trdos503.rom`, `trdos504t.rom`; disarmed when feature off, TR-DOS not paged, bank 0 RAM, or `INI` bytes absent.
2. **Drain correctness**: single-sector read via trap produces bytes identical to a non-trapped read; `HL`, `B`, `PC` correct; FDC ends with INTRQ set, no lost-data, correct status.
3. **Multi-sector**: `CMD_MULTIPLE` read across a track through the trap; overrun-at-end-of-track behaviour unchanged.
4. **Differential timing**: same `LOAD` armed vs. disarmed — identical memory result, armed completes in a small fraction of frames.
5. **Late reader**: a loader that starts reading DRQ after a delay less than one revolution gets no lost-data when armed.
6. **BUSY window / E flag**: after a command, BUSY is observable; E=1 command still takes ~15 ms.
7. **Non-TR-DOS safety**: direct-port FDC access with CP/M or custom ROM keeps authentic timing.
8. **TTD**: feature auto-off while recording (existing behaviour).
9. Full `core-tests` run; zero compiler warnings per the project policy.

## 7. Risks

- Byte-cell compression can expose loaders that rely on DRQ pacing; the DRQ hold and preserved BUSY/E windows mitigate this (same trade-off as Xpeccy+).
- `S_READ_CRC` guard assumes it is reachable directly after a drain; verify on multi-sector and error paths.
- Custom TR-DOS variants (Profi, Scorpion ROMs) will not match traps and fall back to compressed-timing-only, which is the intended behaviour.
