# Disassembly window

`core/src/debugger/disassembler/disassemblywindow.h` (`disasm::Window`). A virtual window over a CPU's 64K that
decodes only the rows asked for: rows from an address forward, rows that end at an address, and the row start a
scrollbar drag snaps to. Every surface lists code through it: `GET /disasm`, the debugger snapshot, CLI `disasm`,
Python / Lua `disasm`, MCP `debug_code disassemble`, the card CPUs (`cpu=gs` / `ms`). Design and requirements:
[docs/inprogress/2026-11-05-disasm-virtual-scroll](../../../inprogress/2026-11-05-disasm-virtual-scroll/README.md).

## Rows

| Kind | `kind` field | When |
|:--|:--|:--|
| Instruction | absent | the bytes decode to an instruction that ends before the next boundary |
| Data | `data` | a data range of the memory markup: `db` rows of at most 8 bytes, counted from the range's start or the last anchor in it |
| Cut | `cut` | an instruction would run across a boundary (an anchor, a data range, `#FFFF`): its bytes up to there are one `db` row |

A boundary is an **anchor** (an address known to start an instruction) or the first byte of a data range or the
byte after its end. Anchors of the main CPU:

- PC;
- labels, except `data` and `const` ones, and only on the page mapped at the address now;
- active execution breakpoints at one address;
- the memory markup's instruction starts (`MemoryMarkup::Anchors`: entries, the first bytes of code claims; the runtime
  layer's opcode fetches when the analysis facts or the coverage are on).

Data ranges are the markup's winning spans of any kind but `code`; a span holding PC ends before PC. A card CPU has
no anchors or data ranges: its window reads only its bytes.

## Backward: a local sync

The rows above an address A come from a short stretch below it, never from a listing of the 64K:

1. Inside a data range: the row of the range's 8-byte count when it ends right before A, else the 8 bytes before
   A (entered mid-row).
2. Else the last boundary at most 64 bytes below A: when decoding forward from it lands on A, its rows.
3. Else every start offset of the 32 bytes below A (above that boundary) decodes forward; the chains that land on A
   vote for the row before A, and the one most of them agree on wins (Z80 chains converge in a few instructions).
4. None lands (A is mid-instruction whichever way): the byte before A is a one-byte `cut` row.

Forward decoding from the first row of the result shows the same rows down to A, so a view that scrolls up N rows
and back down N rows shows what it showed (W3). Row lengths are cached per stretch: about 70 decodes for a screen
scrolled up.

## Snap

`SnapToInstruction(address)`: the address when an anchor is there; inside a data range the start of its 8-byte row;
else the start of the row holding the address on the chain from the last boundary, or the one most start offsets
agree on.

## Planes

| Plane | Form |
|:--|:--|
| REST / WebSocket `call` | `GET /disasm?address=&count=&before=&snap=1&cpu=` |
| MCP | `debug_code` `disassemble` with `before`, `snap` |
| CLI | `disasm [address] [count] [before]` |
| Python | `emu.disasm(address=-1, count=10, cpu="", before=0)` |
| Lua | `disasm(address, count, before)` or `disasm(address, count, cpu, before)` |

The answer of `GET /disasm`: `address` (after the snap), `count`, `before` (when asked), `instructions[]` with
`address`, `bytes`, `mnemonic`, `size`, `kind` (data rows), `label`, `target`, `targetLabel`, `displacement`,
`effectiveAddress`, `effectiveAddressLabel`.
