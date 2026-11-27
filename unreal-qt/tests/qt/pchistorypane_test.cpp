// The debugger's PC history pane (PcHistoryPane; debugger additions tdd §7): it records while shown, a refresh at a
// stop lists the newest instructions with their window's page and the instruction, a row picked names its address,
// and hiding it stops the recording.

#include <QApplication>
#include <QCoreApplication>
#include <QHeaderView>
#include <QAbstractItemModel>
#include <QItemSelectionModel>
#include <QPaintEvent>
#include <QTreeView>
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

namespace
{
int Rows(QTreeView* list)
{
    return list->model()->rowCount();
}
QString Text(QTreeView* list, int row, int column)
{
    return list->model()->index(row, column).data().toString();
}
}  // namespace

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
    auto* list = pane.findChild<QTreeView*>();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(Rows(list), 2);
    EXPECT_EQ(Text(list, 0, 0), "8001");
    EXPECT_EQ(Text(list, 0, 1), "RAM2") << "48K: #8000 is RAM page 2";
    EXPECT_EQ(Text(list, 0, 2), "nop");

    std::vector<uint16_t> picked;
    QObject::connect(&pane, &PcHistoryPane::addressPicked, [&](uint16_t address) { picked.push_back(address); });
    list->setCurrentIndex(list->model()->index(1, 0));
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
    auto* list = pane.findChild<QTreeView*>();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->header()->sectionResizeMode(0), QHeaderView::Interactive);
    EXPECT_EQ(list->header()->sectionResizeMode(1), QHeaderView::Interactive);
    EXPECT_GT(list->columnWidth(0), 0);
    pane.setRecording(true);
    emulator->RunSingleCPUCycle(true);
    emulator->RunSingleCPUCycle(true);
    pane.refresh();
    ASSERT_EQ(Rows(list), 2);
    EXPECT_EQ(pane.rowsMade(), 2);
    for (int i = 0; i < 3; ++i)
        emulator->RunSingleCPUCycle(true);
    pane.refresh();
    ASSERT_EQ(Rows(list), 5);
    EXPECT_EQ(pane.rowsMade(), 5) << "the stop made its 3 rows, not all 5";
    EXPECT_EQ(Text(list, 0, 0), "8004");
    EXPECT_EQ(Text(list, 3, 0), "8001") << "the rows shown before moved down";
    EXPECT_EQ(Text(list, 4, 0), "8000");
    pane.refresh();  // nothing ran
    EXPECT_EQ(pane.rowsMade(), 5);
    EXPECT_EQ(Rows(list), 5);

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
    auto* list = pane.findChild<QTreeView*>();
    ASSERT_NE(list, nullptr);
    pane.setRecording(true);
    emulator->RunSingleCPUCycle(true);
    pane.refresh();
    ASSERT_EQ(Rows(list), 1);
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
    if (!TestWait::For([&] { return held.load(); }))
    {
        release = true;
        other.join();
        FAIL() << "the other client never got the machine";
    }
    // The machine is held until the refresh is back: its one try finds it busy whatever the host's speed
    const int busy = pane.busyReads();
    pane.refresh();
    EXPECT_EQ(pane.busyReads(), busy + 1) << "one try, found busy";
    EXPECT_EQ(Rows(list), 1) << "busy: the list shown stays";
    release = true;
    other.join();
    EXPECT_TRUE(TestWait::For([&] {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        return Rows(list) == 2;
    }, std::chrono::seconds(5)));
    EXPECT_EQ(Rows(list), 2) << "read again once the machine was free";

    pane.setRecording(false);
    emulator->Stop();
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

/// A stop reads the entries since the rows shown (by the history's total) and kOverlap shown ones that prove they
/// continue them; the first read, or rows that do not continue, read the whole ring. Either way the list is the newest entries, at most
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
    auto* list = pane.findChild<QTreeView*>();
    ASSERT_NE(list, nullptr);
    pane.setRecording(true);
    const auto run = [&](int n) {
        for (int i = 0; i < n; ++i)
            emulator->RunSingleCPUCycle(true);
        pane.refresh();
    };
    const auto address = [](uint32_t at) { return QString("%1").arg(at & 0xFFFF, 4, 16, QChar('0')).toUpper(); };
    run(2);
    ASSERT_EQ(Rows(list), 2);
    run(100);  // 100 new entries
    ASSERT_EQ(Rows(list), 102);
    EXPECT_EQ(Text(list, 0, 0), address(0x8000 + 101));
    EXPECT_EQ(Text(list, 101, 0), "8000");
    EXPECT_EQ(pane.rowsMade(), 102) << "the old rows stay";
    run(10);
    ASSERT_EQ(Rows(list), 112);
    EXPECT_EQ(Text(list, 0, 0), address(0x8000 + 111));
    EXPECT_EQ(Text(list, 111, 0), "8000");
    EXPECT_EQ(pane.rowsMade(), 112);
    run(1000);  // past the ring's capacity: the oldest leave
    ASSERT_EQ(Rows(list), static_cast<int>(PcHistory::kCapacity));
    EXPECT_EQ(Text(list, 0, 0), address(0x8000 + 1111));
    EXPECT_EQ(Text(list, static_cast<int>(PcHistory::kCapacity) - 1, 0),
              address(0x8000 + 1112 - static_cast<uint32_t>(PcHistory::kCapacity)));
    run(5);
    ASSERT_EQ(Rows(list), static_cast<int>(PcHistory::kCapacity));
    EXPECT_EQ(Text(list, 0, 0), address(0x8000 + 1116));
    EXPECT_EQ(Text(list, static_cast<int>(PcHistory::kCapacity) - 1, 0),
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
    auto* list = pane->findChild<QTreeView*>();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(Rows(list), 22);

    pane->setRecording(false);
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

namespace
{
class ViewportPaints : public QObject
{
public:
    QRegion painted;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Paint)
            painted += static_cast<QPaintEvent*>(event)->region();
        return QObject::eventFilter(watched, event);
    }
};

void Pump()
{
    for (int i = 0; i < 5; i++)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

/// The pane shown, `before` instructions recorded and listed
struct ShownPane
{
    std::shared_ptr<Emulator> emulator;
    QWidget host;
    PcHistoryPane* pane = nullptr;
    QTreeView* list = nullptr;

    ShownPane(const char* id, int before)
    {
        emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(id, "48K", LoggerLevel::LogError);
        EmulatorContext* context = emulator->GetContext();
        for (uint32_t a = 0x8000; a < 0x10000; ++a)
            context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(a), 0x00);  // NOPs
        context->pCore->GetZ80()->pc = 0x8000;
        host.resize(400, 400);
        auto* layout = new QVBoxLayout(&host);
        pane = new PcHistoryPane(emulator, &host);
        layout->addWidget(pane);
        host.show();
        pane->setRecording(true);
        list = pane->findChild<QTreeView*>();
        if (list)
            list->clearFocus();  // no focus frame on the current row (it moves with its row, as a repaint shows it)
        Run(before);
        if (list)
            list->selectionModel()->clearCurrentIndex();  // no current row's frame either
        Pump();
    }
    ~ShownPane()
    {
        pane->setRecording(false);
        EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    }
    void Run(int n)
    {
        for (int i = 0; i < n; ++i)
            emulator->RunSingleCPUCycle(true);
        pane->refresh();
        Pump();
    }
    QImage Shot() const { return list->viewport()->grab().toImage(); }
};

/// The first pixel row where `a` and `b` differ (-1: none), for the failure messages
int FirstDifferentRow(const QImage& a, const QImage& b)
{
    if (a.size() != b.size())
        return 0;
    for (int y = 0; y < a.height(); y++)
        for (int x = 0; x < a.width(); x++)
            if (a.pixel(x, y) != b.pixel(x, y))
                return y;
    return -1;
}

/// The band of pixel rows [top, top + height) of `image`
QImage Band(const QImage& image, int top, int height)
{
    return image.copy(0, top, image.width(), height);
}

/// A stop of one instruction, the list filling (rows inserted) or full (the ring turning): the rows shown move down
/// a row as they were and only the new top row is painted (an item view repaints every row on screen for a row put
/// at the top, and alternate colours by the row would flip every stripe). Runtime: about 20 ms
void ExpectOneRowStop(ShownPane& shown)
{
    ASSERT_NE(shown.list, nullptr);
    const int rowHeight = shown.list->sizeHintForRow(0);
    ASSERT_GT(rowHeight, 0);
    const int height = shown.list->viewport()->height();
    ASSERT_GT(height, rowHeight * 8) << "the view shows a few rows";
    const QImage before = shown.Shot();
    ViewportPaints spy;
    shown.list->viewport()->installEventFilter(&spy);
    shown.Run(1);
    shown.list->viewport()->removeEventFilter(&spy);
    EXPECT_LE(spy.painted.boundingRect().height(), 2 * rowHeight)
        << "a stop of one row painted " << spy.painted.boundingRect().height() << " px of " << height;
    const QImage after = shown.Shot();
    const int band = (height / rowHeight - 2) * rowHeight;
    EXPECT_EQ(FirstDifferentRow(Band(after, rowHeight, band), Band(before, 0, band)), -1)
        << "the rows shown did not move down a row as they were (their stripes); row height " << rowHeight;
    // What the moved pixels show is what a full paint of the rows shows
    shown.list->viewport()->repaint();
    EXPECT_EQ(FirstDifferentRow(shown.Shot(), after), -1) << "the moved rows differ from the rows painted anew";
}
}  // namespace

TEST(PcHistoryPane_Test, AStopPaintsOnlyItsNewRowWhileTheListFills)
{
    ShownPane shown("pchistory-pane-fill", 60);
    ASSERT_EQ(Rows(shown.list), 60);
    ExpectOneRowStop(shown);
    EXPECT_EQ(Rows(shown.list), 61);
    EXPECT_EQ(Text(shown.list, 0, 0), "803C");
}

TEST(PcHistoryPane_Test, AStopPaintsOnlyItsNewRowOnceTheRingIsFull)
{
    ShownPane shown("pchistory-pane-full", static_cast<int>(PcHistory::kCapacity) + 40);
    ASSERT_EQ(Rows(shown.list), static_cast<int>(PcHistory::kCapacity));
    ExpectOneRowStop(shown);
    EXPECT_EQ(Rows(shown.list), static_cast<int>(PcHistory::kCapacity));
    EXPECT_EQ(Text(shown.list, 0, 0), QString("%1").arg(0x8000 + PcHistory::kCapacity + 40, 4, 16).toUpper());
}

/// The row picked stays on its entry when a stop's rows come above it, the ring full (its rows turn as data) or not;
/// it is not a new pick. Runtime: a few ms
TEST(PcHistoryPane_Test, ThePickedRowFollowsItsEntry)
{
    for (const int before : {30, static_cast<int>(PcHistory::kCapacity) + 30})
    {
        ShownPane shown(before > 100 ? "pchistory-pane-pick-full" : "pchistory-pane-pick", before);
        ASSERT_NE(shown.list, nullptr);
        shown.list->setCurrentIndex(shown.list->model()->index(5, 0));
        const QString picked = Text(shown.list, 5, 0);
        int picks = 0;
        QObject::connect(shown.pane, &PcHistoryPane::addressPicked, [&](uint16_t) { picks++; });
        shown.Run(3);
        EXPECT_EQ(shown.list->currentIndex().row(), 8) << before;
        EXPECT_EQ(Text(shown.list, shown.list->currentIndex().row(), 0), picked) << before;
        EXPECT_EQ(picks, 0) << "a stop's rows are not a pick";
    }
}
