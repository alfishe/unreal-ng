# Network bridge: an emulated Ethernet card on your real LAN

An emulated Ethernet card that works with raw frames (today the Sprinter's NE2000 / RTL8019AS and 3Com 3C509B in
its ISA slots) normally sits behind the emulator's own small router. That is **NAT**, the default: the card gets
`10.0.2.15` from the emulator, reaches the internet through the host, and needs no setup.

In **bridge** mode the card's frames go out on one of the host's network adapters as they are. The card then gets
its address from your real router, and other computers on the LAN can reach it.

Example: the Sprinter RTL8019AS kit's `IFUP` prints `DHCP: lease IP=10.0.2.15 (server 10.0.2.2 ...)` in NAT. In
bridge mode on a Mac's `en0` it prints `DHCP: lease IP=172.16.30.122 (server 172.16.16.1 ...)`, the office router's
answer, and `PING 172.16.16.1` gets replies from that router.

Socket-level adapters (ESP Wi-Fi modules, ZiFi, ZXNETUSB / W5300, the Hayes modem) are not affected: they have no
frames to bridge and always use the emulator's network.

Design and as-built notes: [sn6-bridge-design.md](../inprogress/2026-10-02-sprinter-network/sn6-bridge-design.md).
Step-by-step recipe: [.recipe/machines/sprinter-network.md](../../.recipe/machines/sprinter-network.md#bridge-to-the-host-lan-verified-2026-10-04-wired-en0-and-wi-fi-en1).

## Words used here

| Word | Meaning |
|---|---|
| **Host adapter** | A network interface of the computer the emulator runs on: `en0` on a Mac, `eth0` on Linux, `\Device\NPF_{...}` on Windows |
| **Packet library** | The system library that reads and writes raw frames: libpcap on macOS and Linux, Npcap on Windows. The emulator loads it when the bridge starts; it is not needed to build or start the emulator |
| **BPF** | macOS's raw-frame devices, `/dev/bpf0`, `/dev/bpf1`, ...: libpcap opens one of them for the bridge |
| **MAC translation** | What the bridge does on Wi-Fi: the card's frames leave with the Wi-Fi adapter's own MAC address, because an access point drops frames from any other address |

## Turning it on

| Where | How |
|---|---|
| Machine config | `[NETWORK] EthernetMode=BRIDGE` and `BridgeAdapter=en0` |
| Qt | Network window, group "Ethernet cards": Path "Bridge to a host adapter", then the adapter |
| WebAPI | `POST /api/v1/emulator/{id}/network/config` `{"ethernet_mode":"bridge","bridge_adapter":"en0"}`; adapters: `GET .../network/adapters` |
| CLI | `network adapters`, then `network set ethernet_mode=bridge bridge_adapter=en0` |
| Lua / Python | `network_adapters()`, `network_configure{ethernet_mode="bridge", bridge_adapter="en0"}` / `network_configure(ethernet_mode="bridge", bridge_adapter="en0")` |
| MCP | `invoke_api` with the WebAPI calls above |

Back to NAT: `ethernet_mode=nat`. The state report shows the result under `ethernet_gateway`: `mode` and the
`bridge` part (`adapter`, `open`, `error`, `library`, frame counters; on Wi-Fi `translation`, `host_mac`, `guests`).
When the adapter cannot be opened, `bridge.error` says why and what to do.

Wired adapters bridge as they are. Wi-Fi adapters bridge through MAC translation, chosen automatically. On Wi-Fi
only IPv4 crosses (ARP, DHCP, ICMP, UDP, TCP).

## Host permissions

The bridge needs the host's permission to read and write raw frames. Without it the bridge stays closed, the card
sees nothing from outside, and `bridge.error` names the fix.

### macOS

libpcap is part of macOS. What is missing is access to `/dev/bpf*`, which only root has by default:

```text
$ ls -l /dev/bpf0
crw-------  1 root  wheel  ...  /dev/bpf0      <- the bridge cannot open it
```

| Way | Lasts | How |
|---|---|---|
| **Temporary: open the devices** | **until the next reboot of the Mac.** macOS recreates `/dev/bpf*` with root-only rights at every start, so after a reboot the bridge fails again until you repeat the command | `sudo chmod o+rw /dev/bpf*` (in Claude Code: `! sudo chmod o+rw /dev/bpf*`) |
| **Permanent: ChmodBPF** | across reboots | install Wireshark (<https://www.wireshark.org>) and its "ChmodBPF" component: a startup item that gives the group `access_bpf` read / write on `/dev/bpf*` at every boot and adds you to that group. Log out and in once after installing |
| Run the emulator as root | while it runs | not recommended: every file it writes then belongs to root |

Check before starting the bridge:

```text
$ ls -l /dev/bpf0
crw----rw-  1 root  wheel  ...  /dev/bpf0      <- after the chmod: the bridge can open it
crw-rw----  1 root  access_bpf ...             <- with ChmodBPF (you are in access_bpf)
```

### Linux

libpcap is usually installed (`libpcap.so.1`; package `libpcap0.8` on Debian / Ubuntu, `libpcap` on Fedora). The
emulator needs two capabilities:

```bash
sudo setcap cap_net_raw,cap_net_admin=eip /path/to/unreal-qt
```

This lasts **until the binary is replaced**: a rebuild or an update installs a new file without the capabilities, so
run `setcap` again after each one. Running as root also works but is not recommended.

### Windows

Install **Npcap** (<https://npcap.com>). That is all: the emulator finds `System32\Npcap\wpcap.dll` itself. It lasts
until Npcap is uninstalled.

## Limits

- The computer the emulator runs on does not see the card through the same adapter: what the packet library sends
  leaves on the wire, and the host's own network stack never receives it (VirtualBox's bridge on macOS has the same
  limit). Other computers on the LAN do see it.
- A card answers ARP and ping only while a program on the emulated machine runs a network stack. The Sprinter kits
  (`IFUP`, `PING`) run and exit, so after `IFUP` nobody answers until the next program starts.
- Every frame from the LAN is recorded as a time-travel (TTD) input: a recorded bridged session replays without the
  LAN and without any permission.
