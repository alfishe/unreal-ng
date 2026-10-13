# NextPi 1.93D: the supervisor and the NBN commands taken apart

Date 2026-10-13. Source: `NextPi-1_93D.tar.gz` from https://zx.xalior.com/NextPi2/ (MD5 bb12e6f6e262f7030435ced61d94ea78), kept in the
collection at `/Volumes/WDC14Tb-3/Consoles/ZX Spectrum/OS/NextPi/1.93D/` (archive, extracted image, `extracted/opt-nextpi/`). Image
`NextPi2-20260924.img` (15 GB, MD5 61739968c7cb832846a47c33d977173b): MBR with 0 FAT boot (44 MB), 1 ext4 root (10 GB), 2 FAT `/NextPi`
(3 GB), 3 ext4 (1 GB). Raspbian stretch (pi-gen, 2018-11-13), kernel 4.19.66+. This note replaces the guesses of
[research-pi-accelerator.md](research-pi-accelerator.md) where the two differ (section 5).

## 1. What serves the protocol

There is no daemon. The chain is:

1. `/lib/systemd/system/nextpi-supervisor.service` (linked from `getty.target.wants`): `ExecStart=/opt/nextpi/supervisor/supervisor`,
   `StandardInput/Output=tty`, `TTYPath=/dev/ttyAMA0`, `Restart=always`. The supervisor is the getty on the PL011.
2. `supervisor/supervisor` is a 6-line bash loop: `cd /ram; /bin/bash --rcfile /opt/nextpi/supervisor/.bashrc; echo; echo "Restarting
   Supervisor"`. So `SUP>` is an ordinary root bash on the UART; ^D ends it and the loop restarts it, printing `Restarting Supervisor`.
3. `.bashrc` sources `/root/.profile` and sets `PROMPT_COMMAND='PS1="$(/opt/nextpi/supervisor/SUP)"'`: the prompt is the output of
   the small ELF `SUP`, run before every prompt. `.profile` (`lib/profile`, `lib/bashrc`) also runs `nextpi-plus_list` at login and
   disables `apt` with aliases. `system/startup.sh` builds the volatile RAM environment, runs `nextpi-admin_update` and `/NextPi/nextpi/autoexec.sh`.
   `PATH` is `/ram/nextpi:/mnt/dongle/nextpi:/PlusPack/*/nextpi:/PCache/nextpi/bin:/NextPi/nextpi:/opt/nextpi/bin` (`system/` and `lib/bashrc`).
4. The commands are files in `/opt/nextpi/bin` (47): Python 2 and sh scripts, plus `pylib/` (cache, error, help, pienv, pup, utils).
   `nextpi-file_receive` and `nextpi-file_transmit` are the ~300-line Python 2 scripts below; `nextpi-admin_version` is `cat /opt/nextpi/VERSION`
   (the file is `1.93D\n`).
5. `/boot/cmdline.115K` / `cmdline.2M`: `console=serial0,115200`; the second file is the 2 Mbaud variant (what selects between them was not looked at).

So "the binary that serves the protocol" is: bash + `SUP` (prompt) + the two Python scripts, which speak the raw tty.

## 2. SUP (ELF 32-bit ARM, EABI5, 8.9 KB, not stripped, built from `SUP.c`)

Symbols: `main`, `reset_io`, `get_fs_marker`; imports `nextpi_gpio_init_noatexit` and `nextpi_gpio_set_alt` from `libnextpi.so`
(plus fopen / fgets / strncmp / strstr / fcntl / getcwd / geteuid / printf). Disassembled (`llvm-objdump -d`):

```
main:
    reset_io()                      ; fcntl(0, F_GETFL); fcntl(0, F_SETFL, flags & ~O_NONBLOCK)  (2048 = 0x800)
                                    ; a failure: perror("fcntl F_GETFL error" / "fcntl F_SETFL error"), exit(1)
    if (geteuid() != 0) { if (getcwd(buf, 1024)) printf(buf); exit(0); }   ; not root: print the cwd
    if (nextpi_gpio_init_noatexit() == 0) {
        nextpi_gpio_set_alt(14, 0);  nextpi_gpio_set_alt(15, 0);          ; GPIO14 / 15 back to ALT0 = the UART
    }
    c = get_fs_marker();
    printf("SUP%c ", c); fflush(stdout); exit(0);

get_fs_marker:                       ; reads /proc/mounts
    fopen("/proc/mounts") fails -> return '>' (62)
    for each line: if strncmp(line, "/dev/root", 9) == 0 and (strstr(line, " rw,") or strstr(line, " rw ")) count++
    return count == 1 ? '#' (35) : '>' (62)
```

Meaning: the prompt is `SUP> ` with a read-only root (the default image) and `SUP# ` when the root is writable
(`nextpi-admin_enable`). The step that matters for the Next: **before every prompt GPIO14 and 15 are forced back to ALT0**, i.e. a
script that took the pins over with pigpio (`nextpi-file_receive -gpioalpha`, `nextpi-gpio_mode`) cannot leave the UART dead. No
protocol bytes other than the prompt text come from `SUP`.

## 3. libnextpi.so (ELF ARM shared object, 18 KB, not stripped, `libnextpi.c`)

Exports: `nextpi_get_version`, `nextpi_get_version_str` (reads `/opt/nextpi/VERSION`), `nextpi_gpio_init`,
`nextpi_gpio_init_noatexit`, `nextpi_gpio_deinit`, `nextpi_gpio_config_io`, `nextpi_gpio_set_val`, `nextpi_gpio_get_val`,
`nextpi_gpio_set_byte`, `nextpi_gpio_get_byte`, `nextpi_gpio_set_alt`, `nextpi_gpio_should_quit`, `nextpi_gpio_start_quit_listener`,
`nextpi_read_with_timeout`, `nextpi_write_exact`, `pireg_pthread_loop`. It maps `/dev/gpiomem` (`gpio_mmap`, `gpio_original_io`)
and has a quit-listener thread: the library for Pi programs that talk GPIO to the Next (the "proposed NextPi GPIO protocol"); the
registers the Next side pairs with it are NR #90-#9B. Not disassembled further here (the pthread loop is the next candidate).

## 4. The NBN scripts, exactly (Python 2, `/usr/bin/python2 -u`, version strings 0.06A)

Both put stdin in raw mode (`tty.setraw`: no echo, no CR/LF translation, non-blocking) and write replies to **stderr**: every text
reply is LF only. Per-byte timeout: 1000 polls of 1 us (about 1 ms plus scheduling), then `" Timeout ERROR\n"` and exit 255.

`nextpi-file_receive -nbn <N>` (N is the checksum modulus; clients pass 256):

```
 -> OK\n
<- u32 size (BE), u32 blocks, u16 remainder, filename bytes, 0
 -> (if blocks * 16384 + remainder != size: "Size mismatch ... ERROR\n", exit 253)  else OK\n      ; a second OK
for each of `blocks`: <- 16384 bytes, <- checksum byte; computed = sum(block) % N; equal: -> '!'  else -> '?' and the block is read again
then one more block of `remainder` bytes (empty too: only its checksum byte) with the same '!' / '?' answer
file written to /ram/<filename>   (dirs = ['/', '/ram/', '/NextPi/']; the receiver always uses dirs[1])
```

`-stream` writes to stdout instead; `-gpioalpha` is the experimental GPIO block transfer (clock on GPIO25, data bank 1, handshake on
GPIO24; after the header it sends OK and clocks `size` bytes through `pigpio.read_bank_1`), `-v` prints `0.06A`.

`nextpi-file_transmit -nbn`:

```
 -> OK\n
<- u8 partition (0 "/", 1 "/ram/", 2 "/NextPi/"), filename bytes, 0
 -> missing or empty file: 00 00 00 00 and exit 255   (four zero bytes, no more header)
 -> u32 size, u32 blocks, u16 remainder (BE)
<- one status byte (the script compares it with '<', a bug: it is a string compare with an int, so it never matches)
for each of `blocks`, then the remainder block (empty too: its sum byte 0):
 -> block bytes, checksum byte (sum % 256)
<- one byte; 0x21 '!' goes on, anything else sends the same block again
```

Limits: `max_filesize` is checked (error text `max_filesizeERROR\n`). No CRC: an 8-bit additive sum per block.

## 5. Differences from the earlier research report (and what the emulator did)

| item | research-pi-accelerator.md / first peer | the scripts |
|---|---|---|
| reply line ends | `OK\r\n` | `OK\n` (raw tty) |
| OKs on receive | one | two: at start and after the header check |
| last block on receive | only when remainder > 0 | always: an empty final block still carries its sum byte |
| partition on transmit | a leading 0, then the path | one partition byte (0 / 1 / 2) chosen by the client, then the name |
| missing file | 10 zero header bytes | four zero bytes |
| first block on transmit | a status byte before every block | one status byte, then block + sum answered with '!' each |
| checksum | 8-bit sum | `sum % N` with N from `-nbn N` |
| saved path | the name | `/ram/<name>` |

`NextPiPeer` (core/src/emulator/io/serial/nextpipeer.*) follows the scripts since this note.

## 6. For the emulator

- A faithful peer is: line discipline with echo, bash-like prompt (`SUP> ` / `SUP# `, `bash: x: command not found`), the NBN state
  machines above, the `nextpi-*` command table (47 names; unknown ones reply `ERROR:2`), the version file, `/ram` `/NextPi` `/PCache`.
- Beyond the console: the Pi's other services (audio through I2S, TZX playback, HDMI) need the real image: `ComPort2=TCP:` to
  `nextpi-sandbox`'s QEMU raspi0 (the image above is the same file it downloads).
- To run the image locally: `qemu-system-arm -M raspi0 -kernel boot/kernel.img -dtb boot/bcm2708-rpi-zero.dtb -drive file=...` (the
  sandbox's `bin/run-nextpi`); not done here (no QEMU on this host).
