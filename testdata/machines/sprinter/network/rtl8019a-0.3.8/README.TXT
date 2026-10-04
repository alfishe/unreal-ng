============================================================
  Sprinter RTL8019AS Network Kit
  Network utilities for Sprinter DSS and an ISA-8 Ethernet
  card based on the Realtek RTL8019AS / DP8390
============================================================

This package connects a Sprinter computer to an Ethernet network through
an ISA-8 RTL8019AS card. It contains network setup tools, diagnostics,
file-transfer clients, a network clock client, and an ANSI Telnet client.


GETTING STARTED
---------------

1. Install

   Unpack the package to C:\RTL (or use the preinstalled copy). Add the
   package directory to PATH in C:\SYSTEM.BAT so all tools can be run from
   any location. Add this line without removing the existing PATH entries:

       SET PATH=%PATH%;C:\RTL

   Reboot DSS or run C:\SYSTEM.BAT once to apply the change. Then switch to
   the package directory for the initial configuration:

       C:
       CD \RTL

2. Install the sample configuration:

       REN NETSMPL.CFG NET.CFG

3. Edit NET.CFG for your network. For a normal DHCP network, the essential
   lines are:

       RTL_HW=0/#300
       IP=DHCP
       TZ=+5
       NTP=pool.ntp.org

   RTL_HW is <ISA slot>/#<I/O base>. Use 0 or 1 according to the physical
   slot containing the card. Run NICINFO and copy the value printed on its
   first [N0] Slot/Addr: line for the exact detected slot and I/O base.
   Omit RTL_HW to let every utility scan for the card automatically.

4. Check and load the configuration:

       NETCFG -c
       NETCFG -i

5. Bring the interface up and verify connectivity:

       IFUP
       PING 8.8.8.8

   For a quick start after NET.CFG has been installed, run:

       CONNECT

   CONNECT.BAT runs NETCFG -i, IFUP, and PING 8.8.8.8 in sequence.

STATIC IP CONFIGURATION
-----------------------

For a static network, use values appropriate for your LAN:

       RTL_HW=0/#300
       IP=192.168.1.50
       NETMASK=255.255.255.0
       GATEWAY=192.168.1.1
       DNS1=192.168.1.1
       DNS2=1.1.1.1
       TZ=+5
       NTP=pool.ntp.org

RTL_MAC may be left empty to use the MAC stored in the card PROM. After any
NET.CFG change, run NETCFG -i again. IFUP reads the published NET_* values;
the other network clients use the same values and do not open NET.CFG.


PROGRAMS
--------

  NETCFG.EXE    check NET.CFG and publish NET_* environment variables
  IFUP.EXE      bring up a static interface or request a DHCP lease
  PING.EXE      check host reachability
  NSLOOKUP.EXE  resolve a DNS A record
  NTP.EXE       retrieve network time and set the DSS clock
  WGET.EXE      download a file over plain HTTP
  FTP.EXE       FTP download, upload, and directory listing
  TFTP.EXE      TFTP download and upload client
  TELNET.EXE    ANSI/VT100 Telnet client with Zmodem and Ymodem
  NICINFO.EXE   show card identity, MAC, memory layout, and registers
  ISAPROBE.EXE  inspect the ISA window when the card is not detected

  UNETRTL.DLL   reusable RTL8019AS network API for other DSS programs


EVERYDAY COMMANDS
-----------------

Run NETCFG -i and IFUP after boot. DHCP users must run IFUP to obtain a
lease; static users run it to verify the configured address and MAC.

  PING host
      Send ICMP echo requests to an IPv4 address or DNS name.

  NSLOOKUP host
      Query the configured DNS server.

  NTP [server]
      Set the DSS clock. Without an argument, NET_NTP from NET.CFG is used.

  WGET http://host/path/file -o FILE.BIN -y
      Download a plain HTTP resource. HTTPS is not supported.

  FTP host file -o FILE.BIN -u user -p password
      Download a file over passive FTP. See FTP.TXT for upload and listing.

  TFTP host GET remote.bin -o LOCAL.BIN -y
      Download a file over TFTP. PUT is also supported; see TFTP.TXT.

  TELNET host[:port]
      Open an ANSI/VT100 terminal session. Alt+X closes the session. Zmodem
      receive starts automatically; Ymodem operations use Alt+D/Alt+U/Alt+G.

Most commands accept either an IPv4 address or a DNS hostname. Use /? with
an executable for its short built-in help.


FILES AND DOCUMENTATION
-----------------------

  README.TXT    this package guide
  CONNECT.BAT   load NET.CFG, bring up the interface, and test connectivity
  HOWTO.TXT     common options, environment variables, and batch examples
  USAGE.TXT     documentation index
  <NAME>.TXT    detailed help for the corresponding utility
  NETSMPL.CFG   configuration template; rename it to NET.CFG

All package files use DOS 8.3 names. Keep NET.CFG beside NETCFG.EXE unless
your system installation provides a different managed location.


ERRORS AND CANCELLATION
-----------------------

Network waits can normally be cancelled with Esc or Ctrl+C. TELNET reserves
Esc for an active file transfer and uses Alt+X to close the terminal session.

Common ERRORLEVEL values:

  0  success
  1  invalid command line
  2  RTL8019AS not detected
  3  network communication or timeout error
  4  missing or invalid configuration
  5  local file error
  6  server rejected the operation
  7  cancelled by the user

For full syntax and utility-specific status codes, read HOWTO.TXT and the
matching <NAME>.TXT file.
