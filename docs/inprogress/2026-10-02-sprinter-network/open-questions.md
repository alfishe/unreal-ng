# Sprinter network adapters: open questions for the owner

| | |
|---|---|
| **Date** | 2026-10-02 |
| **For** | [tdd.md](tdd.md) (the design follows each recommendation until decided otherwise); facts in [research.md](research.md) |
| **Already decided (2026-10-02)** | NE2000-class Ethernet is built and comes first; maximum reuse of the shared network stack, no Sprinter-only parallel path; ISA bus Sprinter-only with the `ZxBusPresent()` seam, slot 1 = ZX-bus adapter + NeoGS, ISA RESET resets the GS (ISA design Q1-Q3) |
| **Needed first** | Q9 before SN2; Q7 and Q1 before SN1; the rest can be answered during the phases |

## Q1. Should the `SPRINTER` model ship with a network card fitted?

Slot 1 holds the ZX-bus adapter with the NeoGS (ISA Q2). Slot 2 is free.

| Option | Effect |
|---|---|
| **A (recommended)** | `Slot2=NONE` in the shipped config, as a stock Sp2000; one INI line (`Slot2=NE2000`) or one network window click fits the Ethernet card. The Sprinter TTD boot fixture does not change |
| B | `Slot2=NE2000` (RTL8019AS at `#300`) by default: the kits work out of the box; costs nothing while idle (the chip runs lazily), but the boot fixture gains blob 39 and every Sprinter user has a network card |

**Recommendation: A**, plus a ready-made recipe line. B is a one-line change later if the owner prefers it.

**Owner decision (2026-10-02): B** - the NE2000 is fitted by default (slot 2, RTL8019AS at `#300`): "the network card is fitted by default - that is the most important part". The Sprinter boot fixture is re-recorded with its blob when SN1 lands.

## Q2. Bridge to the host's real LAN (phase SN6)?

The user-mode gateway (NAT) gives the Sprinter everything the kits need (DHCP, DNS, TCP, UDP, ping, inbound
forwards) with no admin rights. A bridge puts the card's frames on the real LAN (the router's DHCP, other machines
see the Sprinter), but needs libpcap / Npcap (Windows users install Npcap) or a TAP device and admin rights, and every
received frame becomes a journaled TTD input.

**Recommendation:** not now. Build NAT (SN2); keep SN6 in the plan as optional and start it only on request.

**Owner decision (2026-10-02): the bridge right away** - SN6 is no longer optional and moves up next to SN2 (NAT stays as the no-admin default; the bridge is the second host path for the same card).

## Q3. Default MAC address

| Option | Effect |
|---|---|
| **A (recommended)** | `02:53:50:00:<instance>:<slot>` - locally administered ("53 50" = "SP"), unique per emulator instance and slot, so two instances on a bridged LAN never collide |
| B | a Realtek-style address `00:E0:4C:xx:xx:xx` | looks like the real card, but could collide with a real Realtek device on a bridged LAN |

**Recommendation: A**; `SlotNMac=` overrides it.

## Q4. RTL8019AS internal loopback: datasheet or MAME behavior?

On the real chip an internal-loopback transmit (TCR.LB) does not reach the receive ring (the data stays in the
FIFO). MAME puts the frame into the ring. The RTL kit's `NICLB` accepts both.

**Recommendation:** the datasheet behavior (the owner's faithful-hardware rule). MAME's variant is not needed by
any program.

## Q5. The UM9003 clone's reset-port hang

On the UMC UM9003AF a read of the NE2000 reset port (`base + #1F`) stalls the ISA cycle and freezes the Sprinter
(the RTL kit documents it and avoids that port on non-Realtek chips).

| Option | Effect |
|---|---|
| **A (recommended)** | Model it literally for the `UM9003` variant: the machine stops as on the real board; the slot report and the log say "ISA cycle stalled by slot N (UM9003 reset port)" so the user sees why |
| B | Treat the read as a reset like the RTL8019AS | friendlier, but software that is wrong on real hardware would work here |

**Recommendation: A** (faithful to real hardware; only the non-default variant is affected).

## Q6. Which kit versions become test fixtures

The kits are young (0.1-0.3) and change weekly. The end-to-end tests need their programs.

**Recommendation:** pin the latest tagged releases at the time of SN0 (today: RTL kit 0.3.8, Wi-Fi kit 0.2.1, 3C509B
kit 0.1.2) as release archives / floppy images in `testdata/machines/sprinter/network/` with `testdata/NOTICE.md`
entries; refresh them deliberately, and let the tests assert behavior (lease, file received), not screen text.

## Q7. Order: network cards before ISA RAM?

The ISA design ordered I3 ISA RAM (one program: Shaos's TIMER) before the UART cards. Network phase SN1 needs only
ISA I1.

**Recommendation:** after I1 and I2 (the owner's MOD-playback goal), build SN0-SN2 (Ethernet), then SN3 (Wi-Fi),
then I3 ISA RAM, I4 PIO lines, SN4 (modem), SN5 (3C509B). Network cards have about thirty programs, ISA RAM one.

## Q8. SprinterNet (Shaos's W5100 card)

No card was released; its programs (INET1-4, a Gopher browser) run only in SprintEm through a high-level DSS call
(`RST #10`, `C = #D0`).

**Recommendation:** not planned. Revisit if the card is released (the W5100 is close to our W5300 model, so the chip
side would be cheap); a high-level DSS call emulation is a software patch, not hardware.

## Q9. The gateway: our own code, or a vendored stack?

The NE2000 and the 3C509B send Ethernet frames; the virtual network works with sockets. Something must terminate
the guest's TCP / IP.

| Option | Effect |
|---|---|
| **A (recommended)** | Our own small user-mode gateway (`EthernetGateway`, ARP / IPv4 / ICMP / UDP / TCP) on top of the virtual network's socket API. Deterministic by construction (no host timers), reuses `DhcpServer`, DNS, hosts table, forwards, the host bridge and the existing TTD journal unchanged. New TCP code to get right (tested against the real kit programs) |
| B | Vendor lwIP (BSD) as the router side | a mature TCP, but its timers and allocator must run on emulated time, and its own DHCP / DNS would duplicate the virtual network's |
| C | Vendor libslirp (BSD-3, needs GLib) | QEMU's proven NAT, but it talks to host sockets itself: it would bypass our host bridge and the TTD journal (a parallel host path, against the reuse rule) |

**Recommendation: A.**

**Owner decision (2026-10-02): A** - our own gateway component, shared by every Ethernet card, "and test it well": a dedicated test suite (ARP, IPv4 fragments, ICMP, UDP, TCP state machine incl. retransmit, window, out-of-order, RST / FIN races, DHCP and DNS through the virtual network, inbound forwards, TTD replay bit-exact) plus end-to-end runs of the real Sprinter kit programs.

## Q10. Guest registry for virtual network peers

Each new serial peer today needs a fixed guest number and edits in five places (`SerialGuests`, `hasGuest`,
`ComPort::SerialNetGuests`, `VirtualNetwork::Reset` / `SaveState` / `LoadState`); the ATM2IOESP took guest 4. The
Sprinter cards need up to four UART channels plus the Ethernet gateway.

**Recommendation:** replace the fixed fields by a registry keyed by a stable name (`"isa2.uart0"`, `"isa2.eth"`),
mapping the existing guests 1-4 to their old numbers so older recordings load (tdd §5.3). Minimal alternative:
guests 5-9 as five more fixed fields.

## Q11. Changing a network card while the machine runs

The existing network window changes `Card=` at run time (refit). The ISA design reads the slot population at
instance creation (ISA Q5 = A).

**Recommendation:** follow ISA Q5 in v1: the card kind and base change with a new instance (the reply says so);
peer, MAC-independent settings, host access, AT dialect and phone book change live. Runtime refit of slot cards comes
with PLAN #82.

## Q12. SprinterSerial: what do the UART's unconnected modem inputs read?

Found while building SN4 from the rev 1.1.1 netlist: COM1's CH340 is wired to the PC16552D modem pin for modem pin
(the CH340's `RTS#` / `DTR#` outputs on the UART's `-RTS1` / `-DTR1` outputs, its `CTS#` / `DSR#` / `DCD#` / `RI#`
inputs on the UART's inputs), so nothing drives the UART's CTS / DSR / DCD / RI on COM1; COM2's DB-9 brings CTS
only. The CH340 datasheet names no pull-ups on those pins; the PC16552D / SC16C2552B datasheets do not say what a
floating modem input reads. Consequence: BC-Term waits for CTS before every byte (MSR bit 4), so with "inactive" it
cannot send on COM1 (on COM2 an external modem's CTS reaches it).

| Option | Effect |
|---|---|
| **A (built)** | Unwired inputs read **inactive** (as an open TTL input, or one with a pull-up, reads high = inactive for these active-low pins). COM1 is a plain RX / TX line; software that waits for CTS does not send there |
| B | Read **asserted** (DSR / DCD / CTS on, RI off), as the SprinterESP and ATM2IOESP models do for their unconnected pins | BC-Term would work on COM1 |

**Recommendation: A** until a real card is measured (a terminal program on COM1 that waits for CTS would tell);
switching is one `Uart16550::Params::msrUnwired` value in the preset.

**Owner decision (2026-10-04): A, as the hardware, plus a loopback test plug.** Unwired and unconnected inputs read
inactive. A new peer `PLUG` (any `ComPort=` / `SlotNPeer=` / `SlotNPeerB=`) is an RS-232 loopback connector: the bytes
come back and the UART's own RTS drives CTS, DTR drives DSR and DCD (`LOOPBACK` keeps holding them active). It acts on
the inputs a card wires to its connector: on SprinterSerial COM2 (CTS only) BC-Term can then send; COM1 stays a
plain RX / TX line, because its CH340 drives none of the UART's modem inputs.

## Q13. SprinterSerial with both IRQ jumpers fitted on the Sprinter

The Sprinter joins every IRQ pin of a slot into one line (ISA I4). SprinterSerial's INTA / INTB are push-pull
outputs through J5 / J6: with both jumpers fitted, an idle channel drives the line low while the other requests
(high) - two outputs fight; the level the PIO sees is not defined by any datasheet.

**Built:** the higher level wins (a request is seen), the slot report says `irq_contention: true` and the IRQ cause
names it; J6 is open by default (`Slot1IrqB=0`), so one channel interrupts. **Question:** keep this, or make the
contended line read low (no interrupt), as the stronger low-side driver of CMOS outputs usually wins?
