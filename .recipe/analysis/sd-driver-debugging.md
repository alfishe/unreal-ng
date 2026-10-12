# Recipe: Debugging and hardening an SD card driver

Goal: find why an SD driver (a TS-Conf / ZX-Evo shell, an esxDOS / NextZXOS driver, a DivMMC firmware, a NeoGS player)
fails, and make sure it survives real cards - which answer far slower than the emulator's ideal card. Uses the bus
analyzer ([bus-analyzer.md](bus-analyzer.md)) and the SD card's timing profiles. Artifacts in `scratch/`.

## 1. Which bus, which ports

| Machine | Bus | Driver's ports |
|:--|:--|:--|
| TS-Conf, ZX-Evo (BaseConf) | `zc.spi` | `#77` chip selects (D1 = SD /CS, D2 = FT812 on VDAC2), `#57` data: an `IN` returns the previous exchange's byte and clocks `#FF`; TS-Conf SPI DMA (DMA_CTRL device 2 / #A) on the same master |
| ZX Next | `next.spi` | `#E7` select (`#FE` card 0, `#FD` card 1), `#EB` data; a byte takes 16 CPU clocks at any speed |
| DivMMC card (48K, ...) | `divmmc.spi` | `#E7` select (bit 0 card 0, bit 1 card 1), `#EB` data |
| NeoGS (GS CPU) | `neogs.sd` | `SD_SEND` / `SD_READ`, SCTRL bit 0 = SD /CS; the card's DMA reads blocks itself |

## 2. Trace a run and read what the driver did

```text
bus_analyzer {"action":"enable","bus":"zc.spi"}
media {"action":"insert","slot":"sd.zc","path":"<image or folder>"}       # if not already there
... run until the driver has done its work (control_execution run_frames) ...
bus_analyzer {"action":"events","bus":"zc.spi","kinds":"command","limit":60}
bus_analyzer {"action":"findings","bus":"zc.spi"}
```

A healthy init: `CMD0 r1=0x01`, `CMD8 r1=0x01` (SD v2), `CMD55` / `ACMD41` until `r1=0x00`, `CMD58` (OCR: the CCS bit
says SDHC - a driver that skips it addresses an SDHC card in bytes), then `CMD17` / `CMD18` / `CMD24` / `CMD25`.
`r1=0x05` / `0x04` = illegal command, `0x09` = CRC error (CRC on after CMD59), `r1=none` = the card ignored the
command (before CMD0, or while streaming CMD18 - only CMD12 / CMD0 get through then). `read block=N` /
`write block=N stored|refused` say which sectors moved; compare N with the argument (SDSC: byte address / 512).

## 3. Read the findings

Each finding names the PC: set a breakpoint there (`control_execution bp_add`) or look at it in the TTD.

- `command_while_busy`: after CMD24 / CMD25 the driver must poll until the card returns `#FF` (not `#00`) before the
  next command. Fix: a busy wait after the data response, and before every command (FatFs `wait_ready`).
- `command_drops_answer`: the driver did not read the whole block (512 bytes + 2 CRC bytes) or the CSD / CID
  (16 + 2) before the next command.
- `deselect_mid_command`: something dropped /CS inside the 6 command bytes - usually an interrupt handler that
  uses the same bus (a music player streaming from SD, the VDAC2 FT812). Guard the bus in the handler.
- `dma_collision` (TS-Conf): the CPU touched `#57` / `#77` before the SPI DMA finished. Poll DMA_STATUS (port
  `#27AF` bit 7) before reading the CRC bytes or deselecting.
- `early_access` (Next / NeoGS): the driver is faster than the SPI master (Next at 28 MHz with `INIR` reads
  without a gap; a NeoGS loop that restarts the SD master).

## 4. Harden against real cards

```text
bus_analyzer {"action":"sd_timing","bus":"zc.spi","profile":"realistic","seed":1}
... repeat the run for seeds 1..5 ...
bus_analyzer {"action":"sd_timing","bus":"zc.spi","profile":"slow"}
... run again ...
bus_analyzer {"action":"sd_timing","bus":"zc.spi","profile":"ideal"}      # back to the default
```

| Profile | NCR (bytes before R1) | to the data token | write busy | ready after the first ACMD41 |
|:--|:--|:--|:--|:--|
| ideal | 1 | 8 bytes | 64 bytes | 4 polls |
| realistic (seeded) | 1..8 | 100 us..2 ms | 250 us..20 ms | 50..500 ms |
| slow (specification worst case) | 8 | 100 ms | 250 ms | 1 s |

Delays run on the emulated clock: a driver that deselects and waits sees them end as on a real card. A driver that
gives up early stops at a visible point: the last `command` events show the retry loop (e.g. 255 ACMD41 polls, then
nothing), the bytes show the `#FF` polls before a token that came too late. Common causes: an 8-bit poll counter
for the data token, an ACMD41 loop without a delay, no timeout at all on write busy (hangs on a broken card).

The profile is part of the card's saved state (snapshots, TTD) and a machine change for automation (it opens a
run-control session). WebAPI: `POST /api/v1/emulator/{id}/bus/{bus}/sd-timing {"profile":"slow","seed":1,"slot":0}`;
CLI `bus zc.spi sd-timing slow`; Lua `bus_sd_timing("zc.spi", {profile="slow"})`; Python
`emu.bus_sd_timing("zc.spi", "slow")`.

## 5. Compare with a real board

`bus_analyzer {"action":"export","bus":"zc.spi","path":"<repo>/scratch/run.vcd"}` and open it next to a logic analyzer
capture from the board in PulseView (SPI decoder, then "SD card (SPI mode)"). The emulator's wires are synthesized at
the bus's byte time; the order and the bytes are exact, the gaps between bytes are the CPU's.
