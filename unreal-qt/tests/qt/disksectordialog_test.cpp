// The debugger's Disk sector dialog (DiskSectorDialog; debugger additions tdd §3): a sector's data field as hex, a
// typed byte written through SectorWrite (CRC recalculated, image modified), refusals shown, the size fixed.

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "QHexView/model/qhexdocument.h"
#include "QHexView/qhexview.h"
#include "debugger/disksectordialog.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"

class DiskSectorDialog_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("disksector-dialog-test", "PENTAGON",
                                                                            LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        std::string error;
        ASSERT_TRUE(_emulator->CreateBlankDisk(0, Emulator::BlankDiskFormat::Plus3, 40, 1, &error)) << error;
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    DiskImage::Sector* Sector(int id)
    {
        DiskImage* image = _emulator->GetContext()->coreState.diskDrives[0]->getDiskImage();
        return image->getTrackForCylinderAndSide(0, 0)->findSector(static_cast<uint8_t>(id));
    }
    std::shared_ptr<Emulator> _emulator;
};

TEST_F(DiskSectorDialog_Test, TypedBytesReachTheSector)
{
    DiskSectorDialog dialog(_emulator.get());
    auto* view = dialog.findChild<QHexView*>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->isEnabled()) << "drive A, 0/0, sector 1 is shown at once";
    ASSERT_EQ(view->getDocument()->length(), 512);

    view->getDocument()->replace(7, static_cast<uchar>(0x42));
    EXPECT_EQ(Sector(1)->data[7], 0x42);
    EXPECT_TRUE(Sector(1)->isDataCRCValid());

    const uint8_t first = Sector(1)->data[0];
    view->getDocument()->insert(0, static_cast<uchar>(0xAA));
    EXPECT_EQ(view->getDocument()->length(), 512) << "an insert is put back";
    EXPECT_EQ(Sector(1)->data[0], first);
}

TEST_F(DiskSectorDialog_Test, RefusalsAndMissingSectors)
{
    DiskSectorDialog dialog(_emulator.get());
    auto spins = dialog.findChildren<QSpinBox*>();
    ASSERT_EQ(spins.size(), 3);  // cylinder, side, sector
    spins[2]->setValue(10);      // +3DOS tracks have IDs 1-9
    dialog.load();
    EXPECT_FALSE(dialog.findChild<QHexView*>()->isEnabled());

    spins[2]->setValue(2);
    _emulator->GetContext()->coreState.diskDrives[0]->setWriteProtect(true);
    dialog.load();
    auto* view = dialog.findChild<QHexView*>();
    ASSERT_TRUE(view->isEnabled());
    const uint8_t before = Sector(2)->data[0];
    view->getDocument()->replace(0, static_cast<uchar>(before ^ 0xFF));
    EXPECT_EQ(Sector(2)->data[0], before) << "a write-protected disk refuses";
    bool shown = false;
    for (QLabel* label : dialog.findChildren<QLabel*>())
        shown = shown || label->text().contains("write-protected");
    EXPECT_TRUE(shown);
}
