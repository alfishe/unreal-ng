# The Link - the stages, one folder each

Each step from the author's working disk to the sjasmplus project is kept as it was built, so each
can be built and run on its own. Every folder holds its sources, its `build.py` and the disk it
builds, `TheLink.scl`.

| Stage | Folder | What it adds | Takes from the author's disk | Disk |
|---|---|---|---|---|
| 0. The original | [`../src/`](../src/), `testdata/machines/pentagon1024sl/TheLink.trd` | the author's start: `RUN "THELINK"` loads ALASM, which assembles the demo and runs it (28 s) | everything | `TheLink.trd` |
| 1. A loader | [`1-loader/`](1-loader/) | a loader instead of the assembler: title, main block, 28 page files by TR-DOS, `JP #7800`. No machine check: on a wrong machine it starts the demo anyway | the effect binaries, pictures, music | [`1-loader/TheLink.scl`](1-loader/TheLink.scl), 1169 sectors |
| 2. The machine check | [`2-machine-check/`](2-machine-check/) | Pentagon 1024 paging and NeoGS detection before loading; an error screen naming what is missing | the same | [`2-machine-check/TheLink.scl`](2-machine-check/TheLink.scl), 1172 sectors |
| 3. From source | [`../sjasm/`](../sjasm/README.md) | every effect converted from ALASM to sjasmplus (unreal-asm) and assembled; the tables computed; the stage 2 loader | pictures, models, music only | `testdata/machines/pentagon1024sl/TheLink-sjasm.scl` |

Stages 1 and 2 assemble the main block from `thelink_main.asm`, a port of `THELINK.asm`'s code
part made by hand (stage 3 replaced it with the converted `THELINK.asm`, which builds the same
14677 bytes). Their `build.py` reads the effect binaries and data from the author's disk:

```
SJASMPLUS=/path/to/sjasmplus python3 docs/disasm/demo/thelink/stages/2-machine-check/build.py \
    testdata/machines/pentagon1024sl/TheLink.trd scratch/stage2
```

Stage 2 and stage 3 build the same disk byte for byte: the same loader, and the objects stage 3
assembles are the author's binaries stage 2 copies.

## The music fix, and the first release build

`GO` clears `#D800`-`#DAFF` of whatever page is at `#C000`; the first loader left the music page
there and 733 bytes of the player were zeroed (the picture was unaffected, so a screen compare
missed it; [sjasm/loader/README.md](../sjasm/loader/README.md) section 4). The fix - `#7FFD` = `#17`
before `JP GO` - is in the loaders of all three stages, so every disk here plays the music whole.

`testdata/machines/pentagon1024sl/TheLink.scl` is the first release build, kept as it was: stage 2
**without** the fix. It boots into the demo; its music page differs from `thelinkm` in those 733
bytes.

| Disk | sha256 |
|---|---|
| `testdata/.../TheLink.scl` (first release build, no fix) | `559312bf2a36b329416ca415079709b24fe68ae41a0b3d618002ea4728c8a749` |
| `1-loader/TheLink.scl` | `ff5a446eef0fba2300d136e89721f766733229c4042f8619caf6417483851ada` |
| `2-machine-check/TheLink.scl` = `testdata/.../TheLink-sjasm.scl` | `53dd7ad7a007f74adcc60316c19d3632e506a1df54a93b0943c25334f9ee704d` |

All four disks were booted in unreal-ng (Pentagon 1024 + NeoGS) until the tunnel runs.
