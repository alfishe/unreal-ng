# C9 — bulk `ReadSectors`: measured, dropped

**Status:** research done, 2026-10-06; **not landing** (the A/B gate of [tdd.md](../tdd.md) §4.3 / NFR-P7 is not
met in any way that matters). Phase C9 of tdd.md §14.

## 1. The idea

tdd.md §4.3: an optional `IBlockDevice::ReadSectors(lba, count, dst)` with a per-sector default; `RawImage`
answering it with one `read`, the composite splitting at run and extent boundaries, `SessionWriteMap` around its
changed sectors. Callers: ATA READ MULTIPLE / DMA, SD CMD18, ATAPI READ (10). It lands only if it gains on bulk reads
and loses nothing on single reads.

## 2. How sectors are read today

Every caller reads one sector into a 512-byte buffer at the moment the guest asks for it:

| Caller | Where | When |
|---|---|---|
| ATA disk | `AtaDisk::ReadIntoBuffer` | at the command, then after each 512 bytes the guest took from the data port |
| SD card (SPI) | `SdCardSpi::readBlock` | per block of CMD17 / CMD18 (CMD18 runs until CMD12: the count is not known up front) |
| ATAPI CD-ROM | `AtapiCdRom` | per 2048-byte frame of a READ |

So a bulk read needs more than the device method: each caller would have to read ahead into a larger buffer.

## 3. Measured

`core/benchmarks/emulator/io/bulkread_benchmark.cpp` (Release, Linux container, Xeon 2.1 GHz, two runs each; a
32 MiB file of random data, sequential):

| Path | Per sector |
|---|---|
| `RawImage::ReadSector` | 0.80-0.83 µs |
| a session over it | 0.79-0.88 µs |
| a composite built from a host folder (the same bytes as one file) | 0.85-0.87 µs |
| a session over that composite | 0.85-0.90 µs |
| the same file, 1 sector per `read` | 0.72-0.81 µs |
| 8 sectors per `read` | 0.14 µs |
| 128 sectors per `read` | 0.05-0.06 µs |
| 256 sectors per `read` | 0.06 µs |

The guest's own side: `BM_PortIn` (portin_benchmark.cpp) runs `IN A,(C)` at about 13 M a second, 75-78 ns an
instruction with its port decode. A sector moved by the guest is at least 512 port reads (`INIR`, or an `INI`
loop with more instructions around it): **38 µs or more of host time per sector**.

## 4. Decision

- **At the device, a bulk read is 14x cheaper** (0.8 µs → 0.06 µs a sector).
- **For the machine, it saves about 2 %** of the host time a guest spends per sector read (0.75 µs of at least
  38 µs), and only when the emulator runs unpaced (turbo, headless). At normal speed the machine waits for the
  frame clock anyway: nothing is gained.
- **The composite costs no more than an image file** (0.85 vs 0.80 µs): the run lookup is not where the time goes,
  so splitting reads at run boundaries would gain nothing either.
- **The cost is in the callers**: read-ahead buffers in ATA, SD and ATAPI; for SD CMD18 a guess at how far to read
  (it ends at CMD12); keeping the read-ahead coherent with writes that follow; the time-travel read tap would record
  sectors the guest never took.

C9 is dropped: no `ReadSectors` in `IBlockDevice`. The benchmark stays as the record; if a profile of a real guest
ever shows the device read as a real share (a much faster CPU core, or a DMA path that moves sectors without port
reads), it can be measured again with it.

Where the bytes of a sector really go for a guest is the port path (per-instruction cost of `IN` / `INI`), already
covered by the CPU and port work (portin_benchmark.cpp, performance-guidelines.md).
