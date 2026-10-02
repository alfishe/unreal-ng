# fusetest - FUSE's timing regression program

`fusetest` is the test program the FUSE emulator project wrote for itself (Philip Kendall, 2007-2010,
GNU General Public License). It runs on a ZX Spectrum, times short pieces of code against the frame
interrupt, and prints one line per test. This folder holds a built copy, `fusetest.tap`, and the script
that builds it. The source is not copied here; see below for where to get it.

## What it tests

When it starts, the program:

1. measures the frame length (the number of CPU clock ticks between two frame interrupts);
2. guesses the machine from it: 48K, 128K, +3 (a 128K frame, told apart by how the memory paging port is
   decoded), Pentagon, TS2068 or 48K NTSC;
3. finds the first clock tick where the CPU has to wait for the screen ("Contention offset"), so the timing
   tests line up on any emulator.

Then it runs twelve tests. Each one has a mask of the machines it is meant for, and prints `skipped` elsewhere.

| Test | What it checks | Machines |
|:--|:--|:--|
| `BIT n,(IX+d)` | the two undocumented flag bits after `BIT n,(IX+d)` | all |
| `DAA` | the flags after `DAA` for one tricky input | all |
| `OUTI` | the undocumented flags after `OUTI` | all |
| `LDIR` | the timing of an `LDIR` that copies across the start of slow (contended) memory | 48K, 128K, +3, Pentagon |
| `Contended IN` | the timing of `IN` from a port whose high byte points into slow memory | 48K, 128K, +3, Pentagon |
| `Floating bus` | reading an unused port while the screen is being drawn returns the screen byte being fetched | 48K, 128K |
| `Contended memory` | the timing of an instruction fetch at the first slow address | 48K, 128K, +3, Pentagon |
| `High port contention 1`, `2` | the timing of `IN` from port #FFFF with a fast / a slow memory page at #C000 | 48K, 128K, +3, Pentagon |
| `0xbffd read` | reading port #BFFD: #FF on most machines, the selected sound chip register on the +2A / +3 | all |
| `0x3ffd read`, `0x7ffd read` | on the 128K, reading the paging port stores whatever is on the data bus (here a screen byte) into it, and so switches the memory page | 128K |

Each line ends in `passed`, `failed (0xNN)` (a value that tells how far off it was), `incomplete (B=0xNN)` (the
test could not synchronize) or `skipped`.

**Known problem in the program.** On a Pentagon it prints `Machine type: TS2068`: in `guessmachine.asm` the
Pentagon branch falls through into the TS2068 branch (a missing jump, since FUSE SVN r3852). Every test then
uses the TS2068 settings and the timing tests fail, on FUSE as well. The program cannot test a Pentagon.

## Where the pieces come from

| Piece | Source |
|:--|:--|
| Program source | FUSE's Subversion repository, folder `fusetest/`, mirrored on GitHub: `https://github.com/vamposdecampos/fuse-emulator-svn` (the files are also listed at Spectrum Computing, entry 32100) |
| Revision used | mirror commit `57aa2436f80ab972ee269b23824e8a9543cf0411` (FUSE SVN trunk r5736, 2016). The last change to `fusetest/` is SVN r4115 (2010-02-14), so any later revision gives the same files |
| Assembler | pasmo 0.5.5 by Julián Albo, `https://pasmo.speccy.org/bin/pasmo-0.5.5.tar.gz` (sha256 `c83ff23e06b26ab5de05efaf13d9ebaf485d43f9a3a1ed50bba17be5a87918ac`); builds with `./configure && make` |

The program's Makefile builds four tapes; only `fusetest.tap` is used here (the others are tools for finding
contention patterns by hand).

## How to build

```
# 1. Get the source (a sparse clone takes only the one folder)
git clone --depth 1 --filter=blob:none --sparse https://github.com/vamposdecampos/fuse-emulator-svn.git fuse-svn
git -C fuse-svn sparse-checkout set fusetest

# 2. Build pasmo
curl -LO https://pasmo.speccy.org/bin/pasmo-0.5.5.tar.gz
tar xzf pasmo-0.5.5.tar.gz && (cd pasmo-0.5.5 && ./configure && make)

# 3. Assemble (writes fusetest.tap next to build.sh; OUT=<file> to write elsewhere)
FUSETEST_SRC=fuse-svn/fusetest PASMO=$PWD/pasmo-0.5.5/pasmo tools/verification/contention/fusetest/build.sh
```

`build.sh` downloads nothing. It runs the same command as the program's Makefile,
`pasmo --alocal --tapbas fusetest.asm fusetest.tap`, and the result is byte for byte the tape the Makefile makes
(sha256 `2971bdcd989e6412849e7e09c984cf9cb9db1c5474c7aa8a7daf37ce41f37594`). The tape holds a BASIC loader
and the program at #A000.

## How to run

Load it like any tape: `LOAD ""` on the 48K or from 128 BASIC, the "Tape Loader" or `LOAD "t:": LOAD ""` on the
+3. It runs by itself for a few seconds and returns to BASIC.

unreal-ng's test suite runs it on the 48K, 128K, +3 and Pentagon and checks every line:
`core/tests/emulator/video/fusetest_test.cpp` (`FuseTest_Test`). Set `UNREAL_FUSETEST_PRINT=1` to see the
whole report.

## Results (2026-09-30)

FUSE 1.6.0 prints `passed` (or `skipped`) for every test on the 48K, 128K and +3, which is what the real machines
print. unreal-ng differs in four lines:

| Machine | Test | unreal-ng | Why |
|:--|:--|:--|:--|
| 48K, 128K | `Floating bus` | `failed (0xff)` | The CPU takes the data of an `IN` at the end of the input cycle. When the port's high byte points into slow memory (#4000-#7FFF), the screen chip holds the CPU several more times inside that cycle, so the byte arrives up to 12 ticks later. unreal-ng picks the floating bus byte at the start of the cycle, where the screen chip is between fetches, and returns #FF |
| 128K | `0x3ffd read`, `0x7ffd read` | `failed (0x00)` | On the 128K and grey +2, reading a port that the paging logic decodes as #7FFD also stores the data bus into the paging register. unreal-ng does not do this. `0x7ffd read` also needs the floating bus fix above |
| +3 | `0xbffd read` | `failed (0xaa)` | On the +2A / +3 port #BFFD reads the selected sound chip register, like #FFFD. unreal-ng returns #FF |

On the Pentagon unreal-ng prints the same report as FUSE (see the known problem above).

## Running it under the co-emulation harness

The harness ([`../../coemu/README.md`](../../coemu/README.md), "The program's side") waits for a byte labeled
`DONE` to become 1 and then reads a memory dump between `START` and `PROBEEND`. fusetest keeps its verdicts only on
the screen and returns to BASIC, so it runs there inside a small wrapper, `fusetest-coemu`:

| File | Content |
|:--|:--|
| `fusetest-coemu.asm` | the wrapper, at #9000: points the output routine of the channels K and S at a hook that keeps a copy of every printed character in a buffer (#9000-#9FFF), calls fusetest at #A000, restores the channels and sets `DONE` |
| `fusetest-coemu.tap`, `.trd` | one code block from #9000: the wrapper, then fusetest's code from `fusetest.tap`; loads and starts at 36864 |
| `fusetest-coemu.sym` | `START`, `DONE`, `BUFFER`, `PROBEEND`, ... |
| `fusetest-coemu-compare.py` | reads the buffer out of a dump and checks every line: right when each says `passed` or `skipped` |

```
PROGRAM=$PWD/tools/verification/contention/fusetest/fusetest-coemu tools/verification/coemu/run-all.sh 48k 128k plus2 plus2a plus3
```

The files are built by unreal-ng's test suite (`core/tests/emulator/video/fusetest_test.cpp`,
`FuseTestCoemuFiles_Test`; `UNREAL_FUSETEST_EXPORT=1` writes them), which also checks that the buffer holds the
report the screen shows (`FuseTestCoemu_Test`). fusetest itself is unchanged.

Results on eight emulators: [reports/2026-10-02-fusetest-matrix.md](../../coemu/reports/2026-10-02-fusetest-matrix.md).
FUSE and unreal-ng pass on the 48K, 128K, +2, +2A and +3. fusetest cannot test the clones: it takes the Pentagon
for a TS2068 (and, started from TR-DOS, its paging tests switch the ROM under BASIC), cannot measure a Scorpion's
frame, and takes a contention-free 48K frame for a 48K.
