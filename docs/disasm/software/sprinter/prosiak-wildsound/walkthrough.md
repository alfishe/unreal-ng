# prosiak.exe reverse engineering: walkthrough

A step-by-step log of how `DEMOS/WILDSND/prosiak.exe` (the Wild Sound XM player for the Sprinter) was
unpacked and disassembled with the unreal-ng time-travel debugger (TTD) through MCP, on 2026-10-08. It
records the calls that helped, the ones that worked badly, and the ones that were missing. Results:
[README.md](README.md).

Setup: BIOS 3.06 Hotfix 2 (the default), the MAME pack's `sp_hdd_sys.chd` (DSS 1.71) in
`ide0.master`, Flex Navigator 1.15. MCP calls are written `tool {"args"}`; most went through a 20-line
Python helper that posts `tools/call` to `:8092/mcp` and prints `content` / `structuredContent`.

## 0. Getting MCP up

- `.mcp.json` starts `cmake-build-release/bin/unreal-mcp-bridge`. That build directory held only empty
  `.app` bundles (no bridge, no `unreal-qt` binary), so Claude Code reported `ENOENT` and `/mcp`
  reconnect failed the same way.
- No emulator was running (ports 8090 / 8092 free), so even a working bridge would have had nothing to
  talk to.
- Fix: build `unreal-qt` and `unreal-mcp-bridge` with `tools/build/build.sh` (the agent build
  directory), start `unreal-qt` from there and use MCP over HTTP (`POST :8092/mcp`); later copy the
  bridge to the path `.mcp.json` names, after which `/mcp` connected.
- **Friction:** both build slots were busy with other sessions; the build queued for several minutes.
- **Missing:** the bridge path in `.mcp.json` is one build directory. A bridge that is found anywhere,
  or an error that says "no emulator on :8092" instead of `ENOENT`, would have shown the cause at once.

## 1. Recording the run

```text
emulator_manage {"action":"create","model":"SPRINTER"}
media {"action":"insert","slot":"ide0.master","path":".../sp_hdd_sys.chd"}
emulator_manage {"action":"reset"};  control_execution {"action":"run_frames","frames":1500}
time_travel {"action":"start","journal":true}
type_input {"action":"type","text":"demos\\wildsnd\\prosiak.exe","delay_frames":6};  run_frames 400
type_input {"action":"tap","key":"enter","frames":3};  run_frames 400
time_travel {"action":"stop"}       # frames 1560..2360, 801 checkpoints
```

The program came back to Flex Navigator within the first frames; the panel redraw hid what it printed.

- **Bad:** `emulator_manage create` answered "Created and started emulator ... (model )": the model
  name is empty. `media insert` printed its CHD note twice.
- **Bad:** `inspect_state sprinter_text` gave "0 text squares" and `screen_ocr` gave `????` on Flex
  Navigator's graphics-mode text; every look at the screen needed a screenshot.
- **Bad:** `capture_media screenshot` with `path` URL-encodes it: `/Volumes/.../boot.png` became a file
  named `%2FVolumes%2F...%2Fboot.png` in the emulator's working directory (the project root), and a
  relative `scratch/prosiak/x.png` became `scratch%2Fprosiak%2Fx.png` there. Every screenshot needed a
  `mv` afterwards.
- **Bad:** `time_travel status` while recording showed "frames 1560..1561, 2 checkpoint(s)" when
  `position` said frame 2360; after `stop` it showed the real 1560..2360 and 801 checkpoints.

## 2. The entry point and the header

The header bytes after `"EXE"` did not match the 22-byte header of other Sprinter programs, so the
start address `#4409` at `+#12` was a guess. TTD settled it:

```text
time_travel {"action":"find_last","access":"execute","addr":"0x4409","before_frame":2360}
#   → Last execute at frame 1969 t=78540 by PC 0x4409, RAM page 236
time_travel {"action":"seek","frame":1969,"tinframe":78540}
inspect_state {"aspects":["registers","paging"]}     # window 1 = page 236
```

`time_travel memory_at {"space":"ram236","frame":1969}` read the physical page; the code at `#0327` of
the page equals file offset `#24347`, which is the dword at header `+4`. So: `+4` loader offset, `+8`
loader length, `+#10/#12/#14` load / start / SP; the loader sits at the end of the file.

- **Helped:** `find_last execute` answers "when did this address run" in one call, with the physical
  page. It carried the whole session.
- **Helped:** `memory_at` with `ramN` reads a physical page at a past frame without moving the machine.
- **Bad:** `memory_at` ignores `tinframe`: `frame 1969, tinframe 297852` returned the page as at the
  start of frame 1969 (still a DSS directory buffer), with `"exact": true`. Reads inside a frame had to
  go through `seek` and `memory_access read` instead.

## 3. Stage 1 and the packer

`z80dasm` on the loader (extracted from the file) showed the banner "Boot Loader For Sprinter By
Rob F.", DSS `GetMem` / BIOS `GetMemBlkPages` calls, a `#90`-byte routine copied to `#3D4F` and called
with HL = `#44EA`, DE = `#0000`, then `CALL #0100`. The routine's shape (token bits `XYZ LL MMM`, a
nibble reader, offsets selected by the top token bits) is LZSA2.

`find_last execute #43C4` gave the moment of `CALL #0100`; `seek` there and `memory_access read
#0000-#0606` gave the unpacked stage 2 as the machine had it.

A first Python decoder written from memory of the format diverged at once (wrong offset formulas). The
official tool settled it:

```bash
git clone https://github.com/emmanuel-marty/lzsa && make -C lzsa
lzsa -d -r -f 2 stage1.lzsa2 stage1.out      # 1543 bytes = the 1543 bytes in memory, byte for byte
```

The Python decoder was then rewritten after `expand_block_v2.c` and checked against `lzsa` on every
stream of the file ([tools/lzsa2.py](tools/lzsa2.py)).

- **Bad:** `debug_code disassemble` returns only a header line in `content`; the instructions are in
  `structuredContent` alone, so a text-only client sees nothing.
- **Missing:** saving a memory range at a TTD point to a host file. `memory_access save` takes device
  regions only, not the CPU view or a RAM page; dumps went through `read` + hex parsing.

## 4. Stage 2 is a VM

Stage 2 ends its start-up in a loop at `#024A` that loads a byte from the buffer `#4106` and stores it
as the displacement of a `JR` at `#0252`. To see the script as it ran, every opcode fetch was listed by
walking `find_last` backwards:

```python
fr, tin = 1972, 0
while True:
    r = find_last(access="execute", addr="0x024d", before_frame=fr, before_tin=tin)
    if not r["found"] or r["frame"] < 1969: break
    fr, tin = r["frame"], r["tinframe"]
    seek(fr, tin); regs = inspect_state(["registers"]); bytes_at(regs.HL, 12)
```

Two fetches: `05 80 0000 487F 0556` and `24 00 45`, then nothing: the program left through another
path. The same walk on the DSS trampoline (`#3DE5`, C = function) found 123 calls: 4 `Move_FP` + 4
`Read` (about `#700` bytes of the 150 003), 114 `PutChar`, one `PChars`. The rest of the file was never
read in this run.

- **Helped:** `find_last` with `before_frame` / `before_tin` makes a backward iterator.
- **Missing:** a query for **all** executions of an address in a frame range, with the registers at
  each hit (a trace point). The walk above costs three calls per hit (`find_last`, `seek`,
  `inspect_state`); 123 DSS calls took about 370 round trips. A `find_all` returning
  `[(frame, t, regs)]` would be one call.

## 5. Stage 3 and the probe

Op `05` unpacked block 0 (file `#180`; its first word is the unpacked length `#8D5`) to `#4500`; op
`24` called it. `seek` to frame 1972 and a screenshot showed the text before Flex Navigator redrew:

```text
Boot Loader For Sprinter By Rob F.
This Text From Shadow Page ...
!!! WOW HOW DOW !!!
XM Init ... OK
Wild Sound ... ISA #0 Not Found
Wild Sound ... ISA #1 Not Found
Done ...
```

The probe routine (`#4D47`) sends `#F0` and "Wild Sound Enable " and compares the answer with `#DF`.
`find_last execute #4D88` (the `CP #DF`) twice, with `inspect_state registers`: A = `#FF` in both slots.

Later, `invoke_api GET /api/v1/emulator/{id}/state/isa/journal` at the same TTD point listed the probe
cycle by cycle (`#F0` to port 0, the 18 characters and two zeros to port 1, the read of `#FF`).

- **Helped:** `search_api` found the ISA endpoints (`/state/isa`, `/state/isa/journal`) that no MCP
  tool exposes directly.
- **Bad:** the ISA journal numbers slots from 1 while the program and the page numbers (`#D4` / `#D6`)
  count from 0, and its `t` is on another scale than the TTD's `tinframe` (the probe's read is at
  `t=38404`, the following `CP #DF` at `tinframe` 230676 of the same frame).
- **Missing:** a TTD search over ISA cycles. `port_events` covers `IN` / `OUT`; the Sprinter's ISA is
  memory-mapped, so "when did the program write `#F0` to ISA port 0" has no query. The journal holds
  64 entries of the present moment only.

## 6. Forcing the probe

To see what the player does next, A was set to `#DF` at the `CP` and the run continued as a new branch
of the history (the old one saved first: `time_travel dump`).

1. `seek` to the `CP`, `invoke_api PUT /registers/a {"value":223}`, `time_travel resume`: the output
   was unchanged. `resume` restarts from the recorded state of that point, and the register edit made
   while detached was not part of it.
2. `control_execution bp_add` on `#4D88`, `seek` a little earlier, `resume` (the recording continues
   live and stops on the breakpoint), `PUT /registers/a`, `bp_clear`, `run_frames 600`, `stop`.
   This worked: "Wild Sound ... ISA #0" and then a loop.

The loop is `WaitReady` (`#465C`): command `#F3`, a 32-byte status frame from port 1, signature
`#FA #3D`, a checksum. With no card the frame is `#FF` bytes and the player waits for ever, flashing the
border. Past this point a card model is needed.

- **Helped:** `dump` before `resume`, so the first history stayed available as a file.
- **Bad:** a register edit while detached is dropped silently by `resume`. Either record it as a tool
  edit (like memory writes) or refuse it with a note.
- **Missing:** a register write in a tool (`control_execution` has `port_out` but no register set);
  it went through `invoke_api PUT /registers/{name}`.

## 7. The rest of the file without running it

The script itself is in the file at `#100`. Its `05` records chain: each block ends where the next
begins, and the last one ends at `#24347`, the loader. Decoding the eight records with `lzsa` gave
lengths equal to their length words, and the stream of each record is consumed exactly to its
end-of-data marker. Block 0 equals what TTD showed at `#4500`; block 1 is track data with the name
"Prosiak", block 2 instrument headers, blocks 3-7 8-bit samples. No block uses the XOR path.

[tools/unpack_prosiak.py](tools/unpack_prosiak.py) does all of this from the file;
[tools/make_listings.py](tools/make_listings.py) rebuilds the listings with the names and comments.

## 8. MCP: summary

| | Call | Note |
|---|---|---|
| Helped | `time_travel find_last execute` | the backbone: entry, stage switches, every hit of an address (backwards) |
| Helped | `time_travel seek` + `inspect_state registers, paging` | the exact machine at each hit |
| Helped | `memory_access read` after a seek | ground truth for the unpacked code |
| Helped | `time_travel memory_at ramN` | a physical page at a past frame (frame boundary only) |
| Helped | `time_travel dump` / `resume`, `control_execution bp_add` / `wait` | a second branch of the history with a forced probe |
| Helped | `search_api` + `invoke_api` | ISA state and journal, register write |
| Helped | `capture_media screenshot` after a seek | the program's text before the redraw |
| Bad | `capture_media screenshot path` | URL-encoded file name, written to the emulator's working directory |
| Bad | `time_travel status` while recording | stale frame range and checkpoint count |
| Bad | `time_travel memory_at` | `tinframe` ignored, reported as exact |
| Bad | `time_travel memory_diff` | at most 100 byte ranges (`truncated`), no per-page summary |
| Bad | `debug_code disassemble` | instructions only in `structuredContent` |
| Bad | `time_travel resume` after a detached register edit | edit dropped without a word |
| Bad | `inspect_state sprinter_text`, `screen_ocr` | no text from Flex Navigator's graphics mode |
| Bad | `emulator_manage create`, `media insert` | empty model name; duplicated note |
| Missing | `find_all` / trace points | every hit of an address in a range with registers, one call |
| Missing | TTD query over ISA cycles | memory-mapped ISA is invisible to `port_events` |
| Missing | save a CPU range or RAM page at a TTD point to a file | `memory_access save` is regions-only |
| Missing | register write in a tool | `invoke_api PUT /registers/{name}` meanwhile |
| Missing | a scripted ISA responder | answer `#DF` and status frames from a script, to walk the player further without writing a device |
