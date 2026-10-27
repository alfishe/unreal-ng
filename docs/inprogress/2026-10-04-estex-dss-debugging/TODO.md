# TODO: Estex DSS aware debugging

Index: [README.md](README.md).

## Remaining

- [x] Research: the DSS memory model, call mechanics (`RST #10` / `RST #08`), structures and addresses in a running
  system — done 2026-10-21 as [dss-system-state.md](dss-system-state.md) (Russian) + the detection playbook
  [leak-corruption-playbook.md](leak-corruption-playbook.md); addresses verified against the 1.71.66
  `system.exp` (`CORE_BUFFERS.*`), mechanics against the official 1.70b sources. A "kernel reference like the
  NedoOS one" in English can be distilled from these when the layer is built.
- [ ] Requirements for the DSS layer (calls, programs, files and drives, memory, symbols, actions), each on every
  automation surface and in Qt, numbered like NK-n — the playbook's classes A-G and the draft `dss.yaml` are the
  input for the memory/files part
- [ ] A Python prototype over the WebAPI (as POC 020 for NedoOS) checking the requirements on a live DSS
- [ ] First case: explain the "Unexpected application termination" of `TYPE` after leaving Flex Navigator —
  run it through the playbook (hypotheses: class C, leaked file handles, or class F, window/stack state after
  the return to Flex Navigator)

## Open questions from the research (2026-10-21)

- [ ] Full semantics of the BIOS memory commands (`RST #08` #C0/#C2/#C3/#C4/#9D/#9E, the 16-bit codes like
      #04C2 "GET DSS PAGES HANDLE", the matching release call) from **INC**
      (`zxgit/Shared_Includes`: `Docs/BIOS functions.asm`, `constants/BIOS_equ.inc`) — so far inferred from
      their use in the DSS sources
- [ ] Where the BIOS keeps its own handle/page tables (for a BIOS-level ledger without trapping every #C2/#C3;
      the fallback is the INFOMEM free-page delta + poison-fill)
- [ ] Confirm the 1.71 FM layout (`_sFM` stride 54, `FTASK` offset) and the ADRST10 table address against the
      1.71.66 `system.sym` / a live run; `DSS_MAP.TXT` in the source tree is the 1.70b layout
- [ ] GSWITCH multitasking model (Flex Navigator): which TASK ids are live at once, when LEAVE actually kills
      a task — decides the balance checks of classes A/C
- [ ] Poison-fill mode for freshly allocated BIOS pages (read-before-write detection) — needs core support

## Prototype order (research → requirements → WebAPI prototype → core, per plan row #99)

- [~] R0: syscall traps 0x0010/0x0008 with a `dss.yaml`-driven decoder - events into the event stream and the
      CLI log only. **Started 2026-10-23 on branch `wt-dss`** (worktree `scratch/wt-dss`): the `DssAnalyzer`
      (id `dss`) in `core/src/debugger/analyzers/dss/` - kernel detection by the window-0 shape plus the
      1.71.66 profile from its own `system.exp` (RETFAR 0x1CAC / RST_10 0x0068 / LEAVE 0x1E7E / TASK 0x1E72 /
      MemoryTable 0x38EB / FM_BUF 0x318E), traps on RST #10 / RST #08 / the 0x0000 RETFAR vector / LEAVE,
      decoded DSS + BIOS streams (quiet console and #C4 window-mapper calls counted, not streamed), unknown
      builds fall to a vector-only mode. The decoder is a compiled-in profile table for now; `dss.yaml`
      remains the Phase 6 target format. One `IOSAnalyzer` branch serves WebAPI/CLI/Python/Lua
      (`analyzer dss events|state`).
- [~] R1: memory/files ledger + findings A/B/C at the lifecycle points (LEAVE vector 0x0000, EXEC #40, GSWITCH)
      **on `wt-dss`**: MEMTAB diff polling after every memory-touching call, QUIT/LEAVE audit ("task exits
      owning blocks" - the kernel frees them, warning; "task exits leaving file handles open" - class C,
      nothing frees them, the shape of the `TYPE` case). EXEC #40 / GSWITCH lifecycle hooks and the acceptance
      `TYPE`-after-FlexNavigator run are still open (mutation tests below cover the audit math meanwhile).
- [ ] R2: checks D/E/F (system-zone checksum, ownership write, window restore) over TTD M1
- [ ] R3: "hungriness" metrics for a program run (peak pages, INFOMEM delta, FM handles) as a report
- [ ] Mutation tests: a mutant FM (no FREEMEM / no CLOSE / SETWIN of a foreign block / a write into MEMTAB /
      vector corruption) that every check must catch - the pattern of
      [2026-10-11-bus-analyzer/mutation-tests.md](../2026-10-11-bus-analyzer/mutation-tests.md)
- [ ] Symbols: load `system.exp` / `system.sym` next to the running `system.dos`
      ([estex-dss-build.md](../2026-09-28-sprinter/estex-dss-build.md) §3)
- [ ] FM handle names: DSS overwrites byte 0 of the name field on an open entry (drive/state), so
      live handles render as `YSTEM  BAT` - read the name from +1 or decode byte 0 (live 2026-10-24
      run in scratch/osaware-live/)
