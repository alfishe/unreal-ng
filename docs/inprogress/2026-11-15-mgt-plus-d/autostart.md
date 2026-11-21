# MGT +D: boot and autostart

How an MGT image gets from "insert" to "the work runs", for a person (drag and drop, menu) and for an agent
(`load_software`, `media insert` + `autostart`). Facts about G+DOS are in
[hardware-reference.md §7](hardware-reference.md#7-boot-gdos).

## 1. The problem

G+DOS lives on the disk, not in the ROM. The +D ROM only knows `RUN` (load `+SYS*`, then `AUTO*`). Most +D disks do not
carry `+SYS*`: they were made for a machine whose DOS was already in the +D RAM. So an autostart that only types
`RUN` would start few of them.

A real user did: boot the system disk (`RUN`), swap disks, `LOAD D1"Loader"` or `LOAD P1`. The autostart does the
same, with the emulator's own system disk.

## 2. The emulator's system disk

`data/boot/plusd/gdos-2a-system.mgt`: a clean G+DOS 2a system disk with only `+SYS 2a`.

Where it comes from (owner decision D-2, 2026-11-15): the emulator writes it itself by running the official Plus D
System Tape v2a (`CONFIG` with the default answers) on the +D once P2 works (task P0.4). It is reproducible and
documents the default configuration; the recipe goes into `data/boot/plusd/README.md` with the hashes. The clean
disk of the FD test set (S-DISK, body prefix `ba1582991bed6c7c`) is only compared byte by byte with it; the
differences are configuration bytes and are listed.

The disk is never written: it is inserted read-only, and the autostart ejects it before the work's disk goes in.

## 3. The autostart plan

`DiskAutostart` has a +D branch (`PlusDAutostart`, `core/src/emulator/slots/cards/plusd/plusdautostart.*`; the
TR-DOS and +3 branches stay as they are). The plan is chosen from the directory of the work's disk, read by the
G+DOS file system reader of unreal-disk (`GdosFileSystem`, also the `DiskFs` volume `gdos`), with no emulation:

| Plan | Disk holds | Steps |
|---|---|---|
| `run` | `+SYS*` and `AUTO*` | work in drive 1; reset; type `RUN` (the ROM loads both) |
| `run+load` | `+SYS*`, no `AUTO*` | work in drive 1; reset; `RUN`; wait for the DOS; the **start command** |
| `system-disk+load` | no `+SYS*` | system disk in drive 1 (read-only); reset; `RUN`; wait for the DOS; the work back in drive 1; the **start command** |
| `mount` | nothing to start | the work stays mounted, no reset; the result says why |

Start command, first match wins:

1. An `AUTO*` file (case-insensitive) that is BASIC or a snapshot.
2. A BASIC file named `loader`, `boot`, `menu` or `start` (case-insensitive, exact).
3. The first BASIC file in directory order.
4. The first snapshot in directory order (several snapshots are usually parts A, B, C of one adventure).
5. Only CODE -> `mount`, `no program to start`. A type 9 (128K) snapshot on a 48K machine -> `mount` with a hint
   naming `128K-PLUSD`.

The command is `LOAD d1"<name>"`, plus ` S` (type 5) or ` K` (type 9). The work's title is not used for matching.

Every step is a condition with a timeout of 500 frames (10 s emulated), not a delay:

- typing goes through the `CommandTyper` (it waits for the editor and leaves the 128K menu itself);
- "the DOS is loaded": the whole `+SYS` body was seen at the +D RAM's `#2000` (G+DOS rewrites the body's last byte
  as it starts, so the full match is latched; all but the last byte also counts), and the +D is paged out. The first
  bytes alone are not enough: they are there after the first sector, and swapping the disk then breaks the load.
  Then the ROM editor waits for a key (the editor monitor of the typer, armed for this wait only): the +D ROM's
  `RUN` has also looked for `AUTO*` on the system disk. A swap before that let the ROM load the work's `AUTO*` by
  chance (disks with `AUTO*` and no `+SYS`);
- "the disk is back": drive 1 holds an image that is not the system disk's;
- `RUN` and the start command are taken when the typer reports success (the line was accepted; a syntax error ends the plan).

A timeout ends the plan with a message naming the step, e.g. `the DOS did not load in 10 s (dos)`. The steps run on
the emulation thread at the frame end (`MainLoop`, after the typer); the reset is a coordinator op, the system disk
is inserted before it (a media insert may not run inside the op).

### Machine for the work

| Work says | Machine |
|---|---|
| a type 9 snapshot, `(128K)` in the title, a 128K `.z80` | Spectrum 128K |
| otherwise | Spectrum 48K |

The directory rule does not see every 128K work: some need 128 BASIC (`SAVE !`, `PLAY`, paging from BASIC) with
nothing in the directory saying so. Such a disk runs on `128K-PLUSD`.

The works were written for a Sinclair 48K / 128K / +2 with a +D. A Pentagon with the card is possible in the
emulator (its fit is the slot manager's to say, [design.md §3](design.md#3-the-card)), but it is not what the
authors ran, and its built-in Beta 128 shares the ROM area.

### The BASIC of the reset

The autostart's reset picks the BASIC itself, not the emulator's `RESET=` setting (its default, 48 BASIC, would
leave a 128K in 48K mode, and every 128K work would see a 48K):

| `basic` (the caller) | Reset | Note |
|---|---|---|
| empty (default) | the machine's own: 48 BASIC on a 48K, 128 BASIC from the 128K menu | |
| `48` | 48 BASIC (on a 128K: the 128K's ROM 1 with 128K paging free) | for works that need "128K in 48K mode" |
| `128` | 128 BASIC | refused on a 48K: `no 128 BASIC on a 48K: use 128K-PLUSD` |

`AutostartDisk(path, drive, basic)` takes it; the result reports it (`basic`). The planes get the field in P5.2.
The CommandTyper chooses `128 BASIC` from the menu before typing `RUN`.

## 4. What the automation sees

| Verb | Today | After |
|---|---|---|
| `load_software` (MCP) / `POST /emulator/{id}/load` with an `.mgt` | mounts in Beta drive A of the current machine, `autostart` refuses ("not TR-DOS") | with a +D on the machine: mounts in `fdd.plusd1`, autostart runs the plan of §3; without a +D: error `no +D on this machine: an MGT disk needs the +D card (models 48K-PLUSD, 128K-PLUSD, or plusd in an edge slot)`; drives other than 1 / 2 are refused |
| `media insert fdd.plusd1 <file>` | unknown slot | inserts; the alias `plusd1` resolves |
| `media` listing | Beta drives only | the +D drives with their controller |
| result fields | `autostarted: false` | `autostarted`, `plan` (`run` / `run+load` / `system-disk+load` / `mount`), `command`, `dos` (`disk` / `system-disk` / empty), `basic` (`48` / `128`) |
| `state fdc` | Beta only | the +D node has `autostart`: plan, dos, command, start_file, active, finished, ok, stage, message, `steps` [{step, frame since the reset}] |

Owner decision D-3 (2026-11-15): refuse, never switch the model on the caller's behalf. An MGT on a machine without
a +D is never mounted in a Beta drive any more: a Beta drive cannot read it (512-byte
sectors are readable, but TR-DOS cannot), and today that only produced a misleading "not TR-DOS" result.
