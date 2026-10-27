// The Next layers window (unreal-qt/src/nextlayers/nextlayerswindow.h, design-layer-capture.md L4): the capture switch, the layers of the last
// completed frame with a toggle each, the winner map, the pixel inspector, and a machine that is not a Next.

#include <QApplication>
#include <QCheckBox>
#include <gtest/gtest.h>

#include <memory>

#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/video/next/screennext.h"
#include "nextlayers/nextlayerswindow.h"

namespace
{
class NextLayersWindow_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        _binding.unbind();
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }
    std::shared_ptr<Emulator> Create(const std::string& model)
    {
        std::string error;
        auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("", model, LoggerLevel::LogError, &error);
        EXPECT_NE(emulator, nullptr) << error;
        if (emulator)
            _binding.bind(emulator);
        return emulator;
    }
    EmulatorBinding _binding;
};
}  // namespace

TEST_F(NextLayersWindow_Test, TheSwitchTheLayersAndTheInspector)
{
    auto emulator = Create("NEXT");
    ASSERT_NE(emulator, nullptr);
    ScreenNext* screen = dynamic_cast<ScreenNext*>(emulator->GetContext()->pScreen);
    ASSERT_NE(screen, nullptr);
    emulator->GetContext()->pFeatureManager->setFeature(Features::kScreenHQ, true);
    NextLayersWindow window;
    window.setBinding(&_binding);
    screen->SetLayerCapture(false);
    window.refresh();
    EXPECT_TRUE(window.currentImage().isNull()) << "capture off: nothing to show";

    auto* capture = window.findChild<QCheckBox*>();
    ASSERT_NE(capture, nullptr);
    capture->setChecked(true);
    EXPECT_TRUE(screen->LayerCaptureEnabled()) << "the checkbox is the switch";
    for (int i = 0; i < 3 && !screen->LastLayerFrame(); i++)
        emulator->RunFrame(true);
    window.refresh();
    ASSERT_FALSE(window.currentImage().isNull());
    EXPECT_EQ(window.currentImage().width(), 640);
    EXPECT_EQ(window.currentImage().height(), 512);
    EXPECT_FALSE(window.inspect(QPoint(320, 256)).isEmpty()) << "the inspector answers for a pixel";
    EXPECT_TRUE(window.inspect(QPoint(700, 10)).isEmpty()) << "outside the picture: nothing";
}

TEST_F(NextLayersWindow_Test, AnotherMachineSaysSo)
{
    auto emulator = Create("PENTAGON");
    ASSERT_NE(emulator, nullptr);
    NextLayersWindow window;
    window.setBinding(&_binding);
    window.refresh();
    EXPECT_TRUE(window.currentImage().isNull());
}

/// The mode and the layer ticks the automation sets (next_layers_mode, next_layer.<name>), each change signalled once
TEST_F(NextLayersWindow_Test, TheModeAndTheTicksForTheAutomation)
{
    NextLayersWindow window;
    int changes = 0;
    QObject::connect(&window, &NextLayersWindow::viewChanged, [&changes] { ++changes; });
    EXPECT_EQ(window.mode(), 0);
    window.setMode(2);
    EXPECT_EQ(window.mode(), 2);
    window.setMode(9);
    EXPECT_EQ(window.mode(), 2) << "an unknown mode changes nothing";
    EXPECT_TRUE(window.layerShown(3));
    window.setLayerShown(3, false);
    EXPECT_FALSE(window.layerShown(3));
    EXPECT_TRUE(window.layerShown(0));
    EXPECT_FALSE(window.layerShown(7));
    EXPECT_EQ(changes, 2);
}
