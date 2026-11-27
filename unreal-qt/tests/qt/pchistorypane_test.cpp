// The debugger's PC history pane (PcHistoryPane; debugger additions tdd §7): it records while shown, a refresh at a
// stop lists the newest instructions with their window's page and the instruction, a row picked names its address,
// and hiding it stops the recording.

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>
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

/// A stop reads the newest kProbe entries when they hold all the new ones (and the rows shown continue them); more
/// instructions than that since the last stop read the whole ring. Either way the list is the newest entries, at most
/// the ring's capacity. Runtime: a few ms
TEST(PcHistoryPane_Test, ALongRunBetweenStopsReadsTheWholeRing)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("pchistory-pane-ring", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (uint32_t a = 0x8000; a < 0x10000; ++a)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(a), 0x00);  // NOPs
    context->pCore->GetZ80()->pc = 0x8000;

    PcHistoryPane pane(emulator);
    auto* list = pane.findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    pane.setRecording(true);
    const auto run = [&](int n) {
        for (int i = 0; i < n; ++i)
            emulator->RunSingleCPUCycle(true);
        pane.refresh();
    };
    const auto address = [](uint32_t at) { return QString("%1").arg(at & 0xFFFF, 4, 16, QChar('0')).toUpper(); };
    run(2);
    ASSERT_EQ(list->topLevelItemCount(), 2);
    run(100);  // more than a probe holds
    ASSERT_EQ(list->topLevelItemCount(), 102);
    EXPECT_EQ(list->topLevelItem(0)->text(0), address(0x8000 + 101));
    EXPECT_EQ(list->topLevelItem(101)->text(0), "8000");
    EXPECT_EQ(pane.rowsMade(), 102) << "the old rows stay";
    run(10);  // within a probe
    ASSERT_EQ(list->topLevelItemCount(), 112);
    EXPECT_EQ(list->topLevelItem(0)->text(0), address(0x8000 + 111));
    EXPECT_EQ(list->topLevelItem(111)->text(0), "8000");
    EXPECT_EQ(pane.rowsMade(), 112);
    run(1000);  // past the ring's capacity: the oldest leave
    ASSERT_EQ(list->topLevelItemCount(), static_cast<int>(PcHistory::kCapacity));
    EXPECT_EQ(list->topLevelItem(0)->text(0), address(0x8000 + 1111));
    EXPECT_EQ(list->topLevelItem(static_cast<int>(PcHistory::kCapacity) - 1)->text(0),
              address(0x8000 + 1112 - static_cast<uint32_t>(PcHistory::kCapacity)));
    run(5);
    ASSERT_EQ(list->topLevelItemCount(), static_cast<int>(PcHistory::kCapacity));
    EXPECT_EQ(list->topLevelItem(0)->text(0), address(0x8000 + 1116));
    EXPECT_EQ(list->topLevelItem(static_cast<int>(PcHistory::kCapacity) - 1)->text(0),
              address(0x8000 + 1117 - static_cast<uint32_t>(PcHistory::kCapacity)));

    pane.setRecording(false);
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

/// The status line ("N instructions") changes at every stop; on one line it relays out nothing (a word-wrapping
/// QLabel relaid out the pane, the side tabs, the splitter and the debugger window, which repainted). Runtime: a few ms
TEST(PcHistoryPane_Test, AStopsStatusRelaysOutNothing)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("pchistory-pane-layout", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (uint16_t a = 0x8000; a < 0x8100; ++a)
        context->pMemory->DirectWriteToZ80Memory(a, 0x00);  // NOPs
    context->pCore->GetZ80()->pc = 0x8000;

    QWidget host;
    host.resize(400, 300);
    auto* layout = new QVBoxLayout(&host);
    auto* pane = new PcHistoryPane(emulator, &host);
    layout->addWidget(pane);
    host.show();
    pane->setRecording(true);
    emulator->RunSingleCPUCycle(true);
    pane->refresh();
    for (int i = 0; i < 5; i++)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);

    struct LayoutSpy : QObject
    {
        int requests = 0;
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (event->type() == QEvent::LayoutRequest)
                requests++;
            return QObject::eventFilter(watched, event);
        }
    } spy;
    pane->installEventFilter(&spy);
    host.installEventFilter(&spy);
    for (int stop = 0; stop < 3; stop++)
    {
        for (int i = 0; i < 7; i++)  // "1 instructions" -> "8 instructions" -> "15 instructions": a new text each
            emulator->RunSingleCPUCycle(true);
        pane->refresh();
        for (int i = 0; i < 3; i++)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    EXPECT_EQ(spy.requests, 0) << "a new status text of one line relaid out the window";
    auto* list = pane->findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->topLevelItemCount(), 22);

    pane->setRecording(false);
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
