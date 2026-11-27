// The embedded debugger's twin timeline strip (gs-debugger TODO P5.6 phase 2, gui-card-debugger.md A5.1, C03): both
// CPUs on one clock from timeline::Read. Paused: blocks on both lanes whose widths follow their durations, NOW, the
// host port marker of the game's OUT, the next instruction; a step moves it; a card's stop splits main's instruction
// in bus cycles. Running: the density bars at most 10 times a second. The timeline is armed only while the strip is
// shown. Zoom, pan, recenter and a click retargeting a CPU's disassembly through the view's accessors.
//
// Runtime justification: each machine test boots a Pentagon with a General Sound and runs one to three frames (the
// card stop and the running density in turbo mode), about 0.1-0.3 s each.
//
// UNREAL_TIMELINE_PNG_DIR=<dir>: the strip is saved there as PNGs (paused, after a step, a card stop, running; light
// and dark) for the C03 mockup review.

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPalette>
#include <QPixmap>
#include <QWheelEvent>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <utility>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/cardpanes.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/timeline/twintimeline.h"
#include "debugger/twintimelineview.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace
{
bool PumpUntil(const std::function<bool()>& done, int timeoutMs = 1000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done())
    {
        if (clock.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

/// Main: DI, then LD A,#2A / OUT (#BB),A / a short DJNZ wait, again and again (about 170 T a round)
void WriteOutLoop(Z80* z80)
{
    static const uint8_t program[] = {
        0xF3,        // 8000 DI
        0x3E, 0x2A,  // 8001 LD A,#2A
        0xD3, 0xBB,  // 8003 OUT (#BB),A
        0x06, 0x0A,  // 8005 LD B,10
        0x10, 0xFE,  // 8007 DJNZ #8007
        0x18, 0xF6,  // 8009 JR #8001
    };
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    z80->pc = 0x8000;
}

/// The strip as a PNG for the mockup review, when asked for
void Save(QWidget& view, const char* name)
{
    const char* dir = std::getenv("UNREAL_TIMELINE_PNG_DIR");
    if (!dir || !*dir)
        return;
    QDir().mkpath(QString::fromLocal8Bit(dir));
    view.grab().save(QDir(QString::fromLocal8Bit(dir)).filePath(QString::fromLatin1(name) + ".png"));
}

void Darken(QWidget& view)
{
    QPalette dark = view.palette();
    dark.setColor(QPalette::Window, QColor(40, 41, 45));
    dark.setColor(QPalette::WindowText, QColor(225, 225, 225));
    view.setPalette(dark);
}

class TwinTimelineView_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        TwinTimelineSettings::Instance().SetShown(false);  // the default: off
        _action.reset(TwinTimelineSettings::Instance().CreateAction(nullptr));
    }
    void TearDown() override
    {
        TwinTimelineSettings::Instance().SetShown(false);
        _action.reset();
        if (_emulator)
        {
            _emulator->Stop();
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        }
    }
    void Create()
    {
        std::function<void(CONFIG&)> slotOverride;
        std::string error;
        ASSERT_TRUE(SlotControl::CreateOverride({{"zxbus.1", "gs"}}, slotOverride, error)) << error;
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("twin-timeline-test", "PENTAGON",
                                                                           LoggerLevel::LogError, &error, slotOverride);
        ASSERT_NE(_emulator, nullptr) << error;
        _context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _context->pMemory->UpdateFeatureCache();
        ASSERT_NE(_context->pSoundManager->getGeneralSound(), nullptr);
    }
    bool Armed() const { return _context->pDebugManager->GetTimeline()->Armed(); }
    /// Debug > Show GS Timeline on, through its action
    void Enable()
    {
        ASSERT_FALSE(_action->isChecked());
        _action->trigger();
        ASSERT_TRUE(TwinTimelineSettings::Instance().Shown());
    }
    std::unique_ptr<QAction> _action;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
};
}  // namespace

/// Paused: both lanes have blocks, widths follow the durations, NOW is in the middle, the game's OUT (#BB) shows a
/// host port marker inside its block, the next instructions are known; a click on a block asks for that CPU's
/// disassembly; the wheel zooms around the pointer. A step moves NOW: the instruction that was next is the newest block
TEST_F(TwinTimelineView_Test, PausedBlocksMarkersAndAStep)
{
    ASSERT_NO_FATAL_FAILURE(Create());
    Z80* z80 = _context->pCore->GetZ80();
    _emulator->RunNFrames(1);
    WriteOutLoop(z80);

    TwinTimelineView view;
    view.resize(1100, view.sizeHint().height());
    view.setSpan(200);
    view.follow(_emulator, DebugCpuId::GS);
    EXPECT_FALSE(view.armed()) << "the switch off: not shown, not armed";
    EXPECT_FALSE(Armed());
    ASSERT_NO_FATAL_FAILURE(Enable());
    ASSERT_TRUE(view.isVisible());
    ASSERT_TRUE(view.armed());
    EXPECT_TRUE(Armed()) << "shown: the timeline records";
    _emulator->RunTStates(500);
    view.refresh();

    ASSERT_TRUE(view.hasDetail()) << view.state().toStdString();
    EXPECT_EQ(view.cardCpuName(), "gs");
    ASSERT_FALSE(view.blocks(TwinTimelineView::MainLane).empty());
    ASSERT_FALSE(view.blocks(TwinTimelineView::CardLane).empty()) << "the card ran with main";
    ASSERT_TRUE(view.next(TwinTimelineView::MainLane).known);
    ASSERT_TRUE(view.next(TwinTimelineView::CardLane).known);
    EXPECT_EQ(view.next(TwinTimelineView::MainLane).pc, z80->pc);
    EXPECT_NEAR(view.xOfT(0.0), view.xOfT(-200.0) + 200.0 * view.pixelsPerT(), 1e-6) << "NOW in the middle";
    EXPECT_TRUE(view.laneReadout(TwinTimelineView::CardLane).contains("MHz"))
        << view.laneReadout(TwinTimelineView::CardLane).toStdString();
    EXPECT_TRUE(view.nowReadout().startsWith("T ")) << view.nowReadout().toStdString();

    // Width ∝ duration, on both lanes
    for (auto lane : {TwinTimelineView::MainLane, TwinTimelineView::CardLane})
        for (size_t i = 0; i < view.blocks(lane).size(); i++)
        {
            const TwinTimelineView::Block& b = view.blocks(lane)[i];
            if (b.duration * view.pixelsPerT() < 1.0)
                continue;
            EXPECT_NEAR(view.blockRect(lane, i).width(), b.duration * view.pixelsPerT(), 1e-6) << lane << "/" << i;
        }

    // The OUT (#BB),A: 11 T, a host port marker inside it
    const TwinTimelineView::Block* out = nullptr;
    for (const auto& b : view.blocks(TwinTimelineView::MainLane))
        if (b.pc == 0x8003 && !b.open)
            out = &b;
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->duration, 11.0);
    bool port = false;
    for (const auto& m : view.markers())
        if (m.kind == "host_port" && m.at >= out->at - 0.001 && m.at <= out->at + out->duration)
        {
            EXPECT_EQ(m.lane, TwinTimelineView::MainLane);
            EXPECT_TRUE(m.text.contains("#BB")) << m.text.toStdString();
            const QPoint at(static_cast<int>(std::lround(view.xOfT(m.at))), 3 + 6);
            EXPECT_EQ(view.toolTipAt(at), m.text) << "the access in the tooltip";
            port = true;
        }
    EXPECT_TRUE(port) << "no host port marker in the OUT's block";
    Save(view, "timeline-paused");
    {
        TwinTimelineView dark;
        Darken(dark);
        dark.resize(view.size());
        dark.setSpan(200);
        dark.setEmulator(_emulator);
        dark.show();
        Save(dark, "timeline-paused-dark");
    }
    EXPECT_TRUE(Armed()) << "the other view's release leaves this one's lease";

    // A click on a block: that CPU's disassembly at its address
    std::vector<std::pair<DebugCpuId, uint16_t>> targets;
    QObject::connect(&view, &TwinTimelineView::cpuTargetRequested, &view,
                     [&](DebugCpuId cpu, uint16_t pc) { targets.emplace_back(cpu, pc); });
    for (const auto& b : view.blocks(TwinTimelineView::MainLane))
        if (&b == out)
        {
            const size_t index = static_cast<size_t>(&b - view.blocks(TwinTimelineView::MainLane).data());
            EXPECT_TRUE(view.clickAt(view.blockRect(TwinTimelineView::MainLane, index).center().toPoint()));
        }
    size_t cardIndex = view.blocks(TwinTimelineView::CardLane).size();
    for (size_t i = 0; i < view.blocks(TwinTimelineView::CardLane).size(); i++)
        if (view.blocks(TwinTimelineView::CardLane)[i].kind != "int" &&
            view.blockRect(TwinTimelineView::CardLane, i).width() >= 3 &&
            view.blockRect(TwinTimelineView::CardLane, i).left() > view.xOfT(-200))
            cardIndex = i;
    ASSERT_LT(cardIndex, view.blocks(TwinTimelineView::CardLane).size());
    EXPECT_TRUE(view.clickAt(view.blockRect(TwinTimelineView::CardLane, cardIndex).center().toPoint()));
    ASSERT_EQ(targets.size(), 2u);
    EXPECT_EQ(targets[0].first, DebugCpuId::Main);
    EXPECT_EQ(targets[0].second, 0x8003);
    EXPECT_EQ(targets[1].first, DebugCpuId::GS);
    EXPECT_EQ(targets[1].second, view.blocks(TwinTimelineView::CardLane)[cardIndex].pc);
    EXPECT_FALSE(view.clickAt(QPoint(view.width() - 5, 20))) << "the readouts are not a block";

    // The wheel zooms around the pointer: the T under it stays
    const double anchorX = view.xOfT(-100.0);
    QWheelEvent wheel(QPointF(anchorX, 30), view.mapToGlobal(QPointF(anchorX, 30)), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&view, &wheel);
    EXPECT_NEAR(view.span(), 200.0 / 1.25, 1e-9);
    EXPECT_NEAR(view.xOfT(-100.0), anchorX, 1e-6);
    view.recenter();
    view.setSpan(3000);  // zoomed out: the blocks merge into a bar (a wider read: the blocks above are replaced)
    EXPECT_TRUE(view.merged());
    Save(view, "timeline-zoomed-out");
    view.setSpan(200);

    // A step: NOW moves on, the instruction that was next is the newest block
    const int nextPc = view.next(TwinTimelineView::MainLane).pc;
    const double mainT = view.nowT() + view.gapT();  // main's own time (the lazy card stays where it was)
    _emulator->RunSingleCPUCycle();
    view.refresh();
    EXPECT_GT(view.nowT() + view.gapT(), mainT);
    ASSERT_FALSE(view.blocks(TwinTimelineView::MainLane).empty());
    EXPECT_EQ(view.blocks(TwinTimelineView::MainLane).back().pc, nextPc);
    EXPECT_EQ(view.next(TwinTimelineView::MainLane).pc, z80->pc);
    Save(view, "timeline-after-step");

    view.hide();
    EXPECT_FALSE(view.armed());
    EXPECT_FALSE(Armed()) << "hidden: released";
}

/// A card breakpoint stops the machine with main inside the card's moment: main's instruction at NOW in its bus
/// cycles (gs-debugger design §5.4), the breakpoint's marker on the card lane at NOW
TEST_F(TwinTimelineView_Test, ACardStopSplitsMainsInstructionInBusCycles)
{
    ASSERT_NO_FATAL_FAILURE(Create());
    _emulator->RunNFrames(3);  // the firmware boots; stopped at a frame end, the card caught up
    GeneralSoundCard* card = _context->pSoundManager->getGeneralSound();
    Z80CpuRegisters regs{};
    ASSERT_TRUE(card->cardRegisters(regs));

    TwinTimelineView view;
    view.resize(1100, view.sizeHint().height());
    view.setSpan(40);
    ASSERT_NO_FATAL_FAILURE(Enable());
    view.follow(_emulator, DebugCpuId::GS);
    ASSERT_TRUE(Armed());
    BreakpointManager* breakpoints = _context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS);
    const uint16_t id = breakpoints->AddExecutionBreakpoint(regs.pc);
    ASSERT_NE(id, BRK_INVALID);
    _emulator->EnableTurboMode();
    _emulator->StartAsync();
    // The first stop can come at the card's first instruction of a frame, before main's first one: the next stops
    // come inside main's instructions
    GSCardTarget* target = _context->pDebugManager->GetGSTarget();
    for (int stop = 0; stop < 6; stop++)
    {
        const uint64_t stops = target->StopCount();
        if (stop)
            _emulator->Resume();
        ASSERT_TRUE(TestWait::For([&] {
            return target->StopCount() > stops && _emulator->IsPaused() && _emulator->IsParkedInsideCardRun();
        }));
        view.refresh();
        if (!view.segments().empty())
            break;
    }
    ASSERT_TRUE(view.hasDetail()) << view.state().toStdString();
    EXPECT_EQ(view.state(), "paused");
    EXPECT_EQ(view.next(TwinTimelineView::CardLane).pc, regs.pc);
    ASSERT_FALSE(view.segments().empty()) << "main's instruction in bus cycles; tight "
                                          << _emulator->GetFeatureManager()->cardTightHold() << "\n"
                                          << timeline::TextOf(timeline::Read(_emulator.get(), timeline::Query{}).body);
    ASSERT_GE(view.segmentBlock(), 0);
    const TwinTimelineView::Block& at = view.blocks(TwinTimelineView::MainLane)[static_cast<size_t>(view.segmentBlock())];
    EXPECT_LE(at.at, 0.0);
    EXPECT_EQ(view.segments().front().at, at.at);
    EXPECT_EQ(view.segments().front().kind, "ocf");
    bool stopMarker = false;
    for (const auto& m : view.markers())
        if (m.kind == "breakpoint" && m.lane == TwinTimelineView::CardLane && std::abs(m.at) < 0.001)
            stopMarker = true;
    EXPECT_TRUE(stopMarker);
    Save(view, "timeline-card-stop");
    Darken(view);
    Save(view, "timeline-card-stop-dark");
    breakpoints->RemoveBreakpointByID(id);
}

/// Running: the density bars at most 10 times a second (main: GS port accesses, card: busy / idle), armed while shown,
/// released when hidden; no read while paused
TEST_F(TwinTimelineView_Test, RunningDensityWhileShown)
{
    ASSERT_NO_FATAL_FAILURE(Create());
    WriteOutLoop(_context->pCore->GetZ80());
    TwinTimelineView view;
    view.resize(1100, view.sizeHint().height());
    ASSERT_NO_FATAL_FAILURE(Enable());
    view.follow(_emulator, DebugCpuId::GS);
    ASSERT_TRUE(Armed());
    EXPECT_TRUE(view.liveLine()->active());
    _emulator->EnableTurboMode();
    _emulator->StartAsync();
    ASSERT_TRUE(PumpUntil([&] { return view.running() && view.density().size() >= 3; }, 2000))
        << view.state().toStdString() << " " << view.density().size();
    EXPECT_FALSE(view.hasDetail());
    bool accesses = false;
    for (const auto& t : view.density())
    {
        EXPECT_GT(t.steps, 0);
        accesses = accesses || t.commands > 0;
    }
    EXPECT_TRUE(accesses) << "the program's OUT (#BB) per round";
    EXPECT_GT(view.runningReads(), 0);
    EXPECT_TRUE(view.nowReadout().startsWith("running")) << view.nowReadout().toStdString();
    // At most 10 Hz: the reads over a measured time
    const int reads = view.runningReads();
    QElapsedTimer clock;
    clock.start();
    PumpUntil([&] { return clock.elapsed() >= 250; });
    EXPECT_LE(view.runningReads() - reads, 4) << "over " << clock.elapsed() << " ms";
    Save(view, "timeline-running");
    Darken(view);
    view.update();
    Save(view, "timeline-running-dark");

    view.hide();
    EXPECT_FALSE(Armed()) << "hidden: released, nothing recorded";
    EXPECT_FALSE(view.liveLine()->active());
    view.show();
    EXPECT_TRUE(Armed());
    view.setEmulator(nullptr);
    EXPECT_FALSE(Armed()) << "no machine: the old one released";
    view.setEmulator(_emulator);
    EXPECT_TRUE(Armed());
    _emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsEmulationParked(); }));
}

/// Debug > Show GS Timeline is off by default: the debugger's strip stays hidden, the timeline is not armed (nothing
/// records) and nothing is read, at a stop or a step; the action on shows, arms and reads it; off again hides and
/// releases it. Runtime: one machine, no frames
TEST_F(TwinTimelineView_Test, OffByDefaultHiddenNotArmedNoReads)
{
    ASSERT_NO_FATAL_FAILURE(Create());
    EXPECT_FALSE(TwinTimelineSettings::Instance().Shown());
    EXPECT_FALSE(_action->isChecked());
    EXPECT_EQ(_action->text(), "Show GS &Timeline");
    TwinTimelineView view;
    view.follow(_emulator, DebugCpuId::GS);  // the debugger's call: a card CPU is fitted
    view.refresh();
    _emulator->RunSingleCPUCycle();
    view.follow(_emulator, DebugCpuId::GS);  // the step's refresh
    EXPECT_TRUE(view.isHidden());
    EXPECT_FALSE(view.armed());
    EXPECT_FALSE(Armed()) << "off: the histories stay off";
    EXPECT_EQ(view.reads(), 0) << "off: nothing read";
    EXPECT_FALSE(view.liveLine()->active());

    _action->trigger();
    EXPECT_TRUE(view.isVisible());
    EXPECT_TRUE(Armed());
    EXPECT_GT(view.reads(), 0);
    EXPECT_TRUE(view.hasDetail()) << view.state().toStdString();

    _action->trigger();
    EXPECT_FALSE(_action->isChecked());
    EXPECT_TRUE(view.isHidden());
    EXPECT_FALSE(Armed()) << "off again: released";
}

/// A view destroyed while shown releases its lease
TEST_F(TwinTimelineView_Test, DestroyedWhileShownReleases)
{
    ASSERT_NO_FATAL_FAILURE(Create());
    {
        TwinTimelineView view;
        ASSERT_NO_FATAL_FAILURE(Enable());
        view.follow(_emulator, DebugCpuId::GS);
        EXPECT_TRUE(Armed());
    }
    EXPECT_FALSE(Armed());
}

/// Zoom, pan and recenter: the default ± 60 T around NOW; zoomed far out the blocks merge into a density bar
TEST(TwinTimelineView_ViewTest, ZoomPanRecenter)
{
    TwinTimelineView view;
    view.resize(1000, view.sizeHint().height());
    EXPECT_EQ(view.span(), 60.0);
    EXPECT_EQ(view.center(), 0.0);
    EXPECT_FALSE(view.merged());
    const double x = view.xOfT(30.0);
    view.zoomBy(1, 30.0);
    EXPECT_NEAR(view.span(), 48.0, 1e-9);
    EXPECT_NEAR(view.xOfT(30.0), x, 1e-6) << "the anchor stays";
    view.zoomBy(-1, 30.0);
    EXPECT_NEAR(view.span(), 60.0, 1e-9);
    view.recenter();
    const double ppt = view.pixelsPerT();
    view.panByPixels(100);
    EXPECT_NEAR(view.center(), -100.0 / ppt, 1e-9) << "dragged right: earlier time";
    view.recenter();
    EXPECT_EQ(view.center(), 0.0);
    view.setSpan(1e9);
    EXPECT_EQ(view.span(), TwinTimelineView::kMaxSpanT);
    EXPECT_TRUE(view.merged());
    view.setSpan(0);
    EXPECT_EQ(view.span(), TwinTimelineView::kMinSpanT);
}

namespace
{
/// The longest stretch the GUI thread spent in one call (a step, a refresh, one event loop turn)
struct GuiBlock
{
    QElapsedTimer clock;
    qint64 last = 0;
    qint64 longest = 0;
    std::string where;
    qint64 longestRead = 0;  ///< of the timeline's own work: the refreshes and the event loop's turns (its retries)
    std::string whereRead;
    GuiBlock() { clock.start(); }
    void Mark(const char* what)
    {
        const qint64 now = clock.nsecsElapsed();
        if (now - last > longest)
        {
            longest = now - last;
            where = what;
        }
        if (std::string(what).rfind("step", 0) != 0 && now - last > longestRead)
        {
            longestRead = now - last;
            whereRead = what;
        }
        last = now;
    }
    void Restart() { last = clock.nsecsElapsed(); }
};

/// Main: CALL #8010 / JR #8000; #8010: LD B,40 / DJNZ / RET (about 560 T in the call)
void WriteCallLoop(Z80* z80)
{
    static const uint8_t program[] = {0xF3, 0xCD, 0x10, 0x80, 0x18, 0xFA};  // 8000 DI; 8001 CALL #8010; 8004 JR #8000
    static const uint8_t sub[] = {0x06, 0x28, 0x10, 0xFE, 0xC9};            // 8010 LD B,40; DJNZ $; RET
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    for (size_t i = 0; i < sizeof(sub); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8010 + i), sub[i]);
    z80->pc = 0x8001;
}
}  // namespace

/// The owner's report (2026-11-26): Step Over / Step Out with the strip shown froze the debugger's GUI thread while eCat
/// was connected. The debugger's path: the step (StepOver: a run to a temporary breakpoint; StepOut: the same to the
/// return), the window's refresh right after it (the machine running or just stopping), the stop's refresh when the
/// stop arrives - while another client (eCat over --connect: the WebAPI's GET debug/timeline) reads at every moment,
/// holding the CPU for each read. Before the fix a paused read polled for a free moment up to 500 ms (seen: 45-57 ms
/// per refresh with this reader; after it about 2 ms). The longest GUI-thread block over 12 step overs and
/// outs stays under 50 ms, the longest of the timeline's refreshes under 20 ms. Runtime: about 0.6 s (one machine, 12
/// short runs, the other client's 30 reads a stop)
TEST_F(TwinTimelineView_Test, StepOverAndOutNeverBlockTheGuiThread)
{
    ASSERT_NO_FATAL_FAILURE(Create());
    TwinTimelineView view;
    view.resize(1100, view.sizeHint().height());
    ASSERT_NO_FATAL_FAILURE(Enable());
    view.follow(_emulator, DebugCpuId::GS);
    ASSERT_TRUE(Armed());
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _context->emulatorState.frame_counter > 1; }));
    _emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsPaused() && _emulator->IsEmulationParked(); }));
    Z80* z80 = _context->pCore->GetZ80();
    WriteCallLoop(z80);
    view.refresh();
    ASSERT_TRUE(view.hasDetail()) << view.state().toStdString();

    PumpUntil([] { return false; }, 50);  // the first paint (the font cache) before the clock starts
    // Another client at every stop (eCat over --connect: a burst of WebAPI reads, its GET debug/timeline among them,
    // each holding the CPU while it reads)
    std::atomic<bool> stopReader{false};
    std::atomic<bool> readerStarted{false};
    std::atomic<int> remoteReads{0}, remoteBusy{0};
    std::thread reader([&] {
        readerStarted = true;
        uint64_t seen = _emulator->DebugSeq();
        while (!stopReader.load())
        {
            seen = _emulator->WaitDebugChange(seen, 20);
            for (int burst = 0; burst < 30 && !stopReader.load(); burst++)
            {
                timeline::Query q;
                q.client = "remote";
                q.recent = 512;
                q.spanT = 2000;
                q.waitMs = 0;  // as eCat asks now (wait_ms=0)
                const timeline::Reply r = timeline::Read(_emulator.get(), q);
                remoteReads++;
                if (r.status == 503)
                    remoteBusy++;
            }
        }
    });
    ASSERT_TRUE(TestWait::For([&] { return readerStarted.load(); }));
    constexpr int kSteps = 12;
    GuiBlock block;
    int stops = 0;
    for (int i = 0; i < kSteps; i++)
    {
        block.Restart();
        z80->pc = 0x8001;  // on the CALL
        z80->iff1 = z80->iff2 = 0;
        if (i % 2 == 0)
            _emulator->StepOver();  // a run to #8004
        else
        {
            _emulator->RunSingleCPUCycle();  // into the call
            _emulator->RunSingleCPUCycle();  // LD B,40
            _emulator->StepOut();            // a run to the return
        }
        block.Mark(i % 2 ? "step out" : "step over");
        view.refresh();  // the window's refresh right after the step
        block.Mark("refresh after the step");
        QElapsedTimer wait;
        wait.start();
        while (!(_emulator->IsPaused() && _emulator->IsEmulationParked()) && wait.elapsed() < 2000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            block.Mark("event loop while it runs");
        }
        ASSERT_TRUE(_emulator->IsPaused()) << i << " pc " << std::hex << z80->pc << " running " << _emulator->IsRunning();
        view.refresh();  // the stop's refresh
        block.Mark("refresh at the stop");
        // Busy (the other client's read), it is tried again from the event loop: the stop's detail arrives
        wait.restart();
        while (!(view.hasDetail() && view.next(TwinTimelineView::MainLane).pc == z80->pc) && wait.elapsed() < 1000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            block.Mark("event loop until the stop's detail");
        }
        stops += view.hasDetail() && view.next(TwinTimelineView::MainLane).pc == z80->pc ? 1 : 0;
    }
    stopReader = true;
    reader.join();
    EXPECT_GT(remoteReads.load(), 20) << "the other client read meanwhile";
    EXPECT_LT(block.longest, 50'000'000) << "the GUI thread blocked " << block.longest / 1000000.0 << " ms in "
                                         << block.where;
    // The timeline's reads never wait: one try each (the step itself may queue behind the other client's read)
    EXPECT_LT(block.longestRead, 20'000'000) << "a refresh blocked " << block.longestRead / 1000000.0 << " ms in "
                                             << block.whereRead;
    std::printf("[ timeline ] longest GUI block %.2f ms in %s; longest refresh %.2f ms in %s; remote reads %d; busy tries %d; %d/%d stops with "
                "detail\n",
                block.longest / 1e6, block.where.c_str(), block.longestRead / 1e6, block.whereRead.c_str(), remoteReads.load(), view.busyReads(), stops, kSteps);
    EXPECT_EQ(stops, kSteps) << "each stop's detail, at once or tried again";
}
