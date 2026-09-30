// DBG-1 (TSConf implementation-plan phase 7): the TS-Conf device state report
// every automation interface renders (WebAPI /state/tsconf, CLI state tsconf,
// Lua / Python tsconf_state, MCP aspect tsconf) reflects the machine.

#include "tsconffixture.h"

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
