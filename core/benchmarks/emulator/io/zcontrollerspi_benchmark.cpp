// Z-Controller SPI: the per-byte cost of the SD path (vdac2-integration-design.md §4).
// The bus became a hub for several devices; machines with one device must not
// pay for it. A/B: build this file against the old and the new ZControllerSpi.

#include <benchmark/benchmark.h>

#include <cstdint>

#include "emulator/io/spi/spidevice.h"
#include "emulator/io/spi/zcontrollerspi.h"

namespace
{
    /// Answers a counter: the cheapest device, so the controller's own cost shows
    class CountingDevice : public SpiDevice
    {
    public:
        uint8_t value = 0;
        bool selected = false;
        void select(bool on) override { selected = on; }
        uint8_t exchange(uint8_t mosi) override { return static_cast<uint8_t>(++value ^ mosi); }
    };
}  // namespace

/// A 512-byte sector read: the driver's IN loop (each read returns the previous
/// exchange and clocks #FF)
static void BM_ZControllerSpi_SdSectorRead(benchmark::State& state)
{
    CountingDevice card;
    ZControllerSpi zc;
    zc.SetDevice(&card);
    zc.Reset();
    zc.WriteConfig(0x00);  // SD selected
    uint32_t sum = 0;
    for (auto _ : state)
    {
        for (int i = 0; i < 512; i++)
            sum += zc.ReadData();
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * 512);
}
BENCHMARK(BM_ZControllerSpi_SdSectorRead);

/// Chip-select toggling, as drivers do around every command
static void BM_ZControllerSpi_ConfigToggle(benchmark::State& state)
{
    CountingDevice card;
    ZControllerSpi zc;
    zc.SetDevice(&card);
    zc.Reset();
    for (auto _ : state)
    {
        zc.WriteConfig(0x00);
        zc.WriteConfig(0x02);
        benchmark::DoNotOptimize(card.selected);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_ZControllerSpi_ConfigToggle);
