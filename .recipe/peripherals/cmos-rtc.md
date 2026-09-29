# Recipe: CMOS clock (RTC) and its battery-backed cells

One chip class serves every machine with a clock: the MC146818 / DS12887
([ds12887.h](../../core/src/emulator/io/rtc/ds12887.h)). On the ZX-Evo it is
the board AVR's emulation of one ([evoavr.h](../../core/src/emulator/memory/atm/evoavr.h)).

| Model | Ports the guest uses | Cells | Battery file |
|:--|:--|:--|:--|
| `ATM3` (ZX-Evo) | `#DFF7` address / `#BFF7` data after `#EFF7` bit 7; `#DEF7` / `#BEF7` in shadow | 256 (A/C/D and `#F0-#FF` are the AVR's) | `[EVO] NvramFile=` |
| `PROFI` | `#BF` / `#FF` address, `#9F` / `#DF` data, extended mode only (CP/M + ROM14) | 256 | `[PROFI] NvramFile=` |
| `SCORPION`, `PROFSCORP` | SMUC `#DFBA`, `#FFBA` bit 7 selects address / data | 256 | none |

The Scorpion's clock lives on the SMUC board: the shipped configs have
`[HDD] Scheme=NONE`, so there is no clock until the config says `Scheme=SMUC`
(a config toggle: it needs a new instance). Every other model answers
"no CMOS clock" with the reason.

Cell map: `#00` seconds, `#02` minutes, `#04` hours, `#06` day of week
(1 = Sunday), `#07` day, `#08` month, `#09` year, `#01/#03/#05` alarms,
`#0A-#0D` registers A-D, `#0E` and up RAM.

Time base: `host` = host local time plus the offset the guest set by writing
the time registers; `emulated` while a TTD session records (a replay reads
the same time); `fixed` in tests. The report's `time_mode` says which.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) only inside host-side pipelines or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

Read the report (time, registers decoded, every cell as hex):

```json
{"tool": "inspect_state", "arguments": {"target": "auto", "aspects": ["rtc"]}}
```

Read or write cells through the router:

```json
{"tool": "invoke_api", "arguments": {"method": "GET", "path": "/api/v1/emulator/{id}/rtc/cells",
  "query_params": {"start": "0x0E", "count": "16"}}}
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/rtc/cells",
  "body": {"start": "0x40", "bytes": [18, "0x34"]}}}
```

## WebAPI

```bash
B=http://localhost:8090/api/v1/emulator; ID=<id>
curl -s $B/$ID/state/rtc | jq '{chip, time_mode, time: .time.text, b: .register_b}'
curl -s "$B/$ID/rtc/cells?start=0x0E&count=16" | jq .hex
curl -s -X POST $B/$ID/rtc/cells -H 'Content-Type: application/json' -d '{"start": "0x40", "bytes": [18, 52]}'
```

### Worked example: set 31 December 1999, 23:59:58

Values are BCD while register B bit 2 is clear (read B first: some
firmwares switch to binary, ProfROM for one leaves B = `#5E`). A guest sets
the clock with SET (B bit 7) held; do the same:

```bash
B_REG=$(curl -s "$B/$ID/rtc/cells?start=11&count=1" | jq '.bytes[0]')
curl -s -X POST $B/$ID/rtc/cells -H 'Content-Type: application/json' -d "{\"start\": 11, \"bytes\": [$((B_REG | 0x80))]}"
curl -s -X POST $B/$ID/rtc/cells -H 'Content-Type: application/json' -d '{"start": 0, "bytes": [88, 0, 89, 0, 35]}'   # #58 s, #59 min, #23 h
curl -s -X POST $B/$ID/rtc/cells -H 'Content-Type: application/json' -d '{"start": 7, "bytes": [49, 18, 153]}'      # #31, #12, #99
curl -s -X POST $B/$ID/rtc/cells -H 'Content-Type: application/json' -d "{\"start\": 11, \"bytes\": [$((B_REG & 0x7F))]}"
curl -s $B/$ID/state/rtc | jq -r .time.text      # 99-12-31 23:59:58
```

The ZX-Evo AVR ignores SET: there each field reads back as written until
the next update, so the fields can be written in any order.

## CLI / Lua / Python

```text
rtc                        # report (also: state rtc, cmos)
rtc read 0x0E 16           # cells
rtc write 0x40 0x12 0x34   # write like the guest
```

```lua
local r = rtc_state(); print(r.time.text)
local cells = rtc_read(0x0E, 16)
rtc_write(0x40, {0x12, 0x34})
```

```python
r = emu.rtc_state(); print(r["time"]["text"])
cells = emu.rtc_read(0x0E, 16)          # bytes
emu.rtc_write(0x40, [0x12, 0x34])
```

## Notes

- Reads are peeked: register C keeps its flags. Writes act like the guest's:
  C and D are read-only, the guest's address latch is not touched, and while
  a TTD session records the write is marked as a debugger edit.
- The battery file keeps A, B, the alarms and the RAM cells; the time comes
  from the time base on every start.
