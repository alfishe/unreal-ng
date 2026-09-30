# coemu runner: ZX-M8XXX

Runs the test program (by default the contention probe, [`../../contention/ctprobe`](../../contention/ctprobe/README.md))
on [ZX-M8XXX](https://github.com/Bedazzle/ZX-M8XXX), a ZX Spectrum emulator and debugger written in plain
JavaScript that runs in a web browser. The runner opens it in headless Chrome (no window, no sound) and drives
it through `window.zxDebug`, the automation interface the emulator documents in its own `M8XXX.md`. See
[`../README.md`](../README.md) for the contract all runners follow.

```
M8XXX_DIR=<ZX-M8XXX checkout> tools/verification/coemu/zx-m8xxx/run.sh            # all machines
M8XXX_DIR=<ZX-M8XXX checkout> tools/verification/coemu/zx-m8xxx/run.sh 48k plus3  # some
```

## What it needs

| What | Where it is found |
|:--|:--|
| A ZX-M8XXX checkout (no build step) | `M8XXX_DIR`. Not set, or not a checkout (no `index.html` and `M8XXX.md`): every machine is `skipped`, exit 3 |
| Google Chrome or Chromium | `CHROME_BIN`, then `google-chrome`, `google-chrome-stable`, `chromium`, `chromium-browser` or `chrome` on `PATH`, then the standard macOS application folders. None found: every machine is `skipped`, exit 3 |
| The ROMs | `M8XXX_ROMS`, a folder with the ROM files under ZX-M8XXX's names; by default unreal-ng's `data/rom` (see [ROMs](#roms)) |
| `python3` | on `PATH`; only the standard library |

Nothing is installed and nothing in the checkout is changed. The browser profile is a fresh folder under
`build/` for each machine, deleted after the run.

## Machines

| Machine | ZX-M8XXX machine | How the program is loaded |
|:--|:--|:--|
| `48k` | `48k` (ZX Spectrum 48K) | ZX-M8XXX's own tape auto-loader types `LOAD ""` |
| `128k` | `128k` (ZX Spectrum 128K) | the auto-loader presses ENTER on the 128K menu's Tape Loader (128 BASIC: paging open) |
| `plus2` | `+2` (ZX Spectrum +2) | as the 128K |
| `plus2a` | `+2a` (ZX Spectrum +2A) | the auto-loader presses ENTER on the +2A menu's Loader |
| `plus3` | `+3` (ZX Spectrum +3) | ENTER on the +3 menu's Loader; no disk is inserted, so it loads from tape |
| `pentagon` | `pentagon` (Pentagon 128K) | the `.trd` in drive A, then ZX-M8XXX's Auto Load for TR-DOS disks: it boots TR-DOS, which runs the disk's `boot` by itself (the same as `RUN` at the `A>` prompt) |
| `scorpion` | `scorpion` (Scorpion ZS 256) | as the Pentagon; its TR-DOS is the one in the Scorpion ROM |
| `profscorp` | - | `skipped`: ZX-M8XXX's Scorpion takes only the stock 64 KB Scorpion ROM, there is no ProfROM machine |
| `atm710`, `atm3`, `profi` | - | `skipped`: ZX-M8XXX has no ATM Turbo, no ZX-Evo and no Profi |

ZX-M8XXX also has a Pentagon 1024; it is not one of the harness machines.

The loading is ZX-M8XXX's own `zxDebug.autoLoad`, the same key sequence its Auto Load checkbox types for a
user. Tapes go through the ROM's `LOAD ""` with ZX-M8XXX's default "Flash Load": the ROM's tape routine is
trapped and each block is copied into memory at once, instead of being played as sound. The program measures
only what happens after it starts, so this does not change any result.

## Settings

Stock settings, as a fresh browser profile has them. The log (`<machine>.log`) prints the ones that bear on
timing for each run, for example:

```
m8xxx: started 48k - tape flash load true - late timings false - frame 69888 T
```

| Setting | Value | Note |
|:--|:--|:--|
| Late Timings | off (the page's default) | only changes the 48K; "on" moves its interrupt by 1 tick |
| ULA Snow | off (the page's default) | the 48K / 128K picture fault when `I` points at the screen; the probe does not use it |
| Flash Load | on (the page's default) | see above |
| Auto Load | used through `zxDebug.autoLoad` | see above |

## ROMs

ZX-M8XXX does not ship ROMs. At start it fetches them from its `roms/` folder, which is ignored by its git.
To keep runs the same on every computer, the runner never uses that folder: it serves the ROMs itself, under the
names ZX-M8XXX asks for, and answers "not found" for any other file in `roms/`.

| ZX-M8XXX name | Taken from (default: unreal-ng's `data/rom`) | Size ZX-M8XXX expects |
|:--|:--|:--|
| `48.rom`, `128.rom`, `plus2.rom`, `plus2a.rom`, `plus3.rom`, `scorpion.rom`, `trdos.rom` | the file of the same name | 16, 32, 32, 64, 64, 64 and 16 KB |
| `pentagon.rom` | `pentagon128k.rom` | 32 KB: 128 BASIC, then 48 BASIC |

The Pentagon is the one exception because unreal-ng's `pentagon.rom` is a 64 KB ROM in unreal-ng's page order
(service ROM, TR-DOS, 128 BASIC, 48 BASIC). Given to ZX-M8XXX it boots into the service ROM and the program
crashes. With `M8XXX_ROMS=<folder>` every file, `pentagon.rom` included, is taken from that folder by its
ZX-M8XXX name. A machine whose ROM is missing is an `error` that names the file.

## How it runs

1. `run.sh` runs the machines one after another, so only one browser runs at a time.
2. For each machine, `driver.py` starts a small web server on `127.0.0.1` (a free port) that serves the
   checkout, the program file, the ROMs and `harness.html`, then starts headless Chrome on `harness.html`.
3. `harness.html` loads ZX-M8XXX's `index.html` in a frame and follows the rules in its `M8XXX.md`: it waits
   for the page's own start-up (see below), then `zxDebug.ready({machine})`, `zxDebug.start({machine})`,
   `zxDebug.loadUrl(<program>)` and `zxDebug.autoLoad({type})`.
4. It then runs the machine one frame at a time (`zxDebug.runFrames(1)`), checking the `DONE` byte after
   every frame, and stops when it is 1 or after `MAX_FRAMES` frames in all. Everything is counted in emulated
   frames, never in wall-clock time. It yields to the browser every 250 frames so its progress reaches the
   driver.
5. It sends back memory from `START` to `PROBEEND - 1` and the screen at `#4000`. The driver writes
   `<machine>.bin` and `<machine>.screen.txt` (the screen matched against the 48K ROM font) and closes the
   browser. `run.sh` then calls the compare script.

The page's log lines, its console output and the progress go to `out/zx-m8xxx/<machine>.log`. If the page
sends nothing for 3 minutes, the browser is taken to be stuck and the machine is an `error`; this is the only
wall-clock limit. A full probe run takes about half a minute per machine: 13000 to 15000 frames.

**Waiting for the page's start-up.** The page fetches all its ROMs one by one when it opens, and when the
last one has arrived it resets the machine and starts it. `zxDebug.ready()` returns as soon as the machine's own
ROM is in memory, which can be before that. A driver that goes on at once has its run reset by the page midway
(the 128K ended up back at its menu), and the TR-DOS ROM is not there yet ("Cannot boot TR-DOS: TR-DOS ROM
not loaded"). So the harness first waits until the page has started the machine by itself. This is the one
place it reads `zxDebug.spectrum` directly (`running`, `stop()`), as well as for the timing settings in the log.

## Results (ZX-M8XXX 26.09.03, 2026-09-30)

| Machine | Result |
|:--|:--|
| 48K | `wrong`: 215 values in 14 checks |
| 128K, +2 | `wrong`: 264 values in 18 checks |
| +2A, +3 | `wrong`: 33 values in 3 checks |
| Pentagon | `ok`: all values as expected |
| Scorpion | `ok`, but only because ZX-M8XXX's Scorpion is timed as a Pentagon (see below) |
| Scorpion + ProfROM, ATM Turbo 2+, ZX-Evo, Profi | `skipped`: not in ZX-M8XXX |

How to read a line of `<machine>.compare.txt`: each check runs one instruction at 12 to 20 start ticks in a
row, and `got` / `exp` are how long it took there, in T-states, on ZX-M8XXX and on a real machine. For example
the 48K's

```
N-06: from T14333, one value per tick
  got  32  31  38  37  36  35  34  33  ...
  exp  48  47  54  53  52  51  50  49  ...
```

says that `LD A,(IX+0)`, started 2 ticks before the screen's first contended tick, takes 32 ticks on ZX-M8XXX
and 48 on a 48K: 16 ticks of waiting are missing. The explanations below come from ZX-M8XXX's source
(`core/z80.js`, `core/spectrum.js`, `core/machines.js`).

### How ZX-M8XXX times memory waits

ZX-M8XXX adds an instruction's length to the tick counter after the instruction has run. To find when each
memory access happens inside it, it keeps a running offset: 4 ticks for an opcode fetch, 3 for any other
access, plus the internal ticks an instruction reports. When an instruction does not report its internal ticks,
or reports them on the wrong address, the waits of the accesses after them are looked up at the wrong tick.
Most of the memory differences below come from this.

### 48K, 128K and +2

| Check | What it runs | What ZX-M8XXX does |
|:--|:--|:--|
| M1-06 | `RLC (IX+0)` at `#4000` | 1 tick short everywhere. The real CPU spends 2 internal ticks on the fourth byte's address (`#4003`, contended), then 1 after reading `(IX+0)`. ZX-M8XXX puts 5 internal ticks on the `IX+0` address instead, which is not contended here |
| M1-07 | `DD DD DD NOP` at `#4000` | 8 ticks short. In a chain of prefixes each extra `DD` is counted twice when ZX-M8XXX works out when the next fetch happens (it adds 4 to the tick counter and also 4 to the running offset), so the later fetches are looked up 4, then 8 ticks too late, where they wait less |
| N-03 | `EX (SP),HL`, stack in contended memory | 4 ticks short at first. Its 3 internal ticks (1 after the reads, 2 after the writes) are not reported, and it writes the low byte first where the CPU writes the high byte first |
| N-05B | `CPIR` over 2 bytes in contended memory | 33 ticks short. The 5 internal ticks after each read (on `HL`) are missing; only the 5 ticks of the repeat are there |
| N-06 | `LD A,(IX+0)` at `#4000` | 16 ticks short. The 5 internal ticks spent on the displacement's address (`#4002`, contended) are not reported at all, so they never wait and the read of `(IX+0)` is looked up 5 ticks early |
| P-01A, B, D | `IN A,(C)` from `#00FE`, `#40FE`, `#40FF` | 48K: 1 tick late. Every `IN` has its port cycle placed 7 ticks into the instruction, right for `IN A,(n)` but 1 tick early for `IN r,(C)` (8 ticks in). 128K / +2: no wait at all (22 every time): an `IN` waits only on the 48K |
| P-03C | `OUT (C),A` to `#40FF` | no wait at all (26 every time). ZX-M8XXX makes an `OUT` wait only on an even port (the ULA's); a port with its high byte in `#40`-`#7F` should wait four times whatever its low byte |
| P-03D | `IN A,(#FE)` with `A` = `#40` | 128K / +2 only: no wait, as P-01 |
| P-04A, C | `INI`, `INIR` from `#40FE` | 48K: 2 ticks late, the port cycle placed 7 ticks in instead of 9 (after the 5-tick second fetch). 128K / +2: no wait at all |
| P-04B, D | `OUTI`, `OTIR` to `#40FE` | 4 ticks late: the port cycle is placed 8 ticks in, as for `OUT (C),r`, instead of 12 (after the memory read) |
| P-05A, B, D | 128K / +2: `IN` from a port whose high byte points at a contended page at `#C000` | no wait at all, as P-01 |
| P-02 | `IN A,(#FF)`, the floating bus | 48K: the right bytes in the right order, but 11 ticks late. Most likely because ZX-M8XXX looks up the byte the screen is reading at the tick the `IN` instruction starts (its tick counter only moves on after the instruction), not at the port read near its end. 128K / +2: always `#FF`, ZX-M8XXX has a floating bus only on the 48K |

The 128K and the +2 give the same results. Their dumps differ in one byte only, a copy of the ROM's frame
counter that the program keeps while it checks the CPU speed. The +2A and the +3 below are the same.

### +2A and +3

The gate array makes no port waits and no internal-tick waits, and ZX-M8XXX models that, so most checks pass.

| Check | What ZX-M8XXX does |
|:--|:--|
| M1-03 (`NOP` at `#4000` at the end of a screen line) | one value 1 tick short: the gate array's extra wait just after the last fetch of each line is missing. FUSE and xpeccy-plus miss it too |
| M1-07 (`DD DD DD NOP`) | 8 ticks short, the prefix chain as on the 48K |
| N-03 (`EX (SP),HL`) | 1 tick long at first: the unreported internal tick after the reads moves the writes' lookup, and the write order is swapped |

### Pentagon and Scorpion

The Pentagon has no memory waits, and ZX-M8XXX gets every value right.

ZX-M8XXX's Scorpion uses the Pentagon's timing (`ulaProfile: 'pentagon'` in `core/machines.js`): 71680 ticks per
frame, no waits, no Even M1 (a Scorpion fetch from RAM that would start on an odd tick waits one) and no
attribute bus (on a Scorpion an unused port reads the attribute being fetched). The probe sees a plain
clone with no waits and checks it as one, so everything it measures is as expected. The result is `ok`, but
the two Scorpion features are simply not there. On unreal-ng the probe finds both: its report says "Even M1" and
it runs the attribute-bus check P-02, which is skipped on ZX-M8XXX's Scorpion.

## Files

| File | What it is |
|:--|:--|
| `run.sh` | the runner: discovery, the machine table, ROMs, one `driver.py` per machine, the compare |
| `driver.py` | web server + headless Chrome for one machine; writes the dump and the screen text |
| `harness.html` | the page Chrome opens: drives ZX-M8XXX through `window.zxDebug` and posts the results back |
| `build/` | browser profiles while a machine runs (ignored by git) |
