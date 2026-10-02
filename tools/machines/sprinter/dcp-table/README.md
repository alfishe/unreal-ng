# dcp-table

Decodes the Sprinter port table (the "DCP" page `#40`): for every port and decoder map, the internal
device code the PLD dispatches to. Python 3, no dependencies.

```bash
python3 tools/machines/sprinter/dcp-table/dcp-table.py --rom data/rom/sprinter/sp2k-3.04.rom   # print map 0
python3 tools/machines/sprinter/dcp-table/dcp-table.py --selftest                              # built-in check
```

Other options (`--map`, `--dump`, `--compare-page`, `--compare-records`, `--records`) are listed by
`--help`. Background: [hardware-reference.md](../../../../docs/inprogress/2026-09-28-sprinter/hardware-reference.md) §4.4.
