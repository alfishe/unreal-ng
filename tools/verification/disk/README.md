# Disk image verification

Scripts for disk images that do not load or do not boot: one reads a DSK without the emulator, two drive unreal-qt over
the WebAPI. They were made for the CPDRead protection tracks (Licence To Kill, Captain Blood, Driller, Toobin and
others), which the DSK loader rejected and which then hung in a READ ID loop.

| Script | Needs | What it answers |
|:--|:--|:--|
| [`dsk-tracks.py`](dsk-tracks.py) | the file only | which tracks are not a plain format; for one track, the sectors, the marks inside the long dumps and the layout on the disk |
| [`disk-boot-check.py`](disk-boot-check.py) | unreal-qt | does each image insert, and what is on the screen after N frames (results table, screenshots, contact sheet) |
| [`fdc-probe.py`](fdc-probe.py) | unreal-qt | where a hanging load spins: registers and floppy controller per frame, the stack and the code it returns to |

`disklib.py` holds the shared WebAPI client.

## Running the emulator scripts

`--launch` starts unreal-qt offscreen with its WebAPI on `--port` (default 8590) and the other servers on +100 .. +500,
so it never answers for, or clashes with, an instance you are using. Without `--launch`, the scripts use the instance
already on `--port`.

- `--launch` runs `cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt`; `--binary` names another one.
- Every image gets a fresh machine, muted, which is destroyed afterwards.
- `--model auto` takes PLUS3 for `.dsk` and PENTAGON for TR-DOS images.
- After `--settle` frames (150) the scripts tap `--key` (ENTER, which picks the +3 Loader). Pass `--key none` where the
  image autostarts on insert.

```bash
# Static: odd tracks, then one in detail (python3 -I: the image is data from outside the repository)
python3 -I tools/verification/disk/dsk-tracks.py game.dsk
python3 -I tools/verification/disk/dsk-tracks.py game.dsk --track 39 --hex 4

# Boot a set of images, 6000 frames each; scratch/disk-boot-check/{results.tsv,*.png,sheet.png}
python3 tools/verification/disk/disk-boot-check.py --launch disks/*.dsk

# A load that hangs: 30 consecutive frames after 2000, then the stack and the code around it
python3 tools/verification/disk/fdc-probe.py --launch --frames 2000 --samples 30 game.dsk
```

`disk-boot-check.py` exits 1 when an image fails to insert, gives no screenshot, or leaves a single-colour screen; the
blank-screen test and the contact sheet need Pillow.

## Reading the results

- **Insert failed.** The loader's message says why. Take it to `dsk-tracks.py`, which shows the track as stored. A
  truncated sector is a damaged copy, not an emulator gap: find an intact one.
- **Blank screen.** Run `fdc-probe.py`.
  - The same command returning the same result every revolution (10 frames on a +3) is a protection check that
    waits for something the modelled track never gives. The CPDRead tracks did this: READ ID returned ID 2 forever
    while the game waited for ID 15.
  - The return addresses show the game's loop around the DOS call. On the +3, ROM 2 is +3DOS, and IY holds the
    routine of a DOS call.
- **Long dumps.** A CPDRead track stores sector N as the 128 << N bytes a uPD765 read from that field on, so a long
  dump holds the ID and data marks of the sectors after it. `dsk-tracks.py --track` lists them. Where they all agree,
  it prints the layout the loader rebuilds: field length, GAP#3, the distance between the sectors.
  - Licence To Kill: 16 fields of 128 bytes, GAP#3 200, 390 bytes apart, 6240 of 6250 bytes.
  - CPDRead (v3.24) does not write the sector data back to back, as the Extended DSK format has it: a 128-byte sector
    takes a 256-byte slot of the file, its data then 128 zero bytes, and every sector after it starts 128 bytes
    later. The track block is sized for the data back to back, so the slots of the last sectors can fall past it;
    the ones seen there are sectors without a data field. `dsk-tracks.py` and the loader read the slots when the
    creator in the disk header starts with `CPDRead`. Read back to back, a Captain Blood track looks as if every long
    dump held 128 bytes before its field (518 bytes apart, longer than a revolution); read in slots, it is the
    Licence To Kill layout.
  - Tintin on the Moon: 16 fields of 128 bytes, GAP#3 50, 240 bytes apart.
  - Where the dumps disagree, the loader cuts the long fields to fit.

The loader side is `core/src/3rdparty/unreal-disk/src/loaders/loader_dsk.cpp` (`layOutOverlappingFields`, the cut
fallback after it, the CPDRead slots in `parseTrack`), with tests in `core/tests/loaders/disk/loader_dsk_test.cpp`
(`Load_OverlappingLongFields_*`, `Load_CpdreadSlots_*`).
