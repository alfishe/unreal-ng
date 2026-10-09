# Memory spaces on every automation surface

Status: step 1 done 2026-10-08 (one registry; MCP `memory_access`); steps 2-5 open. Owner request: "нужно ли как-то менять automation planes, чтобы иметь доступ ко всем возможностям и градациям памяти; если да - дизайн, делаем".

## 1. The problem

The emulator has more memories than the Z80 sees, and the automation surfaces reach them unevenly. A survey of WebAPI, CLI, Lua, Python, MCP, GDB, DeZog and ZEsarUX (2026-10-08) found:

- **Two registries that do not know each other.**
  - `DeviceMemory` (`devicememory.h`) is what the surfaces use for named regions. It lists only `vram` (Sprinter), `cram` and `sfile` (TS-Conf), `cmos` (RTC) and `eeprom` (ZX-Evo AVR).
  - The TTD engine's region list (`TTDRegionId`, `ITTDRegionSource`) has every large device memory: `gs.ram`, `neogs.ram`, `neogs.flash`, `moonsound.wave`, `sprinter.vram`, `sprinter.fastram`, `vdac2.*`, `evo-avr.eeprom`, `smuc.eeprom`, `evo.flash`, and the MultiSound GS RAM. None of these has raw access on any surface.
  - The names differ between the two (`vram` vs `sprinter.vram`).
- **Address forms that do not compose.**
  - Search (`mem_find`, `/memory/find`), windowed reads (`debug_snapshot` windows) and `memory save` take `cpu | ram | ramN | romN | cacheN` only (`MemorySearch::ParseSpace`). They cannot reach a region.
  - TTD find-last has its own hard-coded `space` (`ram | vram | cache`).
  - TTD coverage takes `phys_page` 0..255 only.
- **Missing capabilities, on every surface:**
  - watchpoints on a region (e.g. "stop when video RAM #4805 is written");
  - reading memory at a past TTD position without seeking;
  - comparing memory between two positions.
- **Uneven surfaces.**
  - MCP has no first-class memory write, page read, or region list, write, save or load; everything goes through `invoke_api`.
  - WebAPI `/memory/page/{type}/{n}` refuses `cache` / `misc`, while `/memory/{type}/{page}/{offset}` accepts them.
  - GDB physical access is RAM-only (`0x01PPAAAA`), and its `monitor ttd findlast` takes Z80 addresses only.
  - The Lua positional `ttd_find_last` has no `space` argument.

## 2. The model

**A memory space** is anything addressable by an offset. There are three kinds:

| Kind | Names | Offsets |
|---|---|---|
| The CPU view | `cpu` | Z80 address 0..#FFFF through the current banking |
| Machine pages | `ram`, `ramN`, `romN`, `cacheN` (and `rom`, `cache` whole) | page-relative, or whole-type when no N |
| Device memories | canonical dotted names: `sprinter.vram`, `sprinter.fastram`, `tsconf.cram`, `tsconf.sfile`, `rtc.cmos`, `evo-avr.eeprom`, `gs.ram`, `neogs.ram`, `neogs.flash`, `moonsound.wave`, `vdac2.ram_g`, `smuc.eeprom`, `evo.flash`, ... | 0..size-1 |

**One address form everywhere:** `space:offset`. Examples: `cpu:0x5C78`, `ram5:0x100`, `sprinter.vram:0x4805`, `neogs.ram:0x10000`. A bare number still means `cpu`. Ranges are `space:from-to` or a space with `addr_from` / `addr_to`.

**Names.** The canonical name is the TTD region's name, so one name means the same memory in the live machine, in a recording and in a session file. The old short names stay as aliases on every surface: `vram` → `sprinter.vram`, `cram` → `tsconf.cram`, `sfile` → `tsconf.sfile`, `cmos` → `rtc.cmos`, `eeprom` → `evo-avr.eeprom`, `cache` → `sprinter.fastram` (in TTD find-last). The region list shows both.

**One registry.** `DeviceMemory` becomes the only list the surfaces see. It is built from:

1. the regions machines declare today (`CollectMemoryRegions`), which keep their own write paths (side effects);
2. a generic adapter over every TTD region source (`ITTDRegionSource::TTDRegions`). Its `TTDRegionDesc` has the live memory pointer and size, which is enough for reads. Writes go to the bytes directly, as a tool edit (`EditMemoryFromTool` marks the pieces for TTD). A region that has a piece-restore function (memory where a write has side effects, e.g. VDAC2 registers) is read-only through the adapter. When both lists name the same memory, the declared region wins.

Every region in the list carries:

- `name`;
- `aliases`;
- `size`;
- `page_size`;
- `writable`;
- `write_path`;
- `ttd_region` (the TTD id, or none);
- `ttd_access`: whether the journal, probe and coverage see accesses to it — today only `sprinter.vram` and `sprinter.fastram`;
- `watchable`: whether watchpoints can fire on it.

## 3. What each capability gets

| Capability | Today | After |
|---|---|---|
| Read / write / save / load a region | 5 regions, not on MCP | every region in the registry, on every surface, MCP first-class |
| Search (`mem_find`, `/memory/find`, `find_bytes`) | `cpu`/pages | any space |
| Snapshot windows, `memory save space:addr:len` | `cpu`/pages | any space |
| TTD find-last `space` | `ram`/`vram`/`cache` hard-coded | any space with `ttd_access`; the list comes from the registry |
| TTD coverage | `phys_page` 0..255 | plus `space` (spaces with `ttd_access`) |
| TTD memory at a past position | seek, read, seek back | `ttd memory-at --space S --offset O --length N --frame F`: bytes of any TTD region at a checkpoint, from the engine's store, without touching the machine |
| TTD compare | none | `ttd memory-diff --space S --from F1 --to F2`: the byte ranges that differ between two checkpoints (the pieces whose versions differ, then a byte compare) |
| Watchpoints | Z80 address, RAM/ROM/cache page | plus `space` + offset range for `watchable` spaces (first: `sprinter.vram`, `sprinter.fastram`) |
| GDB | RAM pages | `monitor mem <space:offset> [len]`, `monitor ttd findlast <space:offset>` |

## 4. Limits, stated

- **TTD access tracking needs a hook in the device.**
  - The journal and the probe see an access only where the memory code reports it. Today that is the Sprinter's video RAM and fast RAM.
  - Memories written by another CPU (the GS / NeoGS Z80, the VDAC2 coprocessor) would need a CPU id in the record. A record names the main CPU's PC.
  - So find-last and coverage on those spaces are refused with a reason. Their contents are still readable now and at any checkpoint (`memory-at`), and comparable (`memory-diff`).
- **`memory-at` and `memory-diff` read checkpoint boundaries,** the frame starts the engine stores. A point inside a frame needs a replay: use seek.
- **Virtual pages.** The journal's page field has 512 values: 0..255 for RAM pages, 0x100..0x1FF for other spaces. That is 256 pages of 16 KB, 4 MB in all, shared out to the spaces with `ttd_access`. The Sprinter takes 0x110-0x12F. A space added later gets the next free range from a fixed table in `ttdphyspage.h` (stored in files, appended, never reused).

## 5. Steps

1. **One registry.**
   - The TTD-region adapter in `DeviceMemory`, canonical names with aliases, and the new list fields.
   - Read and write for every TTD region on all surfaces through the existing region verbs.
   - MCP gets first-class region list / read / write / save / load.
   - Tests: every machine's region list on every surface; a NeoGS RAM read and write; a VDAC2 register region read-only.
2. **One address form.**
   - `MemorySpace::Parse` (`space:offset`, names, aliases) replaces `MemorySearch::ParseSpace`.
   - Search, snapshot windows, `memory save`, WebAPI page endpoints (`cache`/`misc`), the Lua positional `space` and GDB `monitor mem` all go through it.
3. **TTD over spaces.**
   - find-last and coverage take `space` from the registry.
   - The new verbs `memory-at` and `memory-diff` on every surface.
4. **Watchpoints on spaces.**
   - A breakpoint descriptor gains `space` + an offset range.
   - The Sprinter's graphics-window and fast-RAM hooks fire them.
   - Surfaces: WebAPI, CLI, Lua, Python, MCP, DeZog (by bank where it maps).
5. **Docs and recipes.** The command reference, the interface docs, `.recipe/analysis/ttd-reverse-debugging.md`, and a memory-spaces recipe.

Each step lands on its own, with its tests, mutants and docs.

## 6. Done

**Step 1 (2026-10-08).**
- `DeviceMemory` lists every memory the engine records: `TtdRegionMemory` views over `ITTDRegionSource::TTDRegions`, from a registry of its own (`RegisterMachinePeripherals`).
- Canonical names come with the old ones as aliases, and the list shows `aliases` and `ttd_region`.
- The views are writable where the bytes are restored plainly and the dirty marks are the device's own. VDAC2 is read-only: its marks live in its serializer.
- MCP has the `memory_access` tool (regions / read / write / save / load, space `cpu` or a region).
- Found on the way: listing a device's regions bound its dirty tracker again, which cleared it. `EndToolEdit` lists them, so a debugger edit during a recording dropped the device memory written since the last checkpoint (NeoGS / GS RAM, MoonSound, EEPROMs, flash) from the recording. `TTDRegionTracker::Bind` keeps the marks for the same memory now (`DeviceMemoryTtd_Test.AToolEditDuringARecordingKeepsTheDevicesUnsavedWrites`).
- Mutants caught: Bind resetting, the view's missing mark, the alias match.
