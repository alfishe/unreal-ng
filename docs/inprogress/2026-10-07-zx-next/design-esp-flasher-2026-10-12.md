# ESP flasher on the Next's UART 0: what the ROM bootloader protocol needs, and a minimal plan (2026-10-12)

Status: design note only, nothing implemented. The ESP module is not in the RTL; the RTL's part is the wiring this note relies on
(NR #A8 / #A9 drive ESP GPIO0, UART 0 is the module's line, `zxnext.vhd` 1706-1707, 3340-3350). The protocol below is written from
the documented esptool serial protocol (Espressif's "Serial Protocol" page of the esptool documentation) and from memory of
`esptool.py`: there is no local copy of esptool or of the ESP8266 ROM in the collection, so every number here must be checked against
esptool's source before code is written (see "Unknown" at the end).

## 1. What exists

- `EspModule::RestartIntoDownloadIfStrapped()` (`espmodule.cpp`): with NR #A8 bit 0 set and NR #A9 bit 0 clear (GPIO0 low) at a boot
  (`AT+RST`, or the module's reset), the module goes to download mode: silent, links dropped, `DownloadMode()` true, `Running()` false;
  it answers nothing until the next reset (header comment: "flashing is not emulated").
- NR #A8 / #A9 read back as the VHDL has them; the UART is byte-level (`ISerialPeer`), the module is told the nearest standard baud
  (`OnLineSettings`).

## 2. What the ROM bootloader protocol needs

Transport: SLIP over the UART, 8N1. The ESP8266 ROM autodetects the baud from the sync pattern (115200 is the usual start, esptool
then changes it with CHANGE_BAUDRATE). Before the sync the ROM prints a boot line at 74880 baud (`ets Jan  8 2013,rst cause:...,
boot mode:(...)` and `waiting for download`): at 115200 on the line it arrives as garbage that esptool ignores.

- Framing: frame = `0xC0` ... `0xC0`; inside, `0xC0` -> `0xDB 0xDC` and `0xDB` -> `0xDB 0xDD`.
- Request: `u8 direction = 0`, `u8 command`, `u16 size` (data length, little endian), `u32 checksum`, `size` data bytes. The checksum
  is the XOR of the data bytes seeded with `0xEF`, for the data commands (FLASH_DATA, MEM_DATA, ...); 0 for the others.
- Response: `u8 direction = 1`, `u8 command` (echoed), `u16 size`, `u32 value` (READ_REG's result), data = the command's data then
  the status bytes (`status` 0 ok / 1 failed, `error` code): two bytes on the ESP8266 ROM (four on the later chips: check).
- Commands (ROM): `0x02` FLASH_BEGIN (erase size, block count, block size, offset), `0x03` FLASH_DATA (length, sequence, 8 reserved,
  data), `0x04` FLASH_END (reboot flag), `0x05` MEM_BEGIN, `0x06` MEM_END (execute flag, entry), `0x07` MEM_DATA, `0x08` SYNC (the
  36-byte `07 07 12 20` + 32 x `55`; the ROM answers it several times), `0x09` WRITE_REG, `0x0A` READ_REG, `0x0B` SPI_SET_PARAMS,
  `0x0D` SPI_ATTACH, `0x0F` CHANGE_BAUDRATE. The compressed write (`0x10`-`0x12`), erase (`0xD0`, `0xD1`), read (`0xD2`) and
  `0x13` SPI_FLASH_MD5 are the stub's, not the ROM's.
- esptool's session: reset into the bootloader (DTR / RTS on a USB adapter: on the Next the program drives NR #A8 / #A9 and resets
  the module itself), SYNC until a response, READ_REG of the chip-detect register and of the eFuse words (chip type, MAC), then by
  default uploads its stub with MEM_BEGIN / MEM_DATA / MEM_END and waits for the stub's `OHAI` frame; after that the stub's protocol
  (the ROM's commands plus the compressed and erase / read ones) is what the writes use. `--no-stub` keeps to the ROM's commands.
- The Next side: the NextZXOS ESP update tools / a `.tap` flasher drive the UART with this protocol; they also toggle GPIO0 through
  NR #A8 / #A9 and reset the module with `AT+RST` or the board's reset line (the board has no RTS / DTR: GPIO0 and `CH_PD` /
  reset are what a program has).

## 3. Minimal plan

1. **A ROM loader state machine in the module**, entered where `_downloadMode` is set today: a SLIP decoder over the received bytes,
   one command handled per frame, the response queued for the UART (timed by the peer clock: a flash erase and write take
   milliseconds, which a program that polls sees as a slow response). Commands: SYNC (answer as the ROM does, repeated), READ_REG
   (the chip-detect magic register and the eFuse MAC words; anything else 0), SPI_ATTACH / SPI_SET_PARAMS (acknowledged),
   FLASH_BEGIN / FLASH_DATA / FLASH_END (checksum checked, sequence checked, bad frames get status 1 and the ROM's error codes),
   MEM_BEGIN / MEM_DATA / MEM_END (accepted; with `--no-stub` there is nothing else), CHANGE_BAUDRATE (acknowledged, then the
   module's own rate changes: the UART's prescaler is the program's, so the program changes it too; a peer that stays at 115200
   while the program moved is a wrong-baud case the loader should simply not answer).
2. **Stub emulation** only if the stub is wanted: after MEM_END with the stub's entry address answer with `OHAI`, then serve the
   compressed write (zlib inflate on the data; the project has one for the TTD codec), ERASE_FLASH / ERASE_REGION and
   FLASH_MD5. The stub's code is never run: its behaviours are the emulation.
3. **A virtual flash**, 4 MB for an ESP-12 (the size the Next's module ships with: check the board's schematic), sparse in memory,
   persisted to a file next to the machine's SD image so a flash survives a restart of the emulator. A write is the source of a TTD
   concern: the array is device memory; follow the ZX-Evo flash precedent (`PeripheralId::EvoFlash`: the command state is a blob,
   the array an engine region). Flash writes during a recording are a replay barrier (a host action) otherwise.
4. **What a flashed image does.** The emulator cannot run an arbitrary ESP image. After a FLASH_END with reboot, the module boots
   what the flash holds: recognise the image (a header byte `0xE9`, a SDK / AT version string at a known offset, or a hash of the
   known firmwares) and select the matching emulated firmware (NonOS AT 1.7.4, AT 2.2.1 / 2.2.2); an image nothing recognises boots
   to silence (a "bricked" module), as the real one would to whatever it contains.
5. **Tests**: hand-built SLIP frames (sync, read register, begin / data / end with a bad checksum), a flash of two blocks read back from the
   virtual flash, the boot of a known image, the GPIO0 / reset sequence from NextREG writes. With esptool available, a run of
   `esptool.py --no-stub --port socket://host:port chip_id / flash_id / write_flash` against a `LISTEN`-style TCP peer would be
   the acceptance check (the StreamPeer connects out; esptool's `socket://` URL connects out too: one of them needs a listening mode).

## 4. Unknown (to settle before coding)

- The exact ROM responses of the ESP8266 (status byte count, the error codes, the number of sync responses, the chip-detect magic
  and eFuse register addresses): from esptool's `esp8266_rom.py` / `loader.py`, not verified here.
- Whether the Next's firmware tools use the ROM's raw commands or esptool's stub. The wiki page `ESP8266-01.html` in the Next
  documentation mirror describes the board's use of the module; the tools that flash it were not read.
- The baud change: how a program on the Next follows CHANGE_BAUDRATE with the UART's prescaler and what a mismatch looks like
  to the module.
- The reset path: the modelled way into the bootloader is `AT+RST` with GPIO0 low. The core also has `o_RESET_PERIPHERAL`
  (`zxnext.vhd` 60, 1579, 5119: NR #02 bit 7, "asserted under sw control for esp / exp bus reset"), the line a program pulses to
  reset the module from outside the AT firmware. The emulator keeps NR #02 bit 7 as a register only: `EspModule::SetResetPin` is
  not wired on the Next (only the Sprinter's card uses it). Which module pin the board connects to it (`CH_PD` / `RST`) is the
  top level's, not in the core's RTL: to be taken from the board's schematic before wiring it. A flasher that holds GPIO0 low
  and pulses this bit needs that wire; it does not need `AT+RST`.
