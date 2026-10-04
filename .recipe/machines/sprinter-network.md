# Recipe: Sprinter networking - Ethernet (NE2000, RTL8019AS kit) and Wi-Fi (SprinterESP, ESP kit)

The Wi-Fi card has its own part: [SprinterESP Wi-Fi with the Sprinter ESP Network Kit](#sprinteresp-wi-fi-with-the-sprinter-esp-network-kit).

The `SPRINTER` model has an NE2000-class Ethernet card fitted by default: a Realtek RTL8019AS at ISA I/O `#300`, IRQ 3,
in ISA slot 2 (page `#D6`; owner decision 2026-10-02). It is cabled to the virtual network through the **Ethernet
gateway**: a switch with a home router behind it (router `10.0.2.2`, DNS `10.0.2.3`, DHCP leases from `10.0.2.15`), the
same DHCP server, DNS, hosts table, forwards and host bridge every other network adapter uses. The 2026 RTL8019AS
network kit (`IFUP`, `PING`, `NSLOOKUP`, `WGET`, `FTP`, `TELNET`, `NTP`, `UNETRTL.DLL`) runs unchanged on it.

Ground truth: designs [2026-10-02-sprinter-network/tdd.md](../../docs/inprogress/2026-10-02-sprinter-network/tdd.md)
(§18 as built) and [2026-10-02-sprinter-isa/tdd.md](../../docs/inprogress/2026-10-02-sprinter-isa/tdd.md); code
`core/src/emulator/io/network/ethernet/` (`Dp8390`, `Ne2000Board`), `core/src/emulator/io/network/vnet/ethernetgateway.*`.
The kit's release files are fixtures in [testdata/machines/sprinter/network/](../../testdata/machines/sprinter/network/README.md).

> **How to use the sections:** the [WebAPI](#webapi-verified) steps ran end to end on a live instance (2026-10-03):
> DHCP, ping, a DNS lookup of a real name and a 20 000-byte `WGET` from an HTTP server on the host. MCP reads the same
> reports (`inspect_state` aspects `network`, `isa`) and drives the rest through `invoke_api`. Policy:
> [_common/transports.md](../_common/transports.md).

## The card and its slot

| What | Default | Change it |
|:--|:--|:--|
| Slot 2 (J7, page `#D6`) | `NE2000` | `[ISA] Slot2=NONE` (or another kind); create option `"sprinter": {"isa_slot2": "none"}` |
| Chip | `RTL8019AS` (ID `'P' 'p'` at `#30A/#30B`, page 3, 93C46 EEPROM) | `Slot2Chip=UM9003` (no Realtek ID; reading its reset port `#31F` hangs the machine, as on the real card) or `NE1000` |
| I/O base | `#300` (`#300-#31F`: registers `+0..+F`, data port `+#10`, reset port `+#18..+#1F`) | `Slot2Base=0x340` (`0x200..0x3E0` in steps of `0x20`) |
| MAC | `02:53:50:00:<instance>:<slot>` (`02:53:50:00:00:02` for the first instance) | `Slot2Mac=00:E0:4C:12:34:56` |
| Cable | the Ethernet gateway (NAT); with the runtime feature `network` off: no cable, transmitted frames are counted and lost | `[NETWORK] HostAccess=0` keeps it inside the virtual network |

The kit numbers the slots 0 / 1: the emulator's slot 2 is the kit's `RTL_HW=1/#300`.

## Prepare a disk with the kit

> **BIOS 3.07 BETA 1 (selectable; the default until 2026-10-03) cannot run the kit from the floppy on this disk.** Its floppy driver returns
> with IY changed, and DSS 1.71.57 (the MAME pack's system disk) then fails every program on a floppy ("Invalid
> EXE file" in Flex Navigator, "Bad command or file name" at the prompt). Copying does not help inside the
> machine: `copy b:\netcfg.exe c:\` on 3.07 BETA 1 leaves a 0-byte file on C: (checked 2026-10-03; the same copy on
> 3.06 Hotfix 2 is byte-identical). So either keep the default BIOS 3.06 Hotfix 2 (as below), or put the kit on C: from the
> host before the boot (`mcopy -i scratch/sp-net.img@@32256 $K/*.EXE $K/*.DLL scratch/NET.CFG ::`) and run it from
> C:. The firmware side and the upstream note: [bios-versions.md](../../docs/inprogress/2026-09-28-sprinter/bios-versions.md) §5.2.

```bash
# A 1.44 MB floppy with the kit and a NET.CFG (drive B: on the Sprinter)
K=testdata/machines/sprinter/network/rtl8019a-0.3.8
mformat -C -i scratch/rtlkit.img -f 1440 ::
for f in $K/*; do mcopy -i scratch/rtlkit.img "$f" ::; done
printf 'RTL_HW=1/#300\r\nIP=DHCP\r\nTZ=+0\r\n' > scratch/NET.CFG && mcopy -i scratch/rtlkit.img scratch/NET.CFG ::

# The MAME pack's DSS 1.71 system disk (raw), its SYSTEM.BAT without Flex Navigator: DSS stops at C:\>
cp ~/Downloads/sprinter/hdd/sp_hdd_sys.img scratch/sp-net.img
printf '@echo off\r\nset PATH=%%BOOTDSK%%\\;%%BOOTDSK%%\\BIN\\;\r\n' > scratch/SYSTEM.BAT
mcopy -o -i scratch/sp-net.img@@32256 scratch/SYSTEM.BAT ::
```

## WebAPI (verified)

```bash
B=http://localhost:8090/api/v1/emulator            # UNREAL_WEBAPI_PORT moves the port
ID=$(curl -s -X POST $B/start -H 'Content-Type: application/json' \
       -d '{"model":"SPRINTER","sprinter":{"bios":"3.06","fast_start":true}}' | jq -r .id)   # DSS 1.71 needs BIOS 3.06+
curl -s -X POST $B/$ID/pause
curl -s -X POST $B/$ID/media/ide0.master/insert -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/sp-net.img\",\"access\":\"session\"}"
curl -s -X POST $B/$ID/media/fdd.b/insert -H 'Content-Type: application/json' -d "{\"path\":\"$PWD/scratch/rtlkit.img\"}"
curl -s -X POST $B/$ID/reset
curl -s -X POST $B/$ID/run_frames -d '{"count":1500}'        # BIOS, IDE scan, DSS: C:\>
curl -s -X POST $B/$ID/resume

# Type the kit's commands at the prompt (one at a time; BIOS 3.06 HF2 does not scroll the last line: CLS between)
type() { curl -s -X POST $B/$ID/keyboard/type -H 'Content-Type: application/json' -d "{\"text\":\"$1\\n\"}"; }
type 'B:'; type 'NETCFG -i'; type 'IFUP'          # IFUP: "DHCP: lease IP=10.0.2.15 (server 10.0.2.2 ...)"
type 'PING -n 2 10.0.2.2'                         # "Reply from 10.0.2.2: bytes=32 ..."
type 'NSLOOKUP example.com'                       # the host's resolver answers (DnsMode=HOST)
type 'WGET http://127.0.0.1:18181/f.bin -o C:\F.BIN -y'   # from an HTTP server on the host (python3 -m http.server)
curl -s $B/$ID/state/sprinter/text | jq -r '.lines[].text' | grep -v '^ *$'
```

Answers checked live: `IFUP` -> lease `10.0.2.15`; `PING 10.0.2.2` -> 2 / 2 replies; `NSLOOKUP example.com` -> a real
address; `WGET` -> `20000 bytes received`. A host ping to the internet works only where the host itself may ping
(ICMP is often blocked). Internet answers need real time: `run_frames` runs the machine faster than the host network
answers, so the kit's 1-second timeouts expire - resume the machine for those (the scripted test servers have no such
limit).

## What is plugged where, and what it does

```bash
curl -s $B/$ID/state/isa | jq '{summary, latch, slot2: .slots[1] | {card, resources, z80_access, registers}}'
# summary: "slot 1: empty; slot 2: ne2000 I/O #300-#31F IRQ 3"
# z80_access.io: "#1FFD bit 4 set, window 3 page #D6, #9FBD AEN = 0: CPU #C300-#C31F (A9-A0 decoded: ...)"
curl -s $B/$ID/state/network | jq '{slots, gateway: .ethernet_gateway | {ports, arp, tcp, counters}}'
# slots[1]: chip RTL8019AS, base #300, mac, link ethernet-gateway, registers (CR, ISR, IMR, RCR, TCR, DCR, PSTART,
#           PSTOP, BNRY, CURR, TPSR, TBCR, RSAR, RBCR, CRDA, PAR, CONFIG1-3), counters (tx / rx frames, filtered, ...)
# ethernet_gateway.ports[0].dhcp_lease: "10.0.2.15"
curl -s "$B/$ID/state/isa/journal?last=8" | jq -c '.entries[] | {frame, t, pc, access, cpu_address, what, value}'
# who touched which card register: {"pc":"#..","access":"write","cpu_address":"#C301","what":"PSTART","value":"#46"}
```

## Frames: capture and injection

```bash
curl -s "$B/$ID/network/frames?link=isa2.eth&last=6" | jq -r '.frames[] | "\(.index) \(.direction) \(.summary)"'
# 43 from_card IPv4 10.0.2.15:57070 > 127.0.0.1:18181 TCP . seq ... len 0
curl -s -o scratch/sprinter.pcap "$B/$ID/network/frames?format=pcap"   # tcpdump -nr scratch/sprinter.pcap
curl -s -X POST $B/$ID/network/frame -H 'Content-Type: application/json' \
     -d '{"link":"isa2.eth","hex":"FFFFFFFFFFFF5255000002020806..."}'  # towards the card, as if from the wire
```

CLI: `network`, `network frames isa2.eth [file.pcap]`, `network frame isa2.eth <hex>`, `isa`, `isa journal 8`.
Lua / Python: `network_state()`, `network_frames("isa2.eth", 8)`, `network_inject_frame(link, hex)` (Python also
`network_frames_pcap()`), `isa_state()`, `isa_journal(8)`. MCP: `inspect_state` aspects `network` and `isa`; frames and
the journal through `invoke_api`.

## Time travel

A TTD recording carries the card (blob 45 `EthernetNics`: the DP8390, the packet RAM, the EEPROM, the gateway's ARP /
TCP / UDP tables and queued frames) and the virtual network's tables; every host answer is a journaled input. A replay
needs no host and reproduces every frame byte for byte (checked by `SprinterNetworkKit_Test.TtdReplaysTheFetchWithoutTheHost`).
A recording made with another slot population is refused at load ("ISA slot 2 mismatch: ...").

## Notes

- The kit polls: no interrupt line is needed (the PIO port B lines are ISA phase I4).
- `RESET DRV` (`#9FBD` bit 7, the kit's reset pulse) resets the card and ends a hung UM9003 cycle; a machine reset
  does not reach ISA cards.
- Ring full: frames wait in the gateway (a switch with a buffer) until the driver frees the ring - nothing is lost.
- A TCP connection whose guest stops reading is reset after 256 KB of queued host data (no host-side pause yet).

## SprinterESP Wi-Fi with the Sprinter ESP Network Kit

The SprinterESP card (Roman Boykov, rev 1.0.5; network phase SN3) is a TL16C550C at ISA I/O `#3E8` with an ESP-12F
(ESP8266) behind it. It is fitted on request - slot 1 is free until ISA phase I2's ZX-bus adapter; in slot 2 it
replaces the default NE2000. The emulated ESP runs Espressif's AT firmware 2.2.2 (what the Sprinter ESP Network Kit
expects) and joins the virtual access point `UnrealNG` (DHCP `10.0.2.15`, the same DNS, hosts table, forwards and host
bridge as every network adapter). Kit 0.2.1 runs unchanged: `NETUP`, `PING`, `WGET`, and `UNETESP.DLL` (through the
kit's `UNETTEST`).

| What | Value (from the board's schematic) | Change it |
|:--|:--|:--|
| Slot | none by default | `[ISA] Slot1=SPRINTERESP`; create option `"sprinter": {"isa_slot1": "sprinteresp"}` |
| UART | TL16C550C, 14.7456 MHz (divisor 8 = 115 200 baud), `#3E8-#3EF`, CPU `#C3E8-#C3EF` in window 3 (page `#D4` / `#D6`) | fixed; A13-A3 decoded, AEN and A19-A14 not (any `#9FBD` value reaches it) |
| IRQ | INTR straight to IRQ3 (not gated by OUT2); the kit polls | fixed (the PIO line is ISA phase I4) |
| ESP pins | MCR OUT1 (`#04`) holds the ESP in reset; OUT2 (`#08`) pulls GPIO0 low (download mode at the next reset release) | - |
| Firmware | ESP-AT 2.2.2 | `[NETWORK] EspChip=ESP8266-AT221` (the kit's 2.2.1 profile) or `ESP8266` (NonOS 1.7.4); runtime `esp_chip` |
| Line | `AT` = the card's own ESP | `[ISA] Slot1Peer=` / runtime `isa1_peer`: `loopback`, `tcp:host:port`, `serial:/dev/tty...,115200` (a real ESP on a USB adapter), `none` |
| MAC | `5C:CF:7F:5A:<instance>:<slot>` | `Slot1Mac=` |

The kit probes both slots for a 16550 at `#3E8` (IER high nibble 0, scratch register `#55` / `#AA`): no slot setting
in `NET.CFG`.

```bash
# Disks as for the RTL kit (BIOS 3.06 HF2; see the BIOS 3.07 note above), the ESP kit on the floppy
K=testdata/machines/sprinter/network
mformat -C -i scratch/espkit.img -f 1440 ::
for f in $K/sprinter-esp-0.2.1/*.EXE $K/sprinter-esp-0.2.1-unet/UNET*; do mcopy -i scratch/espkit.img "$f" ::; done
printf 'SSID=UnrealNG\r\nPASS=\r\nDHCP=1\r\nDNS1=\r\nTZ=+0\r\nBAUD=115200\r\n' > scratch/NET.CFG
mcopy -i scratch/espkit.img scratch/NET.CFG ::

B=http://localhost:8090/api/v1/emulator            # UNREAL_WEBAPI_PORT moves the port
ID=$(curl -s -X POST $B/start -H 'Content-Type: application/json' \
       -d '{"model":"SPRINTER","sprinter":{"bios":"3.06","fast_start":true,"isa_slot1":"sprinteresp"}}' | jq -r .id)
curl -s -X POST $B/$ID/pause
curl -s -X POST $B/$ID/media/ide0.master/insert -H 'Content-Type: application/json' \
     -d "{\"path\":\"$PWD/scratch/sp-net.img\",\"access\":\"session\"}"
curl -s -X POST $B/$ID/media/fdd.b/insert -H 'Content-Type: application/json' -d "{\"path\":\"$PWD/scratch/espkit.img\"}"
curl -s -X POST $B/$ID/reset
# Record first (owner rule): a hang can then be rewound and read in the journals instead of re-run blind
curl -s -X POST $B/$ID/ttd/start -H 'Content-Type: application/json' -d '{"history_limit_frames":30000}'
curl -s -X POST $B/$ID/run_frames -H 'Content-Type: application/json' -d '{"count":1500}'   # DSS: C:\>
curl -s -X POST $B/$ID/resume

type() { curl -s -X POST $B/$ID/keyboard/type -H 'Content-Type: application/json' -d "{\"text\":\"$1\\n\"}"; }
type 'B:'; type 'NETUP'          # "ESP firmware profile: 2.2.2.", "UART speed set with RTS/CTS flow control.", "NETUP done."
type 'CLS'; type 'PING 10.0.2.2' # "Reply time: 12 ms"
type 'CLS'; type 'WGET http://127.0.0.1:18215/f.bin -o C:\\F.BIN -y'   # python3 -m http.server 18215 on the host
type 'CLS'; type 'UNETTEST 127.0.0.1 18215'                           # NETINIT ok, resolve, ping, the HEAD reply
curl -s $B/$ID/state/sprinter/text | jq -r '.lines[].text' | grep -v '^ *$'
```

Checked live (2026-10-03, own instance): `NETUP` -> 2.2.2 profile, flow 3, `10.0.2.15`; `PING 10.0.2.2` -> 12 ms;
`PING example.com` resolves through the host (two DNS queries) but times out where the host may not send ICMP;
`WGET` -> "Downloaded: 20000 bytes" from the host's HTTP server; `UNETTEST` -> NETINIT ok, the server's
`HTTP/1.0 200 OK`. Wait for the prompt between commands: keys typed while a program runs are lost.

What is plugged and what it is doing:

```bash
curl -s $B/$ID/state/isa | jq '{summary, slot1: .slots[0] | {card, resources, z80_access}}'
# summary: "slot 1: sprinteresp I/O #3E8-#3EF IRQ 3; slot 2: ne2000 I/O #300-#31F IRQ 3"
curl -s $B/$ID/state/network | jq '.slots[0] | {peer_spec, uart, pins, esp: (.esp | {firmware, state, wifi, ip, at_session})}'
curl -s $B/$ID/state/network | jq -c '.slots[0].esp.exchanges[-4:][]'   # the last AT requests and replies
# {"request":"AT+CWJAP=\"UnrealNG\",\"\"","reply":"WIFI DISCONNECT\r\nWIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n"}
curl -s "$B/$ID/state/isa/journal?last=4" | jq -c '.entries[] | {pc, access, cpu_address, what, value}'
# {"pc":"#54CC","access":"read","cpu_address":"#C3ED","what":"LSR","value":"#61"}
curl -s -X POST $B/$ID/ttd/stop
curl -s -X POST $B/$ID/network/config -H 'Content-Type: application/json' -d '{"esp_chip":"esp8266-at221"}'
# refused while recording ("a TTD recording is running ..."); afterwards the card is fitted again (UART registers kept)
```

Interrupts: the card's INTR reaches PIO port B bit 0 / 1 (ISA I4); BC-Term 1.11 receives the ESP through IM 2 -
[sprinter-isa.md](sprinter-isa.md#interrupt-lines-verified-2026-10-03).

CLI: `network` (slot rows), `network set esp_chip=esp8266-at221 isa1_peer=loopback`, `isa`, `isa journal 8`. Lua /
Python: `network_state().slots[1].esp` / `network_state()["slots"][0]["esp"]`, `network_configure{isa1_peer="at"}` /
`network_configure(isa1_peer="at")`. MCP: `inspect_state` aspect `network` prints one line per slot (UART baud, MCR,
ESP firmware and state, Wi-Fi, IP, links, AT requests); changes through `invoke_api`. Qt: Tools > Network - the
slot line, "Slot 1 SprinterESP: its 16550 is wired to", the chip box with the 2.2.x presets.

TTD: blob 46 (`SlotSerial1`; slot 2: 47) holds the 16550 and the ESP module (AT state, sockets, received bytes by
journal reference); a replay without the host reproduces the session byte for byte
(`SprinterEspKit_Test.TtdReplaysTheSessionWithoutTheHost`).
