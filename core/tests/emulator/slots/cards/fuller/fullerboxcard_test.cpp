// FullerBoxCard (core/src/emulator/slots/cards/fuller/fullerboxcard.h): the Fuller Box in the edge slot of a 48K, its
// AY on #3F / #5F and the stick on #7F through the controller manager's `fuller` connector (controller manager C1c).

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"
#include "emulator/slots/cards/fuller/fullerboxcard.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/soundchip_ay8910.h"

using namespace controllers;

namespace
{

class FullerBoxCard_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Create(const std::vector<std::pair<std::string, std::string>>& slots)
    {
        std::function<void(CONFIG&)> slotSet;
        std::string error;
        ASSERT_TRUE(SlotControl::CreateOverride(slots, slotSet, error)) << error;
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero, slotSet);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    FullerBoxCard* Card() const
    {
        return dynamic_cast<FullerBoxCard*>(_context->pSlotManager->FindCard("edge.1"));
    }

    uint8_t In(uint16_t port, bool& drove)
    {
        return _context->pPortDecoder->ReadCycle(port, 0x8000, drove);
    }
    void Out(uint16_t port, uint8_t value)
    {
        _context->pPortDecoder->WriteCycle(port, value, 0x8000);
    }
};

}  // namespace

TEST(FullerBoxCard_Stick, StickByteIsActiveLowFRLDU)
{
    EXPECT_EQ(FullerBoxCard::StickByte(0), 0xFF);
    EXPECT_EQ(FullerBoxCard::StickByte(kUp), 0xFE);
    EXPECT_EQ(FullerBoxCard::StickByte(kDown), 0xFD);
    EXPECT_EQ(FullerBoxCard::StickByte(kLeft), 0xFB);
    EXPECT_EQ(FullerBoxCard::StickByte(kRight), 0xF7);
    EXPECT_EQ(FullerBoxCard::StickByte(kFire), 0x7F);
    EXPECT_EQ(FullerBoxCard::StickByte(kFire2 | kFire3), 0xFF) << "one fire button: bits 6-4 stay high";
}

/// A 48K with the box (~5 ms): the slot builds the card, the card brings the `fuller` connector, its stick and AY
/// answer their ports and nothing else
TEST_F(FullerBoxCard_Test, StickAndAyAnswerTheirPorts)
{
    Create({{"edge.1", "fuller-box"}});
    FullerBoxCard* card = Card();
    ASSERT_NE(card, nullptr) << "the edge slot builds the card";

    ControllerManager& manager = *_context->pControllerManager;
    const int fuller = manager.Find("fuller");
    ASSERT_GE(fuller, 0) << "the card's socket is a connector";
    const Connector connector = manager.At(static_cast<size_t>(fuller));
    EXPECT_TRUE(connector.fixed);
    EXPECT_EQ(connector.device, RetroDeviceKind::FullerJoystick);
    EXPECT_EQ(manager.Find("fuller:0"), fuller);

    bool drove = false;
    EXPECT_EQ(In(0x007F, drove), 0xFF);
    EXPECT_TRUE(drove);
    ASSERT_TRUE(manager.Submit(static_cast<size_t>(fuller), kUp | kFire));
    EXPECT_EQ(manager.State(static_cast<size_t>(fuller)), kUp | kFire);
    EXPECT_EQ(In(0x007F, drove), 0x7E) << "F---RLDU active low";

    Out(0x003F, 8);     // channel A volume
    Out(0x005F, 0x0B);
    EXPECT_EQ(card->Chip().readRegister(8), 0x0B);
    EXPECT_EQ(In(0x003F, drove), 0x0B) << "#3F reads the selected register";
    EXPECT_TRUE(drove);

    In(0x00BF, drove);
    EXPECT_FALSE(drove) << "#BF is not the card's";
    In(0x007E, drove);
    EXPECT_FALSE(drove) << "full low-byte decode by default";
}

/// decode = a7a6a5: any port with A7-A5 = 011 is the stick (~5 ms)
TEST_F(FullerBoxCard_Test, PartialDecodeAnswersTheAliases)
{
    Create({{"edge.1", "fuller-box"}, {"edge.1.decode", "a7a6a5"}});
    ASSERT_NE(Card(), nullptr);
    bool drove = false;
    EXPECT_EQ(In(0x0061, drove), 0xFF);
    EXPECT_TRUE(drove) << "#61: A7-A5 = 011";
    Out(0x1021, 7);
    Out(0x2041, 0x3E);
    EXPECT_EQ(Card()->Chip().readRegister(7), 0x3E) << "#21 selects, #41 writes";
}

/// The card's blob and the Controllers blob carry what the box holds (~5 ms)
TEST_F(FullerBoxCard_Test, TimeTravelBlobsRoundTrip)
{
    Create({{"edge.1", "fuller-box"}});
    FullerBoxCard* card = Card();
    ASSERT_NE(card, nullptr);
    std::vector<CardTtdDevice> devices;
    card->CollectTtdDevices(devices);
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].instance, "edge.1.fuller-box");
    ttd::TTDSerializable& blob = *devices[0].device;
    EXPECT_EQ(blob.TTDType(), ttd::TTDDeviceType::FullerBox);

    Out(0x003F, 1);
    Out(0x005F, 0x05);
    std::vector<uint8_t> saved(blob.TTDStateSize());
    blob.TTDSaveState(saved.data());
    const uint64_t hash = blob.TTDHashState();
    Out(0x005F, 0x0A);
    EXPECT_NE(blob.TTDHashState(), hash);
    blob.TTDLoadState(saved.data());
    EXPECT_EQ(card->Chip().readRegister(1), 0x05);
    EXPECT_EQ(blob.TTDHashState(), hash);

    // The stick's controls are the manager's: three journaled connectors (fuller, keyjoy.1, keyjoy.2) and the eight
    // host key rows
    ControllerManager& manager = *_context->pControllerManager;
    const size_t fuller = static_cast<size_t>(manager.Find("fuller"));
    ASSERT_TRUE(manager.Submit(fuller, kLeft));
    EXPECT_EQ(manager.TTDStateSize(), 2u + 13u * 3u + 8u);
    std::vector<uint8_t> controls(manager.TTDStateSize());
    manager.TTDSaveState(controls.data());
    ASSERT_TRUE(manager.Submit(fuller, 0));
    manager.TTDLoadState(controls.data());
    EXPECT_EQ(manager.State(fuller), kLeft);
}
