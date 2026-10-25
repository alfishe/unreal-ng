// KempstonCard (core/src/emulator/slots/cards/kempston/kempstoncard.h): the Kempston joystick interface as a slot card on
// the machines whose board has no Kempston decode (48K, 128K, +2, +2A, +3, ATM Turbo 2 / 2+), its decode options, the
// TR-DOS gating behind a Beta 128, the catalog outcome per model and the time-travel registration.

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/ttd/ttdmachineperipherals.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"
#include "emulator/slots/cards/kempston/kempstoncard.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"

namespace
{

class KempstonCard_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(const std::string& model, const std::vector<std::pair<std::string, std::string>>& slots)
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        ASSERT_TRUE(SlotControl::CreateOverride(slots, slotSet, error)) << error;
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    KempstonCard* Card(const std::string& slot = "edge.1") const
    {
        return dynamic_cast<KempstonCard*>(_context->pSlotManager->FindCard(slot));
    }

    uint8_t In(uint16_t port, bool& drove)
    {
        return _context->pPortDecoder->ReadCycle(port, 0x8000, drove);
    }

    /// The guest's own read: IN A,(#1F) with A = 0 (port #001F), stored at #9000; returns the byte the program read
    uint8_t GuestReadsPort1F()
    {
        const uint8_t program[] = {0xAF,              // XOR A
                                   0xDB, 0x1F,        // IN A,(#1F)
                                   0x32, 0x00, 0x90,  // LD (#9000),A
                                   0x18, 0xFE};       // JR $
        uint16_t address = 0x8000;
        for (uint8_t b : program)
            _context->pMemory->DirectWriteToZ80Memory(address++, b);
        _context->pMemory->DirectWriteToZ80Memory(0x9000, 0xA5);
        Z80* cpu = _context->pCore->GetZ80();
        cpu->pc = 0x8000;
        cpu->halted = false;
        for (int i = 0; i < 3; i++)
            cpu->Z80Step(true);
        return _context->pMemory->DirectReadFromZ80Memory(0x9000);
    }
};

}  // namespace

/// A 48K with the interface (~5 ms): the guest's IN #1F reads the injected stick; the joystick's paths come with it
TEST_F(KempstonCard_Test, PluggedOn48KTheGuestReadsTheStick)
{
    Create("48K", {{"edge.1", "kempston"}});
    ASSERT_NE(Card(), nullptr) << "the edge slot builds the card";
    EXPECT_TRUE(_context->pPortDecoder->HasKempstonJoystick());
    EXPECT_FALSE(_context->pPortDecoder->BoardHasKempstonJoystick()) << "the 48K board itself has none";
    EXPECT_GE(_context->pControllerManager->Find("kempston"), 0) << "the machine's Kempston connector appears";

    _context->pJoystick->SetState(Joystick::kUp | Joystick::kFire);
    EXPECT_EQ(GuestReadsPort1F(), 0x18) << "up + fire, active high";

    bool drove = false;
    EXPECT_EQ(In(0xFF1F, drove), 0x18) << "A8-A15 are not decoded";
    EXPECT_TRUE(drove);
    EXPECT_EQ(In(0x0001, drove), 0x18) << "issue 4: every port with A7-A5 = 000";
    In(0x003F, drove);
    EXPECT_FALSE(drove) << "A5 = 1: not the interface";
    In(0x009F, drove);
    EXPECT_FALSE(drove) << "A7 = 1: not the interface (issue 4)";

    _context->pJoystick->SetState(0xFF);
    EXPECT_EQ(In(0x001F, drove), 0x1F) << "D5-D7 read 0 (sixth driver, diodes)";
}

/// decode = a5: the early boards answer every port with A5 = 0 (~5 ms)
TEST_F(KempstonCard_Test, EarlyBoardDecodesA5Only)
{
    Create("48K", {{"edge.1", "kempston"}, {"edge.1.decode", "a5"}});
    ASSERT_NE(Card(), nullptr);
    _context->pJoystick->SetState(Joystick::kLeft);
    bool drove = false;
    EXPECT_EQ(In(0x00DF, drove), Joystick::kLeft) << "#DF: A5 = 0";
    EXPECT_TRUE(drove);
    In(0x003F, drove);
    EXPECT_FALSE(drove);
}

/// Without the card the 48K keeps what it read before (~5 ms): #1F is not decoded, the Z80 serves the floating bus
/// (#FF outside the screen fetch), and the stick does not reach it
TEST_F(KempstonCard_Test, WithoutTheCardThe48KDoesNotDecode1F)
{
    Create("48K", {});
    EXPECT_FALSE(_context->pPortDecoder->HasKempstonJoystick());
    EXPECT_LT(_context->pControllerManager->Find("kempston"), 0);
    _context->pJoystick->SetState(Joystick::kFire);
    bool drove = true;
    In(0x001F, drove);
    EXPECT_FALSE(drove);
    EXPECT_FALSE(_context->pPortDecoder->WasLastPortDecoded()) << "the floating bus applies";
    EXPECT_EQ(GuestReadsPort1F(), 0xFF) << "the idle bus (the program runs at frame start, in the top border)";
}

/// A 128K with its Beta 128 (~10 ms): in TR-DOS #1F is the WD1793's register and the interface sees no cycle (the
/// Beta 128 passes /IORQ on only outside DOS, as the Pentagon's built-in); outside TR-DOS the stick answers
TEST_F(KempstonCard_Test, TrDosOwns1FOnABeta128)
{
    Create("128K", {{"edge.1", "kempston"}});
    ASSERT_NE(Card(), nullptr);
    ASSERT_TRUE(_context->config.trdos_present) << "the stock 128K has the Beta 128";
    _context->pJoystick->SetState(Joystick::kRight | Joystick::kDown);

    bool drove = false;
    EXPECT_EQ(In(0x001F, drove), 0x05);
    EXPECT_TRUE(drove);

    _context->emulatorState.flags |= CF_TRDOS;
    In(0x001F, drove);
    EXPECT_FALSE(drove) << "TR-DOS: the WD1793 status, not the stick";
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded()) << "the board's FDC drove the bus";
    _context->emulatorState.flags &= ~CF_TRDOS;

    EXPECT_EQ(In(0x001F, drove), 0x05);
    EXPECT_TRUE(drove);
}

/// The catalog (pure plan logic): a real fit on the Sinclair edge models, the override on the ATM Turbo 2 / 2+ (no
/// edge connector), refused where a built-in holds the function
TEST(KempstonCard_Catalog, FitsTheModelsWithoutABuiltIn)
{
    static const slots::SlotPlanner planner;
    slots::SlotRequest request;
    request.op = slots::SlotRequest::Op::Plug;
    request.card = KempstonCard::kCardId;
    request.dryRun = true;

    for (MEM_MODEL model : {MM_SPECTRUM48, MM_SPECTRUM128, MM_PLUS2, MM_PLUS2A, MM_PLUS3})
    {
        const slots::SlotPlan plan = planner.Plan(model, {}, request);
        EXPECT_TRUE(plan.allowed) << model;
        EXPECT_EQ(plan.fit, slots::Fit::Real) << model;
        EXPECT_EQ(plan.slot, "edge.1") << model;
    }
    for (MEM_MODEL model : {MM_ATM450, MM_ATM710})
    {
        const slots::SlotPlan plan = planner.Plan(model, {}, request);
        EXPECT_FALSE(plan.allowed) << model << ": needs the override";
        EXPECT_FALSE(plan.hardRefusal) << model;
        EXPECT_EQ(plan.fit, slots::Fit::Unrealistic) << model;
    }
    for (MEM_MODEL model : {MM_PENTAGON, MM_SCORP, MM_PROFSCORP, MM_ATM3, MM_TSL, MM_PROFI, MM_PROFI3, MM_NEXT})
    {
        const slots::SlotPlan plan = planner.Plan(model, {}, request);
        EXPECT_FALSE(plan.allowed) << model;
        EXPECT_TRUE(plan.hardRefusal) << model << ": the board's Kempston holds the function";
    }
}

/// Time travel (~5 ms): the card's presence blob under its slot, the machine's KempstonJoystick blob with it, and the
/// registry matches the slot set
TEST_F(KempstonCard_Test, TimeTravelRegistersTheCardAndTheStick)
{
    Create("48K", {{"edge.1", "kempston"}});
    KempstonCard* card = Card();
    ASSERT_NE(card, nullptr);
    std::vector<CardTtdDevice> devices;
    card->CollectTtdDevices(devices);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].instance, "edge.1.kempston");
    EXPECT_EQ(devices[0].device->TTDType(), ttd::TTDDeviceType::KempstonInterface);

    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    ASSERT_TRUE(ttd::RegisterMachinePeripherals(_context, registry, owned, &error)) << error;
    EXPECT_TRUE(registry.IsRegistered(ttd::TTDDeviceType::KempstonInterface));
    EXPECT_EQ(registry.FindType(ttd::TTDDeviceType::KempstonJoystick), _context->pJoystick)
        << "the stick byte is recorded as on a machine with a built-in decode";
}

/// Without the card the 48K's registry has neither (~5 ms): its sessions stay as they were
TEST_F(KempstonCard_Test, TimeTravelWithoutTheCardCarriesNoStick)
{
    Create("48K", {});
    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned;
    std::string error;
    ASSERT_TRUE(ttd::RegisterMachinePeripherals(_context, registry, owned, &error)) << error;
    EXPECT_FALSE(registry.IsRegistered(ttd::TTDDeviceType::KempstonInterface));
    EXPECT_FALSE(registry.IsRegistered(ttd::TTDDeviceType::KempstonJoystick));
}
