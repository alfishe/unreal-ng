# Instant-play TSFM .sna snapshots — a builder's guide

> **What you get.** A 48K `.sna` that, the moment it loads in any emulator
> with a TurboSound FM device (2×YM2203), starts playing a TFM Music
> Compiler tune — no tape, no loader, no menu, no keypresses. Load it, hear
> it. This guide shows the 30-second path with the ready pipeline, then
> rebuilds the recipe from first principles so you can wrap **any**
> `TFMcom`-format tune, not just the ones from `TSFM-EL.TAP`.

Everything here was proven on the 106-tune `TSFM-EL.TAP` compilation: 106
snapshots generated, 106 verified playing via the emulator's FM state
endpoints. The player itself is documented instruction-by-instruction in
[`docs/disasm/software/tfmplayer/`](README.md)
(its sources are not published anywhere — that listing is the reference).

---

## 1. The moving parts

| piece | where it lives | size | notes |
|---|---|---|---|
| Player | `25000` (`0x61A8`) | 1,883 B | position-dependent; must not be moved |
| Tune | `32768` (`0x8000`) | 389–21,885 B | `TFMcom1.12` module, version char at `tune+9` |
| Stack/PC | `0xFF00` | 2 B | the 48K SNA "PC slot" (see §3) |
| `lnxdata` | 31000 | — | **not needed** — loader graphics, unreferenced |

The player's contract (full listing: `tfmplayer.asm`):

```asm
0x61A8  ld hl,08000h      ; tune address is HARDCODED
        call init         ; version check, pattern relocation, chip reset
        ei                ; player enables interrupts itself
0x61AF  halt              ; one interrupt = one frame
        call frame        ; interpret all 6 channels -> YM2203 register writes
        ...               ; border heartbeat + any-key exit
```

So a snapshot only has to: place player + tune, point PC at `0x61A8`, and
leave interrupts masked. The player does the rest — including the `EI`.

## 2. The 30-second path (batch pipeline)

With the session pipeline (`scratch/tsfm-snapshots/`, generated artifacts in
`scratch/tsfm-snapshots/sna/`):

```bash
# 1. one .sna per tune, named NN_<tap-block-name>.sna + manifest.json
python3 scratch/tsfm-snapshots/extract_tsfm_sna.py

# 2. batch-verify in the running emulator (WebAPI on :8090)
python3 scratch/tsfm-snapshots/verify_sna.py
```

`verify_sna.py` reports `106/106 passed` on the stock tape. Each snapshot is
49,179 bytes (27-byte header + 48K RAM) and is byte-identical in structure to
what §4–§5 build by hand.

## 3. 48K SNA format in one screen

A 48K `.sna` is `27 header bytes + 48K RAM` (`0x4000..0xFFFF` stored
contiguously, banks 5,2,0,1):

| off | field | value we write | why |
|---|---|---|---|
| 0 | I | `0x3F` | typical ROM-environment value; unused under IM1 |
| 1–8 | HL',DE',BC',AF' | `0` | player doesn't read them |
| 9–10 | HL | `0x8000` | tune base (entry reloads it anyway) |
| 11–14 | DE,BC | `0` | — |
| 15–18 | IY,IX | `0x5C3A` | ROM ISR reads `(IY+xx)` sysvars — keep valid |
| 19 | IFF | `0` (DI) | player does the `EI` before `HALT` |
| 20 | R | `0` | irrelevant under IM1 |
| 21–22 | F,A | `0` | — |
| 23–24 | SP | `0xFF00` | points at the PC slot (below) |
| 25 | IM | `1` | player expects IM1 interrupts |
| 26 | border | `0` | black; the loop paints its own heartbeat |

**The 48K trick:** the format has *no PC field*. The loader sets SP, pushes
nothing, and executes a `RET` — so the first two bytes **at the address SP
points to** become PC. We therefore write `A8 61` (25000 little-endian) into
RAM at `0xFF00` and set SP to `0xFF00`. After the pop, SP=`0xFF02` — still far
above any tune, and the player never pushes more than a few bytes anyway.

(128K `.sna` v2 does have a PC field at offsets 30/31 plus paging state;
we deliberately stay 48K: every TSFM-capable machine accepts it, and there is
no banking to get wrong.)

## 4. The state you are freezing

The snapshot captures the machine *at the player's front door*:

- **RAM**: player poked at 25000, tune poked at 32768, everything else zero
  (screen shows attribute-zero black; the play loop repaints the border only).
- **CPU**: PC=25000 (via the stack slot), SP=0xFF00, IM1, `IFF1=0`,
  IY/IX=`0x5C3A`.
- **Sound chips**: both YM2203s still at power-on reset — no register writes
  have happened yet. `INIT` runs its full mute/rearm sweep as the first thing
  it does, so playback starts from a deterministic, silent state. (Same
  convention as `testdata/sound/tsfm/tech_support.sna`: *paused at player
  entry*.)

Why interrupt-masked matters: with `IFF1=0` nothing can fire before the
player has finished `INIT`; with it set, an INT could hit before `I` is even
meaningful. The one-instruction window between `ei` and `halt` is exactly the
player's own design assumption.

## 5. Build one by hand (any tune, ~40 lines of Python)

Requirements on the tune binary: it must start with the `TFMcom` signature
(the exact tag for v1.12 modules is `TFMcom1.12`), carry version character
`'2'` at offset 9, and fit below the PC slot:

```text
32768 + len(tune) < 0xFF00      ->  len(tune) < 0x7F00 (32,512) bytes
```

```python
#!/usr/bin/env python3
"""make_tsfm_sna.py — wrap the TFM player + one tune into an instant-play .sna"""
import sys
from pathlib import Path

PLAYER = Path("docs/disasm/software/tfmplayer/player_org61A8.bin")
PLAYER_BASE, TUNE_BASE, STACK_TOP = 25000, 32768, 0xFF00

def build(tune: bytes, player: bytes) -> bytes:
    assert tune[:8] == b"TFMcom1.", "not a TFMcom module"
    assert tune[9] == ord("2"), "old-format module (1-in-6 divider) - expect slow playback"
    assert TUNE_BASE + len(tune) < STACK_TOP, "tune would overwrite the PC slot"

    ram = bytearray(0x10000)                 # scratch copy, 0x4000+ kept
    ram[PLAYER_BASE:PLAYER_BASE + len(player)] = player
    ram[TUNE_BASE:TUNE_BASE + len(tune)] = tune
    ram[STACK_TOP] = PLAYER_BASE & 0xFF      # PC low  ── the 48K SNA
    ram[STACK_TOP + 1] = PLAYER_BASE >> 8    # PC high    "stack slot"

    h = bytearray(27)
    h[0] = 0x3F                              # I
    h[9], h[10] = 0x00, 0x80                 # HL = tune base
    h[15] = h[16] = h[17] = h[18] = 0x3A     # IY = IX = 0x5C3A
    h[19] = 0x00                             # IFF: DI - player does EI
    h[23], h[24] = STACK_TOP & 0xFF, STACK_TOP >> 8   # SP
    h[25] = 1                                # IM1
    return bytes(h) + bytes(ram[0x4000:])

if __name__ == "__main__":
    tune = Path(sys.argv[1]).read_bytes()
    out = Path(sys.argv[2])
    out.write_bytes(build(tune, PLAYER.read_bytes()))
    print(f"{out}: {27 + 0xC000} bytes, tune {len(tune)} B @32768, player 1883 B @25000")
```

Usage: `python3 make_tsfm_sna.py mytune.bin mytune.sna`. To pull a tune out
of a TAP/TRD image instead, adapt the ~20-line `parse_tap()` from
`extract_tsfm_sna.py` (header block gives name/type/start, the following
`0xFF` block is the payload).

## 6. Verify it actually plays (emulator WebAPI)

The proof that a snapshot works is *FM activity*, not a pretty border:

```bash
# fresh instance with the TSFM device (Pentagon ini ships TurboSound=FM)
curl -s -X POST localhost:8090/api/v1/emulator/start -d '{"model":"PENTAGON"}'

curl -s -X POST localhost:8090/api/v1/emulator/$ID/snapshot/load \
     -H 'Content-Type: application/json' -d '{"path":"mytune.sna"}'
```

then sample twice, ~2 s apart:

| endpoint | healthy reading |
|---|---|
| `GET /state/audio/fm` | any chip shows `keyed_channels`/`sounding_channels` > 0, or the tuple differs between samples (music moving) |
| `GET /registers` → `special.pc` | `pc < 0x4000` (frame-boundary reads land in the ROM ISR) **or** `25000 ≤ pc ≤ 26883` (inside the player block) |

Pass = (playing or active) **and** both PC samples alive. That is exactly the
rule `verify_sna.py` applies to all 106 snapshots.

**Hardware requirement:** the machine config must have the TSFM device
(status-word semantics on `0xFFFD`). On a legacy 2×AY TurboSound the player's
busy-poll reads the AY mixer (`0xFF`, bit7 set) and parks forever at
`0x6293/0x6295` — silent *and* frozen. In unreal-ng use the Pentagon config
with `TurboSound=FM`.

## 7. Troubleshooting

| symptom | cause | fix |
|---|---|---|
| Loads, black screen, total silence, PC stuck at `0x6293/0x6295` | machine has legacy TurboSound (AY register readback, not status) | use a TSFM-capable config (Pentagon + `TurboSound=FM`) |
| Plays but sounds like a stuttery 1/6 of the song | old-format module: `tune+9 != '2'` — INIT kept the frame divider | only `TFMcom1.12` tunes play at full rate |
| Garbage/noise then crash | tune not aligned at `32768` (relocation offsets are tune-relative) | always poke the tune exactly at `0x8000` |
| Nothing at all, PC runs off into ROM | PC slot clobbered — tune overlaps `0xFF00`, or SP wrong | check `32768 + len(tune) < 0xFF00` and `A8 61` at `0xFF00` |
| Plays once, then machine resets on keypress | the player `RET`s to its caller on key exit — a snapshot has garbage under the stack | harmless in emulation (reset); embed a proper caller if you need a menu |
| Snapshot rejected by other emulators | 128K-only loader, or padding/size off | file must be exactly 49,179 bytes; §3 header |

## 8. Beyond snapshots: the TR-DOS `.scl` package

Snapshots are per-tune. To ship the *whole* compilation as two bootable
TurboSound FM disks (55 files each: `boot` B-file, `player`, `T01..T53`),
the same layout is packaged as `.scl` with a BASIC menu that loads player +
tune through a small machine-code stub. Two hard-won facts from that work:

1. **Program-context `LOAD ... CODE` never reaches TR-DOS in this emulator**
   — the ROM is gated on `0x3Dxx` M1 fetches only while `p7FFD` bit 4 is set,
   and BASIC always runs with bit 4 clear. The boot therefore calls the
   TR-DOS service API directly from machine code:

   ```asm
   IN A,(7FFDh) / OR 10h / OUT (7FFDh),A  ; arm the gate
   ; name -> 5CDDh..5CE5h (8 chars + type), (5D06h)=9
   XOR A / LD (5CF9h),A / LD (5D10h),A    ; LOAD flags (0 = not VERIFY)
   LD A,1 / LD HL,addr / LD DE,0 / LD C,0Eh / CALL 3D13h
   ; restore latch, EI, RET
   ```

2. **`0xFFFD` status semantics strike again**: the stub itself is proven
   end-to-end (an isolation disk loads a test payload byte-identical to its
   target address and returns cleanly to BASIC); wiring it into the final
   two-disk menu boot is the remaining step of that work stream.

The pipeline lives in `scratch/tsfm-snapshots/scl/` (`build_scl.py`,
`build_boot_tsfm.py`, `run_disk_test.py`, `verify_scl.py`) — scratch is
git-ignored, so §5's self-contained recipe is the durable path for snapshots;
the TR-DOS 5.04T ROM analysis behind the service-call recipe was done against
a z80dasm disassembly (session artifact; see also the port tables under
`docs/ports/`).

## 9. References

- [`docs/disasm/software/tfmplayer/`](README.md) —
  the player's annotated disassembly, entry points, SMC map, provenance
- [`docs/file-formats/music/tfmcom-module.md`](../../../file-formats/music/tfmcom-module.md) —
  the recovered TFMcom module format (header, opcode grammar, statistics)
- `docs/inprogress/2026-09-10-turbosound-fm/verification/player-entry-points.md`
  — harness-level contract (entry table, port protocol, legacy-TS park)
- `core/tests/_helpers/tsfmplayerharness.{h,cpp}` — in-process test harness
  (poke order, port tracing, traffic hashing)
- SNA format: the 27-byte header and the 48K PC-on-stack convention are as
  implemented by every major emulator (cross-checked against unreal-ng's
  snapshot loader and `testdata/sound/tsfm/tech_support.sna`)
- TFM Music Compiler is **not** published in source form (search verdict in
  the tfmplayer README); Shiru's similarly-named TFM Music Maker (Genesis
  YM2612, TF0/TFE) is a different toolchain — don't cross the streams

