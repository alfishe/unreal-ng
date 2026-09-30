# RZX verification tools

The expected states in `testdata/loaders/rzx/` and a way to check any RZX file
against an independent player: SkoolKit's `rzxplay.py` (headless, exact Z80,
playback-only).

## rzxplay-memptr.py

`rzxplay.py` with SkoolKit's MEMPTR-exact simulator. The stock `rzxplay.py`
runs SkoolKit's fast C simulator, which updates MEMPTR on jumps only in its
contention build; the undocumented flags 3 and 5 of `BIT n,(HL)` then come from
a stale MEMPTR. Worked example: Dargon's Crypt (+2) ends with F = #74 on a real
Z80 (and in unreal-ng), #5C with the fast simulator. Contention does not change
the CPU path of an RZX playback (every `IN` comes from the recording), so the
contention simulator is the right reference.

```bash
python3 -m venv scratch/skvenv && scratch/skvenv/bin/pip install skoolkit   # builds the C simulators
scratch/skvenv/bin/python tools/verification/rzx/rzxplay-memptr.py --quiet --no-screen game.rzx game.end.z80
scratch/skvenv/bin/python tools/verification/rzx/rzxplay-memptr.py --quiet --no-screen --stop 300 game.rzx game-300.z80
```

The dump is SkoolKit's state right after the interrupt that ends the last
played frame.

## rzxtrim.py

Cuts a recording down to a fixture: the first N frames, optionally with the
start snapshot moved to an external file.

```bash
python3 tools/verification/rzx/rzxtrim.py game.rzx short.rzx --frames 5
python3 tools/verification/rzx/rzxtrim.py game.rzx ext.rzx --frames 50 --external ext-start.z80
```

To isolate one frame deep in a recording: `rzxplay-memptr.py --stop N game.rzx
tail.rzx` writes the rest of the recording from frame N with SkoolKit's state
there as its snapshot; `rzxtrim.py tail.rzx case.rzx --frames 2` keeps two frames.

## Checking unreal-ng against SkoolKit

`core-tests` reads these variables (`core/tests/emulator/rzx/rzxsession_test.cpp`):

| Variable | Test | Does |
|---|---|---|
| `UNREAL_RZX_FULL=1` | `RzxArchive_Test.WholeRecordingMatchesSkoolKit`, the Pentagon test | the testdata recordings to their end |
| `UNREAL_RZX_CORPUS=<folder>` | `RzxSession_Test.CorpusFolderPlaysToTheEnd` | every `.rzx` there to its end without a desync; the final state against `<name>.end.z80` when present |
| `UNREAL_RZX_FILE`, `UNREAL_RZX_ORACLE`, `UNREAL_RZX_STOP` | `RzxSession_Test.OneFileAgainstAnOracle` | one file for N frames against one expected state |

Bisecting the first frame where the two disagree:

```bash
f=game.rzx; lo=1; hi=$(frames)
while [ $((hi - lo)) -gt 1 ]; do
  mid=$(((lo + hi) / 2))
  scratch/skvenv/bin/python tools/verification/rzx/rzxplay-memptr.py --quiet --no-screen --stop $mid $f scratch/bis.z80
  if UNREAL_RZX_FILE=$f UNREAL_RZX_ORACLE=scratch/bis.z80 UNREAL_RZX_STOP=$mid \
     cmake-build-agent-release/bin/core-tests --gtest_filter='RzxSession_Test.OneFileAgainstAnOracle' | grep -q PASSED
  then lo=$mid; else hi=$mid; fi
done
echo "first differing frame: $hi"
```

Then trace that frame in SkoolKit (`--trace`) from a cut recording (above).
SkoolKit does not emulate the Pentagon, so a Pentagon recording is checked by
playing it to the end in strict mode alone.
