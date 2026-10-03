    _____            _       __               ___________ ____ 
   / ___/____  _____(_)___  / /____  _____   / ____/ ___// __ \
   \__ \/ __ \/ ___/ / __ \/ __/ _ \/ ___/  / __/  \__ \/ /_/ /
  ___/ / /_/ / /  / / / / / /_/  __/ /     / /___ ___/ / ____/ 
 /____/ .___/_/  /_/_/ /_/\__/\___/_/     /_____//____/_/      
     /_/                                                       
================================================================
  Sprinter ESP Network Kit
  Network utility package for Sprinter DSS and the SprinterESP
  Wi-Fi card (ESP12-F / ESP8266, ESP-AT firmware). Version 0.2.1
================================================================

Utilities that put Sprinter on the network through the
SprinterESP Wi-Fi card: configure and bring up the connection,
diagnostics, file download (HTTP / FTP / TFTP), network time,
and an ESP-AT terminal.

IMPORTANT: the recommended firmware is ESP-AT version 2.2.2.0. Download the
full 2 MB image and the host-side flashing guide from the project GitHub:
https://github.com/witchcraft2001/sprinter_wifi/tree/main/firmware
The package is backwards compatible with ESP-AT 2.2.1, but upgrading is
strongly recommended. Corrupt files, missing bytes and slow transfers are
known limitations of 2.2.1. To check the firmware version run NETPROBE (the
AT+GMR line) or type AT+GMR in WTERM.


GETTING STARTED
---------------
  1. Unpack the package to C:\WIFI and switch to it:
         C:
         CD \WIFI
  2. Add the package directory to PATH in C:\SYSTEM.BAT so the tools can be
     run from any directory. Add this line, preserving the existing PATH:
         SET PATH=%PATH%;C:\WIFI
     Reboot DSS or run C:\SYSTEM.BAT once to apply the change.
  3. Configure Wi-Fi (once):       NETCFG /W
         your SSID and password, DHCP=1, BAUD=115200
  4. Bring the connection up:      NETUP
  5. Check connectivity:           PING 8.8.8.8

  Full quick-start guide ..... see DOCS\HOWTO.TXT
  Per-tool help .............. DOCS\<NAME>.TXT  or  <NAME> /?
  Full reference ............. see DOCS\USAGE.MD


PROGRAMS
--------
  NETCFG.EXE    network configuration editor (NET.CFG)
  NETUP.EXE     bring the connection up
  NETRESET.EXE  reset and reinitialize the ESP
  NETPROBE.EXE  low-level ESP diagnostics
  PING.EXE      host reachability check
  WGET.EXE      HTTP file downloader
  FTP.EXE       FTP file transfer and listing (passive mode)
  TFTP.EXE      TFTP client
  NTP.EXE       set the system clock over the network
  WTERM.EXE     ESP-AT terminal (manual AT commands)
  TELNET.EXE    ANSI Telnet client with Zmodem send/receive


DOCUMENTATION  (DOCS\ directory)
--------------------------------
  HOWTO.TXT     quick start (English)
  HOWTO_RU.TXT  quick start (Russian)
  USAGE.MD      full reference and exit codes
  <NAME>.TXT    per-tool help: PING.TXT, WGET.TXT, FTP.TXT,
                TFTP.TXT, NTP.TXT, NETCFG.TXT, NETUP.TXT,
                NETRESET.TXT, NETPROBE.TXT, WTERM.TXT, TELNET.TXT


CONFIGURATION  (CONFIG\ directory)
----------------------------------
  NETSMPL.CFG   NET.CFG template. Copy it to NET.CFG or run
                NETCFG /W.


EXAMPLES  (EXAMPLES\ directory)
-------------------------------
  TFTPGET.BAT   receive a file over TFTP
  TFTPPUT.BAT   send a file over TFTP
  WGETTRD.BAT   download KLAD026.ZIP from tr-dos.ru
  FTPLIST.BAT   show an FTP directory listing

  CONNECT.BAT   bring the connection up and test it (package root)


OTHER
-----
  README.TXT    this file (English)
  READMERU.TXT  short description (Russian)
  VERSION       package version
  LICENSE       license


----------------------------------------------------------------
Package author: Dmitry Mikhalchenkov.
FidoNet: 2:5030/1997.10
Based on Sprinter-Wi-Fi / ESPKit code (Roman Boykov); the
UART / ISA / ESP-AT modules keep their BSD 3-Clause license.
Original repository: https://github.com/romychs/SprinterESP
