# 018b - TSConf Debugger TUI (157x30 fork)

The TSConf fork of the classic debugger (TDD-DBG-02): same base as
`../classic/` (independent copy, no cross-references) plus the 157-column fork
band - PC history panel, TSConf register board (16 controls in 3 columns) and
the banks-window variant. The fork band is **cell-exact against the oracle
dumps** (chars and attrs), enforced by tests.

## Build and run

```bash
cmake -S . -B build -G Ninja
ninja -C build
./build/dbgtsconf                # mock backend, terminal >= 157x30
./build/dbgtsconf-tests          # golden compare + ring/layout checks
```

Backend selection is identical to classic:
`--backend mock|rest [--endpoint <url>]`. Under `rest` a plain 128K machine
serves no TSConf block (`GetTsConf` default returns false), so the board and
PC-history band stay blank while regs/trace/mem show live data - verified
against a running emulator.

## Keys (fork slice)

| Key | Action |
|-----|--------|
| `F5` / `r` | run until break |
| `F11` / `s` | single step |
| `F9` / `b` | toggle BPX at trace cursor |
| `` ` `` (TIL) | continue until next stop (fork continue key, section 0.1) |
| Up / Down | move trace cursor |
| `1` `2` `3` `4` | focus regs / trace / mem / **banks** |
| Tab / Shift-Tab | cycle REGS -> TRACE -> MEM -> BANKS |
| mouse click | select a board control (visual-only, section 5.3) |
| `t` | reset time-delta mark |
| `q` / `Esc` / `F10` | quit |

## Fork additions over classic

- `model.h`: `TsConfState` (raw regs + decoded fields), `TsDmaState`,
  `PcHistEntry{page, addr}`; snapshot carries `isTsConf/ts/pcHist`.
- `debugger-backend.h`: `GetPcHistory()` / `GetTsConf()` with default no-op
  implementations - backends without TSConf data compile untouched.
- `mock-backend.cpp`: PC-history ring (cap 32) recorded from the z80ex
  `mem_read` M1 signal (each DD/FD/ED/CB prefix fetch is a separate entry,
  matching the original debugger), seeded from the golden sample.
- `paint-tsconf.cpp`: port of the oracle Canvas/Control widget engine
  (tab stops, bit columns, hex24/hl-port/led rows, decoder tables) and the
  16 control painters laid out at x=88/111/134, including the oracle quirks
  (leds on title rows, DMA blank rows, "4:"-labelled cache rows, inverted
  W0_RAM/W0_MAP bits, the `h=` row below HSINT).
- `side.cpp`: banks window fork variant (ROM 0..RAM 0 pages, selection and
  active-window attributes) and raw STAT.

## Golden fixtures

`tests/golden/tsconf.txt` + `tsconf_attr.txt` are dumps of the oracle
(`docs/inprogress/2026-09-24-tui-debugger/unreal_dbg_render.py`). The test
binary compares the painted screen cell-by-cell over the fork band
(x80..156, all 30 rows) and the banks window (x72..78, rows 22..26), then
checks the 16 placed board rects and the PC-ring behavior (seeded 28 entries,
step inserts newest-at-front, ring caps at 32).

Regenerate after changing the oracle sample:

```python
import unreal_dbg_render as r
st2 = r.sample_state()
st2["banks"] = [("ROM 0", True), ("RAM 5", False), ("RAM 2", False), ("RAM 0", False)]
S2, placed = r.debugscr(st2, "tsconf")
open("tests/golden/tsconf.txt", "w").write(r.dump_text(S2))
open("tests/golden/tsconf_attr.txt", "w").write(r.dump_attr(S2))
```

`dump_text` writes 2 header rows before row 0; `dump_attr` writes none - the
test decoder accounts for both formats.

## Next steps

1. Same classic-band polish as `../classic/` (v0-approximate attrs).
2. Serve `GetTsConf`/`GetPcHistory` from a TSConf-capable WebAPI machine
   (server endpoints needed) so the board shows live data under `rest`.
3. Board selection beyond visual highlight (edit dialogs) once the modal
   subsystem lands.
