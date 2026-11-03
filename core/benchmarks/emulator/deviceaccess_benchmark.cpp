#include <benchmark/benchmark.h>

#include <cstdio>
#include <memory>
#include <string>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/deviceaccesswatch.h"
#include "emulator/ports/porthistory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

/// The cost of the device access watch on the port path (docs/inprogress/2026-11-02-device-access-watch, Cost): a
/// block of `IN A,(C)` or `OUT (C),A` on a Pentagon through Z80Step, interrupts off, the T-state counter rewound
/// every block, as BM_PortIn.
///
/// Arguments: access (0 = IN, 1 = OUT) x port (kPorts: #EFFE the ULA's half-row, #001F the Kempston joystick, #FFFD
/// the AY: IN reads the selected register, OUT selects one) x mode (0 = off; 1 = the watch on, as the feature
/// `deviceaccess` keeps it; 2 = the watch on and every port history in ring mode). The block has 1000 sites: past
/// the watch's 8 per channel the rest are counted as other sites, its worst case; a polling loop has one or two
namespace
{
const uint16_t kPorts[] = { 0xEFFE, 0x001F, 0xFFFD };
const char* const kModes[] = { "off", "watch", "watch+histories" };
constexpr int kBlock = 1000;
}  // namespace

static void BM_DeviceAccess(benchmark::State& state)
{
    const bool out = state.range(0) != 0;
    const uint16_t port = kPorts[state.range(1)];
    const int mode = static_cast<int>(state.range(2));

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("bench-dacc", "PENTAGON", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    Memory* memory = context->pMemory;
    context->pScreen->InitFrame();

    uint16_t a = 0x8000;
    for (int i = 0; i < kBlock; i++)
    {
        memory->DirectWriteToZ80Memory(a++, 0xED);
        memory->DirectWriteToZ80Memory(a++, out ? 0x79 : 0x78);
    }
    memory->DirectWriteToZ80Memory(a++, 0xC3);
    memory->DirectWriteToZ80Memory(a++, 0x00);
    memory->DirectWriteToZ80Memory(a, 0x80);

    PortDecoder* decoder = context->pPortDecoder;
    DeviceAccessWatch& watch = decoder->GetAccessWatch();
    int histories = 0;
    if (mode >= 1)
        watch.Start();
    if (mode >= 2)
    {
        for (const std::string& device : decoder->GetPortHistories().Devices())
        {
            context->histories.With(device + ".ports", [&histories](history::IDeviceHistory& h) {
                std::string error;
                if (h.Configure({ .mode = history::Mode::Ring }, error))
                    ++histories;
            });
        }
    }

    const uint32_t start = context->config.intstart + 1 + 14300;
    z80->iff1 = 0;
    z80->iff2 = 0;

    for (auto _ : state)
    {
        z80->pc = 0x8000;
        z80->bc = port;
        z80->a = 0x0E;
        z80->t = start;
        for (int i = 0; i < kBlock; i++)
            z80->Z80Step();
        if (mode >= 1)
            watch.EndFrame();  // one fold per block, as one per frame of a polling program
        uint32_t t = z80->t;
        benchmark::DoNotOptimize(t);
    }

    state.SetItemsProcessed(state.iterations() * kBlock);
    char label[96];
    std::snprintf(label, sizeof(label), "PENTAGON %s #%04X %s%s", out ? "OUT" : "IN", port, kModes[mode],
                  mode >= 2 ? (" (" + std::to_string(histories) + ")").c_str() : "");
    state.SetLabel(label);
    watch.Stop();
    manager->RemoveEmulator(emulator->GetUUID());
}
BENCHMARK(BM_DeviceAccess)->ArgsProduct({ { 0, 1 }, { 0, 1, 2 }, { 0, 1, 2 } })->Unit(benchmark::kMicrosecond);
