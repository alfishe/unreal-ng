/// @file ttdstatecompleteness_test.cpp
/// @brief FR-3 (TTD v2 requirements), first version: a seek restores every
/// piece of machine state a port write can change, on every creatable model.
///
/// For each model: record a few frames, restore checkpoint F and take the
/// reference state; go to a later checkpoint, write every port the model's
/// decoder maps (getPortMapEntries), seek back to F, and require the same
/// state: CPU, chipset (standard latches, in-frame T-state), every paging
/// latch the port map binds, the memory map of the four Z80 banks, RAM and
/// every registered device. A latch the checkpoint does not capture keeps the
/// written value across the seek and fails here, naming the port.
///
/// Limits of this first version: ports are written in the machine's state at
/// the later checkpoint, so a gated row (e.g. Beta-128 outside TR-DOS) may not
/// decode; devices attached or detached at runtime (FR-4) are not covered.
/// Over the 50 ms budget (~0.1 s per model): a machine is created and records
/// per model; the per-port diagnosis runs only when the state differs.

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

namespace
{
/// Everything a restore must bring back, captured from the live machine
struct MachineState
{
    ttd::TTDCpuState cpu;
    ttd::TTDChipsetState chipset;
    std::vector<uint32_t> latches;                  ///< one per latch-bound port map row, in row order
    bool pagingLocked = false;                      ///< #7FFD lock latch (decoder-private)
    std::vector<std::pair<int, size_t>> banks;      ///< per Z80 bank: mode, physical offset
    std::vector<uint8_t> ram;                       ///< pages [0, modelRamPages)
    std::unordered_map<uint8_t, std::vector<uint8_t>> devices;
};

/// The first difference, in words; empty when the states match
std::string Difference(const MachineState& actual, const MachineState& expected, Memory& memory)
{
    std::ostringstream out;
    if (std::memcmp(&actual.cpu, &expected.cpu, sizeof(actual.cpu)) != 0)
        out << "CPU state; ";
    if (std::memcmp(&actual.chipset, &expected.chipset, sizeof(actual.chipset)) != 0)
        out << "chipset (standard latches / in-frame T-state); ";
    for (size_t i = 0; i < expected.latches.size(); i++)
        if (actual.latches[i] != expected.latches[i])
            out << "paging latch row " << i << " (#" << std::hex << actual.latches[i] << " vs recorded #"
                << expected.latches[i] << std::dec << "); ";
    if (actual.pagingLocked != expected.pagingLocked)
        out << "#7FFD paging lock (now " << (actual.pagingLocked ? "locked" : "unlocked") << "); ";
    for (size_t bank = 0; bank < expected.banks.size(); bank++)
        if (actual.banks[bank] != expected.banks[bank])
            out << "memory map bank " << bank << " (now " << memory.GetCurrentBankName(uint8_t(bank))
                << "); ";
    if (actual.ram != expected.ram)
    {
        size_t first = 0;
        while (actual.ram[first] == expected.ram[first])
            first++;
        out << "RAM from page " << first / PAGE_SIZE << " offset #" << std::hex << first % PAGE_SIZE << std::dec
            << "; ";
    }
    for (const auto& [id, blob] : expected.devices)
    {
        const auto it = actual.devices.find(id);
        if (it == actual.devices.end() || it->second != blob)
            out << "device " << int(id) << "; ";
    }
    if (actual.devices.size() != expected.devices.size())
        out << "device set; ";
    return out.str();
}
}  // namespace

class TTD_StateCompleteness_Test : public ::testing::TestWithParam<const char*>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    std::vector<PortMapEntry> _ports;
    ttd::TTDTimePoint _reference{};  ///< checkpoint F
    ttd::TTDTimePoint _later{};      ///< the checkpoint the ports are written at

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
        if (_emulator == nullptr)
            GTEST_SKIP() << GetParam() << " is not provisionable in this build";
        Record();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    /// A fresh machine with the same recording: a restore that missed state
    /// leaves the machine changed, so each diagnosed port starts clean
    void Recreate()
    {
        TearDown();
        _emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        Record();
    }

    void Record()
    {
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _ports = _context->pPortDecoder->getPortMapEntries();
        ASSERT_FALSE(_ports.empty()) << GetParam() << ": the decoder publishes no port map";

        _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        const size_t count = _ttd->GetCheckpointCount();
        ASSERT_GE(count, 3u);
        _reference = {_ttd->GetCheckpoint(1)->time.frame, 0};
        _later = {_ttd->GetCheckpoint(count - 1)->time.frame, 0};
    }

    MachineState Capture() const
    {
        MachineState state;
        const Z80* z80 = _context->pCore->GetZ80();
        state.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
        state.chipset = ttd::CaptureChipsetState(_context->emulatorState, static_cast<uint32_t>(z80->t));
        for (const PortMapEntry& row : _ports)
            if (row.latch != PagingLatch::None)
                state.latches.push_back(PortDecoder::ReadPagingLatch(row.latch, _context->emulatorState));
        state.pagingLocked = _context->pPortDecoder->IsPagingLocked();

        Memory* memory = _context->pMemory;
        for (uint8_t bank = 0; bank < 4; bank++)
            state.banks.emplace_back(int(memory->GetMemoryBankMode(bank)), memory->GetPhysicalOffsetForZ80Bank(bank));
        for (uint16_t page = 0; page < _ttd->GetModelRamPages(); page++)
            if (const uint8_t* data = memory->RAMPageAddress(page))
                state.ram.insert(state.ram.end(), data, data + PAGE_SIZE);

        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        _ttd->GetPeripheralRegistry().CaptureAll(blobs);
        for (const auto& [id, blob] : blobs)
            state.devices[id] = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob);
        return state;
    }

    /// Seek to the later checkpoint, write `ports` there, seek back to F:
    /// what differs from the reference now is state the restore missed
    std::string WriteThenRestore(const std::vector<PortMapEntry>& ports, uint8_t value, const MachineState& reference)
    {
        if (!_ttd->SeekTo(_later))
            return "seek to the later checkpoint failed";
        for (const PortMapEntry& row : ports)
            _context->pPortDecoder->DecodePortOut(row.port, value, 0);
        if (!_ttd->SeekTo(_reference))
            return "seek back to the reference checkpoint failed";
        return Difference(Capture(), reference, *_context->pMemory);
    }
};

TEST_P(TTD_StateCompleteness_Test, SeekRestoresEveryPortWrittenState)
{
    ASSERT_TRUE(_ttd->SeekTo(_reference));
    const MachineState reference = Capture();

    // 0x5A leaves #7FFD bit 5 clear (no 48K lock) so the second pattern still
    // reaches the paging latches; 0x27 sets the bits 0x5A leaves clear
    for (const uint8_t value : {uint8_t(0x5A), uint8_t(0x27)})
    {
        const std::string missed = WriteThenRestore(_ports, value, reference);
        if (missed.empty())
            continue;

        // Something survived the seek: name each port that leaves state behind
        std::ostringstream culprits;
        const std::vector<PortMapEntry> ports = _ports;
        for (const PortMapEntry& row : ports)
        {
            Recreate();
            if (HasFatalFailure() || !_ttd->SeekTo(_reference))
                return;
            const std::string one = WriteThenRestore({row}, value, Capture());
            if (!one.empty())
                culprits << "\n  #" << std::hex << row.port << std::dec << " (" << row.device << ") = #" << std::hex
                         << int(value) << std::dec << ": " << one;
        }
        ADD_FAILURE() << GetParam() << ": a seek did not restore " << missed << culprits.str();
        return;
    }
}

INSTANTIATE_TEST_SUITE_P(CreatableModels, TTD_StateCompleteness_Test,
                         ::testing::Values("48K", "128k", "PENTAGON", "PLUS2", "PLUS2A", "PLUS3", "SCORPION",
                                           "PROFSCORP", "ATM710", "ATM3", "PROFI"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
