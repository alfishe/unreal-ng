# Recipe: Sprinter ISA slots

The Sprinter Sp2000 has two ISA-8 slots. A program reaches them through memory window 3: with `#1FFD` bit 4 set,
the pages `#D0` / `#D2` / `#D4` / `#D6` mean ISA instead of RAM (page bit 2 = I/O, bit 1 = slot), the CPU's A13-A0
become ISA A13-A0 and the latch `#9FBD` (port-table code `#1B`) gives A19-A14, AEN (bit 6) and RESET DRV (bit 7).

| Slot | Connector | I/O page | Memory page | Default card |
|:--|:--|:--|:--|:--|
| 1 | J6 | `#D4` | `#D0` | ZX-bus adapter (owner decision Q2, ISA phase I2), empty: no General Sound on it since 2026-10-04 (owner decision) |
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
  (write `0x300` or `300h`: a `#` starts an INI comment), `Slot2Irq=3`, `Slot2Mac=auto`. `EL3C509B` (the 3Com
  3C509B, built: [sprinter-network.md](sprinter-network.md#3com-3c509b-with-the-sprinter-3c509b-network-kit)) takes
  `Slot2Chip=TPO | TP` and a base in steps of `0x10`; its report adds `resources.id_port` (`#100-#1F0`).
  `SPRINTERESP` (the Wi-Fi card, built: `#3E8` and IRQ 3 fixed by the board) takes `SlotNPeer=AT` (its 16550's line)
  and `SlotNMac=`; it decodes A13-A3 without AEN, so `z80_access.io` reads "any #9FBD AEN" for it
  ([sprinter-network.md](sprinter-network.md#sprinteresp-wi-fi-with-the-sprinter-esp-network-kit)).
  `MODEM` (an ISA Hayes modem: 16550A, `SlotNBase=0x3F8|0x2F8|0x3E8|0x2E8`, `SlotNIrq=4`, `SlotNPeer=MODEM`) and
  `DUAL16552` (SprinterSerial: COM1 `#3F8` + COM2 `#2F8`, `SlotNPeer=` / `SlotNPeerB=`, `SlotNIrq=3` / `SlotNIrqB=0`
  for jumpers J5 / J6, `SlotNDecode=FULL|PARTIAL`): [sprinter-network.md](sprinter-network.md#isa-hayes-modem-and-sprinterserial).
- At create: WebAPI `{"model":"SPRINTER","sprinter":{"isa_slot1":"none","isa_slot2":"ne2000"}}`, CLI
  `create SPRINTER --isa-slot2 none`, MCP `emulator_manage action=create model=SPRINTER sprinter_isa_slot2=none`.
- Built kinds: `ZXBUS` (the adapter; the GS behind it is a slot card in `[SLOTS]`: `isa.1 = neogs` | `gs` | `gs-lw`,
  `isa.1.adapter = sprinter-isa-zxbus`, `isa.1.fit = unrealistic` - the NeoGS as shipped; the legacy
  `[SOUND] GSType` still works; user guide with every card: [docs/features/sprinter-slots.md](../../docs/features/sprinter-slots.md)), `NE2000`,
  `SPRINTERESP`, `MODEM`, `DUAL16552`. `Slot1=NONE` builds no GS at all (the machine has no ZX-bus then); a second
  `ZXBUS` adapter has an empty ZX-bus (one GS per machine). A kind this build does not have yet is not fitted: the slot report says why (`not_fitted`), the machine starts.

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
# resources: io "#300-#31F", memory "none", irq 3, irq_route "J7 IRQ2-IRQ7 pins tied (net IRQ2, 3.9 kOhm pull-up) -> Z84C15 PIO port B bit 1 (PB1, ...)", dma "none (...); DRQ -> PB2, DACK <- PB3"
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
isa journal 8             # the last 8 accesses (frame, T, PC, register name, value) + irq_events; isa journal clear | on | off
isa irq                   # the interrupt lines only: irq_summary, pio_port_b, each slot's irq_line
```

## Lua (verified)

```lua
local s = isa_state()                 -- s.summary, s.latch.value, s.window.mapped, s.slots[2].card, s.slots[2].resources
local j = isa_journal(8)              -- j.entries[1].what = "ID0", .cpu_address, .pc
print(isa_io_read(2, "#30A"))         -- one I/O cycle; isa_io_write(2, 0x300, 0x21), isa_io_peek(2, 778)
print(isa_reset())                    -- true; isa_mem_read / isa_mem_write, isa_latch(0x37)
```

Python has the same names on the emulator object (`emu.isa_state()`, `emu.isa_io_read(2, "#30A")`, ...).

## Interrupt lines (verified 2026-10-03)

Each slot's IRQ pins are one net with a 3.9 kOhm pull-up, wired to the Z84C15's PIO port B: **PB0 = slot 1, PB1 =
slot 2** (PB4 / PB2 DRQ, PB5 / PB3 DACK; schematic `SPRINT_3`). ISA IRQs are active high: a card that drives its pin
holds it low until it requests, a pin nobody drives reads high. A program gets the interrupt by putting port B into bit
mode with the bit monitored and the interrupt enabled (BC-Term: control `#00` vector, `#CF`, `#01`, `#B7`, `#FE`,
`#83`, then IM 2); the CPU takes IM 2 through the Z84C15 daisy chain, ahead of the PLD's frame INT. The NE2000 drives
while CONFIG1.IRQEN is set (ISR & IMR), the SprinterESP's 16550 drives INTR straight to IRQ3 (OUT2 does not gate it).

```bash
ID=$(curl -s -X POST $B/start -H 'Content-Type: application/json' \
       -d '{"model":"SPRINTER","sprinter":{"fast_start":true,"isa_slot1":"sprinteresp"}}' | jq -r .id)
# The lines at a glance (ESP in slot 1, NE2000 in slot 2, BIOS 3.07 at its prompt: port B in bit mode, nothing monitored)
curl -s $B/$ID/state/isa | jq -r .irq_summary
# slot 1 IRQ low (driven by sprinteresp) -> PB0: PB0 is masked (mask #FF): readable at #1E only; 0 requests, 0 acknowledged; slot 2 ...
curl -s $B/$ID/state/isa | jq '.slots[0].irq_line, .pio_port_b'
# irq_line: card_irq 3, pio_bit "PB0", driven "by the card (sprinteresp)", line "low", card_request, cause "IIR #01 (none pending), IER #00; ...",
#           pio {input, monitored, active_level, logic, interrupt_enabled, vector, condition, pending, under_service}, reaches_cpu
# pio_port_b: mode "bit control (mode 3)", lines "#FC", inputs_latched, read, output "#C0", direction "#3F", mask "#FF", ...

# Raise the 16550's receive interrupt by hand: 8N1 at divisor 8, FIFO trigger 1, IER = 1, loopback, one byte to THR
w() { curl -s -X POST $B/$ID/control/isa -H 'Content-Type: application/json' \
       -d "{\"action\":\"io_write\",\"slot\":1,\"address\":\"$1\",\"value\":\"$2\"}" >/dev/null; }
w '#3EB' '#80'; w '#3E8' '#08'; w '#3E9' '#00'; w '#3EB' '#03'; w '#3EA' '#07'; w '#3E9' '#01'; w '#3EC' '#10'; w '#3E8' '#41'
curl -s $B/$ID/state/isa | jq -c '.slots[0].irq_line | {line, cause}'
# {"line":"high","cause":"IIR #C4 (received data at the FIFO trigger level), IER #01; INTR wired to IRQ3, OUT2 does not gate it"}
curl -s "$B/$ID/state/isa/journal?last=8" | jq -c '.irq_events[]'
# ... {"event":"irq","frame":164,"pc":"#89FF","slot":1,"t":1,"value":"#FD","what":"IRQ line high -> PB0 (IIR #C4 ...)"}
# (the line rises at the frame's catch-up: nothing waits for it, the PIO does not monitor PB0)
```

A program that waits for it - BC-Term 1.11 (`MODEM/BCTERM.EXE` on the MAME pack's system disk) with the SprinterESP:
its rate table is for a 1.8432 MHz UART, so its default "57600" (divisor 2) is 460 800 baud on the card's 14.7456 MHz -
give the ESP that rate (`network set isa1_peer=at,460800`, an ESP whose UART_DEF holds 460800). Start a TTD recording
first, then the program:

```bash
curl -s -X POST $B/$ID/ttd/start
curl -s -X POST $B/$ID/keyboard/type -H 'Content-Type: application/json' -d '{"text":"c:\\modem\\bcterm.exe"}'
curl -s -X POST $B/$ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"enter","frames":3}'
curl -s $B/$ID/state/isa | jq -r .irq_summary
# slot 1 IRQ low (driven by sprinteresp) -> PB0: interrupts the CPU; 6 requests, 6 acknowledged; slot 2 ... PB1 is programmed as an output
curl -s "$B/$ID/state/isa/journal?last=4" | jq -r '.irq_events[].what'
# PIO port B requests an interrupt (vector #00; slot 1)
# INT acknowledged: PIO port B, IM 2 vector #00 -> table #B500 (slot 1)
# IRQ line low -> PB0 (IIR #C1 (none pending), IER #01; ...)
# RETI: the PIO port B interrupt service ended
```

BC-Term's screen then shows what its interrupt handler received: the ESP's `ready`, `WIFI CONNECTED`, `WIFI GOT IP`
and the echo of its init string `ATZ` (CR-terminated: ESP-AT waits for CR LF and answers nothing more). Lua:
`isa_state().irq_summary`, `isa_state().slots[1].irq_line.line`, `isa_journal(16).irq_events`; Python the same names on
`emu`; MCP `inspect_state` aspect `isa` prints the `[isa] irq:` line. Qt: Network window, the slot rows end with "IRQ
line low -> PB0, interrupts the CPU, N acknowledged".

## MCP

```json
{"tool": "inspect_state", "arguments": {"target": "auto", "aspects": ["isa"]}}
{"tool": "invoke_api", "arguments": {"method": "POST", "path": "/api/v1/emulator/{id}/control/isa",
  "body": {"action": "io_read", "slot": 2, "address": "#30A"}}}
```

## The General Sound / NeoGS behind the ZX-bus adapter (verified 2026-10-04)

The shipped Sprinter config fits the NeoGS behind the adapter (`configs/sprinter/unreal.ini`, `[SLOTS]`:
`isa.1 = neogs`, `isa.1.adapter = sprinter-isa-zxbus`, `isa.1.fit = unrealistic`; `gs` instead of `neogs` for the
classic card, no `isa.1` lines for none).

ISA I/O `#xxB3` / `#xxBB` / `#xx33` of slot 1 are the GS ports (data, command / status, control): a program maps
page `#D4` into window 3 (`#1FFD` <- `#11`, `OUT (#E2),#D4`, `#9FBD` <- `#00`) and reads / writes `#C0B3` / `#C0BB` /
`#C033`. ISA RESET DRV resets the card; the BIOS pulses it at POST. Play a MOD with ProPlay (the MAME pack's system
disk, a test MOD from `tools/machines/sprinter/test-mod/make-test-mod.py` written over `DOCS\DISP.TXT` - same size,
pad with zeros - and `fn` removed from `SYSTEM.BAT`, so DSS stops at its prompt):

```bash
B=http://localhost:8090/api/v1/emulator
ID=$(curl -s -X POST $B/start -H 'Content-Type: application/json' -d '{"model":"SPRINTER","sprinter":{"fast_start":true}}' | jq -r .id)
# Who is plugged where, at a glance
curl -s $B/$ID/state/isa | jq -r .summary
# slot 1: zxbus I/O #033-#0BB -> NeoGS on the ZX-bus: #B3 / #BB / #33, RESET from ISA RESET DRV; slot 2: ne2000 I/O #300-#31F IRQ 3
curl -s $B/$ID/state/isa | jq '.slots[0].zx_bus | {reset_held, reset_pulses, memory_cycles, gs: .cards[0] | {personality, device, firmware, cpu_addresses, status, ready_for_commands, sound, zx_dma, machine_reset}}'
# gs.device "NeoGS (Z80 @ 10 MHz, 8 x 8-bit DAC, 2048 KB RAM)", firmware "NeoGS flash v1.11", status "#7E",
# zx_dma "unavailable: the adapter passes no memory cycles ...", machine_reset "[SOUND] GSReset=0: ..."

# The disk (session copy), boot to the DSS prompt, a TTD recording, then ProPlay
curl -s -X POST $B/$ID/pause >/dev/null
curl -s -X POST "$B/$ID/media/ide0.master/insert" -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/sp-proplay.img\",\"access\":\"session\"}" | jq -c '{ok}'
curl -s -X POST $B/$ID/reset >/dev/null; curl -s -X POST $B/$ID/pause >/dev/null
curl -s -X POST $B/$ID/run_frames -H 'Content-Type: application/json' -d '{"frames":450}' >/dev/null   # C:\>
curl -s -X POST $B/$ID/ttd/start | jq -c '{state}'                                                     # recording
curl -s -X POST $B/$ID/keyboard/type -H 'Content-Type: application/json' -d '{"text":"proplay.exe \\docs\\disp.txt"}' >/dev/null
curl -s -X POST $B/$ID/run_frames -H 'Content-Type: application/json' -d '{"frames":60}' >/dev/null
curl -s -X POST $B/$ID/keyboard/tap -H 'Content-Type: application/json' -d '{"key":"enter","frames":3}' >/dev/null
curl -s -X POST $B/$ID/run_frames -H 'Content-Type: application/json' -d '{"frames":150}' >/dev/null
curl -s $B/$ID/state/sprinter/text | jq -r '.lines[].text' | grep -v '^$' | tail -4
# General Sound found at slot: 0 / Done. / C:\BIN>
curl -s $B/$ID/state/isa | jq -c '.slots[0] | {io: .counters | {io_reads, io_writes}, gs: .zx_bus.cards[0] | {status, ready_for_commands, sound}}'
# {"io":{"io_reads":17237,"io_writes":3330},"gs":{"status":"#FE","ready_for_commands":true,"sound":"playing (the mix changed in the last frame)"}}
curl -s "$B/$ID/state/isa/journal?last=3" | jq -c '.entries[] | {what, cpu_address, value}'
# {"what":"GS status (#BB)","cpu_address":"#C0BB","value":"#FE"}
curl -s -X POST $B/$ID/control/isa -H 'Content-Type: application/json' -d '{"action":"io_peek","slot":1,"address":"#BB"}' | jq -c '{value}'
# The card's own sound: 1 s of the GS row (the MOD's channel 1 is on the left)
curl -s -X POST $B/$ID/audio/capture -H 'Content-Type: application/json' -d '{"action":"start","seconds":1,"source":"gs"}' >/dev/null
curl -s -X POST $B/$ID/run_frames -H 'Content-Type: application/json' -d '{"frames":60}' >/dev/null
curl -s "$B/$ID/audio/capture/result" | jq -c '{duration_seconds, left: .left.rms, right: .right.rms, dominant_hz}'
# {"duration_seconds":1.0,"left":0.1686,"right":0.0,"dominant_hz":268.5}   (zero-crossing estimate; C-3 is 258.5 Hz)
```

The card itself: `GET /state/audio/gs` (mailbox, DACs, the `neogs` object; `dma.zx.host_memory_bus` false here), the
GS port trace `/state/audio/gs/porttrace` sees ProPlay's traffic like any host's. CLI: `isa` (the `zx_bus` block),
`isa io 1 #BB` (`value: #FE`), `state audio gs`. Lua (verified): `isa_state().slots[1].summary_line`,
`isa_state().slots[1].zx_bus.cards[1].personality` (`ngs`), `isa_io_write(1, 0xB3, 0x5A); isa_io_peek(1, 0xBB)`
(`254`: the data flag). Python: the same names on `emu` (not built in the verifying build). MCP (verified):
`inspect_state` aspect `isa` prints `[isa]   zxbus I/O #033-#0BB -> NeoGS on the ZX-bus: ...` and the card's line
(`NeoGS (...): status #7E, silent; [SOUND] GSReset=0: ...`); `audio_gs` for the card. Qt: Network window, slot 1's
row is the adapter's line.

- `isa.1 = gs` in `[SLOTS]` (legacy `[SOUND] GSType=Z80`) puts the classic GS (`rom/gs105a.rom`) behind the adapter; a TTD recording then replays
  exactly (its RAM is in its blob). The NeoGS replays the same music, not bit-exact (its RAM waits for TTD v2).
- The NeoGS ZX-DMA cannot reach the Sprinter (the adapter passes no memory cycles); the TTD port journals record with
  the NeoGS fitted.
- The same on MAME: `-isa0 zxbus_adapter -isa0:zxbus_adapter:card neogs`; the comparison (pitch equal, waveform
  correlation 0.9998): [i2-outcome.md](../../docs/inprogress/2026-10-02-sprinter-isa/i2-outcome.md).

## Trace the cycles

ISA cycles are memory cycles, but the Sprinter's port trace shows them beside the port accesses while a capture
runs: internal codes `isa_io slot 1`, `isa_io slot 2`, `isa_mem slot 1`, `isa_mem slot 2` (`#200-#203`), the decoded
port is the ISA address's low 16 bits, the raw port the CPU address (`#C30A`). Peeks from tools are not traced.

## Notes

- A machine reset keeps `#9FBD` (the latch has no reset input); BIOS 3.07 pulses RESET DRV while it starts
  (`reset_pulses` in the counters; `#FF` at PC `#0399`, `#00` at `#03AB`) - which resets the GS behind the adapter.
- While RESET DRV is held every cycle reads `#FF`; AEN = 1 makes an I/O card ignore the cycle.
- Tools (debugger, memory viewer, `memory read`) see what the card shows without side effects.
- The cards' lines are pushed to PIO port B on every change; a card's own timed events (a character arriving at the
  16550) are caught up mid-frame only while the PIO waits for an ISA interrupt - polling programs see no difference.
