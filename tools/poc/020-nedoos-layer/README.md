# 020 - NedoOS layer

See and drive a running NedoOS from outside the emulated machine: tasks,
memory, pipes, sockets, open files, what the kernel is doing, kernel calls on
behalf of any task. A Python prototype over the unreal-ng WebAPI, written to
check the NedoOS layer requirements against a live system before the layer is
designed for the core.

**Status (2026-09-30):** all ten POC requirements met
([requirements.md](requirements.md)); results and conclusions in
[results.md](results.md). Next: the core design (`NedoOsAnalyzer`), then the
emulator prerequisites it found.

## Goal

Answer "what is NedoOS doing right now, and why" in one command, and act on
the answer. The test case was a real hang: `zxdb` on the full NedoOS card
freezes at "Sending request...". The POC shows that the whole OS stands still
inside a kernel network call waiting for a Wiznet chip that is not there, and
ends that call so the system recovers.

## Documents

| Document | Content |
|---|---|
| [requirements.md](requirements.md) | P-1..P-10: what the POC had to show, mapped to the product requirements |
| [results.md](results.md) | tool output on the live machine, the zxdb hang, build differences, emulator issues, conclusions |
| [requirements-nedoos-layer.md](../../../docs/inprogress/2026-09-30-nedoos-integration/requirements-nedoos-layer.md) | product requirements NK-1..NK-23 |
| [nedoos-kernel-reference.md](../../../docs/inprogress/2026-09-30-nedoos-integration/nedoos-kernel-reference.md) | kernel memory model, structures, addresses, kernel-call mechanics (what this code implements) |
| [2026-09-30-nedoos-integration](../../../docs/inprogress/2026-09-30-nedoos-integration/) | the design folder: NedoOS layer and network adapters |

## Files

| File | What |
|---|---|
| `nedoos-layer.py` | command-line tool |
| `nedooslayer/webapi.py` | minimal WebAPI client: physical pages, registers, breakpoints, pause / resume |
| `nedooslayer/kernel.py` | symbols (`user.l`), page roles, layout constants |
| `nedooslayer/layer.py` | read-only views: detect, tasks, task memory, page owners, pipes, sockets, files, kernel state |
| `nedooslayer/control.py` | trace, direct kernel call, ending a stuck network call |
| `generate-symbols.sh` | builds the kernel symbols from a NedoOS checkout (macOS / Linux, no IAR) |
| `symbols/evo-inetdrv1-44049473/` | symbols of the ZX-Evo Wiznet kernel (`sd_boot.$C`) at NedoOS 44049473, with the flags used |

Python 3.9+, standard library only.

## Running it

1. Start unreal-ng with a ZX-Evo (`ATM3`) and boot NedoOS from the SD card
   (`.recipe/machines/atm/atm3-zxevo-baseconf.md`, NedoOS section). The symbols match the
   `sd_boot.$C` kernel of NedoOS 44049473; check with `detect`.
2. Read-only views (pause, read, resume):

   ```bash
   ./nedoos-layer.py --url http://localhost:8090 detect
   ./nedoos-layer.py tasks        # or pages, pipes, sockets, files, kernel, all
   ```

3. Operations that change the machine (switch debug mode on for their
   duration, clean up after):

   ```bash
   ./nedoos-layer.py trace 40                    # sample kernel calls
   ./nedoos-layer.py call 6 GETAPPMAINPAGES 4    # CMD by name or number, then DE, HL
   ./nedoos-layer.py unstick                     # end a stuck Wiznet call
   ```

`--id` picks the emulator when several run in one process.

Symbols for another kernel:

```bash
./generate-symbols.sh <nedoos-checkout> evo|evo-esp <out-folder>
```

## Limits

- One kernel build (Evo, Wiznet). The ESP kernel's symbols build, but the
  socket view reads the Wiznet driver's table only.
- `label_at` searches the flat label list: a name printed for an address in
  the wrong page can be wrong. The core layer keeps a page role per label
  (from the sjasmplus `--sld` output).
- Trace and calls use WebAPI breakpoints: about 4 kernel calls per second.
- Program names come from the command line, file names are not resolved
  (needs a sector read of the SD image), socket addresses are not visible
  (only in the network chip).
