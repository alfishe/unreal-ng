// The status bar's video mode label (StatusBarVideoMode, used by StatusBarManager::updateVideoMode) on a
// Sprinter: the label shows the picture's mode from the machine's own screen description. The owner's report of
// 2026-10-02: in the Spectrum mode it said "text 40 (mixed)". The mode tables are the ones the Sprinter's software
// writes (core/tests/.../sprintermodetable.h: the launcher's Spectrum screen, Flex Navigator's graphics 640)

#include <gtest/gtest.h>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/machines/sprinter/sprintermodetable.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/video/screen.h"
#include "widgets/statusbarvideomode.h"

class StatusBarVideoMode_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        _emulator = _manager->CreateEmulatorWithModelAndRAM("statusbar-video", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetPldState().rgMod = 0x00;
    }

    void TearDown() override
    {
        _emulator.reset();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }

    StatusBarVideoMode Label() const { return StatusBarVideoMode::From(_context->pScreen->DescribeScreenState()); }

    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;
};

TEST_F(StatusBarVideoMode_Test, SpectrumModeShowsTheZxScreen)
{
    _decoder->GetPldState().allMode = 0xFE;  // the launcher's Spectrum mode
    _decoder->GetPldState().pn = 0x10;       // #7FFD: screen 5
    SprinterModeTable::WriteSpectrumScreen(_decoder->GetVideoRam(), 0);
    const StatusBarVideoMode label = Label();
    EXPECT_TRUE(label.visible);
    EXPECT_EQ(label.text.toStdString(), "Spectrum 256x192, screen 5");
    EXPECT_TRUE(label.toolTip.contains(QStringLiteral("spectrum 768, border 512"))) << label.toolTip.toStdString();
}

TEST_F(StatusBarVideoMode_Test, NativeModesKeepTheirNames)
{
    SprinterModeTable::Fill(_decoder->GetVideoRam(), 0, 0x00);  // Flex Navigator: graphics 640
    EXPECT_EQ(Label().text.toStdString(), "640x256 16c");
    SprinterModeTable::Fill(_decoder->GetVideoRam(), 0, 0x10, 'A', 0x07);  // DSS: text 80
    EXPECT_EQ(Label().text.toStdString(), "text 80");
}

TEST(StatusBarVideoModeNoBrief_Test, MachinesWithoutABriefHideTheLabel)
{
    ScreenState state;
    state.videoMode = "ZX 256x192";
    EXPECT_FALSE(StatusBarVideoMode::From(state).visible);
}
