// The TS-Conf histories (core/src/emulator/platforms/tsconf/tsconfhistory.h): the FPGA register writes
// (`ts-registers`), the DMA transfers (`ts-registers.dma`) and the VDAC2's FT812 SPI transactions (`vdac2`)

#include "emulator/machines/tsconf/tsconffixture.h"

#include <string>
#include <vector>

#include "emulator/platforms/tsconf/tsconfhistory.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/state/statenodejson.h"

#ifdef ENABLE_VDAC2
#include "emulator/machines/tsconf/vdac2cardfixture.h"
#endif

namespace
{

bool Ring(EmulatorContext* context, const std::string& device)
{
    return context->histories.With(device, [](history::IDeviceHistory& h) {
        std::string error;
        EXPECT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 64 }, error)) << error;
    });
}

std::string RecordsJson(EmulatorContext* context, const std::string& device, uint64_t* written = nullptr)
{
    std::string json;
    context->histories.With(device, [&](history::IDeviceHistory& h) {
        json = StateNodeToJsonText(h.Records(0));
        if (written)
            *written = h.GetStats().written;
    });
    return json;
}

class TsConfHistory_Test : public TsConfFixture
{
};

}  // namespace

/// The register writes, with the register's name; nothing while off
TEST_F(TsConfHistory_Test, RegisterWritesNamed)
{
    Reg(TsConfReg::Border, 0x0F);
    uint64_t written = 1;
    RecordsJson(_context, "ts-registers", &written);
    EXPECT_EQ(written, 0u) << "off: nothing";

    ASSERT_TRUE(Ring(_context, "ts-registers"));
    Reg(TsConfReg::Border, 0x0F);
    const std::string json = RecordsJson(_context, "ts-registers", &written);
    EXPECT_EQ(written, 1u);
    EXPECT_NE(json.find("\"reg_name\":\"BORDER\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"value\":15"), std::string::npos) << json;
}

/// A DMA launch: the device, the addresses and the sizes it took, completed once it ends
TEST_F(TsConfHistory_Test, DmaTransferLaunchAndEnd)
{
    ASSERT_TRUE(Ring(_context, "ts-registers.dma"));
    Reg(TsConfReg::DmaSAl, 0x00);
    Reg(TsConfReg::DmaSAl + 1, 0x00);
    Reg(TsConfReg::DmaSAl + 2, 0x01);
    Reg(TsConfReg::DmaDAl, 0x00);
    Reg(TsConfReg::DmaDAl + 1, 0x00);
    Reg(TsConfReg::DmaDAl + 2, 0x02);
    Reg(TsConfReg::DmaLen, 0x03);
    Reg(TsConfReg::DmaNum, 0x00);
    Reg(TsConfReg::DmaCtrl, 0x01);  // RAM to RAM
    uint64_t written = 0;
    std::string json = RecordsJson(_context, "ts-registers.dma", &written);
    ASSERT_EQ(written, 1u) << json;
    EXPECT_NE(json.find("\"device\":\"ram\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"burst\":4"), std::string::npos) << json;
    EXPECT_NE(json.find("\"blocks\":1"), std::string::npos) << json;

    _decoder->GetDma().Run(100000);  // the transfer to its end
    json = RecordsJson(_context, "ts-registers.dma");
    EXPECT_NE(json.find("\"done\":true"), std::string::npos) << "the transfer ended: " << json;
}

#ifdef ENABLE_VDAC2
class Vdac2History_Test : public Vdac2Test::Vdac2CardFixture
{
};

/// One record a chip select window: a memory write with its address, length and first bytes
TEST_F(Vdac2History_Test, TransactionsRecorded)
{
    Boot();
    ASSERT_TRUE(Ring(_context, "vdac2"));
    Write(Vdac2Test::kRamG + 0x1234, { 0x11, 0x22, 0x33 });
    uint64_t written = 0;
    const std::string json = RecordsJson(_context, "vdac2", &written);
    ASSERT_EQ(written, 1u) << json;
    EXPECT_NE(json.find("\"type\":\"write\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"address\":4660"), std::string::npos) << json;
    EXPECT_NE(json.find("\"length\":3"), std::string::npos) << json;
    EXPECT_NE(json.find("\"data\":\"112233\""), std::string::npos) << json;
}
#endif
