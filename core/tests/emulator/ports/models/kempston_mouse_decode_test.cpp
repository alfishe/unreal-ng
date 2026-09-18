/// @file kempston_mouse_decode_test.cpp
/// @brief Kempston Mouse address decode on every supported model (Kempston Mouse design §3)
///        and [INPUT] config parsing (§7).
///
/// Standard decode (MiSTer mouse.v / kemp_sel): A5-A0 = 011111 and A9 = 1 qualify; A8 = 0 buttons,
/// A8 = 1 A10 = 0 X, A8 = 1 A10 = 1 Y. A7, A6, A15-A11 are mirrors. Higher-priority arms keep their
/// ports: AY, the Beta128 FDC while it drives the bus, the Scorpion joystick at #FF1F.

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "_helpers/testpathhelper.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/portdecoder_spectrum3.h"

namespace
{
constexpr uint8_t kTestX = 0x40;  // distinct from every idle bus / keyboard value
constexpr uint8_t kTestY = 0x6A;
}  // namespace

/// region <Standard predicate>

TEST(KempstonMouseDecode_Test, StandardPredicateSelectsRegisters)
{
    uint8_t reg = 0xAA;
    EXPECT_TRUE(PortDecoder::Standard_IsPort_KempstonMouse(0xFADF, reg));
    EXPECT_EQ(reg, 0);
    EXPECT_TRUE(PortDecoder::Standard_IsPort_KempstonMouse(0xFEDF, reg)) << "A10 don't-care for buttons";
    EXPECT_EQ(reg, 0);
    EXPECT_TRUE(PortDecoder::Standard_IsPort_KempstonMouse(0xFBDF, reg));
    EXPECT_EQ(reg, 1);
    EXPECT_TRUE(PortDecoder::Standard_IsPort_KempstonMouse(0xFFDF, reg));
    EXPECT_EQ(reg, 2);

    // Mirrors: A7, A6 and A15-A11 are not decoded
    for (uint16_t port : {0x031F, 0x035F, 0x039F, 0x7BDF})
    {
        EXPECT_TRUE(PortDecoder::Standard_IsPort_KempstonMouse(port, reg)) << std::hex << port;
        EXPECT_EQ(reg, 1) << std::hex << port;
    }

    EXPECT_FALSE(PortDecoder::Standard_IsPort_KempstonMouse(0xFDDF, reg)) << "A9=0";
    EXPECT_FALSE(PortDecoder::Standard_IsPort_KempstonMouse(0xFBFF, reg)) << "A5=1";
    // A4-A0 must all be 1 (MiSTer kemp_sel = addr[5:0] == #1F)
    for (uint16_t port : {0x0300, 0x03C1, 0xFBDE, 0xFBDD, 0xFBDB, 0xFBD7, 0xFBCF})
        EXPECT_FALSE(PortDecoder::Standard_IsPort_KempstonMouse(port, reg)) << std::hex << port;
}

/// endregion </Standard predicate>

/// region <Per model>

class KempstonMouseModelDecode_Test : public ::testing::TestWithParam<const char*>
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder* _decoder = nullptr;
    Mouse* _mouse = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", GetParam(), LoggerLevel::LogError);
        ASSERT_TRUE(_emulator) << GetParam();
        _context = _emulator->GetContext();
        _decoder = _context->pPortDecoder;
        _mouse = _context->pMouse;
        ASSERT_NE(_decoder, nullptr);
        ASSERT_NE(_mouse, nullptr);
        _context->emulatorState.flags = 0;
        _mouse->SetCounters(kTestX, kTestY);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
    }

    uint8_t In(uint16_t port) { return _decoder->DecodePortIn(port, 0x0000); }
};

TEST_P(KempstonMouseModelDecode_Test, CanonicalPortsAnswer)
{
    EXPECT_EQ(In(0xFBDF), kTestX);
    EXPECT_TRUE(_decoder->WasLastPortDecoded());
    EXPECT_EQ(In(0xFFDF), kTestY);
    EXPECT_EQ(In(0xFADF), 0xFF) << "classic mouse, no buttons: D7-D3 read 1";
    EXPECT_EQ(In(0xFEDF), 0xFF) << "#FEDF buttons alias";
}

TEST_P(KempstonMouseModelDecode_Test, MirroredAddressesAnswer)
{
    EXPECT_EQ(In(0x7BDF), kTestX) << "A15 not decoded";
    EXPECT_EQ(In(0x7FDF), kTestY);
    EXPECT_EQ(In(0x039F), kTestX) << "low byte #9F: A7/A6 not decoded";
    EXPECT_NE(In(0x03C1), kTestX) << "A4-A0 = 00001: not a mouse address";
}

TEST_P(KempstonMouseModelDecode_Test, KeyboardKeepsEvenAddresses)
{
    // Scorpion's keyboard decode also needs A5 = 1 (mask #23), so it never overlaps the
    // mouse pattern there; on the A0-only machines the ULA arm must win the overlap
    const std::string model = GetParam();
    if (model == "SCORPION" || model == "PROFSCORP")
        GTEST_SKIP() << "no keyboard/mouse address overlap on Scorpion";

    // #03DE: A0 = 0 (ULA keyboard); A0 is part of the mouse decode, so never the mouse
    EXPECT_NE(In(0x03DE), kTestX);
}

TEST_P(KempstonMouseModelDecode_Test, AbsentMouseIsNotDecoded)
{
    _mouse->SetPresent(false);
    In(0xFBDF);
    EXPECT_NE(In(0xFBDF), kTestX);
    _mouse->SetPresent(true);
    EXPECT_EQ(In(0xFBDF), kTestX);
}

TEST_P(KempstonMouseModelDecode_Test, TrDosPortsHideMouse)
{
    _context->emulatorState.flags |= CF_DOSPORTS;
    EXPECT_NE(In(0x039F), kTestX) << "design §3.5: gated while TR-DOS ports are on the bus";
    _context->emulatorState.flags &= ~CF_DOSPORTS;
    EXPECT_EQ(In(0x039F), kTestX);
}

INSTANTIATE_TEST_SUITE_P(AllModels, KempstonMouseModelDecode_Test,
                         // PLUS3 (no ROM) and PROFI (no shipped config) cannot boot in this tree:
                         // their decoders are covered on a bare context below
                         ::testing::Values("48K", "128k", "PENTAGON", "SCORPION", "PROFSCORP"),
                         [](const ::testing::TestParamInfo<const char*>& info) {
                             std::string name = info.param;
                             for (auto& c : name)
                                 if (!isalnum(static_cast<unsigned char>(c)))
                                     c = '_';
                             return name;
                         });

/// Decoders whose machines cannot boot in this tree: bare context + Mouse, no CPU
template <typename Decoder>
static void ExpectStandardMouseDecode(const char* model)
{
    EmulatorContext context(LoggerLevel::LogError);
    Mouse mouse(&context);
    context.pMouse = &mouse;
    Decoder decoder(&context);
    mouse.SetCounters(kTestX, kTestY);

    EXPECT_EQ(decoder.DecodePortIn(0xFBDF, 0x0000), kTestX) << model;
    EXPECT_EQ(decoder.DecodePortIn(0xFFDF, 0x0000), kTestY) << model;
    EXPECT_EQ(decoder.DecodePortIn(0x7BDF, 0x0000), kTestX) << model << " mirror";
    EXPECT_EQ(decoder.DecodePortIn(0xFADF, 0x0000), 0xFF) << model;

    mouse.SetPresent(false);
    EXPECT_NE(decoder.DecodePortIn(0xFBDF, 0x0000), kTestX) << model << " absent";
}

TEST(KempstonMouseDecodeBare_Test, Plus3)
{
    ExpectStandardMouseDecode<PortDecoder_Spectrum3>("PLUS3");
}

TEST(KempstonMouseDecodeBare_Test, Profi)
{
    ExpectStandardMouseDecode<PortDecoder_Profi>("PROFI");
}

TEST(KempstonMouseDecodePriority_Test, ScorpionJoystickKeepsFF1F)
{
    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "SCORPION", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    context->pMouse->SetCounters(kTestX, kTestY);

    // #FF1F has A9 = 1, A5 = 0 - the joystick stub (idle 0x00) answers, not the mouse
    EXPECT_EQ(context->pPortDecoder->DecodePortIn(0xFF1F, 0x0000), 0x00);

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

TEST(KempstonMouseDecodePriority_Test, ScorpionTrDosSelectionHandsMouseAddressesToBeta)
{
    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "SCORPION", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    PortDecoder* decoder = context->pPortDecoder;
    EmulatorState& state = context->emulatorState;
    context->pMouse->SetCounters(kTestX, kTestY);
    state.flags = 0;
    state.p1FFD = 0x00;
    state.scorpionDosTrigger = 0;

    // Not selected: the mouse answers its canonical port and the #xx9F mirror alike
    EXPECT_EQ(decoder->DecodePortIn(0xFBDF, 0x0000), kTestX);
    EXPECT_EQ(decoder->DecodePortIn(0x039F, 0x0000), kTestX);

    // TR-DOS selected (session or armed DOS trigger): the Beta interface decodes A2-A0 = 111
    // (MiSTer ScorpionZS256 fdd_sel) and outranks the mouse on every mouse address. A7 = 1 is the
    // system register, so #FBDF / #FFDF / #FADF read what #FF reads
    for (int selection = 0; selection < 2; selection++)
    {
        if (selection == 0)
            state.flags |= CF_TRDOS;
        else
            state.scorpionDosTrigger = 1;

        const uint8_t systemRegister = decoder->DecodePortIn(0x00FF, 0x0000);
        for (uint16_t port : {0xFBDF, 0xFFDF, 0xFADF, 0x039F})
        {
            EXPECT_EQ(decoder->DecodePortIn(port, 0x0000), systemRegister) << std::hex << port << " selection " << selection;
            EXPECT_TRUE(decoder->WasLastPortDecoded()) << std::hex << port;
        }
        // A7 = 0: WD1793 register by A6-A5 - #xx47 and #035F are the sector register
        EXPECT_EQ(decoder->DecodePortIn(0x0347, 0x0000), decoder->DecodePortIn(0x005F, 0x0000));
        EXPECT_EQ(decoder->DecodePortIn(0x035F, 0x0000), decoder->DecodePortIn(0x005F, 0x0000));

        state.flags = 0;
        state.scorpionDosTrigger = 0;
    }

    EXPECT_EQ(decoder->DecodePortIn(0xFBDF, 0x0000), kTestX) << "selection released: mouse back";

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

TEST(KempstonMouseDecodePriority_Test, ScorpionMonitorLatchKeepsMouse)
{
    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "SCORPION", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    PortDecoder* decoder = context->pPortDecoder;
    EmulatorState& state = context->emulatorState;
    context->pMouse->SetCounters(kTestX, kTestY);
    state.flags = 0;
    state.scorpionDosTrigger = 0;

    // Shadow Monitor latch without TR-DOS: MiSTer masks only the Beta low bytes, #xxDF stays mouse
    state.p1FFD = 0x02;
    EXPECT_EQ(decoder->DecodePortIn(0xFBDF, 0x0000), kTestX);
    EXPECT_EQ(decoder->DecodePortIn(0xFFDF, 0x0000), kTestY);
    EXPECT_EQ(decoder->DecodePortIn(0xFADF, 0x0000), 0xFF);

    // ... but the exact Beta low bytes belong to the FDC, which the latch keeps on the bus
    const uint8_t status = decoder->DecodePortIn(0x001F, 0x0000);
    EXPECT_EQ(decoder->DecodePortIn(0x035F, 0x0000), decoder->DecodePortIn(0x005F, 0x0000)) << "#5F: FDC, not mouse";
    EXPECT_EQ(decoder->DecodePortIn(0xFF1F, 0x0000), status) << "#FF1F: joystick gives way to the FDC status";

    state.p1FFD = 0x00;
    EXPECT_EQ(decoder->DecodePortIn(0xFF1F, 0x0000), 0x00) << "latch clear: joystick stub answers again";

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

TEST(KempstonMouseDecodePriority_Test, PentagonTrDosFdcKeepsDF)
{
    auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "PENTAGON", LoggerLevel::LogError);
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    context->pMouse->SetCounters(kTestX, kTestY);
    context->emulatorState.flags = 0;
    EXPECT_EQ(context->pPortDecoder->DecodePortIn(0xFBDF, 0x0000), kTestX);

    // TR-DOS session: #xxDF is the Beta128 #FF rule's address and the FDC keeps it
    context->emulatorState.flags |= CF_TRDOS | CF_DOSPORTS;
    EXPECT_NE(context->pPortDecoder->DecodePortIn(0xFBDF, 0x0000), kTestX);

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

/// endregion </Per model>

/// region <Config>

class KempstonMouseConfig_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pMouse, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    bool LoadInputKeys(const std::string& keys)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath("kempston_mouse_config_test.ini");
        {
            std::ofstream file(path, std::ios::binary);
            file << "[INPUT]\n" << keys;
        }
        Config config(_context);
        const bool ok = config.LoadConfigFile(path);
        _context->pMouse->ApplyConfiguration();
        return ok;
    }
};

TEST_F(KempstonMouseConfig_Test, MouseKempstonFitsDevice)
{
    ASSERT_TRUE(LoadInputKeys("Mouse=KEMPSTON\n"));
    EXPECT_EQ(_context->config.input.mouse, MOUSE_TYPE_KEMPSTON);
    EXPECT_TRUE(_context->pMouse->IsPresent());
}

TEST_F(KempstonMouseConfig_Test, MouseNoneRemovesDevice)
{
    ASSERT_TRUE(LoadInputKeys("Mouse=NONE\n"));
    EXPECT_EQ(_context->config.input.mouse, MOUSE_TYPE_NONE);
    EXPECT_FALSE(_context->pMouse->IsPresent());
}

TEST_F(KempstonMouseConfig_Test, MouseAyIsNotEmulated)
{
    ASSERT_TRUE(LoadInputKeys("Mouse=AY\n"));
    EXPECT_FALSE(_context->pMouse->IsPresent());
}

TEST_F(KempstonMouseConfig_Test, WheelModes)
{
    ASSERT_TRUE(LoadInputKeys("Mouse=KEMPSTON\nWheel=KEMPSTON\n"));
    EXPECT_TRUE(_context->pMouse->IsWheelEnabled());

    ASSERT_TRUE(LoadInputKeys("Mouse=KEMPSTON\nWheel=NONE\n"));
    EXPECT_FALSE(_context->pMouse->IsWheelEnabled());

    ASSERT_TRUE(LoadInputKeys("Mouse=KEMPSTON\nWheel=KEYBOARD\n"));
    EXPECT_FALSE(_context->pMouse->IsWheelEnabled()) << "KEYBOARD mode is not implemented: no wheel nibble";
}

TEST_F(KempstonMouseConfig_Test, SwapAndScale)
{
    ASSERT_TRUE(LoadInputKeys("SwapMouse=1\nMouseScale=-2\n"));
    EXPECT_EQ(_context->config.input.mouseswap, 1);
    EXPECT_EQ(static_cast<signed char>(_context->config.input.mousescale), -2) << "char is unsigned on ARM - cast";

    ASSERT_TRUE(LoadInputKeys("MouseScale=9\n"));
    EXPECT_EQ(static_cast<signed char>(_context->config.input.mousescale), 0) << "out of range falls back to 0";
}

TEST_F(KempstonMouseConfig_Test, ShippedConfigsFitClassicMouseWithoutWheel)
{
    // Wheel=NONE keeps D7-D3 = 1 so ProfROM-style detection (#FADF AND #38 = #38) finds the mouse
    for (const char* model : {"pentagon128k", "scorpion", "profscorp", "spectrum128", "spectrum48", "spectrum3"})
    {
        const std::string path = (TestPathHelper::FindProjectRoot() / "data" / "configs" / model / "unreal.ini").string();
        Config config(_context);
        ASSERT_TRUE(config.LoadConfigFile(path)) << model;
        _context->pMouse->ApplyConfiguration();
        EXPECT_TRUE(_context->pMouse->IsPresent()) << model;
        EXPECT_FALSE(_context->pMouse->IsWheelEnabled()) << model;
    }
}

/// endregion </Config>
