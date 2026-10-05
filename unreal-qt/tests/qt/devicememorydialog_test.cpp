// The debugger's Device memory dialog (DeviceMemoryDialog; debugger additions TODO A2q): the machine's device memory
// regions as hex, a typed byte written through the device's own path (DeviceMemory::Write), the region size fixed.

#include <QApplication>
#include <QComboBox>
#include <gtest/gtest.h>

#include <memory>

#include "QHexView/model/qhexdocument.h"
#include "QHexView/qhexview.h"
#include "debugger/devicememorydialog.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

class DeviceMemoryDialog_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    void Create(const char* model)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("devicememory-dialog-test", model,
                                                                            LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
    }
    std::shared_ptr<Emulator> _emulator;
};

// ~200 ms: creating a TS-Conf machine loads its firmware; nothing runs
TEST_F(DeviceMemoryDialog_Test, TypedBytesReachTheTsConfPalette)
{
    ASSERT_NO_FATAL_FAILURE(Create("TSL"));
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(_emulator->GetContext()->pPortDecoder);
    ASSERT_NE(decoder, nullptr);

    DeviceMemoryDialog dialog(_emulator.get());
    auto* regions = dialog.findChild<QComboBox*>();
    auto* view = dialog.findChild<QHexView*>();
    ASSERT_NE(regions, nullptr);
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(regions->count(), 2);
    EXPECT_EQ(regions->currentData().toString(), "cram");
    ASSERT_NE(view->getDocument(), nullptr);
    EXPECT_EQ(view->getDocument()->length(), 512);

    view->getDocument()->replace(2, static_cast<uchar>(0x1F));
    view->getDocument()->replace(3, static_cast<uchar>(0x00));
    EXPECT_EQ(decoder->GetState().cram[1], 0x001F) << "color 1: pure blue through CommitTableWord";

    // An insert is put back (the region keeps its size) and writes nothing
    const uint16_t color0 = decoder->GetState().cram[0];
    view->getDocument()->insert(0, static_cast<uchar>(0xAA));
    EXPECT_EQ(view->getDocument()->length(), 512);
    EXPECT_EQ(decoder->GetState().cram[0], color0);

    regions->setCurrentIndex(1);
    view->getDocument()->replace(0, static_cast<uchar>(0x34));
    EXPECT_EQ(decoder->GetState().sfile[0] & 0xFF, 0x34);
}

TEST_F(DeviceMemoryDialog_Test, MachineWithoutRegions)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    DeviceMemoryDialog dialog(_emulator.get());
    EXPECT_EQ(dialog.findChild<QComboBox*>()->count(), 0);
    EXPECT_FALSE(dialog.findChild<QHexView*>()->isEnabled());
}
