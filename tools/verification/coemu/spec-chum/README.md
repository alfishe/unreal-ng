# ctprobe on spec_chum, headless

Runs the contention probe ([`../../contention/ctprobe`](../../contention/ctprobe/README.md)) on spec_chum
(<https://github.com/mward-sudo/spec_chum>, MIT), a ZX Spectrum emulator written in Rust. It needs no window and
no sound. The runner drives spec_chum's own loopback HTTP server, `spec-chum-agent` (its "Agent Debug HTTP API",
`docs/AGENT_DEBUG_API.md` in the spec_chum tree). It types the loader keys, plays the tape, and reads memory
back for the compare script. It follows the contract in [`../README.md`](../README.md).

```
SPEC_CHUM_DIR=<spec_chum checkout> ./run.sh              # builds spec-chum-agent, runs every machine
SPEC_CHUM_DIR=<spec_chum checkout> ./run.sh 48k plus3    # only these
SPEC_CHUM_BIN=<a built spec-chum-agent> ./run.sh         # skip the build
```

Needs Rust (`cargo`, from `PATH` or `CARGO`) for the build, and python3. The build is
`cargo build --release -j <half the cores, at most 4> -p agent_server --bin spec-chum-agent`, run inside the
checkout with `CARGO_TARGET_DIR=build/target`. The checkout itself is never changed. The first build takes
about 3 minutes; later runs only check that it is up to date.

Output goes to `../out/spec-chum/`: `<machine>.bin` (the memory dump), `<machine>.log` (the runner's
progress: when the program loaded, the frame it finished at), `<machine>.screen.txt` (the final screen as
text), `<machine>.compare.txt` and `<machine>.result`. One pass over all machines takes 2 to 4 minutes.

## How it works

- `run.sh` finds or builds `spec-chum-agent`, lays out the ROMs (below), and runs `chum.py` once per machine.
- `chum.py` starts `spec-chum-agent --model <model> --port <a free port> --token <random>` and talks to it
  over HTTP on `127.0.0.1`. The server has no frame loop of its own: the machine only advances when asked
  (`POST /v1/run {"frames": N}`), as fast as the host allows. So the frame count is exact and a run takes
  well under a minute.
- It loads the program as a user would. It presses keys through `POST /v1/keys` (key matrix rows and bits),
  a few frames down and a few frames up, like fingers on a keyboard. It inserts the tape with
  `POST /v1/tape/open` and starts it with `POST /v1/tape/play`.
- The program's first 8 bytes (as on the tape) must be in memory at `START` within 6000 frames (2 minutes of
  machine time; the tape at normal speed takes about 1 minute). If not, the result is `error` and the log
  shows where the CPU was.
- Then it runs 100 frames at a time and reads the `DONE` byte (`GET /v1/peek`) after each step. It stops when
  `DONE` is 1 or after `MAX_FRAMES` frames. It dumps `START`..`PROBEEND - 1` and the screen, and stops the
  server. `../mame/screentext.py` turns the screen into text.

`chum.py run --help` lists its options; you can run it by hand on one machine, for example:

```
python3 chum.py run --bin build/target/release/spec-chum-agent --romroot build/romroot --model 48k \
  --load keyword --tap ../../contention/ctprobe/ctprobe.tap --done 36016 --start 36000 --end 45508 --out /tmp/48k
```

## Settings

spec_chum's stock settings, with these choices:

| Setting | Why |
|:--|:--|
| ROMs from this repository's `data/rom/` (table below) | spec_chum ships no ROMs in git; its `scripts/fetch_roms.sh` would write into the checkout |
| `SPEC_CHUM_ROM_ROOT` and the working folder set to `build/romroot` | spec_chum looks for `roms/...` in the working folder first, so a `roms/` folder of your own is not used |
| `--token <random>` per run | the server refuses changes without a token (or `--insecure`); a token keeps it closed to other local programs |
| Tape: spec_chum's default (played as sound, normal speed, no instant load) | a user's load; the probe measures CPU ticks, so loading speed does not matter |
| No border in the picture (the server's default) | the picture is not used |

The ROM files go under `build/romroot/roms/`, with the names spec_chum's ROM catalog
(`crates/machine/src/rom/catalog.rs`) looks for:

| spec_chum file | From `data/rom/` |
|:--|:--|
| `spec48.rom` | `48.rom` |
| `128/spec128uk.rom` | `128.rom` |
| `plus2/plus2uk.rom` | `plus2.rom` |
| `plus2a/plus2a.rom` | `plus2a.rom` (ROM 4.0, the same image as `plus3.rom`) |
| `plus3/plus3.rom` | `plus3.rom` |
| `pentagon/pentagon.rom`, `pentagon/trdos.rom` | `pentagon128k.rom` (the Pentagon's 128 ROM, 32 KB); page 1 of `pentagon.rom` (TR-DOS 5.04TM) |
| `scorpion/scorpion.rom`, `scorpion/trdos.rom` | pages 0-2 of `scorpion.rom` (128 BASIC, 48 BASIC, service monitor: spec_chum takes 48 KB); page 3 (its TR-DOS) |

## Machines

spec_chum's models (`--model`): 16K, 48K, 128K, +2, +2A, +3, +3e, Pentagon 128, Scorpion ZS-256, Timex TC2048
and TS2068.

| Machine | spec_chum model | How the program is loaded |
|:--|:--|:--|
| `48k` | `48k` | `LOAD ""` typed in keyword mode (J, then Symbol Shift + P twice, Enter), then the tape |
| `128k` | `128k` | Enter on the menu's first item, **Tape Loader** (128 BASIC, paging open), then the tape |
| `plus2` | `plus2` | as the 128K |
| `plus2a` | `plus2a` | Enter on the menu's first item, **Loader** (+3 BASIC), then the tape |
| `plus3` | `plus3` | the menu's **+3 BASIC**, `LOAD "t:"`, then `LOAD ""`, then the tape (the probe README's way; the menu's Loader does not load the code, see below) |
| `pentagon` | `pentagon` | Enter on the menu's first item, **Tape Loader** (128 BASIC), then the tape. Not the `.trd`: TR-DOS does not run in spec_chum (below) |
| `scorpion` | `scorpion` | the `.trd` in drive A, then power on: a Scorpion starts in TR-DOS, which runs the disk's `boot`. `error`, see below |
| `profscorp` | - | skipped: spec_chum's Scorpion has no ProfROM (it takes a 48 KB ROM) |
| `atm710`, `atm3` | - | skipped: spec_chum has no ATM Turbo or ZX-Evo |
| `profi` | - | skipped: spec_chum has no Profi |

## Results (spec_chum 0.7.0, commit 6b025bc, 2026-09-30)

| Machine | Result |
|:--|:--|
| 48K, 128K, +2 | `wrong`: 160 values in 9 checks (M1-06, D-04, N-01..N-06): internal ticks never wait |
| +2A, +3 | `wrong`: 404 values in 29 checks: the waits follow the 128K pattern, so the probe takes the machine for a 128K |
| Pentagon | `ok`: all values as expected |
| Scorpion | `error`: it boots into TR-DOS, which crashes; the program never loads |
| Scorpion + ProfROM, ATM Turbo 2+, ZX-Evo, Profi | `skipped`: not in spec_chum |

### 48K, 128K, +2: internal ticks never wait

Some ticks of an instruction do not read or write memory: the CPU only leaves an address on the bus. On a
48K, 128K or +2 the ULA holds the CPU on those ticks too, when the address is in contended memory. spec_chum
does not. Its Z80 core (`crates/z80/src/cpu.rs`, `contend_cycles`) adds those ticks without asking the memory
for a wait; the "contend" events there are only logged for its Fuse test vectors.

Every check that differs has such ticks, and only those:

| Check | Instruction | Internal ticks on a contended address | Example (48K, first value) |
|:--|:--|:--|:--|
| N-02 | `JR +0` at `#4000` | 5, on the offset byte's address | got 16, expected 33 |
| N-04 | `ADD HL,BC` with `I` = `#40` | 7, on `IR` | got 43 at every tick, expected 67..61: no wait at all |
| N-06 | `LD A,(IX+0)` at `#4000` | 5, on the displacement's address | got 32, expected 48 |
| N-05A, N-05B | `LDIR`, `CPIR` | the 5 ticks of each repeat, plus those of `LDI` / `CPI` | got 109, expected 130; got 98, expected 147 |
| N-03 | `EX (SP),HL` | 3, on the stack | got 85, expected 90 |
| D-04 | `LDI` | 2, on the destination (`DE`) | got 77, expected 82 |
| N-01 | `INC (HL)` | 1, on `HL` | got 31, expected 32 |
| M1-06 | `RLC (IX+0)` | 2, on the address of its last byte | got 40, expected 41 |

All other checks, including the floating bus (P-02) and the port checks, are as expected. The 128K and +2 give
the same rows as the 48K, 26 ticks later (their first contended tick is 14361, not 14335).

### +2A, +3: the 128K wait pattern

The +2A / +3 gate array holds the CPU 1, 0, 7, 6, 5, 4, 3, 2 ticks for the 8 ticks of each group; the 128K's
ULA 6, 5, 4, 3, 2, 1, 0, 0. spec_chum uses the 128K one on the +2A / +3: `BusPlus3::contend_at`
(`crates/bus/src/plus3.rs`) calls `contention_delay_128`. The probe tells the two apart by the largest wait
(7 or 6), so it reports `Machine: ULA 128K` and compares everything with the 128K table:

- the screen shows `Paging yes, +3 layouts no`: the probe does not try the +3 layouts on a 128K;
- M1-P1, M1-P3 (expected to wait on a 128K, not on a +3) and M1-P4, M1-P6 (the reverse): spec_chum contends
  pages 4-7, the +3's rule, which is right for a +3;
- all port checks (P-01, P-03, P-04, P-05: 17 checks): spec_chum's +2A / +3 ports never wait, which is right
  for the gate array, but the 128K table expects waits;
- P-02: port `#FF` reads 255 on spec_chum's +2A / +3 (no floating bus there), as on a real gate array; the
  128K table expects the screen bytes;
- M1-06, D-04, N-01..N-06: as on the 48K, but here the gate array does not make internal ticks wait either,
  so only the wait pattern is wrong.

So one cause explains all 29 checks: the wait pattern. With the gate array's pattern, the probe would take the
machine for a +2A / +3 and check it against the +2A / +3 table, which was not measured here. The +2A and the +3
give the same rows.

### Pentagon

All values as expected. The probe finds `no contention`, frame 71680, paging `yes`; the floating-bus check
P-02 is skipped as usual on a plain clone.

### Scorpion: does not load

A Scorpion switches on into TR-DOS. In spec_chum the TR-DOS ROM is switched in when the CPU runs code at
`#3D00`-`#3DFF`, as on the real Beta Disk interface, but it is never switched out when the CPU runs code from
RAM (`#4000` and up), which the real interface does. Only a machine reset switches it out (`BetaDisk::notify_m1`
and `page_trdos` in `crates/bus/src/beta_disk.rs`; the file's header says "the latch stays set across RAM
execution"). TR-DOS calls the BASIC ROM through small routines in RAM, so those calls land in the TR-DOS ROM
instead and the machine crashes. The runner's log shows it: after 6000 frames the CPU is at `#FCCB`, in empty
RAM, with the TR-DOS ROM still switched in, and the screen is blank. The same happens with the disk inserted
or not, and with TR-DOS 5.03, 5.04T and the Scorpion's own TR-DOS page. spec_chum's README lists "TR-DOS `RUN`
with a real TR-DOS ROM" as incomplete; its own tests drive TR-DOS through stand-ins for these ROM paths.

The same fault is why the Pentagon loads from tape: entering TR-DOS from its menu or with
`RANDOMIZE USR 15616` crashes the same way.

Not measured, from the source: spec_chum's Scorpion uses the Pentagon's timing (a 71680-tick frame, no
contention) and has no Even M1, so the probe would compare it with the plain clone table, as for Xpeccy. Its
`#7FFD` lock bit also blocks writes to `#1FFD`.

### +3: the menu's Loader does not load the code

With the menu's Loader, spec_chum loads the BASIC part and prints `Bytes: ctprobe` for the code's header, then
reads over the code block without loading it and waits for the next header. The +2A's Loader, with the same ROM,
loads it. From +3 BASIC with `LOAD "t:"` and `LOAD ""` it loads, so the runner uses that. Not investigated
further.
