#include <benchmark/benchmark.h>

#include <random>
#include <string>
#include <vector>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/emulatorcontext.h"

/// The hot consumer of labels (symbols/test-and-benchmark-plan.md, BM_Symbols_DisasmLine): one disassembly line with
/// label resolution, the instruction's own address and its operand looked up, with N labels loaded (spread over the 64K,
/// one in four lines hits). BM_Symbols_LabelLookup is the address lookup alone over every address. Only the API every
/// version has (AddLabel, GetLabelByZ80Address, disassembleSingleCommand), so the same file measures master and a branch
/// (A/B).
namespace
{
struct LabelFixture
{
    EmulatorContext context{LoggerLevel::LogNone};
    DebugManager* debugManager = nullptr;

    explicit LabelFixture(int count)
    {
        debugManager = new DebugManager(&context);
        context.pDebugManager = debugManager;
        LabelManager* labels = debugManager->GetLabelManager();
        const int step = count > 0 ? std::max(1, 0x10000 / count) : 1;
        for (int i = 0; i < count; i++)
            labels->AddLabel("L" + std::to_string(i), static_cast<uint16_t>(i * step), UINT16_MAX, UINT16_MAX, "code");
    }
    ~LabelFixture()
    {
        context.pDebugManager = nullptr;
        delete debugManager;
    }
};

std::vector<std::vector<uint8_t>> Lines(int count)
{
    // CALL / JP / LD (nn),HL with targets on and off the labels
    std::vector<std::vector<uint8_t>> lines;
    std::mt19937 random(1);
    const int step = count > 0 ? std::max(1, 0x10000 / count) : 1;
    const uint8_t opcodes[] = {0xCD, 0xC3, 0x22, 0x2A};
    for (int i = 0; i < 256; i++)
    {
        uint16_t target = static_cast<uint16_t>(random());
        if (i % 4 == 0)
            target = static_cast<uint16_t>((target / step) * step);
        lines.push_back({opcodes[i % 4], static_cast<uint8_t>(target & 0xFF), static_cast<uint8_t>(target >> 8)});
    }
    return lines;
}

void BM_Symbols_DisasmLine(benchmark::State& state)
{
    const int count = static_cast<int>(state.range(0));
    LabelFixture fixture(count);
    Z80Disassembler& disasm = *fixture.debugManager->GetDisassembler();
    const auto lines = Lines(count);
    const int step = count > 0 ? std::max(1, 0x10000 / count) : 1;
    size_t i = 0;
    uint8_t length = 0;
    for (auto _ : state)
    {
        const size_t n = i++ & 0xFF;
        const uint16_t address = static_cast<uint16_t>(n * 3 * step);
        std::string text = disasm.disassembleSingleCommand(lines[n], address, &length);
        benchmark::DoNotOptimize(text);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Symbols_DisasmLine)->Arg(0)->Arg(1000)->Arg(10000)->Arg(60000);

void BM_Symbols_LabelLookup(benchmark::State& state)
{
    LabelFixture fixture(static_cast<int>(state.range(0)));
    LabelManager& labels = *fixture.debugManager->GetLabelManager();
    uint16_t address = 0;
    for (auto _ : state)
    {
        auto label = labels.GetLabelByZ80Address(address);
        benchmark::DoNotOptimize(label);
        address = static_cast<uint16_t>(address + 0x9E37);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Symbols_LabelLookup)->Arg(1000)->Arg(10000)->Arg(60000);
}  // namespace
