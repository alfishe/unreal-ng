============================================================
  Sprinter 3C509B Network Kit 0.1.2
  Network utilities for Sprinter DSS and a 3Com EtherLink
  III 3C509B (TPO or TP) Ethernet card in an ISA slot
============================================================

This package connects a Sprinter computer to an Ethernet network through a
3Com EtherLink III 3C509B ISA card (the TPO and TP boards are supported; the
TP's AUI connector is not used). It contains network setup tools,
read-only card diagnostics, file-transfer clients, a network clock client,
an ANSI Telnet client, and a loadable network library for other programs.

The card is driven by status polling only: the driver never uses ISA
interrupts and never programs one into the card. This kit therefore has no
IRQ setting anywhere, and the IRQ value printed by the diagnostics is
informational -- it is what the card's EEPROM holds, and nothing acts on it.
The card EEPROM is read for identity and MAC only; nothing in this package
ever writes to it.


GETTING STARTED
---------------

1. Install

   Unpack the package to C:\509B (or use the preinstalled copy). Add the
   package directory to PATH in C:\SYSTEM.BAT so all tools can be run from
   any location. Add this line without removing the existing PATH entries:

       SET PATH=%PATH%;C:\509B

   Reboot DSS or run C:\SYSTEM.BAT once to apply the change. Then switch to
   the package directory for the initial configuration:

       C:
       CD \509B

2. Find the card before configuring anything:

       EL3INFO

   EL3INFO is read-only. By default it looks in ISA slot 1 through ID port
   #110; use -s 0 for the other slot and -p for another ID port. Its [E0]
   and [E1] lines print the slot, ID port, product ID and I/O base of the
   card that answered. Record the card label, physical slot and ID port
   before testing, and never scan the ISA space blindly.

3. Create the configuration interactively:

       NETCFG -W

   For a missing file, NETCFG asks for IDPORT and requests confirmation
   before probing only ISA slots 0 and 1 at that port. Discovery can briefly
   runtime-reset the adapter. It never scans other ID ports and never writes
   EEPROM. Declining or finding no card leaves HW=AUTO and continues. Esc
   cancels without writing. A valid existing file is loaded without probing;
   a corrupt or unreadable one is not overwritten.

   Alternatively install the sample configuration:

       REN NETSMPL.CFG NET.CFG

4. Edit NET.CFG for your network. For a normal DHCP network, the essential
   lines are:

       NET=509B
       HW=AUTO
       IDPORT=#110
       IP=DHCP
       TZ=+5
       NTP=pool.ntp.org

   NET must be 509B. HW is either AUTO or <ISA slot>/#<I/O base>, for
   example 0/#300; AUTO lets every utility discover the card, a pinned
   value must match what EL3INFO printed. IDPORT is #100..#1F0 on a 16-byte
   boundary. There is deliberately no IRQ key.

5. Check and load the configuration (NETCFG -W itself does not publish NET_*):

       NETCFG -c
       NETCFG -i

   The number in `RESULT FAIL code=N` is a detailed diagnostic, not the
   command's ERRORLEVEL. `NETCFG` with `code=3` has read NET.CFG but did not
   accept a card at HW/IDPORT: run `EL3INFO -v`. `stage=E3 code=3` means its
   EEPROM Product ID, manufacturer, or MAC was rejected; another stage means
   no ID-sequence response. After that failure, `IFUP` and `PING` correctly
   show `code=22` because NETCFG
   did not publish the `NET_*` variables. The corresponding batch statuses
   are ERRORLEVEL 2 and 4.

6. Bring the interface up and verify connectivity:

       IFUP
       PING 8.8.8.8

   For a quick start after NET.CFG has been installed, run:

       CONNECT

   CONNECT.BAT runs NETCFG -I and IFUP in sequence.


STATIC IP CONFIGURATION
-----------------------

For a static network, use values appropriate for your LAN:

       NET=509B
       HW=AUTO
       IDPORT=#110
       IP=192.168.1.50
       NETMASK=255.255.255.0
       GATEWAY=192.168.1.1
       DNS1=192.168.1.1
       DNS2=1.1.1.1
       TZ=+5
       NTP=pool.ntp.org

NETMASK is required for a static address; GATEWAY, DNS1 and DNS2 are
optional but needed for off-subnet hosts and for DNS names. MAC may be left
empty to use the address stored in the card EEPROM; a MAC written into
NET.CFG overrides it for the current session only and is never written back
to the card.

After any NET.CFG change, run NETCFG -i again. IFUP reads the published
NET_* values; the other network clients use the same values and do not open
NET.CFG.


PROGRAMS
--------

  NETCFG.EXE    create/check NET.CFG and publish NET_* environment variables
  IFUP.EXE      bring up a static interface or request a DHCP lease
  PING.EXE      check host reachability
  NSLOOKUP.EXE  resolve a DNS A record
  NTP.EXE       retrieve network time and set the DSS clock
  WGET.EXE      download a file over plain HTTP
  FTP.EXE       passive-mode FTP download, upload, and directory listing
  TFTP.EXE      TFTP download and upload client
  TELNET.EXE    ANSI/VT100 Telnet client with Zmodem and Ymodem
  EL3INFO.EXE   show card identity, MAC, I/O base and EEPROM fields

  UNET509B.DLL  reusable 3C509B network API for other DSS programs

The developer floppy image carries additional bring-up and measurement
programs (EL3EEP, EL3REG, EL3LB, EL3TX, EL3RX, ISAPROBE, ARP, PINGALT,
UDPTEST, TCPTEST, DLSPEED, DLDIRECT, NETPROF, UNETTEST). They are not part
of this package and are not required for normal use.


EVERYDAY COMMANDS
-----------------

Run NETCFG -i and IFUP after boot. DHCP users must run IFUP to obtain a
lease; static users run it to verify the configured address and MAC.

  PING host
      Send ICMP echo requests to an IPv4 address or DNS name. -t repeats
      until Esc or Ctrl+C.

  NSLOOKUP host
      Query the configured DNS server.

  NTP [server]
      Set the DSS clock. Without an argument, NET_NTP from NET.CFG is used,
      with the quarter-hour NET_TZ offset applied.

  WGET http://host/path/file -o FILE.BIN -y
      Download a plain HTTP resource. HTTPS is not supported. -r resumes an
      interrupted download when the server answers with 206.

  FTP host file -o FILE.BIN -u user -p password
      Download a file over passive FTP. Without -u/-p the client logs in as
      anonymous. See FTP.TXT for upload (PUT) and listing (-l).

  TFTP host GET remote.bin -o LOCAL.BIN -y
      Download a file over TFTP. PUT is also supported; see TFTP.TXT.

  TELNET host[:port]
      Open an ANSI/VT100 terminal session. Alt+X closes the session. Zmodem
      receive starts automatically; Ymodem operations use Alt+D/Alt+U/Alt+G.

  EL3INFO [-v]
      Re-check the card at any time. It only reads.

Most commands accept either an IPv4 address or a DNS hostname. Use /? with
an executable for its short built-in help.


FILES AND DOCUMENTATION
-----------------------

  README.TXT    this package guide
  READMERU.TXT  the same guide in Russian
  CONNECT.BAT   publish NET.CFG and bring the interface up
  HOWTO.TXT     common options, environment variables, and batch examples
  USAGE.TXT     documentation index
  <NAME>.TXT    detailed help for the corresponding utility
  UNET509B.TXT  the loadable network API, for program authors
  NETSMPL.CFG   configuration template; rename it to NET.CFG
  LICENSE.TXT   the license (BSD-3-Clause)

All package files use DOS 8.3 names. Keep NET.CFG beside NETCFG.EXE unless
your system installation provides a different managed location.


ERRORS AND CANCELLATION
-----------------------

Network waits can normally be cancelled with Esc or Ctrl+C. TELNET reserves
Esc for an active file transfer and uses Alt+X to close the terminal session.
Every wait in this kit is bounded: a stalled card or peer always ends in a
reported timeout rather than a hang.

Common ERRORLEVEL values:

  0  success
  1  invalid command line
  2  3C509B not detected
  3  network communication or timeout error
  4  missing or invalid configuration
  5  local file error
  6  server rejected the operation
  7  cancelled by the user

EL3INFO is the exception: as the discovery tool it reports the exact stage
that failed, with its own codes 1..8 documented in EL3INFO.TXT.

Every utility ends with RESULT OK or RESULT FAIL code=N, so the screen and
ERRORLEVEL always agree. For full syntax and utility-specific status codes,
read HOWTO.TXT and the matching <NAME>.TXT file.


----------------------------------------------------------------
Package author: Dmitry Mikhalchenkov.
FidoNet: 2:5030/1997.10
BSD 3-Clause; see LICENSE.TXT.
