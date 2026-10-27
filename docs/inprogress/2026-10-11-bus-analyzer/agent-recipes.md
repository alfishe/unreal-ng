# Bus analyzer: recipes for agents

**Date:** 2026-10-11 · part of [README.md](README.md)

Recipes follow `.recipe/README.md`: MCP first, a WebAPI section for host-side pipelines, artifacts in
`scratch/`, the scope in the first paragraph, listed in the "Picking recipes" table and the library index.

## New recipes

| File | Phase | Covers |
|:--|:--|:--|
| `.recipe/analysis/bus-analyzer.md` | B3 | The general loop: `bus_analyzer list` → `enable` (with or without checks, `bus:"all"`) → run / step → `events` (filters: kinds, slot, since, frames, limit) → `findings` → `export` (VCD for PulseView, JSON lines). Reading the text form, sequence numbers for incremental reads, capacity and eviction, the TTD rule (rewound events), cost (off by default). The bus ids per machine. |
| `.recipe/analysis/sd-driver-debugging.md` | B3 / B4 | The SD driver writer's workflow on any host (TS-Conf / Evo `zc.spi`, Next `next.spi`, DivMMC `divmmc.spi`, NeoGS `neogs.sd`): enable with checks, insert an image, run the driver, read the protocol events (init sequence, R1 values, block numbers), read findings and what each rule means for real hardware, then harden: `sd_timing` `realistic` with seeds 1..5 and `slow`, re-run, find the command where the driver gives up. Per-host port notes (`#57` / `#77`, `#E7` / `#EB`, NeoGS `SD_SEND` / `SD_READ`, TS-Conf SPI DMA and its status bit). |

## Recipes that get a pointer

| File | Change |
|:--|:--|
| `.recipe/machines/tsconf.md` | SD section: the bus id `zc.spi`, the FT812 slot on the same bus (VDAC2), the DMA collision rule |
| `.recipe/machines/next.md` | SD section: `next.spi` beside the existing SD journal (the journal stays until B7) |
| `.recipe/analysis/port-trace.md` | "When to use the bus analyzer instead": protocol level vs port bytes |

## Later (with B6)

| File | Covers |
|:--|:--|
| `.recipe/analysis/uart-esp-at.md` | ZiFi / Next ESP: AT command and reply lines from the UART bus |
| `.recipe/analysis/i2c-rtc.md` | Next DS1307: I2C transactions, NACK / missing STOP |
