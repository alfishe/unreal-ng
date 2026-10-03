# Crash: the debugger read a null memory window during a Sprinter reset (2026-10-02)

## Symptom

unreal-qt crashed twice with `EXC_BAD_ACCESS` at address `0x0` on the UI thread while the owner's
demo runner (`tools/machines/sprinter/demo-runner/`) drove a Sprinter (BIOS 3.06, MAME-pack HDD)
with the Debugger window open:

- 22:26, after `DEMOS/BUYAN/wave6fb.exe`: `Memory::DirectReadFromZ80Memory` <-
  `DisassemblerWidget::getNextCommandAddress` <- ... <- `DebuggerWindow::updateState`;
- 22:29, after `DEMOS/EXAMPLES/splines.exe`: `Memory::DirectReadFromZ80Memory` <-
  `StackWidget::readStackIntoArray` <- `DebuggerWindow::updateState`.

Both times the runner had just sent `POST /reset` to boot the next program.

## Root cause

`Core::Reset` resets the devices in order. `Memory::Reset` comes first and puts the generic 48K
layout into the four windows (`DefaultBanksFor48k`); the port decoder's reset, later in the same
function, maps the model's own layout. The 48K layout puts `base_sos_rom` into window 0. The
Sprinter has no 48K ROM role (its Spectrum ROMs are RAM pages, "vROM"), so `ROM::LoadROM` sets
`base_sos_rom = nullptr`, and window 0 was **null** between the two steps.

The WebAPI reset runs on its own thread. The debugger refreshes on the UI thread when the
emulator posts a state change, so it can read memory in that gap: the disassembler around
`PC = 0` and the stack around `SP` (`#FFFF + 1` wraps to `#0000`) both read window 0, at host
address `0x0`.

Nothing in the Sprinter's own mapping (`SprinterMemory`: ROM, fast RAM, vROM, graphics pages,
the ISA view, the reset page, the loader layout) ever stores a null: every path maps a real page.
But the tool read ignored the Sprinter's read redirect, so the debugger showed the RAM page behind
a graphics page or the ISA view, not what the CPU reads.

Two smaller gaps of the same kind: the `Memory` constructor left `_bank_read` / `_bank_write`
uninitialized until the first reset, and `SetROM48k` / `SetROM128k` installed a null ROM role
(`SetROMDOS` / `SetROMSystem` already refused that).

## Fix

- **A window is never null.** `DefaultBanksFor48k` uses ROM page 0 when the model has no 48K ROM;
  the constructor fills all four windows; `SetROM48k` / `SetROM128k` keep the current bank when
  their ROM role is null, like `SetROMDOS` / `SetROMSystem`.
- **Tool reads see what the CPU reads.** `DirectReadFromZ80Memory` (the debugger, WebAPI, CLI,
  Lua, Python, GDB / DeZog reads) asks the model through `Memory::ToolReadRedirect` when
  `_toolReadRedirect` is set. `SprinterMemory` sets it whenever a window has a read redirect and
  answers with the same side-effect-free `Redirect` the CPU read uses: a graphics page reads main
  RAM at the video address, the ISA view reads `#FF`, the loader reads fast RAM above the
  Z84C15 CS0 boundary. Every other machine keeps the flag false; the test is one load and one
  branch on a tool path that the emulated CPU never takes, so no A/B is needed.

The raw page views (`MapZ80AddressToPhysicalAddress`, `GetPhysicalAddressForZ80Page`: the
memory dock and the 16 KB page widget) still show the page that is mapped, which is what a
physical page view is for; they now always get a valid page.

## Tests

`core/tests/emulator/machines/sprinter/sprintermemory_test.cpp`:

- `ToolReads_ResetIntermediateStateHasNoNullWindow`: the state between `Memory::Reset` and the
  decoder's reset; every window reads through the debugger's calls (direct read, raw page
  pointer, page address), window 0 = ROM page 0; the four ROM-role switches keep a valid bank.
  Without the fix it fails with SIGSEGV.
- `ToolReads_MatchCpuReadsInEveryMapping`: the tool read equals the CPU read in every mapping
  (port table page while starting, system ROM, fast RAM, vROM, graphics pages, the ISA pages
  `#D0`/`#D2`/`#D4`/`#D6`, the reset page with no reset requested, the Covox-Blaster page, page
  `#FF`, plain RAM again).
- `ToolReads_LoaderLayoutSeesFastRamAboveCs0`: the unconfigured PLD.
