# Recipe: Sprinter port trace side by side with MAME

Goal: check the Sprinter's I/O against MAME's `sprinter` driver access by
access - port, value, PC and the internal code the port table gives - from
power-on. The MAME side is already captured
([testdata/machines/sprinter/reference/ports.csv](../../testdata/machines/sprinter/reference/ports.csv):
the first 10 000 accesses of BIOS 3.04 with no media); new MAME captures come
from [tools/machines/sprinter/mame-capture/](../../tools/machines/sprinter/mame-capture/README.md).

Background: every Sprinter port access reads a code from the table in RAM
page `#40` ([machines/sprinter.md](../machines/sprinter.md#ports-the-table-and-the-codes));
unreal-ng's port trace records it as `code` / `code_name`
([port-trace.md](port-trace.md#internal-port-codes-zx-evo-sprinter)), MAME's
capture as `index` / `code`.

> **How to use the sections:** the capture runs through [MCP](#mcp-preferred)
> or [WebAPI](#webapi); the comparison is a host-side Python step (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
emulator_manage   {"action":"create","model":"SPRINTER","sprinter_bios":"3.04"}   # the MAME capture is BIOS 3.04 (the default is 3.07). [SPRINTER] FastStart=0 (the default): the full start, like MAME
control_execution {"action":"pause"}
invoke_api {"method":"PUT", "path":"/api/v1/emulator/{id}/feature/porttrace","body":{"enabled":true}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/config","body":{"capacity":20000,"overflow":"stop"}}
emulator_manage   {"action":"reset"}                          # power-on state again; the machine stays paused
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/start"}
control_execution {"action":"run_frames","frames":200}        # the loader (~94 frames) and 0.74 s of BIOS
#   → Ran 200 frame(s) ... (the trace auto-stops at 20 000 events)
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/save",
            "body":{"path":"/abs/path/scratch/sprinter-boot-trace.json","format":"json"}}
#   → {"format":"json","path":".../scratch/sprinter-boot-trace.json","saved":20000}
```

## WebAPI

```bash
BASE=http://localhost:8090/api/v1
EMU=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' -d '{"model":"SPRINTER","sprinter":{"bios":"3.04"}}' | jq -r .id)
curl -s -X POST "$BASE/emulator/$EMU/pause" >/dev/null
curl -s -X PUT  "$BASE/emulator/$EMU/feature/porttrace" -H 'Content-Type: application/json' -d '{"enabled":true}' >/dev/null
curl -s -X POST "$BASE/emulator/$EMU/profiler/porttrace/config" -H 'Content-Type: application/json' \
     -d '{"capacity":20000,"overflow":"stop"}' >/dev/null
curl -s -X POST "$BASE/emulator/$EMU/reset" >/dev/null
curl -s -X POST "$BASE/emulator/$EMU/profiler/porttrace/start" >/dev/null
curl -s -X POST "$BASE/emulator/$EMU/run_frames" -H 'Content-Type: application/json' -d '{"frames":200}' >/dev/null
curl -s -X POST "$BASE/emulator/$EMU/profiler/porttrace/save" -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/sprinter-boot-trace.json\",\"format\":\"json\"}" | jq -c .
#   {"format":"json","path":".../scratch/sprinter-boot-trace.json","saved":20000}
```

## Compare

MAME short-cuts the PLD loader (it configures after 4 096 of the 473 720
writes), so the two runs line up from the BIOS's first access (MAME's rows
with `phase` `closed` / `open`); the 11 loader accesses before it are skipped.

```bash
python3 - <<'EOF'
import csv, json
ours = json.load(open('scratch/sprinter-boot-trace.json'))['events']
mame = [r for r in csv.DictReader(open('testdata/machines/sprinter/reference/ports.csv')) if r['phase'] != 'loader']
first = mame[0]
start = next(i for i, e in enumerate(ours) if e['raw'] == int(first['port'], 16) and e['pc'] == int(first['pc'], 16))
same = 0
for k, row in enumerate(mame):
    if start + k >= len(ours):
        break
    e = ours[start + k]
    if (e['raw'], e['val'], e['pc']) != (int(row['port'], 16), int(row['value'], 16), int(row['pc'], 16)) or \
       (row['code'] not in ('', 'z84') and e.get('code') is not None and e['code'] != int(row['code'], 16)):
        print('first difference at', k, row, e)
        break
    same += 1
print('accesses that match (port, value, PC, code):', same, 'of', len(mame))
EOF
#   accesses that match (port, value, PC, code): 9989 of 9989
```

On BIOS 3.04 every one of MAME's 9 989 post-loader accesses matches: the
port, the value, the PC and the code (the Z84C15's own ports have no code in
MAME's file, `z84`; unreal-ng gives them `#100` + the low byte). A different
BIOS image needs a new MAME capture.

## New MAME captures

`tools/machines/sprinter/mame-capture/mame-capture.sh` runs MAME's `sprinter`
driver headless (a MAME subset build, binary `zxsp`:
[tools/verification/coemu/mame/README.md](../../tools/verification/coemu/mame/README.md#sprinter))
and writes the reference files; the exact command lines are in
[testdata/machines/sprinter/reference/README.md](../../testdata/machines/sprinter/reference/README.md).
For example, the floppy controller over a DSS boot (codes `#10-#17`, the DSS
floppy in drive B):

```bash
cd tools/machines/sprinter/mame-capture
export MAME_BIN=<path to zxsp> SPC_OUT=../../../../scratch/mame
./mame-capture.sh boot SPC_FLOP2=../../../../testdata/machines/sprinter/dss_1_62_92.img \
                  SPC_CODES=10-17 SPC_PORTS=3000000 SPC_END=2500
```

(Not run for this recipe: no MAME build on the machine it was written on. The
checked-in captures were made with MAME 0.289.) The unreal-ng side of such a
run is the capture above with the floppy inserted before the reset and a
code filter, e.g. `{"include":[{"code":"FdcCommand"},{"code":"Density1440K"}]}`
on `/profiler/porttrace/filter`.

## Pitfalls

- **MAME's WD1793 never reads the HD disk** (its clock change does not reach
  a running command): past the BIOS density probe the floppy runs diverge on
  purpose - testdata/machines/sprinter/reference/README.md, `fdc-probe.csv`.
- **Start the trace before the reset**, with `overflow: stop`: a ring buffer
  drops the start of the boot, the part MAME's file has.
- **CMOS time**: MAME's RTC starts from the host clock, so the CMOS reads
  (code `#1C`) differ in value; compare them by code, not by value.
