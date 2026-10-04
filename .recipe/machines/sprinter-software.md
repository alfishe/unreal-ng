# Recipe: run Sprinter software - native programs, TR-DOS, Spectrum demos and games

The shortest verified paths on a `SPRINTER` machine with the MAME-pack hard disk (DSS 1.71, Flex Navigator 1.15,
the Spectrum launcher in `C:\ZX`): a native Sprinter program, and a Spectrum TRD in the three launcher modes
SP (21 MHz), P128 (Pentagon 128, 3.5 MHz) and SC256 (Scorpion ZS 256). Verified 2026-10-03 on BIOS 3.07 BETA 1
(the default) through the WebAPI; the MCP tools take the same steps (`emulator_manage` create, `load_software` /
media insert, `type_input` type / tap, `inspect_state`). Machine details: [sprinter.md](sprinter.md); the hard
disk: [../media/sprinter-hdd.md](../media/sprinter-hdd.md).

**Record TTD before you start anything** (owner rule): a hang can then be rewound instead of reproduced
([../analysis/ttd-recording.md](../analysis/ttd-recording.md)). The media set is fixed while a recording runs, so
insert every disk first, then start the recording.

## 1. Boot to Flex Navigator (once)

```bash
BASE=http://localhost:8090/api/v1
ID=$(curl -s -X POST $BASE/emulator/start -H 'Content-Type: application/json' \
       -d '{"model":"SPRINTER"}' | jq -r .id)
curl -s -X POST $BASE/emulator/$ID/media/ide0.master/insert -H 'Content-Type: application/json' \
     -d '{"path":"/abs/path/sp_hdd_sys.chd"}'
curl -s -X POST $BASE/emulator/$ID/reset
# about 10 s of emulated time; Flex Navigator idles at PC #A441
curl -s -X POST $BASE/emulator/$ID/run_frames -H 'Content-Type: application/json' -d '{"count":500}'
curl -s $BASE/emulator/$ID/registers | jq .special.pc          # → 42049 (#A441)
# disks for the SC256 mode go in now (section 4), then:
curl -s -X POST $BASE/emulator/$ID/ttd/start -H 'Content-Type: application/json' \
     -d '{"history_limit_frames": 8820}'                          # about 3 minutes of history
```

A CHD is never written in place (the guest's writes stay in the instance). Paused machines advance with
`run_frames` at full host speed; a running one just runs.

## 2. A native Sprinter program (`.EXE`)

Type its full path on Flex Navigator's command line. DSS starts it in its own folder, so data files next to it
are found:

```bash
curl -s -X POST $BASE/emulator/$ID/keyboard/type -H 'Content-Type: application/json' \
     -d '{"text":"\\demos\\balls\\balls.exe\n"}'
curl -s -X POST $BASE/emulator/$ID/run_frames -H 'Content-Type: application/json' -d '{"count":200}'
curl -s "$BASE/emulator/$ID/capture/screen?area=full&format=png&path=/abs/path/scratch/balls.png"
```

- `cd` is not a Flex Navigator command: the line `cd \demos\balls` does nothing. A `.BAT` runs by name from the
  panel's current folder (walk there first, e.g. with the demo runner's `navigate`).
- The program's own exit key (often Esc) returns to Flex Navigator.
- Many programs in one go, with verdicts, screenshots and kept TTD sessions:
  [tools/machines/sprinter/demo-runner](../../tools/machines/sprinter/demo-runner/README.md)
  (`--all DEMOS`); the results of the MAME-pack `DEMOS/` are in
  [docs/inprogress/2026-09-28-sprinter/demo-status.md](../../docs/inprogress/2026-09-28-sprinter/demo-status.md).

## 3. A Spectrum TRD in SP or P128 mode (from the hard disk)

The launcher loads the TRD from the hard disk into the BIOS RAM disk, so no floppy is needed:

```bash
# SP = Sprinter mode, 21 MHz:   \zx\sp.zx      P128 = Pentagon 128, 3.5 MHz:   \zx\p128.zx
curl -s -X POST $BASE/emulator/$ID/keyboard/type -H 'Content-Type: application/json' \
     -d '{"text":"\\zx\\spectrum.exe \\zx\\p128.zx \\trd\\scroller.trd\n"}'
curl -s -X POST $BASE/emulator/$ID/run_frames -H 'Content-Type: application/json' -d '{"count":400}'
#   → the 128 menu "Sprinter": TR-DOS / Hardware / 128 BASIC / Calculator / 48 BASIC / Options
curl -s -X POST $BASE/emulator/$ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"enter","frames":6}'
curl -s -X POST $BASE/emulator/$ID/run_frames -H 'Content-Type: application/json' -d '{"count":150}'
#   → Sprinter TR-DOS v.7.03, prompt A>
curl -s -X POST $BASE/emulator/$ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"r","frames":6}'
curl -s -X POST $BASE/emulator/$ID/run_frames -H 'Content-Type: application/json' -d '{"count":26}'
curl -s -X POST $BASE/emulator/$ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"enter","frames":6}'
#   → RUN: TR-DOS starts the disk's "boot"
curl -s $BASE/emulator/$ID/state/sprinter/zx-mode | jq .active    # → true
curl -s $BASE/emulator/$ID/state/sprinter | jq '.clock | {ratio, mhz}'   # P128: 1, "3.5"; SP: 6, "21"
```

- Hold every key for **6 frames** (`"frames":6`): TR-DOS misses a 3-frame tap.
- `R` in keyword mode is `RUN`; on a disk without `boot`, `LIST` (`k`, Enter) shows the catalog and
  `RUN "name"` starts a file.
- The images on the MAME-pack disk are in `C:\TRD` (`scroller.trd`, `atarin.trd`, `shock.trd`, games as `.SCL`
  too). Any `.trd` / `.scl` path on the hard disk works.
- Enter on a `.trd` in the Flex Navigator panel also launches it, but in **SP** mode (`sp.zx`) whatever
  `C:\FN\FN.EXT` says (open item); type the command line to choose the mode.
- Back to DSS: Ctrl+Alt+Del (the modes carry `/ret-fn`).
- Multicolor demos timed for a Pentagon belong in P128 (3.5 MHz, the PLD's INT 17 990 T before the first
  Spectrum cell); SP runs them at 21 MHz.

## 4. Scorpion ZS 256 mode (SC256, floppy only)

The Scorpion TR-DOS of `sc256.zx` reads only the real floppy (drive A), not the RAM disk. Insert the image
before the TTD recording, launch the mode without an image, and the menu's first entry boots the disk:

```bash
curl -s -X POST $BASE/emulator/$ID/media/fdd.a/insert -H 'Content-Type: application/json' \
     -d '{"path":"/abs/path/scratch/scroller.trd"}'      # a host file: copy it off the disk first, e.g.
                                                          # mcopy -i sp_hdd_sys.img@@32256 ::/TRD/scroller.trd scratch/
curl -s -X POST $BASE/emulator/$ID/keyboard/type -H 'Content-Type: application/json' \
     -d '{"text":"\\zx\\spectrum.exe \\zx\\sc256.zx\n"}'
curl -s -X POST $BASE/emulator/$ID/run_frames -H 'Content-Type: application/json' -d '{"count":400}'
#   → the Scorpion menu "1992-94 Scorpion ZS 256": 128 TR-DOS / 128 BASIC / ... / 48 TR-DOS
curl -s -X POST $BASE/emulator/$ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"enter","frames":6}'
#   → "128 TR-DOS" boots the floppy's "boot" at once (no RUN needed)
```

- SC256 runs at 21 MHz with 312 lines (`/turbo /lines312`). Its `/sc-int` option is never applied: the launcher
  parses `int-sc` (open item, upstream).
- ORIGIN.ZX (`\zx\origin.zx`, 3.5 MHz, 312 lines, the original waits) also reads only the floppy; its 128 menu
  has TR-DOS as the fifth entry (Down x 4, Enter).

## 5. BIOS 3.06 / 3.07 without the hard disk

Esc at the BIOS boot prompt starts the BIOS's own Spectrum 128 menu (no launcher, 21 MHz); TR-DOS then reads
drive A. Details and the BIOS 3.04 path: [sprinter.md](sprinter.md) §4.
