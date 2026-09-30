# NedoOS bugs found while emulating it

**Status:** found 2026-09-30 while designing the network adapters and reading
the NedoOS kernel and `wizcfg.com`. NedoOS revision 44049473 (kernel driver
`src/kernel/w5300.asm`, last changed 5d55f302, 2024-03-01). Each entry: what
happens, why, how to see it, a proposed fix. Proposed patches are for the
NedoOS authors; unreal-ng emulates the hardware faithfully and does **not**
work around them. Folder index: [README.md](README.md).

| # | Where | Effect | Confidence |
|---|---|---|---|
| B-1 | kernel, W5300 UDP / IPRAW read | receive stream misaligned after a truncated odd datagram; later reads return garbage | source + arithmetic; reproduces in a faithful W5300 model |
| B-2 | kernel, W5300 driver loops | no card or a stuck chip hangs the whole OS forever | seen live (zxdb) |
| B-3 | kernel, `close` with "wait for TX" | the handle is freed before `EAGAIN` is returned; the retry fails and the chip socket leaks | source reading |
| B-4 | kernel, `write` with length 0 | about 64 KB are pushed into the chip's TX FIFO | source reading |
| B-5 | kernel, `close` "TX drained" test | only correct with the default 8 KB TX memory per socket | source reading |
| B-6 | `wizcfg.com` DHCP | Pad options break the option walk; ACK never checked; fallback mask `0.255.255.0` | disassembly |

---

## B-1. UDP / IPRAW read loses one word after a truncated odd datagram

### What happens

A program reads a UDP (or raw IP, e.g. ping) datagram with a buffer shorter
than the datagram. The kernel copies what fits and must throw the rest away
before telling the chip `RECV`. When the buffer length is **even** and the
datagram length is **odd** and **longer** than the buffer, the kernel throws
away one 16-bit word too few. The chip's receive FIFO is then out of step:
the next read takes the leftover byte and the padding byte as the start of
the next packet header, so the sender address, port and length of every
following datagram on that socket are garbage.

Not affected: datagrams that fit the buffer (the usual case: NedoOS DNS reads
with a 512-byte buffer, `network.c` `DNS_PKT_MAX`), odd buffer lengths, even
datagram lengths.

### Why

The W5300 moves data in 16-bit words and pads an odd-length packet with one
byte, so a datagram of `size` bytes occupies `ceil(size / 2)` words after its
header. `w53_rd_nontcp` (`w5300.asm:498-591`) reads `count` bytes to the user,
then skips `skip = size - count` bytes:

```
w5300.asm:563  w53_rd_udp_loop      ; reads count/2 words (ini RX_H, ini RX_L)
w5300.asm:570  ld a,xl : rra        ; count odd?
w5300.asm:572  jr nc,w53_rd_udp_odd ;   no  (the label name is misleading: this is the even path)
w5300.asm:573  ini ... in a,(c)     ;   yes: last user byte + the next byte of the same word
w5300.asm:577  w53_rd_udp_odd
w5300.asm:578  pop hl               ; hl = skip
w5300.asm:585  .l2  add hl,de (de=-2) : jp c,.l1   ; skips floor(skip/2) words
```

Words consumed = `ceil(count/2) + floor(skip/2)`. Words needed =
`ceil(size/2)`:

| count | size | skip | consumed | needed | |
|---|---|---|---|---|---|
| even | even | even | count/2 + skip/2 | size/2 | ok |
| odd | odd | even | (count+1)/2 + skip/2 | (size+1)/2 | ok |
| odd | even | odd | (count+1)/2 + (skip-1)/2 | size/2 | ok (the odd path already took one skip byte) |
| **even** | **odd** | **odd** | count/2 + (skip-1)/2 = (size-1)/2 | (size+1)/2 | **one word short** |

Example: buffer 64 bytes, datagram 101 bytes: consumed 32 + 18 = 50 words,
needed 51. The last data byte and the pad stay in the FIFO.

### How to see it

In unreal-ng (the W5300 model keeps the real FIFO): send a 101-byte UDP
datagram to a NedoOS program that reads with a 64-byte buffer, then send a
second datagram; the second read reports a wrong sender and length. The
emulator's socket view (network debugging TDD) shows `RSR` not returning to 0
after the first `RECV`. The Unreal_NS model hides the bug because its `RECV`
drops the whole packet regardless of what was read.

### Proposed fix

Round the skip up when the user part ended on a word boundary (even count):
the skipped part then starts at a word boundary and must cover the pad byte.

```diff
 		ld a,xl
 		rra
-		jr nc,w53_rd_udp_odd
+		jr nc,w53_rd_udp_even
 		ini
 		ld b,WIZ_S_RX_L
 		in a,(c)
 		dec b
-w53_rd_udp_odd
 		pop hl
 		jp .l2
+w53_rd_udp_even
+		pop hl
+		inc hl			;count even: the skip starts on a word boundary, cover the pad of an odd datagram
+		jp .l2
 .l1
```

Check for all four rows: even count, even skip: `floor((skip+1)/2) = skip/2`;
even count, odd skip: `(skip+1)/2`, the missing word; odd count: unchanged;
datagram shorter than the buffer (`skip = 0`, count = size even):
`floor(1/2) = 0`. Cost: 5 bytes in the driver page; `IX`, `DE`, `BC` untouched.

---

## B-2. No timeouts, no card check: one missing answer stops the OS

### What happens

With the W5300 kernel (`sd_boot.$C`) and no ZXNETUSB card (or a chip that
stops answering), any network call hangs the whole computer: zxdb stays at
"Sending request...", no other task runs, Esc does nothing, only a reset
helps. Seen live on 2026-09-30 (see
[nedoos-kernel-reference.md](nedoos-kernel-reference.md) §6).

### Why

The driver polls chip registers with no limit:

| Loop | Line | Waits for |
|---|---|---|
| `w53_cmd0` | `w5300.asm:489-495` | `Sn_CR` to read 0 after every command |
| `w53_op_cmd1` | `:371-374` | `Sn_SSR` non-zero after OPEN |
| `w53_connect2` | `:395-400` | `ESTABLISHED` or `CLOSED` after CONNECT |
| `w53_close2` | `:478-481` | `Sn_SSR` 0 after CLOSE |

Without a card every port reads `#FF`, so none of them ends. The kernel does
not switch tasks while a kernel call runs (interrupts inside the kernel only
tick the timer), so one stuck call stops everything. The kernel never checks
that the card exists; `wizcfg.com` does (`#81AB` read-back) but only prints
"ZXNetUsb not found".

### Proposed fix

1. **Card check once**: at the first network call (or at boot), write `#0A` to
   `#81AB` and read it back as `wizcfg` does; without a card, every network
   call returns `ERR_HOSTUNREACH` at once (or a new `ERR_NETDOWN`).
2. **Bounded waits**: each loop counts frames with `sys_timer` and gives up
   after a limit (for example 50 frames for `Sn_CR`, the connect timeout for
   CONNECT), returning an error. A shared helper keeps the cost to a few bytes
   per loop:

```asm
; out: NC = the chip took the command; CY = no answer within ~1 s (A = ERR_INTR, HL = -1)
w53_cmd:        ld b,WIZ_S_CR
                out (c),a
                ld a,(sys_timer)
                ld (w53_t0),a
w53_cmd0:       in a,(c)
                or a
                ret z               ;NC (OR clears carry): done
                ld a,(sys_timer)
w53_t0=$+1
                sub 0               ;frames since the command
                cp 50
                jr c,w53_cmd0
                ld hl,-1
                ld a,ERR_INTR
                scf
                ret
```

Callers that `call w53_cmd` add `ret c`; the ones that end with `jp w53_cmd`
pass the result straight to the BDOS caller. The other three loops get the
same pattern. `sys_timer` is in the system page, mapped at `#0000` while the
driver runs.

---

## B-3. `close` "only when TX is empty" frees the handle before answering EAGAIN

### What happens

`OS_NETSHUTDOWN` with `E = 1` ("close only if the send buffer is empty") is
documented (`api_net.txt`) to return `ERR_EAGAIN` while data is still being
sent, and the example retries until it succeeds. The first `EAGAIN` already
released the handle, so the retry fails with `ERR_NOTSOCK`, and the chip
socket is never closed. A later `OS_NETSOCKET` may hand out the same handle
while the chip socket is still connected.

### Why

`w53_close_valid` (`w5300.asm:428-431`) clears the owner (`ld (ix+4),l`
with `l = 0`) as its first action, before the TX-empty check and before the
`ret nz` that returns `ERR_EAGAIN` for an established TCP socket.

### Proposed fix

Clear the owner only on the paths that really close: move `ld (ix+4),0` to
`w53_close3` (the common exit after `Sn_CR_CLOSE`), keeping the early
`ret z` for an already dead socket.

---

## B-4. `write` with length 0 floods the chip

### What happens / why

`wiznet_write` (`w5300.asm:730+`) copies `ceil(len/2)` words with a loop that
starts from `IX = len - 1` and runs until `IX` wraps (`w5300.asm:771-784`). With
`len = 0` it runs about 32768 iterations: 64 KB into an 8 KB FIFO, then
`WRSR = 0` and `SEND`. On a real chip the FIFO pointer wraps and the socket's
TX memory is garbage.

### Proposed fix

At the top of `wiznet_write`: `ld a,h : or l : ret z` (returns `HL = 0`, no
error), as the ESPNET protocol does (it never sends 0 bytes).

---

## B-5. "TX drained" is tested as `FSR high byte = #20`

### What happens / why

The `E = 1` close path (`w5300.asm:437-443`) treats `Sn_TX_FSR` high byte
`#20` as "everything sent". That is true only when the socket's TX memory is
8 KB (`TMSR` default). `wizcfg` does not change TMSR, so today it works; any
other memory split (for example 16 KB for a file server) makes `close` think
data is pending forever, or never pending.

### Proposed fix

Remember the socket's TX size at OPEN (or read `TMSR`) and compare the full
`FSR` with it; or use the chip's `Sn_IR.SENDOK` for the last SEND.

---

## B-6. `wizcfg.com` DHCP client

Found by disassembly (no source in the NedoOS repository): details in
[reference-wizcfg.md](reference-wizcfg.md) §3.

| Problem | Effect | Proposed fix |
|---|---|---|
| The option walk treats every option as code + length (`#06CC-#0773`) | a server that sends Pad (`#00`) options misreads the rest; the lease has a wrong mask / router / DNS | skip `#00` as a one-byte option |
| The DHCP message type is not checked; the ACK is never read (`#0877-#089D`) | a NAK or a second server's offer is taken as success | check option 53 (OFFER = 2, ACK = 5) |
| DHCP mode is marked by `mask[0] = 0` | when DHCP fails, the static fallback uses mask `0.255.255.0` | keep a separate DHCP flag |
| Only 5 receive windows of ~3.2 s for 6 DISCOVERs | about 16 s at boot without a DHCP server | fewer tries or a shorter window when no answer at all |

The unreal-ng virtual network sends no Pad options, so NedoOS works with it;
a real home router may trigger the first problem.
