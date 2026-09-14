#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/scorpionfixture.h"
#include "emulator/notifications.h"
#include "3rdparty/message-center/messagecenter.h"
#include "base/featuremanager.h"

#include <atomic>
#include <chrono>
#include <thread>

/// Integration coverage for ProfROM plane (quadrant) switching.
///
/// The plane is clocked by a GAL read strobe, not by any port write, and the
/// four ROM role pointers are re-resolved from it on every bank rebuild
/// (ScorpionMemory::ResolveScorpionRomBases: basePage = plane * 4, then +0
/// BASIC 128 / +1 48K / +2 service / +3 TR-DOS). These tests pin that the
/// strobe moves the ABSOLUTE ROM page and that the move reaches the HUD's
/// frame-end NC_ROM_PAGE_CHANGED notification - the unit tests around
/// ScorpionRomWindow only exercise the state machine in isolation.
class ProfRomPlane_Test : public ScorpionMachineFixture
{
protected:
    /// The fixture builds no FeatureManager; Memory only tracks page switches
    /// while Features::kHud is enabled
    FeatureManager* EnableHudTracking()
    {
        auto* featureManager = new FeatureManager(_context);
        _context->pFeatureManager = featureManager;
        featureManager->setFeature(Features::kHud, true);
        _memory->UpdateFeatureCache();
        return featureManager;
    }

    /// Page the service ROM at #0000, which is what arms the read strobe
    void ArmStrobe()
    {
        _context->emulatorState.p1FFD |= 0x02;
        _memory->UpdateZ80Banks();
    }
};

TEST_F(ProfRomPlane_Test, ReadStrobeMovesAbsoluteRomPage)
{
    ASSERT_TRUE(RebuildWithModel(MM_PROFSCORP, RAM_256));
    ASSERT_TRUE(LoadSyntheticRom(4));   // 256 KB: both GAL state bits live

    ArmStrobe();

    const uint8_t planeBefore = _context->emulatorState.profrom_bank;
    const uint16_t pageBefore = _memory->GetROMPage();

    _memory->MemoryReadFast(0x0104, false);   // S=1 row: Q0 -> Q3

    EXPECT_EQ(planeBefore, 0);
    EXPECT_EQ(_context->emulatorState.profrom_bank, 3) << "strobe must clock the GAL to plane 3";

    // Service ROM of plane 3 is absolute page 3 * 4 + 2
    EXPECT_EQ(pageBefore, 2u);
    EXPECT_EQ(_memory->GetROMPage(), 14u) << "ROM bases must follow the plane";
}

namespace
{
std::atomic<int> g_romNotifications{0};
std::atomic<int> g_lastPage{-1};
std::atomic<int> g_lastPlane{-1};
std::atomic<int> g_lastMinPlane{-1};
std::atomic<int> g_lastMaxPlane{-1};
std::atomic<bool> g_lastPlaneAware{false};

void onRomPageChanged(int, Message* message)
{
    if (!message)
        return;
    auto* payload = dynamic_cast<ROMPagePayload*>(message->obj);
    if (!payload)
        return;
    g_lastPage.store(payload->page);
    g_lastPlane.store(payload->plane);
    g_lastMinPlane.store(payload->minPlane);
    g_lastMaxPlane.store(payload->maxPlane);
    g_lastPlaneAware.store(payload->planeAware);
    g_romNotifications.fetch_add(1);
}
}  // namespace

TEST_F(ProfRomPlane_Test, PlaneSwitchReachesRomPageNotification)
{
    ASSERT_TRUE(RebuildWithModel(MM_PROFSCORP, RAM_256));
    ASSERT_TRUE(LoadSyntheticRom(4));

    FeatureManager* featureManager = EnableHudTracking();
    ArmStrobe();

    MessageCenter& mc = MessageCenter::DefaultMessageCenter(true);
    g_romNotifications.store(0);
    g_lastPage.store(-1);
    mc.AddObserver(NC_ROM_PAGE_CHANGED, &onRomPageChanged);

    _memory->handleFrameStart();
    _memory->MemoryReadFast(0x0104, false);   // Q0 -> Q3
    _memory->handleFrameEnd();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (g_romNotifications.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    mc.RemoveObserver(NC_ROM_PAGE_CHANGED, &onRomPageChanged);

    EXPECT_GT(g_romNotifications.load(), 0) << "plane switch produced no ROM page notification";
    EXPECT_EQ(g_lastPage.load(), 14) << "notification must carry the plane-composed absolute page";

    // The HUD cannot infer a plane from the page - only ProfROM has planes - so
    // the payload has to carry it, and the frame's range has to show the hop
    EXPECT_TRUE(g_lastPlaneAware.load()) << "ProfROM machine must report itself plane-aware";
    EXPECT_EQ(g_lastPlane.load(), 3) << "current plane";
    EXPECT_EQ(g_lastMinPlane.load(), 0) << "frame started in plane 0";
    EXPECT_EQ(g_lastMaxPlane.load(), 3) << "frame reached plane 3";

    delete featureManager;
    _context->pFeatureManager = nullptr;
}

TEST_F(ProfRomPlane_Test, StrobeIsInertOnInstructionFetchAndOffGridReads)
{
    ASSERT_TRUE(RebuildWithModel(MM_PROFSCORP, RAM_256));
    ASSERT_TRUE(LoadSyntheticRom(4));

    ArmStrobe();

    // An M1 fetch of the grid must not clock the GAL
    _memory->MemoryReadFast(0x0104, true);
    EXPECT_EQ(_context->emulatorState.profrom_bank, 0) << "instruction fetch must not switch planes";

    // #0101 is off-grid (A1:A0 != 0) - the monitor's plane-signature read
    _memory->MemoryReadFast(0x0101, false);
    EXPECT_EQ(_context->emulatorState.profrom_bank, 0) << "off-grid read must not switch planes";

    // The same address as a data read on the grid does switch
    _memory->MemoryReadFast(0x0104, false);
    EXPECT_EQ(_context->emulatorState.profrom_bank, 3);
}

TEST_F(ProfRomPlane_Test, StrobeIsInertWhileServiceRomIsNotPaged)
{
    ASSERT_TRUE(RebuildWithModel(MM_PROFSCORP, RAM_256));
    ASSERT_TRUE(LoadSyntheticRom(4));

    // Service ROM NOT paged: p1FFD bit 1 clear, BASIC chain at #0000
    _context->emulatorState.p1FFD &= ~0x02;
    _memory->UpdateZ80Banks();

    _memory->MemoryReadFast(0x0104, false);

    EXPECT_EQ(_context->emulatorState.profrom_bank, 0)
        << "strobe must be armed only while the service ROM window is paged";
}
