# Sprinter demo runner

Runs Sprinter programs from a hard-disk image one after another, through a running unreal-qt's
WebAPI, and sorts each one into a verdict. A run that used to take half an hour of hand-driven
key presses takes about a minute of wall time per program: the machine is paused and driven with
`run_frames` at full host speed.

Each program goes through three steps:

1. **boot** — create a `SPRINTER` instance (or reuse `--id`), insert the disk unless it is
   already in `ide0.master`, reset, and run frames until Flex Navigator waits in its idle loop.
2. **navigate** — walk Flex Navigator's left panel to the file and press Enter. The panel order
   comes from the disk's own directory listing (`..`, folders, then files, by name), read with
   mtools from a raw copy of the same disk.
3. **probe** — run the program for `--seconds` of emulated time, take a screenshot every two
   seconds and classify it (the picture is compared by `/state/screen/digest` `combined`: video
   memory + border):

| Verdict | Meaning |
|:--|:--|
| `running` | the picture changes |
| `static` | the picture does not change and the CPU is not parked in a `HALT` |
| `waits-for-int` | parked in a `HALT` with no picture change; with IM 2 the result lists the vectors the program installed, so an interrupt source nobody raises shows up at once |
| `exited-to-fn` | back in Flex Navigator's idle loop |
| `pld-reload` | the program reloaded the PLD (code `#2E`, seen in the PLD journal): the machine restarted with the configuration the loaded bitstream chose. The result names it: `pld_module` / `pld_selected_by` (from `/state/sprinter`: `Game` by `full_hash` for `GAME_00.ACX` / `GC.BIN`, `Standard` by `unknown_bitstream` for a stream nobody knows), the journal's `pld_configured` events, and `after_reload` (`running`, `static`, `exited-to-fn`: what the machine did on the new configuration) |

A `running` verdict means only that the picture changes: look at the screenshots (the contact sheet)
before calling a program good.

Every result also carries the Covox-Blaster state (mode, rate, interrupt requests).

## Requirements

- unreal-qt with the WebAPI on (`UNREAL_WEBAPI_PORT`)
- Python 3, mtools (`mdir`), Pillow (optional: contact sheet)
- the disk the emulator mounts (CHD or raw) and a raw copy of the same disk for the listing (the
  FAT partition starts at byte 32256)

## Usage

```bash
tools/machines/sprinter/demo-runner/demo-runner.py --port 8097 \
    --hdd ~/Downloads/mame_release_v306_25.05.2025/IMG/sp_hdd_sys.chd \
    --listing-image ~/Downloads/sprinter/hdd/sp_hdd_sys.img \
    --out scratch/demos DEMOS/BALLS/balls.exe DEMOS/BADAPPLE/badapple.exe
```

| Option | Default | |
|:--|:--|:--|
| `--all FOLDER` | | every `.exe` below a folder on the disk |
| `--start-at PATH` | | skip the programs before this one (resume a broken run) |
| `--bios` | `3.07` | firmware for a new instance |
| `--id` | | an existing `SPRINTER` instance instead of a new one |
| `--seconds` | 8 | emulated seconds per program |
| `--out` | `scratch/demos` | screenshots, `results.json`, `contact-sheet.png`, kept `.ttd` sessions |
| `--ttd` / `--no-ttd` | on | record TTD from the Enter on each program (development mode, write journal on) |
| `--ttd-keep` | `failures` | `failures`: save the session as `<out>/<program>.ttd` for every verdict other than `running`; `all`: for every program |
| `--ttd-history-frames` | 8 820 (about 3 min) | rolling history limit while recording |

### TTD on every run

Every program runs under a TTD recording, so a hang can be rewound and inspected instead of
reproduced: the recording starts right before the Enter that launches the program (after the boot
and the panel walk) and stops after the probe. A kept session (`"ttd"` in the result, about 50 MB
for 8 s) loads into a `SPRINTER` instance with `POST /ttd/load {"path": ...}`; then seek, step back
and read the port events (`POST /ttd/port-events`) and the PLD journal
(`GET /state/sprinter/pld-journal`). Recipe:
[`.recipe/analysis/ttd-recording.md`](../../../../.recipe/analysis/ttd-recording.md),
[`.recipe/analysis/sprinter-ttd.md`](../../../../.recipe/analysis/sprinter-ttd.md). For a hand-driven
run outside the runner, start the recording first:

```bash
curl -s -X POST "$BASE/emulator/$ID/ttd/start" -H 'Content-Type: application/json' \
     -d '{"history_limit_frames": 8820}'
```

The layers are importable on their own (`Emu`, `boot`, `navigate`, `probe`), for scripts that
boot once and then type a command line in Flex Navigator, e.g. a Spectrum-mode launch:

```python
emu.call("POST", "/keyboard/type", {"text": "\\zx\\spectrum.exe \\zx\\p128.zx \\trd\\across\\0.trd\n"})
```

Notes:

- A CHD is never written in place: the guest's writes live in the instance until the slot is
  saved or exported (see [`.recipe/media/sprinter-hdd.md`](../../../../.recipe/media/sprinter-hdd.md)).
