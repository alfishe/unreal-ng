// Memory wait states through a host bus overlay (PLAN #60(d),
// memorywaitoverlay.h): a machine marks the slots whose accesses wait and
// supplies the rule; every other slot, and every machine without the overlay,
// keeps its timing.

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memorywaitoverlay.h"

namespace
{
/// A fixed number of clocks per access; records what the rule was asked
struct FixedWaits : MemoryWaitOverlay
{
    explicit FixedWaits(Z80* cpu) : MemoryWaitOverlay(cpu) {}

    uint32_t clocks = 3;
    std::vector<MemoryWaitAccess> kinds;
    std::vector<uint16_t> addrs;
    std::vector<uint32_t> starts;

    uint32_t ExtraClocks(MemoryWaitAccess kind, uint16_t addr, uint32_t startClock) override
    {
        kinds.push_back(kind);
        addrs.push_back(addr);
        starts.push_back(startClock);
        return clocks;
    }
};

class MemoryWaitOverlay_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _core = _emulator->GetContext()->pCore;
        _z80 = _core->GetZ80();
    }

    void TearDown() override
    {
        if (_core)
            _core->ClearBusOverlays();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// T-states one instruction at #8000 takes
    uint32_t timeOne(std::initializer_list<uint8_t> code)
    {
        uint16_t a = 0x8000;
        for (uint8_t b : code)
            _z80->DirectWrite(a++, b);
        _z80->pc = 0x8000;
        _z80->t = 1000;
        _z80->Z80Step();
        return _z80->t - 1000;
    }
};
} // namespace

TEST_F(MemoryWaitOverlay_Test, OnlyMarkedSlotsWait)
{
    FixedWaits waits(_z80);
    waits.observesReads = true;
    ASSERT_TRUE(_core->AddBusOverlay(&waits));
    waits.SetSlotWaits(3, true); // #C000-#FFFF; the program runs in slot 2

    EXPECT_EQ(timeOne({0x3A, 0x00, 0x40}), 13u) << "LD A,(#4000): slot 1 does not wait";
    EXPECT_EQ(timeOne({0x3A, 0x00, 0xC0}), 13u + 3u) << "LD A,(#C000): one data read in slot 3";
    EXPECT_EQ(timeOne({0x32, 0x00, 0xC0}), 13u + 3u) << "LD (#C000),A: one data write in slot 3";
    ASSERT_EQ(waits.kinds.size(), 2u) << "the rule is asked only for marked slots";
    EXPECT_EQ(waits.kinds[0], MemoryWaitAccess::Read);
    EXPECT_EQ(waits.kinds[1], MemoryWaitAccess::Write);
    EXPECT_EQ(waits.addrs[1], 0xC000);
}

TEST_F(MemoryWaitOverlay_Test, CodeFetchesWaitAndTheRuleSeesTheAccessStart)
{
    FixedWaits waits(_z80);
    ASSERT_TRUE(_core->AddBusOverlay(&waits));
    waits.SetSlotWaits(2, true); // the program's own slot
    waits.clocks = 1;

    // LD A,(#4000) = M1 (4) + two operand reads (3 + 3) + the data read (3):
    // three code fetches in slot 2 wait, the data read in slot 1 does not
    EXPECT_EQ(timeOne({0x3A, 0x00, 0x40}), 13u + 3u);
    ASSERT_EQ(waits.kinds.size(), 3u);
    for (MemoryWaitAccess kind : waits.kinds)
        EXPECT_EQ(kind, MemoryWaitAccess::Code);
    // Each access starts after the previous one and its wait: M1 at 1000, the
    // first operand at 1000 + 4 + 1, the second at 1005 + 3 + 1
    EXPECT_EQ(waits.starts[0], 1000u);
    EXPECT_EQ(waits.starts[1], 1005u);
    EXPECT_EQ(waits.starts[2], 1009u);
}

TEST_F(MemoryWaitOverlay_Test, ZeroClocksAndNoOverlayKeepTheTiming)
{
    FixedWaits waits(_z80);
    waits.clocks = 0;
    ASSERT_TRUE(_core->AddBusOverlay(&waits));
    waits.SetSlotWaits(3, true);
    EXPECT_EQ(timeOne({0x3A, 0x00, 0xC0}), 13u);

    _core->RemoveBusOverlay(&waits);
    EXPECT_EQ(timeOne({0x3A, 0x00, 0xC0}), 13u);
    EXPECT_EQ(waits.kinds.size(), 1u) << "removed: the rule is not asked again";
}
