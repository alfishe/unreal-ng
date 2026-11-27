// The debugger's PC history pane (PcHistoryPane; debugger additions tdd §7): it records while shown, a refresh at a
// stop lists the newest instructions with their window's page and the instruction, a row picked names its address,
// and hiding it stops the recording.

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "debugger/debugmanager.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/pchistorypane.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

TEST(PcHistoryPane_Test, RecordsWhileShownListsInstructionsPicksAnAddress)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("pchistory-pane-test", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (uint16_t a = 0x8000; a < 0x8004; ++a)
        context->pMemory->DirectWriteToZ80Memory(a, 0x00);  // NOPs
    context->pCore->GetZ80()->pc = 0x8000;

    PcHistoryPane pane(emulator);
    PcHistory* history = context->pDebugManager->GetPcHistory();
    EXPECT_FALSE(history->IsArmed()) << "a hidden pane costs nothing";
    pane.setRecording(true);
    EXPECT_TRUE(history->IsArmed());
    emulator->RunSingleCPUCycle(true);
    emulator->RunSingleCPUCycle(true);
    pane.refresh();
    auto* list = pane.findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->topLevelItemCount(), 2);
    EXPECT_EQ(list->topLevelItem(0)->text(0), "8001");
    EXPECT_EQ(list->topLevelItem(0)->text(1), "RAM2") << "48K: #8000 is RAM page 2";
    EXPECT_EQ(list->topLevelItem(0)->text(2), "nop");

    std::vector<uint16_t> picked;
    QObject::connect(&pane, &PcHistoryPane::addressPicked, [&](uint16_t address) { picked.push_back(address); });
    list->setCurrentItem(list->topLevelItem(1));
    ASSERT_EQ(picked.size(), 1u);
    EXPECT_EQ(picked[0], 0x8000);

    pane.setRecording(false);
    EXPECT_FALSE(history->IsArmed());
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

/// A stop's refresh makes only the rows of the instructions since the last one (the older rows move down), and the
/// columns keep fixed widths (ResizeToContents measured all 1024 rows on every stop and window layout). Runtime: a few
/// ms
TEST(PcHistoryPane_Test, AStopMakesOnlyItsNewRows)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("pchistory-pane-rows", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (uint16_t a = 0x8000; a < 0x8010; ++a)
        context->pMemory->DirectWriteToZ80Memory(a, 0x00);  // NOPs
    context->pCore->GetZ80()->pc = 0x8000;

    PcHistoryPane pane(emulator);
    auto* list = pane.findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->header()->sectionResizeMode(0), QHeaderView::Interactive);
    EXPECT_EQ(list->header()->sectionResizeMode(1), QHeaderView::Interactive);
    EXPECT_GT(list->columnWidth(0), 0);
    pane.setRecording(true);
    emulator->RunSingleCPUCycle(true);
    emulator->RunSingleCPUCycle(true);
    pane.refresh();
    ASSERT_EQ(list->topLevelItemCount(), 2);
    EXPECT_EQ(pane.rowsMade(), 2);
    QTreeWidgetItem* older = list->topLevelItem(0);
    for (int i = 0; i < 3; ++i)
        emulator->RunSingleCPUCycle(true);
    pane.refresh();
    ASSERT_EQ(list->topLevelItemCount(), 5);
    EXPECT_EQ(pane.rowsMade(), 5) << "the stop made its 3 rows, not all 5";
    EXPECT_EQ(list->topLevelItem(0)->text(0), "8004");
    EXPECT_EQ(list->topLevelItem(3), older) << "the rows shown before moved down";
    EXPECT_EQ(list->topLevelItem(4)->text(0), "8000");
    pane.refresh();  // nothing ran
    EXPECT_EQ(pane.rowsMade(), 5);
    EXPECT_EQ(list->topLevelItemCount(), 5);

    pane.setRecording(false);
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

/// The owner's report (2026-11-26): Step Over / Out froze the debugger while eCat was connected. A paused stop's
/// refresh read the history at a coherent moment and waited up to 500 ms when another client held the machine. Now
/// it tries once: the list stays, it is read again from the event loop when the machine is free. Runtime: about 50 ms
TEST(PcHistoryPane_Test, ARefreshNeverWaitsForABusyMachine)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("pchistory-pane-busy", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return context->emulatorState.frame_counter > 1; }));
    emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return emulator->IsPaused() && emulator->IsEmulationParked(); }));
    for (uint16_t a = 0x8000; a < 0x8010; ++a)
        context->pMemory->DirectWriteToZ80Memory(a, 0x00);  // NOPs
    context->pCore->GetZ80()->pc = 0x8000;

    PcHistoryPane pane(emulator);
    auto* list = pane.findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    pane.setRecording(true);
    emulator->RunSingleCPUCycle(true);
    pane.refresh();
    ASSERT_EQ(list->topLevelItemCount(), 1);
    emulator->RunSingleCPUCycle(true);

    // Another client holds the machine (eCat's WebAPI read at a coherent moment) until released
    std::atomic<bool> held{false};
    std::atomic<bool> release{false};
    std::thread other([&] {
        emulator->RunAtCoherentMoment(
            [&] {
                held = true;
                while (!release.load())
                    std::this_thread::yield();
            },
            1000);
    });
    ASSERT_TRUE(TestWait::For([&] { return held.load(); }));
    QElapsedTimer clock;
    clock.start();
    pane.refresh();
    const qint64 blocked = clock.elapsed();
    EXPECT_LT(blocked, 20) << "the GUI thread waited " << blocked << " ms for the machine";
    EXPECT_GE(pane.busyReads(), 1);
    EXPECT_EQ(list->topLevelItemCount(), 1) << "busy: the list shown stays";
    release = true;
    other.join();
    QElapsedTimer pump;
    pump.start();
    while (list->topLevelItemCount() != 2 && pump.elapsed() < 1000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    EXPECT_EQ(list->topLevelItemCount(), 2) << "read again once the machine was free";

    pane.setRecording(false);
    emulator->Stop();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
