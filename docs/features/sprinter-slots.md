# Sprinter: cards in the slots

The Peters Plus Sprinter Sp2000 takes its add-on cards in two places: the **AY socket** (the sound chip) and two
**ISA-8 slots**. This page says what you can plug in, how to write it in the configuration file, and gives an example
for every card the emulator supports on the Sprinter. The general slot model (plan, restart, every surface) is in
[slots.md](slots.md).

The configuration file is `configs/sprinter/unreal.ini`. Cards are read when the machine is **created**: after an
edit, restart the emulator (or create a new Sprinter instance). A reset inside the running machine is not enough.

## What the shipped configuration has

| Place | Card | Where it is set |
|---|---|---|
| AY socket | the AY in the FPGA | `[SLOTS]` `ay-socket = ay` |
| ISA slot 1 | the ISA to ZX-bus adapter with a **NeoGS** on it | `[ISA]` `Slot1=ZXBUS` + `[SLOTS]` `isa.1 = neogs` |
| ISA slot 2 | NE2000 network card (RTL8019AS) | `[ISA]` `Slot2=NE2000` |

```ini
[SLOTS]
ay-socket = ay                       ; the AY in the FPGA
isa.1 = neogs                        ; NeoGS behind the ISA to ZX-bus adapter (settings in [NGS])
isa.1.adapter = sprinter-isa-zxbus
isa.1.fit = unrealistic

[ISA]
Slot1=ZXBUS
Slot2=NE2000
```

## Two sections, two kinds of card

- **`[ISA]`** says what physical ISA card sits in each slot: `Slot1=` and `Slot2=`, one of `NONE`, `ZXBUS`,
  `NE2000`, `EL3C509B`, `SPRINTERESP`, `MODEM`, `DUAL16552`. The network and serial cards are complete here.
- **`[SLOTS]`** says which ZX Spectrum card goes **on** the ZX-bus adapter (`isa.1`), and what is in the AY socket.

So a General Sound needs both: the adapter in `[ISA]` (`Slot1=ZXBUS`) and the card in `[SLOTS]` (`isa.1 = ...`).

### Rules for `[SLOTS]`

- One line per setting, `key = value`. Text after `;` is a comment.
- A ZX-bus card in an ISA slot always takes the adapter and the fit lines with it:

  ```ini
  isa.1.adapter = sprinter-isa-zxbus
  isa.1.fit = unrealistic
  ```

  `unrealistic` is not an error: the real adapter does not pass every ZX-bus signal (no memory cycles, no IRQ), and
  the reports say so. The card works for everything the software reaches through the ISA window.
- A card option is `isa.1.<option> = <value>` (for example `isa.1.ram = 512k`).
- Only **one** General Sound per machine, on `isa.1`. A second `ZXBUS` adapter in slot 2 stays empty.
- A wrong line is skipped and logged when the machine is created; the machine then starts without that card.
  Check the result with `slots` / `state/isa` (see [Checking what is plugged in](#checking-what-is-plugged-in)).

## AY socket

| Card | What it is |
|---|---|
| `ay` | the AY-3-8910 in the FPGA (default) |
| `ts` | TurboSound: two AY chips |
| `tsfm` | TurboSound FM: two YM2203 (AY + FM) |
| `none` | empty socket: no AY at all |

```ini
ay-socket = ay        ; the Sprinter's own AY
ay-socket = ts        ; TurboSound
ay-socket = tsfm      ; TurboSound FM
ay-socket = none      ; no AY
```

## ISA slot 1 with the ZX-bus adapter: General Sound cards

The program reaches the card through ISA slot 1 (window 3, page `#D4`): ISA I/O `#xxB3` / `#xxBB` / `#xx33` are
the GS data, command / status and control ports. ISA RESET DRV resets the card. ProPlay and the other Sprinter GS
players work this way.

| Card | What it is | Options |
|---|---|---|
| `neogs` | NeoGS (default) | `ram` = `2m` \| `4m`; the rest in `[NGS]` (flash, SD card, MP3, stereo) |
| `gs` | the classic General Sound (`rom/gs105a.rom`) | `ram` = `128k` \| `256k` \| `512k` \| `1m` \| `2m`; `rom` = `1.04` \| `1.05` |
| `gs-lw` | a lightweight GS player (emulator only: plays MOD without the card's Z80) | - |

NeoGS (the shipped setting):

```ini
[SLOTS]
ay-socket = ay
isa.1 = neogs
isa.1.adapter = sprinter-isa-zxbus
isa.1.fit = unrealistic
isa.1.ram = 4m                       ; optional, 2m by default
```

The classic General Sound with 512 K:

```ini
[SLOTS]
ay-socket = ay
isa.1 = gs
isa.1.adapter = sprinter-isa-zxbus
isa.1.fit = unrealistic
isa.1.ram = 512k
```

The lightweight player:

```ini
[SLOTS]
ay-socket = ay
isa.1 = gs-lw
isa.1.adapter = sprinter-isa-zxbus
isa.1.fit = unrealistic
```

No General Sound (the adapter stays, nothing on it): leave out every `isa.1` line.

```ini
[SLOTS]
ay-socket = ay
```

Other ZX-bus cards (MoonSound, Covox, SounDrive, ZX-MultiSound, ZXNETUSB, ZX-WiFi) are **not** supported on the
Sprinter: the adapter carries the General Sound family only.

## ISA cards (`[ISA]`)

Each slot takes one card; both slots take the same kinds. Keys are `Slot1...` for slot 1 and `Slot2...` for slot 2.
Write I/O bases as `0x300` or `300h`: a `#` starts a comment in the INI file.

| `SlotN=` | Card | Settings |
|---|---|---|
| `NONE` | empty slot | - |
| `ZXBUS` | ISA to ZX-bus adapter (the General Sound sits on it, see above) | - |
| `NE2000` | NE2000 Ethernet (Sprinter RTL8019AS kit) | `SlotNChip` = `RTL8019AS` \| `UM9003` \| `NE1000`; `SlotNBase` = `0x200`..`0x3E0` step `0x20`; `SlotNIrq`; `SlotNMac` = `auto` \| `aa:bb:cc:dd:ee:ff` |
| `EL3C509B` | 3Com 3C509B Ethernet (Sprinter 3C509B kit) | `SlotNChip` = `TPO` \| `TP`; `SlotNBase` = `0x200`..`0x3E0` step `0x10`; `SlotNIrq`; `SlotNMac` |
| `SPRINTERESP` | SprinterESP Wi-Fi (I/O `#3E8`, IRQ 3 fixed by the board) | `SlotNPeer` = `AT` (its own ESP) \| `loopback` \| `tcp:host:port` \| `serial:/dev/tty...,115200` \| `none`; `SlotNMac` |
| `MODEM` | ISA Hayes modem (16550A) | `SlotNBase` = `0x3F8` \| `0x2F8` \| `0x3E8` \| `0x2E8`; `SlotNIrq` = 2 \| 3 \| 4 \| 5 \| 7; `SlotNPeer` = `MODEM` |
| `DUAL16552` | SprinterSerial: COM1 `#3F8` + COM2 `#2F8` | `SlotNPeer` / `SlotNPeerB` (COM1 / COM2 line); `SlotNIrq` = 3 \| 2 \| 0 (J5); `SlotNIrqB` = 4 \| 2 \| 0 (J6); `SlotNDecode` = `FULL` \| `PARTIAL` |

NE2000 in slot 2 (the shipped setting):

```ini
[ISA]
Slot2=NE2000
Slot2Chip=RTL8019AS
Slot2Base=0x300
Slot2Irq=3
Slot2Mac=auto
```

3Com 3C509B in slot 2:

```ini
[ISA]
Slot2=EL3C509B
Slot2Chip=TPO
Slot2Base=0x300
Slot2Irq=3
```

SprinterESP Wi-Fi in slot 2:

```ini
[ISA]
Slot2=SPRINTERESP
Slot2Peer=AT
```

A modem in slot 2 (dial `5551234` to reach a telnet BBS):

```ini
[ISA]
Slot2=MODEM
Slot2Base=0x3F8
Slot2Irq=4
Slot2Peer=MODEM

[NETWORK]
ModemPhonebook=5551234=bbs.example.org:23
```

SprinterSerial in slot 2, COM1 to a host serial port:

```ini
[ISA]
Slot2=DUAL16552
Slot2Peer=serial:/dev/ttyUSB0,115200
Slot2Irq=3
Slot2IrqB=0
```

An empty slot 1 (no adapter, so no General Sound either):

```ini
[ISA]
Slot1=NONE
```

More on the network cards: [.recipe/machines/sprinter-network.md](../../.recipe/machines/sprinter-network.md).

## At creation, without editing the file

WebAPI / MCP `create` takes the ISA kinds and the `[SLOTS]` set:

```json
{"model": "SPRINTER", "sprinter": {"isa_slot2": "none"},
 "slots": {"ay-socket": "tsfm", "isa.1": "gs", "isa.1.adapter": "sprinter-isa-zxbus", "isa.1.fit": "unrealistic"}}
```

CLI: `create SPRINTER --isa-slot2 none`.

## Checking what is plugged in

```bash
B=http://localhost:8090/api/v1/emulator
curl -s $B/$ID/state/isa | jq -r .summary
# slot 1: zxbus I/O #033-#0BB -> NeoGS on the ZX-bus: #B3 / #BB / #33, RESET from ISA RESET DRV; slot 2: ne2000 I/O #300-#31F IRQ 3
curl -s $B/$ID/slots | jq '.slots[] | {slot, card, fit, state}'
```

CLI: `slots`, `isa` (or `state isa`). A card that was not fitted shows the reason (`not_fitted` in `state/isa`, the `state` of
the slot in `slots`).
