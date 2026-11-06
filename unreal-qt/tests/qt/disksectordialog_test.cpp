// The debugger's Disk sector dialog (DiskSectorDialog; debugger additions tdd §3): a sector's data field as hex, a
// typed byte written through SectorWrite (CRC recalculated, image modified), refusals shown, the size fixed; the
// choices come from the disk (drives, geometry, the track's IDs) and Prev / Next cross tracks.

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
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
    /// The hex view is the page shown (not the "nothing here" note)
    static bool Shown(DiskSectorDialog& dialog)
    {
        auto* pages = dialog.findChild<QStackedWidget*>();
        return pages && pages->currentWidget() == dialog.findChild<QHexView*>();
    }
    static QComboBox* SectorChoice(DiskSectorDialog& dialog) { return dialog.findChildren<QComboBox*>().at(1); }
    std::shared_ptr<Emulator> _emulator;
};

TEST_F(DiskSectorDialog_Test, TypedBytesReachTheSector)
{
    DiskSectorDialog dialog(_emulator);
    auto* view = dialog.findChild<QHexView*>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(Shown(dialog)) << "drive A, 0/0, sector 1 is shown at once";
    EXPECT_TRUE(DiskSectorDialog::HasDrives(_emulator.get()));
    ASSERT_EQ(view->getDocument()->length(), 512);

    view->getDocument()->replace(7, static_cast<uchar>(0x42));
    EXPECT_EQ(Sector(1)->data[7], 0x42);
    EXPECT_TRUE(Sector(1)->isDataCRCValid());

    const uint8_t first = Sector(1)->data[0];
    view->getDocument()->insert(0, static_cast<uchar>(0xAA));
    EXPECT_EQ(view->getDocument()->length(), 512) << "an insert is put back";
    EXPECT_EQ(Sector(1)->data[0], first);
}

TEST_F(DiskSectorDialog_Test, ChoicesComeFromTheDiskAndWalkAcrossTracks)
{
    DiskSectorDialog dialog(_emulator);
    QComboBox* sectors = SectorChoice(dialog);
    ASSERT_EQ(sectors->count(), 9) << "+3DOS tracks have IDs 1-9";
    EXPECT_EQ(sectors->itemData(8).toInt(), 9);
    auto spins = dialog.findChildren<QSpinBox*>();
    ASSERT_EQ(spins.size(), 2);  // cylinder, side
    EXPECT_EQ(spins[0]->maximum(), 39);
    EXPECT_FALSE(spins[1]->isEnabled()) << "a single-sided disk";

    QPushButton* next = nullptr;
    for (QPushButton* button : dialog.findChildren<QPushButton*>())
        if (button->text() == QString("\u25B6"))
            next = button;
    ASSERT_NE(next, nullptr);
    sectors->setCurrentIndex(8);
    next->click();
    EXPECT_EQ(spins[0]->value(), 1) << "past the last sector: the next cylinder";
    EXPECT_EQ(sectors->currentData().toInt(), 1);
    EXPECT_TRUE(Shown(dialog));
}

TEST_F(DiskSectorDialog_Test, RefusalsAreShown)
{
    DiskSectorDialog dialog(_emulator);
    SectorChoice(dialog)->setCurrentIndex(SectorChoice(dialog)->findData(2));
    _emulator->GetContext()->coreState.diskDrives[0]->setWriteProtect(true);
    dialog.load();
    auto* view = dialog.findChild<QHexView*>();
    ASSERT_TRUE(Shown(dialog));
    const uint8_t before = Sector(2)->data[0];
    view->getDocument()->replace(0, static_cast<uchar>(before ^ 0xFF));
    EXPECT_EQ(Sector(2)->data[0], before) << "a write-protected disk refuses";
    bool shown = false;
    for (QLabel* label : dialog.findChildren<QLabel*>())
        shown = shown || label->text().contains("write-protected");
    EXPECT_TRUE(shown);
}

TEST(DiskSectorDialogNoDisk_Test, SaysSoInsteadOfAnEmptyView)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("disksector-nodisk-test", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EXPECT_TRUE(DiskSectorDialog::HasDrives(emulator.get())) << "every model has the Beta Disk drives";
    {
        DiskSectorDialog dialog(emulator);
        auto* pages = dialog.findChild<QStackedWidget*>();
        ASSERT_NE(pages, nullptr);
        auto* note = qobject_cast<QLabel*>(pages->currentWidget());
        ASSERT_NE(note, nullptr);
        EXPECT_TRUE(note->text().contains("No disk in drive A")) << note->text().toStdString();
    }
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
