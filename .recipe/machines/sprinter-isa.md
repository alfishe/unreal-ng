# Recipe: Sprinter ISA slots

The Sprinter Sp2000 has two ISA-8 slots. A program reaches them through memory window 3: with `#1FFD` bit 4 set,
the pages `#D0` / `#D2` / `#D4` / `#D6` mean ISA instead of RAM (page bit 2 = I/O, bit 1 = slot), the CPU's A13-A0
become ISA A13-A0 and the latch `#9FBD` (port-table code `#1B`) gives A19-A14, AEN (bit 6) and RESET DRV (bit 7).

| Slot | Connector | I/O page | Memory page | Default card |
|:--|:--|:--|:--|:--|
| 1 | J6 | `#D4` | `#D0` | none (the ZX-bus adapter + NeoGS once ISA phase I2 lands) |
| 2 | J7 | `#D6` | `#D2` | NE2000 (RTL8019AS at `#300`; owner decision 2026-10-02) |

Worked example: the RTL8019AS network kit reads the chip ID of the card in slot 2: `#1FFD` <- `#11`,
`OUT (#E2),#D6`, `#9FBD` <- `#00`, `LD A,(#C30A)` = ISA I/O address `(#00 << 14) | #030A` = `#0030A`, register `#0A`
of page 0 of the card at base `#300`: `#50` ('P').

Ground truth: design [2026-10-02-sprinter-isa/tdd.md](../../docs/inprogress/2026-10-02-sprinter-isa/tdd.md) (§14 as
built), code `core/src/emulator/io/sprinter/isa/` (`SprinterIsaBus`, `IsaAccess`).

> **How to use the sections:** the [WebAPI](#webapi-verified), [CLI](#cli-verified) and [Lua](#lua-verified)
> commands were run on a live instance (2026-10-03). MCP reads the same report (`inspect_state` aspect `isa`) and
> runs cycles through `invoke_api`. Policy: [_common/transports.md](../_common/transports.md).

## Configure the slots

The population is read when the instance is created and stays for its lifetime (a TTD recording made with one
population refuses to load on another).

- Machine config `configs/sprinter/unreal.ini`, section `[ISA]`: `Slot1=` / `Slot2=` with `NONE | ZXBUS | RAM |
  NE2000 | EL3C509B | SPRINTERESP | MODEM | DUAL16552`; a network card adds `Slot2Chip=RTL8019AS`, `Slot2Base=0x300`
  (write `0x300` or `300h`: a `#` starts an INI comment), `Slot2Irq=3`, `Slot2Mac=auto`.
- At create: WebAPI `{"model":"SPRINTER","sprinter":{"isa_slot1":"none","isa_slot2":"ne2000"}}`, CLI
  `create SPRINTER --isa-slot2 none`, MCP `emulator_manage action=create model=SPRINTER sprinter_isa_slot2=none`.
- A kind this build does not have yet is not fitted: the slot report says why (`not_fitted`), the machine starts.

## WebAPI (verified)

```bash
B=http://localhost:8090/api/v1/emulator          # UNREAL_WEBAPI_PORT moves the port
ID=$(curl -s -X POST $B/start -H 'Content-Type: application/json' \
       -d '{"model":"SPRINTER","sprinter":{"fast_start":true}}' | jq -r .id)

# The slot report: #9FBD latch, what window 3 shows, both slots (configured, fitted card, why not, counters)
curl -s $B/$ID/state/isa | jq '{summary, latch, window, slots: [.slots[] | {slot, card, configured, enabled, not_fitted}]}'
# summary: "slot 1: empty; slot 2: ne2000 I/O #300-#31F IRQ 3" (the default NE2000, recipe sprinter-network.md)
# What a card uses and how the Z80 reaches it; conflicts (none possible between the slots: each has its own select)
curl -s $B/$ID/state/isa | jq '.slots[1] | {resources, z80_access}, .conflicts'
# resources: io "#300-#31F", memory "none", irq 3, irq_route "Z84C15 PIO port B bit 1 (not wired yet: ISA phase I4)", dma none
# z80_access.io: "#1FFD bit 4 set, window 3 page #D6, #9FBD AEN = 0: CPU #C300-#C31F (A9-A0 decoded: ...)"
# Who touched which card register (512 entries): frame, t, pc, slot, access, isa / cpu address, register name, value
curl -s "$B/$ID/state/isa/journal?last=4" | jq -c '.entries[]'
# {"access":"read","address":"#0030A","cpu_address":"#C30A","frame":26,"pc":"#339E","slot":2,"space":"io","value":"#50","what":"ID0",...}
# The same section inside the Sprinter report
curl -s $B/$ID/state/sprinter | jq .isa.latch

# One ISA cycle at a 20-bit address: io_read | io_write | io_peek | mem_read | mem_write | mem_peek
curl -s -X POST $B/$ID/control/isa -H 'Content-Type: application/json' \
     -d '{"action":"io_read","slot":2,"address":"#30A"}'      # {"value":"#50"}: the RTL8019AS ID; #FF in an empty slot
# One RESET DRV pulse to both slots; write the latch
curl -s -X POST $B/$ID/control/isa -H 'Content-Type: application/json' -d '{"action":"reset"}'
curl -s -X POST $B/$ID/control/isa -H 'Content-Type: application/json' -d '{"action":"latch","value":"#37"}'
```

Answers checked (again 2026-10-03 with the NE2000 fitted): slot 2 `#30A` reads `#50`, the empty slot 1 reads `#FF`; `"slot": 3` -> 400 `slot: 1 or 2`; a Pentagon -> 404 `no ISA slots on
this machine (the Sprinter has two)`; an unknown `isa_slot2` at create -> 400 with the list of kinds.

## CLI (verified)

```text
select <id>
isa                       # or: state isa - the slot report
isa io 2 #30A             # one I/O read cycle in slot 2 at ISA #30A (isa io 2 #300 #21 writes)
isa mem 1 0xDC000         # one memory cycle; isa peek 2 #30A [mem] shows what the card answers, no side effect
isa latch 0x37            # #9FBD: A19-A14 = #37 -> window 3 addresses ISA #DC000-#DFFFF
isa reset                 # one RESET DRV pulse to both slots
isa journal 8             # the last 8 accesses (frame, T, PC, register name, value); isa journal clear | on | off
```

## Lua (verified)

```lua
local s = isa_state()                 -- s.summary, s.latch.value, s.window.mapped, s.slots[2].card, s.slots[2].resources
local j = isa_journal(8)              -- j.entries[1].what = "ID0", .cpu_address, .pc
print(isa_io_read(2, "#30A"))         -- one I/O cycle; isa_io_write(2, 0x300, 0x21), isa_io_peek(2, 778)
print(isa_reset())                    -- true; isa_mem_read / isa_mem_write, isa_latch(0x37)
```

Python has the same names on the emulator object (`emu.isa_state()`, `emu.isa_io_read(2, "#30A")`, ...).

## MCP

```json
{"tool": "inspect_state", "arguments": {"target": "auto", "aspects": ["isa"]}}
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/control/isa",
  "body": {"action": "io_read", "slot": 2, "address": "#30A"}}}
```

## Trace the cycles

ISA cycles are memory cycles, but the Sprinter's port trace shows them beside the port accesses while a capture
runs: internal codes `isa_io slot 1`, `isa_io slot 2`, `isa_mem slot 1`, `isa_mem slot 2` (`#200-#203`), the decoded
port is the ISA address's low 16 bits, the raw port the CPU address (`#C30A`). Peeks from tools are not traced.

## Notes

- A machine reset keeps `#9FBD` (the latch has no reset input); BIOS 3.07 pulses RESET DRV while it starts
  (`reset_pulses` in the counters).
- While RESET DRV is held every cycle reads `#FF`; AEN = 1 makes an I/O card ignore the cycle.
- Tools (debugger, memory viewer, `memory read`) see what the card shows without side effects.
