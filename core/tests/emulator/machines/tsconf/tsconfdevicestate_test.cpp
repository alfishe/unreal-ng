// DBG-1 (TSConf implementation-plan phase 7): the TS-Conf device state report
// every automation interface renders (WebAPI /state/tsconf, CLI state tsconf,
// Lua / Python tsconf_state, MCP aspect tsconf) reflects the machine.

#include "tsconffixture.h"

#include <cstring>

#include "emulator/state/devicestate.h"

class TsConfDeviceState_Test : public TsConfFixture
{
};

TEST_F(TsConfDeviceState_Test, DBG1_ReportFollowsTheRegisters)
{
    Reg(TsConfReg::VConfig, 0x42);  // 256C, 320x200
    Reg(TsConfReg::VPage, 0x20);
    Reg(TsConfReg::MemConfig, 0x81);  // auto LCK128, mapped, ROM128
    Reg(TsConfReg::Page3, 0x33);
    Reg(TsConfReg::SysConfig, 0x02);
    Reg(TsConfReg::TConfig, 0xA0);
    _decoder->GetState().sfile[0] = 0x2000;

    const StateNode node = DeviceState::TsConf(_context);
    const std::string json = DeviceState::ToText(node);
    EXPECT_NE(json.find("256C"), std::string::npos) << json;
    EXPECT_NE(json.find("320x200"), std::string::npos);
    EXPECT_NE(json.find("14 MHz"), std::string::npos);
    EXPECT_NE(json.find("mapped"), std::string::npos);
    EXPECT_NE(json.find("auto"), std::string::npos) << "LCK128";
    EXPECT_NE(json.find("51"), std::string::npos) << "page 3 = 0x33";
}

TEST(TsConfDeviceStateOther_Test, DBG1_UnavailableOnOtherMachines)
{
    EmulatorContext context(LoggerLevel::LogError);
    const std::string text = DeviceState::ToText(DeviceState::TsConf(&context));
    EXPECT_NE(text.find("Not a TS-Conf machine"), std::string::npos) << text;
}

/// DBG-4: the TSU objects and the palette for debug views (DeviceState::TsConfTsu:
/// WebAPI /state/tsconf/tsu, CLI state tsconf tsu, Lua / Python tsconf_tsu, MCP
/// aspect tsconf_tsu) - descriptors decoded, sprite layers by LEAP, tile layers, CRAM
TEST_F(TsConfDeviceState_Test, DBG4_TsuObjectsAndPalette)
{
    TsConfState& ts = _decoder->GetState();
    std::memset(ts.sfile, 0, sizeof(ts.sfile));
    ts.sfile[3 * 3] = static_cast<uint16_t>(0x8000 | 0x4000 | 0x2000 | (1 << 9) | 100);  // y flip, LEAP, active, 16 high, y 100
    ts.sfile[3 * 3 + 1] = static_cast<uint16_t>((3 << 9) | 300);                          // 32 wide, x 300
    ts.sfile[3 * 3 + 2] = static_cast<uint16_t>((9 << 12) | (2 << 6) | 5);                // palette 9, tile row 2 column 5
    ts.cram[0x10] = 0x7C00;
    Reg(TsConfReg::TConfig, 0xE4);   // sprites, T1, T0, draw tile 0 of layer 0
    Reg(TsConfReg::PalSel, 0x9F);    // T0 palette 1, T1 palette 2
    Reg(TsConfReg::T0GPage, 0x28);
    Reg(0x40, 0x34);                 // T0 X offset low
    Reg(0x41, 0x01);                 // bit 8

    const StateNode node = DeviceState::TsConfTsu(_context);
    const StateNode* sprites = node.find("sprites");
    ASSERT_NE(sprites, nullptr);
    ASSERT_EQ(sprites->items.size(), 85u);
    const StateNode& s3 = sprites->items[3];
    EXPECT_TRUE(s3.find("active")->b);
    EXPECT_TRUE(s3.find("leap")->b);
    EXPECT_TRUE(s3.find("y_flip")->b);
    EXPECT_EQ(s3.find("x")->i, 300);
    EXPECT_EQ(s3.find("y")->i, 100);
    EXPECT_EQ(s3.find("width")->i, 32);
    EXPECT_EQ(s3.find("height")->i, 16);
    EXPECT_EQ(s3.find("tile")->i, (2 << 6) | 5);
    EXPECT_EQ(s3.find("bitmap_x")->i, 40);
    EXPECT_EQ(s3.find("bitmap_y")->i, 16);
    EXPECT_EQ(s3.find("palette")->i, 9);
    EXPECT_EQ(s3.find("layer")->s, "s0") << "the LEAP descriptor ends S0";
    EXPECT_EQ(sprites->items[4].find("layer")->s, "s1");
    EXPECT_EQ(node.find("active_sprites")->i, 1);

    const StateNode* layers = node.find("tile_layers");
    ASSERT_EQ(layers->items.size(), 2u);
    EXPECT_TRUE(layers->items[0].find("draw_tile_zero")->b);
    EXPECT_FALSE(layers->items[1].find("draw_tile_zero")->b);
    EXPECT_EQ(layers->items[0].find("graphics_page")->i, 0x28);
    EXPECT_EQ(layers->items[0].find("x_offset")->i, 0x134);
    EXPECT_EQ(layers->items[0].find("palette")->i, 1);
    EXPECT_EQ(layers->items[1].find("palette")->i, 2);

    const StateNode* cram = node.find("cram");
    ASSERT_EQ(cram->items.size(), 256u);
    EXPECT_EQ(cram->items[0x10].find("value")->s, "7C00");
    EXPECT_EQ(cram->items[0x10].find("rgb")->s, "#FF0000") << "red, full level";

    EmulatorContext other(LoggerLevel::LogError);
    EXPECT_NE(DeviceState::ToText(DeviceState::TsConfTsu(&other)).find("Not a TS-Conf machine"), std::string::npos);
}
