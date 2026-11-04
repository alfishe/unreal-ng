# unreal-tui-debugger-tsconf - the TS-Conf debugger (157x30 fork)

The TS-Conf fork of the classic debugger (TDD-DBG-02): the classic base (`../classic/`, the shared sources in
`../common/`) plus the 157-column fork band - PC history panel, TS-Conf register board (16 controls in 3 columns) and
the banks-window variant. The fork band is **cell-exact against the oracle dumps** (chars and attrs), enforced by
tests. Started as POC 018b ([tools/poc/018-tui-debuggers](../../../tools/poc/018-tui-debuggers/README.md)).

## Build and run

```bash
tools/build/build.sh unreal-tui-debugger-tsconf tui-debugger-tests
cmake-build-agent-release/bin/unreal-tui-debugger-tsconf          # ws://localhost:8090, terminal >= 157x30
cmake-build-agent-release/bin/unreal-tui-debugger-tsconf-tests    # golden compare + ring/layout checks
```

Backend selection is identical to classic: `--backend ws|rest|mock [--endpoint <url>]`, `ws` by default. Over `ws` or
`rest` the board comes from `GET /state/tsconf` and the PC history from `GET /debug/pchist`
(`src/backend/tsconf-webapi-backend.*`).

## Keys

As in the classic debugger ([../../README.md](../../README.md#run)), with the fork's continue key `` ` `` (TIL) in place
of Esc. A mouse click on the board selects a control (visual only, section 5.3).

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
2. Board selection beyond visual highlight (edit dialogs) once the modal
   subsystem lands.
