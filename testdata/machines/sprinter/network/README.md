# Sprinter network kits (test fixtures)

The released DSS programs of the 2026 Sprinter network kits, byte for byte as their release archives hold them, for
the network tests ([design](../../../../docs/inprogress/2026-10-02-sprinter-network/tdd.md) §15, phase SN0; owner
open question Q6: pin the release that was current when the phase started, refresh deliberately, assert behavior -
a lease, a received file - never screen text).

| Folder | Kit | Release | Archive (SHA-256) | Card | License |
|:--|:--|:--|:--|:--|:--|
| [rtl8019a-0.3.8/](rtl8019a-0.3.8/) | Sprinter RTL8019AS Network Kit, Dmitry Mikhalchenkov | [0.3.8](https://github.com/witchcraft2001/sprinter-rtl8019a/releases/tag/0.3.8) (2026-09-13) | `sprinter-rtl8019a.zip` `eab2a428...b6d5c` | NE2000-class Ethernet (`[ISA] SlotN=NE2000`) | none stated in the repository (test material) |
| [sprinter-esp-0.2.1/](sprinter-esp-0.2.1/) | Sprinter ESP Network Kit, Mikhalchenkov on Roman Boykov's code | [0.2.1](https://github.com/witchcraft2001/sprinter_wifi/releases/tag/0.2.1) (2026-09-02) | `sprinter-esp_v.0.2.1.zip` `3347e1ee...1fbd96d73e` | SprinterESP Wi-Fi (network phase SN3) | BSD-3-Clause (`LICENSE` in the folder) |
| [sprinter-esp-0.2.1-unet/](sprinter-esp-0.2.1-unet/) | `UNETESP.DLL` as committed at the kit's tag [0.2.1](https://github.com/witchcraft2001/sprinter_wifi/tree/0.2.1) (`9b08bd4`; the release archive does not ship it) and `UNETTEST.EXE` assembled from that tag's `src/apps/unettest.asm` (sjasmplus 1.23.1, `--raw`, `-I src/include -I src/lib`, the kit's `tools/build.sh` options) | tag 0.2.1 | (hashes below) | SprinterESP: the UNET DLL interface (`UNETTEST host port`: NETINIT, resolve, ping, connect, send, receive, close) | BSD-3-Clause (`LICENSE`) |

Full hashes: `eab2a42812654dfbeec4c387aded60dc776ccdd315354f26b0f18eef5f9b6d5c` (RTL 0.3.8),
`3347e1ee28dd3243f685baf77e28d36d033d9392ceae6825e622bd0d7ac41fbd` (ESP 0.2.1); `UNETESP.DLL`
`f03352df4f4af42683d1fde4a8260d0bd3a55f51b1366f10b5467b38566e9abc`, `UNETTEST.EXE`
`46f170d4d5e3aa039b9b956fb7e366380d3cb330034de587c7e1a2382bce810e` (sprinter-esp-0.2.1-unet).

## RTL8019AS kit: what the tests run

`NETCFG -i` reads `NET.CFG` (template `NETSMPL.CFG`) into the DSS environment, `IFUP` brings the link up (static or
DHCP), then `PING`, `NSLOOKUP`, `NTP`, `TFTP`, `WGET`, `FTP`, `TELNET`. `NICINFO` and `ISAPROBE` find the card;
`UNETRTL.DLL` serves other programs. The bring-up diagnostics (`NICRAM`, `NICLB`, ...) are not in the release
archive. Usage pages: `USAGE.TXT`, `HOWTO.TXT`. A test copies the files onto a DSS disk (a 1.44 MB floppy it builds,
or a copy of the system hard disk) with a `NET.CFG` such as:

```text
RTL_HW=1/#300
IP=DHCP
```

(slot 1 in the kit's numbering = the emulator's slot 2, page `#D6`). Every file stays exactly as released: the
`.gitattributes` rule for this folder keeps the CRLF text files CRLF.

## ESP kit: what the tests run

`NETUP` reads `NET.CFG` beside `NETUP.EXE` (`SSID=UnrealNG`, `PASS=`, `DHCP=1`, `BAUD=115200`), probes the firmware
with `AT+SYSSTORE?` (ERROR = 2.2.1 profile, OK = 2.2.2), sets `AT+UART_CUR=115200,8,1,0,3`, joins the access point,
publishes `NET_*`; then `PING <host>` (`AT+PING`, the ESP resolves the name), `WGET <url> -o <file> -y`. The card sits
in ISA slot 1 (`[ISA] Slot1=SPRINTERESP`); the kit finds it by probing both slots at `#3E8`. `UNETTEST [-d DLL] host
port` (from the `-unet` folder, with `UNETESP.DLL` beside it) loads the DLL through libman and walks its calls.
