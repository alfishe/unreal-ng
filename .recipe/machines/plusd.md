# Recipe: ZX-Spectrum 48K / 128K + MGT +D (G+DOS) or DISCiPLE (GDOS 3)

A Sinclair 48K or 128K with the MGT +D disk interface on its edge connector: G+DOS v1a, a WD1772 with two drives
(`.mgt`, 800 KB), 8 KB of RAM and the snapshot button. The target for the MGT software of the late 1980s.

Ground truth: the variants in [machinevariants.cpp](../../core/src/emulator/machinevariants.cpp) (`48K-PLUSD`,
`128K-PLUSD`), the card in [plusdcard.cpp](../../core/src/emulator/slots/cards/plusd/plusdcard.cpp), the autostart
in [plusdautostart.cpp](../../core/src/emulator/slots/cards/plusd/plusdautostart.cpp), the design in [docs/inprogress/2026-11-15-mgt-plus-d/](../../docs/inprogress/2026-11-15-mgt-plus-d/README.md).

## Setup

- **The machines:** `48K-PLUSD` (alias `48K-PLUS-D`) and `128K-PLUSD` (alias `128K-PLUS-D`), variants of `48K` /
  `128K` with `edge.1 = plusd`. Every automation surface creates them by name and lists them with the models
  (`variant: true`, `base_model`); unreal-qt and the handheld menu show them in the **Sinclair** family, after their
  base model. Any other machine takes the card through its slots (`plusd` in an edge slot), if the slot planner
  agrees.
- **What it does:**
  - after a reset the card is paged out; it pages in on an opcode fetch at `#0008`, `#003A`, `#0066`, `#028E` and on
    `IN #E7`, out on `OUT #E7`; paged in, the CPU sees its ROM at `#0000-#1FFF` and its RAM at `#2000-#3FFF`;
  - the WD1772 at `#E3 #EB #F3 #FB`, the control latch at `#EF` (drive, side, density, printer strobe);
  - drives `fdd.plusd1` / `fdd.plusd2` (aliases `plusd1`, `plusd2`);
  - G+DOS loads `+SYS` from drive 1 on `RUN`. A disk without `+SYS` needs the system disk first:
    [data/boot/plusd/gdos-2a-system.mgt](../../data/boot/plusd/README.md) (the autostart does this itself).
- **Which machine for a work:** a type 9 (128K) snapshot, a 128K loader or a work that needs 128 BASIC goes on
  `128K-PLUSD`; the rest runs on `48K-PLUSD`.

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"128K-PLUSD"}
load_software {"path":"/path/work.mgt","autostart":true}      # plan: run / run+load / system-disk+load / mount
load_software {"path":"/path/work.mgt","autostart":true,"basic":"48"}   # a 128K reset into 48 BASIC
inspect_state {"aspects":["fdc"]}                               # the +D node: autostart stage, steps, message
```

## WebAPI

```bash
B=http://127.0.0.1:8090/api/v1
id=$(curl -s -X POST $B/emulator/start -H 'Content-Type: application/json' -d '{"model":"48K-PLUSD"}' | jq -r .id)
curl -s -X POST $B/emulator/$id/disk/A/insert -H 'Content-Type: application/json' \
     -d '{"path":"/path/work.mgt","autostart":true}'
curl -s $B/emulator/$id/state/fdc | jq .autostart               # stage, steps [{step, frame}], message
```

`basic` (WebAPI / MCP; Python / Lua `disk_load` argument, CLI `disk insert A <file> autostart 48`, action `media.open`):
omitted = the machine's own BASIC (48 on a 48K, 128 BASIC on a 128K), `48` = 48 BASIC on any machine (works that need
a 128K in 48K mode), `128` = 128 BASIC (refused on a 48K: `no 128 BASIC on a 48K: use 128K-PLUSD`).

An `.mgt` on a machine without an MGT card is refused (`no MGT interface on this machine`), never mounted in a Beta
drive.

## DISCiPLE

`48K-DISCIPLE` / `128K-DISCIPLE` (`edge.1 = disciple`) carry the +D's predecessor, the same card class on other ports
([hardware-reference.md §9](../../docs/inprogress/2026-11-15-mgt-plus-d/hardware-reference.md#9-disciple-p7)):

- GDOS 3 ROM (`data/rom/gdos.rom`); traps `#0001`, `#0008`, `#0066`, `#028E`; `IN #BB` pages in, `OUT #BB` out;
  `OUT #7B` puts the RAM at `#0000` (`IN #7B` back); for 35 T after a reset no trap counts, so the machine's own
  reset and the 128K menu run;
- the WD1772 at `#1B #5B #9B #DB`, the control latch at `#1F` (drive 1 / 2, side, `/DDEN`, inhibit override,
  printer strobe); `IN #1F` reads the Kempston joystick (bits 0-4);
- drives `fdd.disciple1` / `fdd.disciple2` (aliases `disciple1`, `disciple2`); the slot option `inhibit=on`
  (`[SLOTS] edge.1.inhibit = on`, MCP `slots_set` `"options":"inhibit=on"`) presses the inhibit button: no paging unless the latch's bit 4 lets it;
- GDOS loads the first CODE file named `SYS*` from drive 1 on `RUN` (G+DOS disks' `+SYS` do not boot it). A disk
  without one needs the system disk first: [data/boot/disciple/gdos-3d-system.mgt](../../data/boot/disciple/README.md)
  (the autostart does this itself, the same plans as on the +D);
- `state fdc` shows `WD1772 (MGT DISCiPLE)` with `memory_swapped`, `reset_hold`, `inhibit`, `single_density`;
- `.z80` saves byte 83 = 0 (DISCiPLE, Epson); SZX has no DISCiPLE block: its card state is not in an `.szx`.

```text
emulator_manage {"action":"create","model":"48K-DISCIPLE"}
load_software {"path":"/path/work.mgt","autostart":true}
```
