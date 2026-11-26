// The debugger's General Sound panes (gs-debugger TODO P5.1): each a ReportPane over a report of the device catalog the
// automation planes serve; a fitted card fills them, the command log shows the host's command decoded, a machine
// without the card says why each is empty. On a NeoGS (P5.5) the board panes over the core's device boards: the NeoGS
// board replacing the Hardware pane, DMA, SD · MP3 · Flash. On the lightweight player (A10) its interpreter and player
// panes; with a ZX-MultiSound the panes over its General Sound (CPU `ms`).

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QComboBox>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/featuremanager.h"
#include "debugger/boardpane.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/boards/debugboards.h"
#include "debugger/cardpanes.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80mcycles.h"
#include "debugger/gsanalysispane.h"
#include "debugger/gscpuanalysispane.h"
#include "debugger/gslwpane.h"
#include "debugger/gsmonitorpane.h"
#include "debugger/gssendpane.h"
#include "debugger/history/historycontrol.h"
#include "debugger/reportpane.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/zxdmatraceview.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gshostdriver.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"
#include "media/mediapanelwindow.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"

namespace
{
class CardPanes_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    /// A machine with the card the slot fits ({} none)
    void Create(const char* model, const std::vector<std::pair<std::string, std::string>>& cards)
    {
        std::function<void(CONFIG&)> slotOverride;
        std::string error;
        if (!cards.empty())
            ASSERT_TRUE(SlotControl::CreateOverride(cards, slotOverride, error)) << error;
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("card-panes-test", model, LoggerLevel::LogError,
                                                                           &error, slotOverride);
        ASSERT_NE(_emulator, nullptr) << error;
        _context = _emulator->GetContext();
    }
    static QTreeWidgetItem* Field(QTreeWidget* tree, const QString& name)
    {
        const QList<QTreeWidgetItem*> found = tree->findItems(name, Qt::MatchExactly | Qt::MatchRecursive, 0);
        return found.isEmpty() ? nullptr : found.front();
    }
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    QWidget _parent;
};
}  // namespace

TEST_F(CardPanes_Test, PanesInTabOrder)
{
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    ASSERT_EQ(panes.size(), 19u);
    const char* titles[] = {"GS log",    "GS protocol", "GS monitor", "GS send", "GS objects", "GS hardware", "NeoGS hardware",
                            "NeoGS DMA", "NeoGS SD · MP3 · Flash", "GS stats", "GS interpreter", "GS player",
                            "GS dispatch", "GS unknown", "GS writers", "GS profiler", "GS events", "GS coverage",
                            "GS heat map"};
    const CardPanes::Card cards[] = {CardPanes::Card::Any,   CardPanes::Card::Any,   CardPanes::Card::Any, CardPanes::Card::Any,
                                     CardPanes::Card::Any, CardPanes::Card::NotNeoGS, CardPanes::Card::NeoGS,
                                     CardPanes::Card::NeoGS, CardPanes::Card::NeoGS, CardPanes::Card::Any,
                                     CardPanes::Card::Lightweight, CardPanes::Card::Lightweight, CardPanes::Card::Any,
                                     CardPanes::Card::Any, CardPanes::Card::Any, CardPanes::Card::Any, CardPanes::Card::Any,
                                     CardPanes::Card::Any, CardPanes::Card::Any};
    const GsAnalysisPane::View views[] = {GsAnalysisPane::View::Dispatch, GsAnalysisPane::View::Unknown,
                                          GsAnalysisPane::View::Writers};
    const GsCpuAnalysisPane::View cpuViews[] = {GsCpuAnalysisPane::View::Profiler, GsCpuAnalysisPane::View::Events,
                                                GsCpuAnalysisPane::View::Coverage, GsCpuAnalysisPane::View::HeatMap};
    for (size_t i = 0; i < panes.size(); i++)
    {
        EXPECT_EQ(panes[i].title, QString::fromUtf8(titles[i]));
        EXPECT_NE(panes[i].widget(), nullptr);
        EXPECT_EQ(panes[i].card, cards[i]) << titles[i];
        EXPECT_EQ(panes[i].board != nullptr, panes[i].card == CardPanes::Card::NeoGS) << titles[i];
        EXPECT_EQ(panes[i].inspector != nullptr, panes[i].card == CardPanes::Card::Lightweight) << titles[i];
        EXPECT_EQ(panes[i].monitor != nullptr, i == 2) << titles[i];
        EXPECT_EQ(panes[i].send != nullptr, i == 3) << titles[i];
        EXPECT_EQ(panes[i].analysis != nullptr, i == 4 || (i >= 12 && i < 15)) << titles[i];
        EXPECT_EQ(panes[i].cpuAnalysis != nullptr, i >= 15) << titles[i];
        EXPECT_FALSE(panes[i].tooltip.isEmpty());
    }
    EXPECT_EQ(panes[3].widget(), panes[3].send);
    EXPECT_EQ(panes[4].analysis->view(), GsAnalysisPane::View::Objects);
    for (size_t i = 12; i < 15; i++)
        EXPECT_EQ(panes[i].analysis->view(), views[i - 12]) << titles[i];
    for (size_t i = 15; i < panes.size(); i++)
    {
        EXPECT_EQ(panes[i].cpuAnalysis->view(), cpuViews[i - 15]) << titles[i];
        EXPECT_EQ(panes[i].widget(), panes[i].cpuAnalysis) << titles[i];
    }
}

/// The card fitted: every pane has its report; the hardware pane lists the DAC channels, the log the host's command
/// once its history records
TEST_F(CardPanes_Test, AFittedCardFillsThePanes)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    EXPECT_TRUE(CardPanes::Fitted(_context));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);

    ReportPane* hardware = panes[5].pane;
    hardware->refresh(_context, false);
    EXPECT_TRUE(hardware->status().isEmpty()) << hardware->status().toStdString();
    ASSERT_NE(hardware->table(), nullptr);
    EXPECT_GE(hardware->table()->rowCount(), 4) << "the DAC channels";
    EXPECT_NE(Field(hardware->fields(), "page"), nullptr) << "MPAG";

    ReportPane* protocol = panes[1].pane;
    protocol->refresh(_context, false);
    EXPECT_TRUE(protocol->status().isEmpty()) << protocol->status().toStdString();
    EXPECT_NE(Field(protocol->fields(), "protocol"), nullptr);
    EXPECT_NE(Field(protocol->fields(), "command_pending"), nullptr);

    // The command log records while its history does (off by default: zero cost)
    HistoryRequest ring;
    ring.verb = "config";
    ring.device = GSHistoryDevice(_context) + ".commands";
    ring.mode = "ring";
    ASSERT_EQ(HistoryControl::Execute(_context, ring).httpStatus, 200);
    _context->pPortDecoder->DecodePortOut(0x00BB, 0x23, 0);  // #23: Get the RAM's page count
    ReportPane* log = panes[0].pane;
    log->refresh(_context, false);
    EXPECT_TRUE(log->status().isEmpty()) << log->status().toStdString();
    ASSERT_NE(log->table(), nullptr);
    ASSERT_GE(log->table()->rowCount(), 1) << "the host's command";
    bool found = false;
    for (int row = 0; row < log->table()->rowCount(); row++)
        for (int column = 0; column < log->table()->columnCount(); column++)
            if (log->table()->item(row, column) && log->table()->item(row, column)->text() == "35")
                found = true;
    EXPECT_TRUE(found) << "command #23 (35) in a row";
}

/// The card's line: the report's strip; a stop of the card's names it and where the main CPU stands
TEST_F(CardPanes_Test, TheStripNamesTheCardAndWhosePause)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    const QString idle = CardPanes::StripText(_context, Emulator::DebugStop{});
    EXPECT_TRUE(idle.startsWith("GS Z80 12 MHz · PC #")) << idle.toStdString();
    EXPECT_TRUE(idle.contains(" · cycle ")) << idle.toStdString();
    EXPECT_FALSE(idle.contains("paused by")) << idle.toStdString();

    Emulator::DebugStop stop;
    stop.reason = Emulator::DebugStop::Reason::Breakpoint;
    stop.breakpoint.hit = true;
    stop.breakpoint.cpu = DebugCpuId::GS;
    stop.breakpoint.main.known = true;
    stop.breakpoint.main.pc = 0x8003;
    const QString card = CardPanes::StripText(_context, stop);
    EXPECT_TRUE(card.contains(" · paused by the card (gs) · main #8003")) << card.toStdString();

    stop.breakpoint.cpu = DebugCpuId::Main;
    EXPECT_FALSE(CardPanes::StripText(_context, stop).contains("paused by")) << "the main CPU's stop: the banner says it";
}

TEST_F(CardPanes_Test, WithoutTheCardEachSaysWhy)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K", {}));  // the Pentagon's config fits a GS
    ASSERT_FALSE(CardPanes::Fitted(_context));
    EXPECT_TRUE(CardPanes::StripText(_context, Emulator::DebugStop{}).isEmpty());
    for (const CardPanes::Pane& pane : CardPanes::Create(&_parent))
    {
        EXPECT_FALSE(CardPanes::Shown(pane.card, _context)) << pane.title.toStdString();
        if (pane.board)
        {
            pane.board->refresh(_context, false);
            EXPECT_FALSE(pane.board->status().isEmpty()) << pane.title.toStdString();
            continue;
        }
        if (pane.inspector)
        {
            pane.inspector->refresh(_context, false);
            EXPECT_FALSE(pane.inspector->status().isEmpty()) << pane.title.toStdString();
            continue;
        }
        if (pane.monitor)
        {
            pane.monitor->refresh(_context, false);
            EXPECT_FALSE(pane.monitor->status().isEmpty()) << pane.title.toStdString();
            continue;
        }
        if (pane.send)
        {
            pane.send->refresh(_context, false);
            EXPECT_TRUE(pane.send->status().startsWith("No General Sound card fitted")) << pane.title.toStdString();
            continue;
        }
        if (pane.analysis)
        {
            pane.analysis->refresh(_context, false);
            EXPECT_FALSE(pane.analysis->status().isEmpty()) << pane.title.toStdString();
            EXPECT_EQ(pane.analysis->table()->rowCount(), 0) << pane.title.toStdString();
            continue;
        }
        if (pane.cpuAnalysis)
        {
            pane.cpuAnalysis->refresh(_context, false);
            EXPECT_TRUE(pane.cpuAnalysis->status().startsWith("No General Sound card fitted")) << pane.title.toStdString();
            continue;
        }
        pane.pane->refresh(_context, false);
        EXPECT_FALSE(pane.pane->status().isEmpty()) << pane.title.toStdString();
        if (pane.pane->table())
            EXPECT_EQ(pane.pane->table()->rowCount(), 0) << pane.title.toStdString();
    }
}

/// Which panes a card shows: the classic card its Hardware pane, a NeoGS the NeoGS board and its two panes instead
TEST_F(CardPanes_Test, TheNeoGSPanesOnlyWithANeoGS)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    ASSERT_TRUE(CardPanes::Fitted(_context));
    if (_context->pSoundManager->getGeneralSound()->implementation() != GSCardImplementation::NGS)
    {
        EXPECT_FALSE(CardPanes::NeoGSFitted(_context));
        EXPECT_TRUE(CardPanes::Shown(CardPanes::Card::NotNeoGS, _context));
        EXPECT_FALSE(CardPanes::Shown(CardPanes::Card::NeoGS, _context));
        BoardPane dma({{"neogs_dma", "", {}}}, BoardPane::Blocks::Columns, &_parent);
        dma.refresh(_context, false);
        EXPECT_TRUE(dma.status().contains("neogs_dma")) << "the classic card has no NeoGS board: " << dma.status().toStdString();
    }
    EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    _emulator.reset();

    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    ASSERT_TRUE(CardPanes::NeoGSFitted(_context));
    EXPECT_TRUE(CardPanes::Shown(CardPanes::Card::Any, _context));
    EXPECT_TRUE(CardPanes::Shown(CardPanes::Card::NeoGS, _context));
    EXPECT_FALSE(CardPanes::Shown(CardPanes::Card::NotNeoGS, _context)) << "the NeoGS board replaces the GS hardware pane";
    const QString strip = CardPanes::StripText(_context, Emulator::DebugStop{});
    EXPECT_TRUE(strip.startsWith("NeoGS")) << strip.toStdString();
    EXPECT_TRUE(strip.contains(" · GSCFG0 #")) << strip.toStdString();
}

/// The NeoGS board (B3.1): the groups of the core's `neogs` board with the card's counters; GSCFG0, the windows and the
/// clock as the `gs` report has them
TEST_F(CardPanes_Test, TheNeoGSBoardShowsTheReport)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    BoardPane* board = panes[6].board;
    ASSERT_NE(board, nullptr);
    board->refresh(_context, false);
    EXPECT_TRUE(board->status().isEmpty()) << board->status().toStdString();
    const QStringList groups = board->groupTitles("neogs");
    for (const char* group : {"Config", "Paging", "Clock", "DAC", "Output", "Interrupts", "SPI", "Status", "LED", "Mailbox",
                              "Firmware", "Counters"})
        EXPECT_TRUE(groups.contains(group)) << group << " in " << groups.join(", ").toStdString();

    const StateNode gs = DeviceState::Gs(_context);
    const StateNode* neogs = gs.find("neogs");
    ASSERT_NE(neogs, nullptr);
    const int64_t gscfg0 = neogs->find("gscfg0")->i;
    EXPECT_TRUE(board->valueText("neogs", "gscfg0").startsWith(QStringLiteral("#%1").arg(gscfg0, 2, 16, QLatin1Char('0')).toUpper()))
        << board->valueText("neogs", "gscfg0").toStdString();
    EXPECT_EQ(board->valueText("neogs", "clock_hz").remove(','), QString::number(neogs->find("clock_hz")->i));
    const StateNode& windows = *neogs->find("windows");
    ASSERT_EQ(windows.items.size(), 4u);
    for (int w = 0; w < 4; w++)
    {
        const QString id = QStringLiteral("pages[%1]").arg(w);
        EXPECT_EQ(board->valueText("neogs", id),
                  QStringLiteral("#%1").arg(windows.items[static_cast<size_t>(w)].find("page")->i, 2, 16, QLatin1Char('0')).toUpper())
            << id.toStdString();
        EXPECT_EQ(board->valueText("neogs", QStringLiteral("window_flash[%1]").arg(w)),
                  windows.items[static_cast<size_t>(w)].find("flash")->b ? "yes" : "no");
    }
    EXPECT_FALSE(board->valueText("neogs", "card.clock_switches").isEmpty()) << "the clock switch count from the statistics";
    EXPECT_FALSE(board->valueText("neogs", "flash_title").isEmpty());

    // The GS stats pane carries the NeoGS counters (B2 stats tiles)
    ReportPane* stats = panes[9].pane;
    stats->refresh(_context, false);
    QTreeWidget* fields = stats->fields();
    EXPECT_FALSE(fields->findItems("dma", Qt::MatchExactly, 0).isEmpty());
    EXPECT_FALSE(fields->findItems("flash", Qt::MatchExactly, 0).isEmpty());
}

/// The DMA pane (B3.2): a row per module from the statistics block, the ZX-DMA detail from the board
TEST_F(CardPanes_Test, TheDmaPaneShowsTheModules)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    BoardPane* dma = panes[7].board;
    ASSERT_NE(dma, nullptr);
    dma->refresh(_context, false);
    EXPECT_TRUE(dma->status().isEmpty()) << dma->status().toStdString();
    QTableWidget* table = dma->table();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 3);
    EXPECT_EQ(table->item(0, 0)->text(), "ZX");
    EXPECT_EQ(table->item(1, 0)->text(), "SD");
    EXPECT_EQ(table->item(2, 0)->text(), "MP3");
    const StateNode gs = DeviceState::Gs(_context);
    const StateNode& zx = *gs.find("neogs")->find("dma")->find("zx");
    // Run (column 2), Address (3), Starts (5): the ZX module's row filled from the block
    EXPECT_EQ(table->item(0, 2)->text(), zx.find("running")->b ? "●" : "○");
    EXPECT_EQ(table->item(0, 3)->text(), QStringLiteral("#%1").arg(zx.find("address")->i & 0x1FFFFF, 6, 16, QLatin1Char('0')).toUpper());
    EXPECT_NE(table->item(0, 5)->text(), "–");
    EXPECT_EQ(dma->groupTitles("neogs_dma"), QStringList{"ZX-DMA"}) << "the module groups are the table's";
    EXPECT_EQ(dma->valueText("neogs_dma", "zx.mode"), QString::fromStdString(zx.find("mode")->s));
    EXPECT_EQ(dma->valueText("neogs_dma", "zx.read_latch"),
              QStringLiteral("#%1").arg(zx.find("read_latch")->i, 2, 16, QLatin1Char('0')).toUpper());
}

namespace
{
constexpr uint32_t kTraceCardBase = 0x013FF8;  // the block crosses the page boundary #014000
constexpr uint16_t kTraceHostCode = 0x8000;
constexpr uint16_t kTraceHostIdle = 0x8100;
constexpr uint16_t kTraceHostBuffer = 0x9000;

uint8_t TraceCardByte(int i)
{
    return static_cast<uint8_t>(i * 7 + 3);
}

/// The NeoGS's ZX module started in Divert mode at kTraceCardBase (gszxdmatrace_test.cpp's setup): the Spectrum's
/// reads at #0000-#3FFF come from card RAM
void StartZxDma(Emulator& emulator, EmulatorContext* context, SoundChip_NeoGS* card)
{
    const std::vector<uint8_t> program = {
        0xF3,                                             // DI
        0x3E, 0x01, 0xD3, 0x1B,                           // DMA_MOD = 1 (ZX)
        0x3E, uint8_t(kTraceCardBase >> 16), 0xD3, 0x1C,  // HAD
        0x3E, uint8_t(kTraceCardBase >> 8), 0xD3, 0x1D,   // MAD
        0x3E, uint8_t(kTraceCardBase), 0xD3, 0x1E,        // LAD
        0x3E, 0x80, 0xD3, 0x1F,                           // CST = #80: start
        0x18, 0xFE};                                      // JR $
    card->flash().load(program.data(), program.size());
    card->reset();
    for (int i = 0; i < 256; i++)
        card->memory().ram()[kTraceCardBase + i] = TraceCardByte(i);
    Z80* z80 = context->pCore->GetZ80();
    const uint8_t idle[] = {0xF3, 0x18, 0xFE};  // DI : JR $
    for (uint16_t i = 0; i < sizeof(idle); i++)
        z80->DirectWrite(static_cast<uint16_t>(kTraceHostIdle + i), idle[i]);
    z80->pc = kTraceHostIdle;
    z80->iff1 = z80->iff2 = 0;
    emulator.RunNFrames(2);
}

/// LDIR of `count` bytes from `from` (the window) to the host buffer, then JR $; run until the host parks there
void TraceLdir(Emulator& emulator, EmulatorContext* context, uint16_t from, uint16_t count)
{
    Z80* z80 = context->pCore->GetZ80();
    const uint8_t code[] = {0x21, uint8_t(from), uint8_t(from >> 8), 0x11, uint8_t(kTraceHostBuffer),
                            uint8_t(kTraceHostBuffer >> 8), 0x01, uint8_t(count), uint8_t(count >> 8), 0xED, 0xB0,
                            0x18, 0xFE};
    for (uint16_t i = 0; i < sizeof(code); i++)
        z80->DirectWrite(static_cast<uint16_t>(kTraceHostCode + i), code[i]);
    z80->pc = kTraceHostCode;
    for (int guard = 0; guard < 10 && z80->pc != kTraceHostCode + 11; guard++)
        emulator.RunNFrames(1);
    ASSERT_EQ(z80->pc, kTraceHostCode + 11);
    emulator.RunNFrames(1);
}
}  // namespace

/// The DMA pane's ZX-DMA trace (B3.2, Trace ▸): collapsed it reads nothing; expanded, a row a traced byte with the card
/// address, the value and the LDIR's PC; the next refresh appends only the new bytes; a dropped byte tinted
TEST_F(CardPanes_Test, TheDmaPaneTracesTheZxDmaBytes)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    auto* card = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
    ASSERT_NE(card, nullptr);
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    BoardPane* dma = panes[7].board;
    ZxDmaTraceView* trace = panes[7].trace;
    ASSERT_NE(dma, nullptr);
    ASSERT_NE(trace, nullptr);
    for (const CardPanes::Pane& pane : panes)
        if (&pane != &panes[7])
            EXPECT_EQ(pane.trace, nullptr) << pane.title.toStdString();

    StartZxDma(*_emulator, _context, card);
    ASSERT_EQ(card->zxDma().mode(), NeoGSZxDma::Mode::Divert);
    dma->refresh(_context, false);
    EXPECT_TRUE(trace->toggle()->isVisibleTo(dma)) << "Trace ▸ under the board";
    EXPECT_FALSE(trace->table()->isVisibleTo(dma)) << "collapsed";

    // Collapsed: no read; expanded: the how-to line while the trace does not capture
    trace->toggle()->click();
    EXPECT_TRUE(trace->expanded());
    EXPECT_TRUE(trace->table()->isVisibleTo(dma));
    EXPECT_EQ(trace->toggle()->text(), QString::fromUtf8("Trace ▾"));
    dma->refresh(_context, false);
    EXPECT_EQ(trace->rowCount(), 0);
    EXPECT_TRUE(trace->header().startsWith("Not capturing")) << trace->header().toStdString();
    EXPECT_TRUE(trace->header().contains("porttrace")) << "the report's how-to: " << trace->header().toStdString();
    EXPECT_TRUE(trace->startButton()->isEnabled());
    EXPECT_FALSE(trace->stopButton()->isEnabled());

    // Start through tracecontrol, as the planes do; N bytes read through the window: N rows
    trace->capture(_emulator.get(), "start");
    EXPECT_TRUE(card->isPortTraceCapturing());
    EXPECT_TRUE(trace->note().startsWith("Started")) << trace->note().toStdString();
    constexpr uint16_t n = 12;
    ASSERT_NO_FATAL_FAILURE(TraceLdir(*_emulator, _context, 0x0000, n));
    dma->refresh(_context, false);
    ASSERT_EQ(trace->rowCount(), n) << trace->header().toStdString();
    EXPECT_TRUE(trace->header().startsWith("Capturing")) << trace->header().toStdString();
    EXPECT_FALSE(trace->startButton()->isEnabled());
    EXPECT_TRUE(trace->stopButton()->isEnabled());
    for (int k = 0; k < n; k++)
    {
        const uint32_t address = kTraceCardBase + static_cast<uint32_t>(k);
        EXPECT_EQ(trace->cell(k, ZxDmaTraceView::CardAddress),
                  QStringLiteral("#%1 · page #%2")
                      .arg(QString::number(address, 16).toUpper().rightJustified(6, QLatin1Char('0')),
                           QString::number(address >> 14, 16).toUpper().rightJustified(2, QLatin1Char('0'))))
            << "row " << k;
        EXPECT_EQ(trace->cell(k, ZxDmaTraceView::Direction), "read");
        EXPECT_EQ(trace->cell(k, ZxDmaTraceView::HostPc), "#8009") << "the LDIR";
        if (k > 0)  // each read returns the byte the previous one fetched
            EXPECT_EQ(trace->cell(k, ZxDmaTraceView::Value),
                      QStringLiteral("#%1").arg(TraceCardByte(k - 1), 2, 16, QLatin1Char('0')).toUpper())
                << "row " << k;
    }

    // A second refresh after more bytes appends only the new ones; one with none adds nothing
    ASSERT_NO_FATAL_FAILURE(TraceLdir(*_emulator, _context, 0x0000 + n, 5));
    dma->refresh(_context, false);
    ASSERT_EQ(trace->rowCount(), n + 5);
    EXPECT_EQ(trace->cell(n, ZxDmaTraceView::CardAddress).left(7),
              QStringLiteral("#%1").arg(kTraceCardBase + n, 6, 16, QLatin1Char('0')).toUpper());
    dma->refresh(_context, false);
    EXPECT_EQ(trace->rowCount(), n + 5) << "a paused machine: the table stays";

    // A dropped byte is marked
    auto* neogs = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
    ASSERT_NE(neogs, nullptr);
    neogs->zxTraceForTest(true, 0x012345, 0x5A, 0, true);
    dma->refresh(_context, false);
    ASSERT_EQ(trace->rowCount(), n + 6);
    EXPECT_EQ(trace->cell(n + 5, ZxDmaTraceView::Direction), "write (dropped)");
    EXPECT_TRUE(trace->table()->model()->index(n + 5, 0).data(Qt::BackgroundRole).isValid()) << "tinted";
    EXPECT_FALSE(trace->table()->model()->index(0, 0).data(Qt::BackgroundRole).isValid());

    // Clear empties the ring and the table
    trace->capture(_emulator.get(), "clear");
    EXPECT_EQ(trace->rowCount(), 0);
    dma->refresh(_context, false);
    EXPECT_EQ(trace->rowCount(), 0);
    trace->capture(_emulator.get(), "stop");
    EXPECT_FALSE(card->isPortTraceCapturing());
}

/// The card address of a row in the card CPU's view: the window mapping that RAM page now, none for a page no window maps
TEST_F(CardPanes_Test, ACardAddressMapsToTheCardCpusWindow)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    auto* card = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
    ASSERT_NE(card, nullptr);
    const NeoGSMemory& memory = card->memory();
    bool mapped = false;
    for (int w = 0; w < 4 && !mapped; w++)
    {
        if (memory.isFlash(w))
            continue;
        const uint32_t cell = memory.physical(static_cast<uint16_t>(w * 0x4000)) + 0x123;
        const std::optional<uint16_t> at = CardPanes::CardCpuAddress(_context, cell);
        ASSERT_TRUE(at.has_value()) << "window " << w;
        EXPECT_EQ(memory.physical(*at), cell);
        mapped = true;
    }
    EXPECT_TRUE(mapped) << "a RAM window after reset";
    // A page no window maps
    std::vector<uint32_t> windows;
    for (int w = 0; w < 4; w++)
        if (!memory.isFlash(w))
            windows.push_back(memory.physical(static_cast<uint16_t>(w * 0x4000)));
    for (uint32_t page = 0; page < memory.ramSize(); page += 0x4000)
        if (std::find(windows.begin(), windows.end(), page) == windows.end())
        {
            EXPECT_FALSE(CardPanes::CardCpuAddress(_context, page + 1).has_value());
            break;
        }

    EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    _emulator.reset();
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    if (!CardPanes::NeoGSFitted(_context))
        EXPECT_FALSE(CardPanes::CardCpuAddress(_context, 0x1234).has_value()) << "the classic card has no ZX-DMA";
}

/// Without a NeoGS the DMA pane shows no board, so no Trace ▸; the report says why
TEST_F(CardPanes_Test, NoZxDmaTraceWithoutANeoGS)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    if (CardPanes::NeoGSFitted(_context))
        GTEST_SKIP() << "the config fits a NeoGS in the classic slot";
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    BoardPane* dma = panes[7].board;
    ZxDmaTraceView* trace = panes[7].trace;
    ASSERT_NE(trace, nullptr);
    dma->refresh(_context, false);
    EXPECT_FALSE(dma->status().isEmpty());
    EXPECT_FALSE(trace->toggle()->isVisibleTo(dma)) << "no board, no trace";
    EXPECT_FALSE(CardPanes::Shown(panes[7].card, _context)) << "the DMA tab itself is hidden";
    trace->toggle()->click();
    trace->refresh(_context, false);
    EXPECT_EQ(trace->rowCount(), 0);
    EXPECT_TRUE(trace->header().contains("NeoGS")) << trace->header().toStdString();
    EXPECT_FALSE(trace->startButton()->isEnabled());
}

/// The SD · MP3 · Flash pane (B3.3): three cards with their counters, side by side when wide, stacked when narrow; the
/// boards' actions as buttons, carried out by the core's board action
TEST_F(CardPanes_Test, TheMediaPaneShowsThreeCards)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    BoardPane* media = panes[8].board;
    ASSERT_NE(media, nullptr);
    media->resize(1200, 600);
    media->refresh(_context, false);
    EXPECT_TRUE(media->status().isEmpty()) << media->status().toStdString();
    const QStringList cards = media->blockTitles();
    ASSERT_EQ(cards.size(), 3) << cards.join(", ").toStdString();
    EXPECT_TRUE(cards[0].startsWith("NeoGS SD card")) << cards[0].toStdString();
    EXPECT_TRUE(cards[1].startsWith("NeoGS MP3 decoder")) << cards[1].toStdString();
    EXPECT_TRUE(cards[2].startsWith("NeoGS flash")) << cards[2].toStdString();
    EXPECT_FALSE(media->stacked());
    media->resize(300, 600);
    media->refresh(_context, false);
    EXPECT_TRUE(media->stacked());

    EXPECT_TRUE(media->groupTitles("neogs_sd").contains("Medium"));
    EXPECT_TRUE(media->groupTitles("neogs_sd").contains("Counters"));
    EXPECT_TRUE(media->groupTitles("neogs_flash").contains("Counters"));
    EXPECT_FALSE(media->valueText("neogs_flash", "title").isEmpty());
    EXPECT_FALSE(media->valueText("neogs_flash", "flash.bytes_programmed").isEmpty());
    EXPECT_FALSE(media->valueText("neogs_sd", "sd.blocks_read").isEmpty());
    EXPECT_NE(media->actionButton("neogs_sd", "insert"), nullptr);
    EXPECT_NE(media->actionButton("neogs_sd", "eject"), nullptr);
    EXPECT_NE(media->actionButton("neogs_flash", "save"), nullptr);

    const DebugBoardReply refused = media->runAction(_context, "neogs_sd", "insert", QString());
    EXPECT_EQ(refused.status, 400);
    EXPECT_TRUE(media->answer().contains("needs a path")) << media->answer().toStdString();
    const DebugBoardReply eject = media->runAction(_context, "neogs_sd", "eject", QString());
    EXPECT_EQ(eject.status, 200) << eject.text;
    EXPECT_TRUE(media->answer().startsWith("neogs_sd eject")) << media->answer().toStdString();
}

namespace
{
/// A ProTracker module of one pattern and one 64-byte looped sample (soundchip_gslw_test's, smaller): 2,172 bytes
std::vector<uint8_t> TinyModule()
{
    std::vector<uint8_t> m(1084 + 1024 + 64, 0x00);
    const char title[] = "card panes";
    std::copy(title, title + sizeof(title) - 1, m.begin());
    m[20 + 22] = 0;   // sample 1: 32 words (BE)
    m[20 + 23] = 32;
    m[20 + 25] = 63;  // volume
    m[20 + 28] = 0;   // loop length 32 words: the whole sample loops
    m[20 + 29] = 32;
    m[950] = 1;  // song length
    m[952] = 0;  // order: pattern 0
    m[1080] = 'M';
    m[1081] = '.';
    m[1082] = 'K';
    m[1083] = '.';
    m[1084] = 0x01;  // channel 0 row 0: sample 1, period 428
    m[1085] = 0xAC;
    m[1086] = 0x10;
    for (size_t i = 0; i < 64; i++)
        m[1084 + 1024 + i] = (i / 16) % 2 ? 0x30 : 0xB0;
    return m;
}
}  // namespace

/// The lightweight player (A10): no CPU, so no card CPU tab; the interpreter and player panes only with it
TEST_F(CardPanes_Test, TheLightweightPanesOnlyWithTheLightweightPlayer)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs-lw"}}));
    ASSERT_TRUE(CardPanes::LightweightFitted(_context));
    EXPECT_TRUE(CardPanes::Shown(CardPanes::Card::Lightweight, _context));
    EXPECT_TRUE(CardPanes::Shown(CardPanes::Card::Any, _context)) << "log, protocol, firmware, stats stay";
    EXPECT_FALSE(CardPanes::Shown(CardPanes::Card::NeoGS, _context));
    EXPECT_EQ(CardPanes::Front(_context), CardPanes::Target::Slot);
    GSCardTarget* target = _context->pDebugManager->GetCardTarget(DebugCpuId::GS);
    EXPECT_TRUE(!target || !target->Available()) << "no CPU: no card tab, no breakpoints or stepping";

    // The firmware objects answer from the player's mirror
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsAnalysisPane* objects = panes[4].analysis;
    objects->refresh(_context, false);
    EXPECT_TRUE(objects->status().isEmpty()) << objects->status().toStdString();
    EXPECT_GE(objects->table()->rowCount(), 1) << "the window variables";
    EXPECT_TRUE(objects->headline().contains("mirror")) << objects->headline().toStdString();
    // No firmware code: no dispatch table, and the tab says why
    GsAnalysisPane* dispatch = panes[12].analysis;
    dispatch->refresh(_context, false);
    EXPECT_TRUE(dispatch->status().contains("lightweight")) << dispatch->status().toStdString();
    EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    _emulator.reset();

    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    EXPECT_FALSE(CardPanes::LightweightFitted(_context));
    EXPECT_FALSE(CardPanes::Shown(CardPanes::Card::Lightweight, _context)) << "the classic card has its CPU's panes";
    GsLwPane interpreter(GsLwPane::View::Interpreter, &_parent);
    interpreter.refresh(_context, false);
    EXPECT_TRUE(interpreter.status().contains("lightweight")) << interpreter.status().toStdString();
}

/// A module upload in progress: the interpreter lights Upload and reads "Upload, N of M bytes"; once loaded the player
/// lists the module stopped ((S)) and its sample with the GS dialog's columns
TEST_F(CardPanes_Test, TheInterpreterShowsAnUploadAndThePlayerItsModule)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs-lw"}}));
    GeneralSoundCard* card = _context->pSoundManager->getGeneralSound();
    ASSERT_NE(card, nullptr);
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsLwPane* interpreter = panes[10].inspector;
    GsLwPane* player = panes[11].inspector;
    ASSERT_NE(interpreter, nullptr);
    ASSERT_NE(player, nullptr);

    interpreter->refresh(_context, false);
    EXPECT_TRUE(interpreter->status().isEmpty()) << interpreter->status().toStdString();
    EXPECT_EQ(interpreter->litState(), "idle");
    player->refresh(_context, false);
    EXPECT_EQ(player->headline(), "No module loaded");
    EXPECT_EQ(player->modules()->rowCount(), 0);
    EXPECT_EQ(player->channels()->rowCount(), 4);

    const std::vector<uint8_t> module = TinyModule();
    card->sendData(0x01);
    card->sendCommand(0x30);  // load module
    ASSERT_EQ(card->readData(), 1);
    for (size_t i = 0; i < 1500; i++)
        card->sendData(module[i]);
    interpreter->refresh(_context, false);
    EXPECT_EQ(interpreter->litState(), "upload");
    EXPECT_EQ(interpreter->headline(), "Upload, 1,500 of 2,172 bytes");
    EXPECT_TRUE(interpreter->field("Command").startsWith("#30")) << interpreter->field("Command").toStdString();
    EXPECT_EQ(interpreter->uploadBar()->maximum(), 2172);
    EXPECT_EQ(interpreter->uploadBar()->value(), 1500);
    EXPECT_EQ(interpreter->paramsBar()->maximum(), 16) << "the queues are of 16";
    EXPECT_EQ(interpreter->repliesBar()->maximum(), 16);

    for (size_t i = 1500; i < module.size(); i++)
        card->sendData(module[i]);
    card->sendCommand(0xD2);  // end of upload
    interpreter->refresh(_context, false);
    EXPECT_EQ(interpreter->litState(), "idle");
    player->refresh(_context, false);
    EXPECT_EQ(player->headline(), "Stopped");
    ASSERT_EQ(player->modules()->rowCount(), 1);
    EXPECT_EQ(player->modules()->item(0, 1)->text(), "(S)");
    EXPECT_EQ(player->modules()->item(0, 2)->text(), "card panes");
    ASSERT_EQ(player->samples()->rowCount(), 1);
    EXPECT_EQ(player->samples()->item(0, 0)->text(), "1");
    EXPECT_EQ(player->samples()->item(0, 2)->text(), "63");
    EXPECT_EQ(player->samples()->item(0, 6)->text(), "64");
    EXPECT_EQ(player->samples()->item(0, 8)->text(), "(L)");
    EXPECT_EQ(player->field("Position"), "0 of 1");
}

/// A ZX-MultiSound's General Sound (CPU `ms`): with its tab in front (or no card in the GS slot) the panes show its
/// reports: the `multisound` report's `gs`, the firmware and statistics of `cpu ms`, its command log
TEST_F(CardPanes_Test, TheMultiSoundsGsFillsThePanes)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "multisound"}}));
    ASSERT_TRUE(CardPanes::MultiSoundFitted(_context));
    EXPECT_TRUE(CardPanes::Fitted(_context));
    EXPECT_EQ(CardPanes::Front(_context, DebugCpuId::MultiSoundGS), CardPanes::Target::MultiSound);
    EXPECT_EQ(CardPanes::Front(_context, DebugCpuId::Main),
              _context->pSoundManager->getGeneralSound() ? CardPanes::Target::Slot : CardPanes::Target::MultiSound)
        << "the GS slot's card unless the slot is empty";
    EXPECT_TRUE(CardPanes::Shown(CardPanes::Card::NotNeoGS, _context, DebugCpuId::MultiSoundGS));
    EXPECT_FALSE(CardPanes::Shown(CardPanes::Card::NeoGS, _context, DebugCpuId::MultiSoundGS));
    EXPECT_FALSE(CardPanes::Shown(CardPanes::Card::Lightweight, _context, DebugCpuId::MultiSoundGS));
    const QString strip = CardPanes::StripText(_context, Emulator::DebugStop{}, DebugCpuId::MultiSoundGS);
    EXPECT_TRUE(strip.startsWith("GS Z80 16 MHz")) << strip.toStdString();

    const CardPanes::Follow follow = std::make_shared<DebugCpuId>(DebugCpuId::MultiSoundGS);
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent, follow);
    ReportPane* hardware = panes[5].pane;
    hardware->refresh(_context, false);
    ASSERT_TRUE(hardware->status().isEmpty()) << hardware->status().toStdString();
    QTreeWidgetItem* line = Field(hardware->fields(), "strip");
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(line->text(1).startsWith("GS Z80 16 MHz")) << line->text(1).toStdString();
    ASSERT_NE(hardware->table(), nullptr);
    EXPECT_GE(hardware->table()->rowCount(), 4) << "the DAC channels";

    ReportPane* protocol = panes[1].pane;
    protocol->refresh(_context, false);
    EXPECT_TRUE(protocol->status().isEmpty()) << protocol->status().toStdString();
    EXPECT_NE(Field(protocol->fields(), "protocol"), nullptr);

    GsAnalysisPane* objects = panes[4].analysis;
    objects->refresh(_context, false);
    ASSERT_TRUE(objects->status().isEmpty()) << objects->status().toStdString();
    EXPECT_TRUE(objects->headline().contains(" · cpu ms · ")) << objects->headline().toStdString();
    EXPECT_GT(objects->table()->rowCount(), 0) << "the window variables";

    ReportPane* stats = panes[9].pane;
    stats->refresh(_context, false);
    EXPECT_TRUE(stats->status().isEmpty()) << stats->status().toStdString();
    EXPECT_NE(Field(stats->fields(), "implementation"), nullptr);

    ReportPane* log = panes[0].pane;
    log->refresh(_context, false);
    EXPECT_TRUE(log->status().isEmpty()) << log->status().toStdString();
    QTreeWidgetItem* device = Field(log->fields(), "device");
    ASSERT_NE(device, nullptr);
    EXPECT_TRUE(device->text(1).endsWith(".gs.commands")) << device->text(1).toStdString();
    EXPECT_TRUE(device->text(1).startsWith("zxbus.")) << "the MultiSound's slot: " << device->text(1).toStdString();

    // The main CPU in front with a card in the GS slot: the panes show the slot's card again
    if (_context->pSoundManager->getGeneralSound())
    {
        *follow = DebugCpuId::Main;
        hardware->refresh(_context, false);
        QTreeWidgetItem* slotLine = Field(hardware->fields(), "strip");
        ASSERT_NE(slotLine, nullptr);
        EXPECT_TRUE(slotLine->text(1).startsWith("GS Z80 12 MHz")) << slotLine->text(1).toStdString();
    }
}

namespace
{
/// The `gs` report's `neogs.dma.zx` member `key` (an int; -1 when absent)
int64_t ZxDma(EmulatorContext* context, const char* key)
{
    const StateNode gs = DeviceState::Gs(context);
    const StateNode* neogs = gs.find("neogs");
    const StateNode* dma = neogs ? neogs->find("dma") : nullptr;
    const StateNode* zx = dma ? dma->find("zx") : nullptr;
    const StateNode* value = zx ? zx->find(key) : nullptr;
    return value && value->kind == StateNode::Kind::Int ? value->i : -1;
}

/// Qt events for up to `timeout` until `done` (the live line's timer runs on them)
template <typename Predicate>
bool PumpUntil(Predicate done, int timeoutMs = 1000)
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
}  // namespace

/// S4 (gui-card-debugger.md A6): the ZX-DMA banner over the main memory view and disassembly is the core's text while
/// the module diverts the reads (the disassembly's rows marked: ROM paged), hidden while it is off and on a card's tab;
/// reading it consumes no DMA byte
TEST_F(CardPanes_Test, TheZxDmaBannerShowsWhileTheModuleDiverts)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    auto* card = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
    ASSERT_NE(card, nullptr);
    QLabel strip(&_parent), position(&_parent), memory(&_parent), disasm(&_parent);
    const CardPanes::Lines lines{&strip, &position, &memory, &disasm};
    Emulator::DebugStop stop;

    // Off: no banner, the card line alone
    EXPECT_TRUE(CardPanes::ZxDmaBanner(_context).isEmpty());
    EXPECT_FALSE(CardPanes::FillLines(lines, _context, stop, DebugCpuId::Main, false));
    EXPECT_FALSE(memory.isVisibleTo(&_parent));
    EXPECT_FALSE(disasm.isVisibleTo(&_parent));
    EXPECT_FALSE(position.isVisibleTo(&_parent)) << "no card stop";
    EXPECT_TRUE(strip.isVisibleTo(&_parent)) << "the card line";

    StartZxDma(*_emulator, _context, card);
    ASSERT_EQ(card->zxDma().mode(), NeoGSZxDma::Mode::Divert);
    const QString core = QString::fromStdString(DeviceState::ZxDmaBanner(_context));
    ASSERT_TRUE(core.startsWith("ZX-DMA active: reads at #0000-#3FFF return card RAM @#")) << core.toStdString();
    const int64_t readsBefore = ZxDma(_context, "bytes_read");
    const int64_t nextBefore = ZxDma(_context, "next_byte");

    EXPECT_TRUE(CardPanes::FillLines(lines, _context, stop, DebugCpuId::Main, false)) << "Divert, ROM paged: the rows marked";
    EXPECT_TRUE(memory.isVisibleTo(&_parent));
    EXPECT_EQ(memory.text(), core);
    EXPECT_TRUE(disasm.isVisibleTo(&_parent));
    EXPECT_TRUE(disasm.text().startsWith(core)) << disasm.text().toStdString();
    EXPECT_TRUE(disasm.text().contains("come from the card at run time")) << disasm.text().toStdString();
    EXPECT_TRUE(strip.text().contains("ZX-DMA divert")) << strip.text().toStdString();
    EXPECT_EQ(ZxDma(_context, "bytes_read"), readsBefore) << "a peek: no DMA byte consumed";
    EXPECT_EQ(ZxDma(_context, "next_byte"), nextBefore);

    // Live (the machine runs): the same banner
    CardPanes::FillLiveLines(lines, _context, DebugCpuId::Main);
    EXPECT_EQ(memory.text(), core);

    // The card's tab in front: its memory shows, not the main CPU's
    EXPECT_FALSE(CardPanes::FillLines(lines, _context, stop, DebugCpuId::GS, false));
    EXPECT_FALSE(memory.isVisibleTo(&_parent));
    EXPECT_FALSE(disasm.isVisibleTo(&_parent));
}

/// S3 (gui-card-debugger.md A6): at a card's stop the strip on top of the main registers names the main CPU's
/// instruction, its T of the total with the ZX-DMA wait and the bus cycle; the card line then leaves the position out,
/// and keeps it with the card's tab in front (one view, once)
TEST_F(CardPanes_Test, TheMainStripShowsTheWaitOfTheInstruction)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    _context->pCore->GetZ80()->DirectWrite(0x8000, 0x7E);  // LD A,(HL)

    Emulator::DebugStop stop;
    stop.reason = Emulator::DebugStop::Reason::Breakpoint;
    stop.breakpoint.hit = true;
    stop.breakpoint.cpu = DebugCpuId::GS;
    MainPosition& main = stop.breakpoint.main;
    main.known = true;
    main.pc = 0x8000;
    main.tInInstruction = 9;
    main.waitT = 3;
    main.gapT = 2;
    main.timed = true;
    main.elapsedT = 6;
    main.totalT = 7;
    main.cycles.count = 2;
    main.cycles.total = 7;
    main.cycles.cycles[0] = {Z80BusCycle::OpcodeFetch, 4};
    main.cycles.cycles[1] = {Z80BusCycle::MemoryRead, 3};
    main.busCycle = 1;

    const QString text = CardPanes::MainPositionStrip(_context, stop);
    EXPECT_TRUE(text.startsWith("in #8000 ")) << text.toStdString();
    EXPECT_TRUE(text.contains("LD A,(HL)", Qt::CaseInsensitive)) << text.toStdString();
    EXPECT_TRUE(text.contains(" · T 6 of 7 + 3 T wait · memory read · 2 T past the card")) << text.toStdString();

    QLabel strip(&_parent), position(&_parent);
    const CardPanes::Lines lines{&strip, &position, nullptr, nullptr};
    CardPanes::FillLines(lines, _context, stop, DebugCpuId::Main, false);
    EXPECT_TRUE(position.isVisibleTo(&_parent));
    EXPECT_EQ(position.text(), text);
    EXPECT_TRUE(strip.text().contains("paused by the card (gs)")) << strip.text().toStdString();
    EXPECT_FALSE(strip.text().contains("main #8000")) << "not twice: " << strip.text().toStdString();

    // The card's tab in front: no main registers, the card line carries the position (MainPositionText)
    CardPanes::FillLines(lines, _context, stop, DebugCpuId::GS, false);
    EXPECT_FALSE(position.isVisibleTo(&_parent));
    EXPECT_TRUE(strip.text().contains("main #8000: T 6 of 7 + 3 T wait")) << strip.text().toStdString();

    // Running: no stop, no position
    CardPanes::FillLiveLines(lines, _context, DebugCpuId::Main);
    EXPECT_FALSE(position.isVisibleTo(&_parent));
    EXPECT_FALSE(strip.text().contains("paused")) << strip.text().toStdString();

    // A main stop, or a card's without the position: no strip
    Emulator::DebugStop mainStop;
    mainStop.reason = Emulator::DebugStop::Reason::Breakpoint;
    mainStop.breakpoint.main = main;
    EXPECT_TRUE(CardPanes::MainPositionStrip(_context, mainStop).isEmpty());
    stop.breakpoint.main.known = false;
    EXPECT_TRUE(CardPanes::MainPositionStrip(_context, stop).isEmpty());
}

/// The card line while the machine runs (P5.2 left): the live line reads it under a lease at its interval while the
/// window is shown and the machine runs; none while paused or hidden
TEST_F(CardPanes_Test, TheCardLineIsLiveWhileTheMachineRuns)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    QWidget window;
    QLabel strip(&window);
    QStringList seen;
    CardPanes::LiveLine live(
        &window, [this]() { return _emulator; },
        [&](EmulatorContext* context) {
            CardPanes::FillLiveLines({&strip, nullptr, nullptr, nullptr}, context, DebugCpuId::Main);
            seen << strip.text();
        },
        20);
    live.update();
    EXPECT_FALSE(live.active()) << "hidden";
    window.show();
    live.update();
    EXPECT_TRUE(live.active());

    // Not started: nothing read
    QElapsedTimer idle;
    idle.start();
    PumpUntil([&] { return idle.elapsed() >= 60; });
    EXPECT_EQ(live.reads(), 0);

    _emulator->EnableTurboMode();
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _context->emulatorState.frame_counter > 2; }));
    // Two reads that differ (the card's cycle moves on)
    EXPECT_TRUE(PumpUntil([&] { return seen.size() >= 2 && seen.back() != seen.front(); })) << seen.join(" | ").toStdString();
    EXPECT_TRUE(strip.isVisibleTo(&window));
    EXPECT_TRUE(strip.text().contains("PC #")) << strip.text().toStdString();

    // Paused: the stop's refresh fills it, the live line reads nothing
    _emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsEmulationParked(); }));
    const int reads = live.reads();
    QElapsedTimer paused;
    paused.start();
    PumpUntil([&] { return paused.elapsed() >= 60; });
    EXPECT_EQ(live.reads(), reads);

    window.hide();
    live.update();
    EXPECT_FALSE(live.active()) << "hidden: no timer";
    _emulator->Stop();
}

/// N8: the SD card's "Open media panel" asks the owner (no board action runs); the media panel selects slot sd.ngs
TEST_F(CardPanes_Test, TheSdCardOpensTheMediaPanelOnItsSlot)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    BoardPane* media = panes[8].board;
    ASSERT_NE(media, nullptr);
    media->refresh(_context, false);
    QToolButton* open = media->actionButton("neogs_sd", QString::fromLatin1(CardPanes::kOpenMediaPanel));
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->text(), "Open media panel");
    EXPECT_NE(media->actionButton("neogs_sd", "eject"), nullptr) << "beside the board's own actions";
    QStringList asked;
    QObject::connect(media, &BoardPane::uiActionRequested, media,
                     [&](const QString& board, const QString& id) { asked << board + '/' + id; });
    QStringList ran;
    QObject::connect(media, &BoardPane::actionRequested, media,
                     [&](const QString& board, const QString& id, const QString&) { ran << board + '/' + id; });
    open->click();
    EXPECT_EQ(asked, QStringList{"neogs_sd/open_media_panel"});
    EXPECT_TRUE(ran.isEmpty());
    media->refresh(_context, false);
    EXPECT_NE(media->actionButton("neogs_sd", QString::fromLatin1(CardPanes::kOpenMediaPanel)), nullptr) << "kept";

    EmulatorBinding binding;
    binding.bind(_emulator);
    {
        MediaPanelWindow window;
        window.setBinding(&binding);
        EXPECT_TRUE(window.focusSlot(CardPanes::kSdSlot));
        EXPECT_EQ(window.selectedSlot(), CardPanes::kSdSlot);
        EXPECT_FALSE(window.focusSlot("no.such"));
    }
    binding.unbind();
}

/// P5.6 (gui-card-debugger.md A5.3): the GS monitor tab. The command log off: it says so and Turn on asks the owner, who
/// turns the history to ring; the host's data then command show as rows, the strip says the card has not taken it yet
TEST_F(CardPanes_Test, TheMonitorTurnsTheLogOnAndShowsTheConversation)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsMonitorPane* monitor = panes[2].monitor;
    ASSERT_NE(monitor, nullptr);
    EXPECT_EQ(panes[2].title, "GS monitor");
    EXPECT_TRUE(CardPanes::Shown(panes[2].card, _context));

    monitor->refresh(_context, false);
    EXPECT_TRUE(monitor->status().isEmpty()) << monitor->status().toStdString();
    ASSERT_TRUE(monitor->logOff()) << "off by default: zero cost";
    EXPECT_TRUE(monitor->turnOnButton()->isVisibleTo(monitor));
    EXPECT_TRUE(monitor->stripText().contains("command log is off")) << monitor->stripText().toStdString();
    int asked = 0;
    QObject::connect(monitor, &GsMonitorPane::turnOnRequested, monitor, [&]() { asked++; });
    monitor->turnOnButton()->click();
    EXPECT_EQ(asked, 1) << "the owner turns it on under its lease";
    QString error;
    ASSERT_TRUE(monitor->turnOn(_context, &error)) << error.toStdString();
    monitor->refresh(_context, false);
    EXPECT_FALSE(monitor->logOff());
    EXPECT_FALSE(monitor->turnOnButton()->isVisibleTo(monitor));
    EXPECT_EQ(monitor->table()->rowCount(), 0);

    _context->pPortDecoder->DecodePortOut(0x00B3, 0x00, 0);  // data
    _context->pPortDecoder->DecodePortOut(0x00BB, 0x23, 0);  // #23: Get the RAM's page count
    monitor->refresh(_context, false);
    ASSERT_GE(monitor->table()->rowCount(), 2) << "the data and the command";
    bool command = false;
    for (int row = 0; row < monitor->table()->rowCount(); row++)
        command = command || monitor->table()->item(row, 3)->text() == "#23";
    EXPECT_TRUE(command) << "the command in hex";
    const QString strip = monitor->stripText();
    EXPECT_TRUE(strip.contains("not taken yet")) << strip.toStdString();
    EXPECT_TRUE(strip.contains("status #")) << strip.toStdString();
    // #23 takes no data: the byte before it is the log's anomaly, counted in the tile
    EXPECT_TRUE(monitor->tileTinted());
    EXPECT_TRUE(monitor->tileText().startsWith("1 anomalies")) << monitor->tileText().toStdString();
    EXPECT_TRUE(monitor->tileText().contains("data_before_command")) << monitor->tileText().toStdString();

    // Read again: nothing appended twice
    const int rows = monitor->table()->rowCount();
    monitor->refresh(_context, false);
    EXPECT_EQ(monitor->table()->rowCount(), rows);

    // Pause / Continue: the window's actions, by the machine's state
    int pauses = 0, continues = 0;
    QObject::connect(monitor, &GsMonitorPane::pauseRequested, monitor, [&]() { pauses++; });
    QObject::connect(monitor, &GsMonitorPane::continueRequested, monitor, [&]() { continues++; });
    EXPECT_EQ(monitor->runButton()->text(), "Continue");
    monitor->runButton()->click();
    monitor->refresh(_context, true);
    EXPECT_EQ(monitor->runButton()->text(), "Pause");
    monitor->runButton()->click();
    EXPECT_EQ(continues, 1);
    EXPECT_EQ(pauses, 1);
}

/// The error tile counts the anomalies since its Clear, names the last one with its frame and is tinted; Clear zeroes it
TEST_F(CardPanes_Test, TheMonitorTileCountsAnomaliesUntilClear)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    GsMonitorPane monitor;
    ASSERT_TRUE(monitor.turnOn(_context));
    // Two commands while the card (not running) has not taken the first: the second overwrites it
    _context->pPortDecoder->DecodePortOut(0x00BB, 0x23, 0);
    _context->pPortDecoder->DecodePortOut(0x00BB, 0x23, 0);
    monitor.refresh(_context, false);
    ASSERT_EQ(monitor.anomalies(), 1) << monitor.tileText().toStdString();
    EXPECT_TRUE(monitor.tileTinted());
    EXPECT_TRUE(monitor.tileText().startsWith("1 anomalies")) << monitor.tileText().toStdString();
    EXPECT_TRUE(monitor.tileText().contains("command_overwritten") || monitor.tileText().contains("overwr"))
        << monitor.tileText().toStdString();
    EXPECT_TRUE(monitor.tileText().contains("(frame ")) << monitor.tileText().toStdString();
    bool tinted = false;
    for (int row = 0; row < monitor.table()->rowCount(); row++)
        if (monitor.table()->item(row, 2)->text() == "anomaly")
            tinted = monitor.table()->item(row, 0)->background().style() != Qt::NoBrush;
    EXPECT_TRUE(tinted) << "the anomaly's row";

    monitor.clearButton()->click();
    EXPECT_EQ(monitor.anomalies(), 0);
    EXPECT_FALSE(monitor.tileTinted());
    monitor.refresh(_context, false);
    EXPECT_EQ(monitor.anomalies(), 0) << "read again: the same anomaly is not counted twice";
    EXPECT_GE(monitor.table()->rowCount(), 3) << "Clear keeps the log";

    _context->pPortDecoder->DecodePortOut(0x00BB, 0x23, 0);
    monitor.refresh(_context, false);
    EXPECT_EQ(monitor.anomalies(), 1) << "a new one counts";
}

/// The log reads after its cursor: more than one read's worth of records between two refreshes are all shown, in order;
/// records a small ring evicted before the refresh read them are counted as missed in the rows line
TEST_F(CardPanes_Test, TheMonitorLosesNoRecordBetweenRefreshes)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    GsMonitorPane monitor;
    ASSERT_TRUE(monitor.turnOn(_context));
    monitor.refresh(_context, false);
    ASSERT_EQ(monitor.table()->rowCount(), 0);
    const std::string device = GsMonitorPane::HistoryDevice(_context, DebugCpuId::Main);
    const auto written = [&]() {
        HistoryRequest read;
        read.verb = "read";
        read.device = device;
        read.last = 1;
        return HistoryControl::Execute(_context, read).body.find("written")->i;
    };

    // Commands the card (not running) does not take: each one a row, most with an anomaly row of its own
    for (int i = 0; i < 160; i++)
        _context->pPortDecoder->DecodePortOut(0x00BB, 0x23, 0);
    const int64_t total = written();
    ASSERT_GT(total, static_cast<int64_t>(GsMonitorPane::kReadRows)) << "more than one read";
    ASSERT_LT(total, GsMonitorPane::kMaxRows);
    monitor.refresh(_context, false);
    EXPECT_EQ(monitor.table()->rowCount(), total) << "every record since the last refresh";
    EXPECT_EQ(monitor.missed(), 0);
    EXPECT_FALSE(monitor.rowsText().contains("missed")) << monitor.rowsText().toStdString();
    monitor.refresh(_context, false);
    EXPECT_EQ(monitor.table()->rowCount(), total) << "read again: nothing twice";

    // A ring of 50: configuring restarts the log, then 50 of the next records are kept and the rest evicted unread
    HistoryRequest ring;
    ring.verb = "config";
    ring.device = device;
    ring.mode = "ring";
    ring.limit = 50;
    ASSERT_EQ(HistoryControl::Execute(_context, ring).httpStatus, 200);
    for (int i = 0; i < 60; i++)
        _context->pPortDecoder->DecodePortOut(0x00BB, 0x23, 0);
    const int64_t after = written();
    ASSERT_GT(after, 50);
    monitor.refresh(_context, false);
    EXPECT_EQ(monitor.table()->rowCount(), 50) << "restarted: the ring's records only";
    EXPECT_EQ(monitor.missed(), after - 50);
    EXPECT_TRUE(monitor.rowsText().contains(QString("%1 missed").arg(after - 50))) << monitor.rowsText().toStdString();
}

/// A5.4: a log row's actions. Rows before any recording (an upload of 3 bytes, #23): the menu offers by row (the
/// command's breaks on #23, data byte N only on the upload, no Go to without a recording); its actions make the
/// managers' own breakpoints (host_command #23 on main, card_command #23 on the GS CPU, host_data hit 2 on main), an
/// equal one taken instead of a second; Copy puts the selected rows on the clipboard. Then a host program writes three
/// data bytes and #23 while a recording runs: the run stops at the second data byte, at #23 sent, at #23 taken by the
/// firmware; the program's #23 row is recorded: Go to this moment seeks there. Runtime: the card's boot, a few frames
/// recorded
TEST_F(CardPanes_Test, TheMonitorRowActionsBreakGoAndCopy)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    GsMonitorPane monitor;
    ASSERT_TRUE(monitor.turnOn(_context));
    PortDecoder* ports = _context->pPortDecoder;
    ports->DecodePortOut(0x00BB, 0x30, 0);  // load module: an upload
    for (uint8_t b : {0x01, 0x02, 0x03})
        ports->DecodePortOut(0x00B3, b, 0);
    ports->DecodePortOut(0x00BB, 0xD2, 0);  // its end
    ports->DecodePortOut(0x00BB, 0x23, 0);
    _emulator->RunNFrames(1);  // the recording below starts in a later frame than these rows
    monitor.refresh(_context, false);
    const auto findRow = [&](const char* kind, int command, int from = 0) {
        for (int row = from; row < monitor.table()->rowCount(); row++)
        {
            const GsMonitorPane::LogRow r = monitor.logRow(row);
            if (r.kind == kind && (command < 0 || r.command == command))
                return row;
        }
        return -1;
    };
    const int commandRow = findRow("command", 0x23);
    const int blockRow = findRow("block", -1);
    ASSERT_GE(commandRow, 0) << monitor.copyText(-1).toStdString();
    ASSERT_GE(blockRow, 0) << "the upload's block";
    ASSERT_TRUE(monitor.logRow(blockRow).upload);
    EXPECT_EQ(monitor.logRow(blockRow).bytes, 3);

    // The menus, by row
    const auto action = [](QMenu* menu, const char* name) { return menu->findChild<QAction*>(QString::fromLatin1(name)); };
    std::unique_ptr<QMenu> commandMenu(monitor.rowMenu(commandRow));
    std::unique_ptr<QMenu> blockMenu(monitor.rowMenu(blockRow));
    for (const char* name : {"gsLogBreakHost", "gsLogBreakCard", "gsLogBreakByte", "gsLogGoTo", "gsLogCopy"})
        ASSERT_NE(action(commandMenu.get(), name), nullptr) << name;
    EXPECT_EQ(action(commandMenu.get(), "gsLogBreakHost")->text(), "Break when the game sends #23");
    EXPECT_TRUE(action(commandMenu.get(), "gsLogBreakHost")->isEnabled());
    EXPECT_TRUE(action(commandMenu.get(), "gsLogBreakCard")->isEnabled());
    EXPECT_FALSE(action(commandMenu.get(), "gsLogBreakByte")->isEnabled()) << "not an upload";
    EXPECT_FALSE(action(commandMenu.get(), "gsLogGoTo")->isEnabled()) << "no recording";
    EXPECT_TRUE(action(commandMenu.get(), "gsLogGoTo")->toolTip().contains("No time-travel recording"))
        << action(commandMenu.get(), "gsLogGoTo")->toolTip().toStdString();
    EXPECT_TRUE(action(commandMenu.get(), "gsLogCopy")->isEnabled());
    EXPECT_TRUE(action(blockMenu.get(), "gsLogBreakByte")->isEnabled());

    // Triggered as the menu does: the owner runs them on its context
    QString said;
    QObject::connect(&monitor, &GsMonitorPane::rowActionRequested, &monitor, [&](int a, int row, quint32 byte) {
        monitor.runRowAction(_context, static_cast<GsMonitorPane::RowAction>(a), row, byte, &said);
    });
    int askedMax = 0;
    monitor.askByte = [&](int max, int& out) {
        askedMax = max;
        out = 2;
        return true;
    };
    const auto events = [&](DebugCpuId cpu, GSCardEvent kind) {
        std::vector<BreakpointSnapshot> found;
        for (const auto& [id, d] : _context->pDebugManager->GetBreakpointsManager(cpu)->GetAllBreakpoints())
            if (d->type == BRK_EVENT && d->z80address == static_cast<uint16_t>(kind))
                found.push_back(d);
        return found;
    };
    action(commandMenu.get(), "gsLogBreakHost")->trigger();
    std::vector<BreakpointSnapshot> host = events(DebugCpuId::Main, GSCardEvent::HostCommandWrite);
    ASSERT_EQ(host.size(), 1u) << said.toStdString();
    EXPECT_EQ(host[0]->nrValue, 0x23);
    EXPECT_EQ(host[0]->nrMask, 0xFF);
    EXPECT_TRUE(BreakpointManager::IsUserBreakpoint(*host[0])) << "the list and the editor show it";
    EXPECT_TRUE(said.contains("the host sends command #23")) << said.toStdString();
    action(commandMenu.get(), "gsLogBreakHost")->trigger();
    EXPECT_EQ(events(DebugCpuId::Main, GSCardEvent::HostCommandWrite).size(), 1u) << "the same one, not a second";
    EXPECT_TRUE(said.startsWith("Already there")) << said.toStdString();
    EXPECT_EQ(monitor.actionText(), said);

    action(commandMenu.get(), "gsLogBreakCard")->trigger();
    const std::vector<BreakpointSnapshot> card = events(DebugCpuId::GS, GSCardEvent::CardCommandRead);
    ASSERT_EQ(card.size(), 1u) << said.toStdString();
    EXPECT_EQ(card[0]->nrValue, 0x23);
    EXPECT_EQ(card[0]->nrMask, 0xFF);
    EXPECT_TRUE(events(DebugCpuId::Main, GSCardEvent::CardCommandRead).empty());

    action(blockMenu.get(), "gsLogBreakByte")->trigger();
    EXPECT_EQ(askedMax, 3) << "N up to the upload's bytes";
    const std::vector<BreakpointSnapshot> data = events(DebugCpuId::Main, GSCardEvent::HostDataWrite);
    ASSERT_EQ(data.size(), 1u) << said.toStdString();
    EXPECT_EQ(data[0]->nrMask, 0) << "any byte";
    EXPECT_EQ(data[0]->hitMode, BRK_HIT_EQUAL);
    EXPECT_EQ(data[0]->hitTarget, 2u);

    // Copy: the selected rows, a line each
    QItemSelectionModel* selection = monitor.table()->selectionModel();
    selection->select(monitor.table()->model()->index(blockRow, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->select(monitor.table()->model()->index(commandRow, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    action(commandMenu.get(), "gsLogCopy")->trigger();
    const QStringList copied = QApplication::clipboard()->text().split('\n');
    ASSERT_EQ(copied.size(), 2) << QApplication::clipboard()->text().toStdString();
    EXPECT_TRUE(copied[0].contains("\tblock\t")) << copied[0].toStdString();
    EXPECT_TRUE(copied[1].contains("\tcommand\t#23\t")) << copied[1].toStdString();

    // The run, recorded: DI : LD BC,#00B3 : LD A,#11 : OUT (C),A : LD A,#22 : OUT (C),A : LD A,#33 : OUT (C),A :
    // LD C,#BB : LD A,#23 : OUT (C),A : JR $ (no reply polling: each read is a record, the ring would lose the rows)
    const uint8_t program[] = {0xF3, 0x01, 0xB3, 0x00, 0x3E, 0x11, 0xED, 0x79, 0x3E, 0x22, 0xED, 0x79, 0x3E, 0x33,
                               0xED, 0x79, 0x0E, 0xBB, 0x3E, 0x23, 0xED, 0x79, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(program); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;
    ttd::TimeTravelController* controller = _context->pTimeTravelController;
    ASSERT_NE(controller, nullptr);
    ASSERT_TRUE(controller->StartRecording());
    _emulator->EnableTurboMode();
    const auto stopAt = [&](uint16_t id, DebugCpuId cpu) {
        const uint64_t serial = _emulator->LastStop().serial;
        if (_emulator->IsRunning())
            _emulator->Resume();
        else
            _emulator->StartAsync();
        const bool stopped = TestWait::For(
            [&] { return _emulator->IsPaused() && _emulator->LastStop().serial != serial; }, std::chrono::milliseconds(3000));
        const Emulator::DebugStop stop = _emulator->LastStop();
        return stopped && stop.reason == Emulator::DebugStop::Reason::Breakpoint && stop.breakpoint.breakpointId == id &&
               stop.breakpoint.cpu == cpu;
    };
    EXPECT_TRUE(stopAt(data[0]->breakpointID, DebugCpuId::Main)) << "the second data byte";
    EXPECT_EQ(_context->pCore->GetZ80()->pc, 0x800C) << "after OUT #22";
    EXPECT_TRUE(stopAt(host[0]->breakpointID, DebugCpuId::Main)) << "#23 sent";
    EXPECT_EQ(_context->pCore->GetZ80()->pc, 0x8016) << "after OUT #23";
    EXPECT_TRUE(stopAt(card[0]->breakpointID, DebugCpuId::GS)) << "#23 taken by the firmware";
    controller->StopRecording();

    // The program's #23: recorded, Go to seeks to its moment
    const int64_t beforeFrame = monitor.logRow(commandRow).frame;
    monitor.refresh(_context, false);
    int sentRow = -1, oldRow = -1;
    for (int row = 0; row < monitor.table()->rowCount(); row++)
    {
        const GsMonitorPane::LogRow r = monitor.logRow(row);
        if (r.kind == "command" && r.command == 0x23)
            (r.frame == beforeFrame && oldRow < 0 ? oldRow : sentRow) = row;
    }
    ASSERT_GE(sentRow, 0) << monitor.rowsText().toStdString();
    const GsMonitorPane::LogRow sent = monitor.logRow(sentRow);
    std::unique_ptr<QMenu> sentMenu(monitor.rowMenu(sentRow));
    ASSERT_TRUE(action(sentMenu.get(), "gsLogGoTo")->isEnabled()) << action(sentMenu.get(), "gsLogGoTo")->toolTip().toStdString();
    if (oldRow >= 0)  // still in the log (the recording may start the history afresh)
    {
        std::unique_ptr<QMenu> oldMenu(monitor.rowMenu(oldRow));
        EXPECT_FALSE(action(oldMenu.get(), "gsLogGoTo")->isEnabled()) << "before the recording";
        EXPECT_TRUE(action(oldMenu.get(), "gsLogGoTo")->toolTip().contains("not in it"))
            << action(oldMenu.get(), "gsLogGoTo")->toolTip().toStdString();
    }
    action(sentMenu.get(), "gsLogGoTo")->trigger();
    EXPECT_TRUE(said.startsWith("At frame")) << said.toStdString();
    const ttd::TTDTimePoint at = controller->CurrentPosition();
    EXPECT_EQ(at.frame, static_cast<uint64_t>(sent.frame));
    EXPECT_GE(static_cast<int64_t>(at.tInFrame), sent.t);
    EXPECT_LT(static_cast<int64_t>(at.tInFrame), sent.t + 24) << "the instruction boundary at or after the write";
    _emulator->Stop();
}

/// The log follows its tail while the machine runs (the debugger's sends, rows appended at each refresh); scrolling up
/// stops following, Follow resumes it
TEST_F(CardPanes_Test, TheMonitorFollowsTheLogWhileRunning)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    QWidget window;
    GsMonitorPane* monitor = new GsMonitorPane({}, &window);
    window.resize(420, 260);
    monitor->resize(420, 260);
    window.show();
    ASSERT_TRUE(monitor->turnOn(_context));
    _emulator->EnableTurboMode();
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _context->emulatorState.frame_counter > 2; }));

    const auto refresh = [&]() {
        Emulator::ContextLease lease = _emulator->LeaseContext();
        ASSERT_TRUE(lease);
        monitor->refresh(lease.get(), true);
    };
    QScrollBar* bar = monitor->table()->verticalScrollBar();
    // Sends until the log is taller than its view, the view at its end. The loops end at their condition; the 5 s
    // bound only covers a loaded host
    QElapsedTimer clock;
    clock.start();
    while ((monitor->table()->rowCount() < 40 || bar->maximum() == 0) && clock.elapsed() < 5000)
    {
        GSSendRequest send;
        send.command = 0x23;
        GSSend(_context, send, 0);
        PumpUntil([] { return false; }, 10);  // the frames run the send
        refresh();
    }
    ASSERT_GT(bar->maximum(), 0) << monitor->table()->rowCount() << " rows";
    EXPECT_TRUE(monitor->following());
    EXPECT_EQ(bar->value(), bar->maximum()) << "at the tail";

    // Scrolled up: rows still come, the view stays
    bar->setValue(0);
    EXPECT_FALSE(monitor->following());
    EXPECT_FALSE(monitor->followButton()->isChecked());
    const int rows = monitor->table()->rowCount();
    clock.restart();
    while (monitor->table()->rowCount() == rows && clock.elapsed() < 5000)
    {
        GSSendRequest send;
        send.command = 0x23;
        GSSend(_context, send, 0);
        PumpUntil([] { return false; }, 10);  // the frames run the send
        refresh();
    }
    EXPECT_GT(monitor->table()->rowCount(), rows) << "rows appended while running";
    EXPECT_EQ(bar->value(), 0) << "not following: the view stays";

    monitor->followButton()->click();
    EXPECT_TRUE(monitor->following());
    EXPECT_EQ(bar->value(), bar->maximum());
    _emulator->Stop();
}

/// A5.2a Dispatch (M4): the command table as the card holds it, a row per command with its handler and the handler's
/// label (the card CPU's firmware symbols), the baseline; the profile stack above it. The classic card and a NeoGS
TEST_F(CardPanes_Test, TheDispatchShowsTheHandlersWithTheirLabels)
{
    for (const char* kind : {"gs", "neogs"})
    {
        ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", kind}}));
        _emulator->RunNFrames(3);  // the firmware boots (a NeoGS dispatches through its RAM copy of the flash)
        const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
        GsAnalysisPane* dispatch = panes[12].analysis;
        ASSERT_NE(dispatch, nullptr);
        EXPECT_TRUE(CardPanes::Shown(panes[12].card, _context));
        dispatch->refresh(_context, false);
        ASSERT_TRUE(dispatch->status().isEmpty()) << kind << ": " << dispatch->status().toStdString();
        const GSFirmwareProfile* rom = GSFirmwareProfileOf(*_context->pSoundManager->getGeneralSound());
        ASSERT_NE(rom, nullptr);
        EXPECT_TRUE(dispatch->headline().startsWith(QStringLiteral("Profiles: ") + QString::fromLatin1(rom->id)))
            << dispatch->headline().toStdString();
        EXPECT_TRUE(dispatch->headline().contains(" patched · ")) << dispatch->headline().toStdString();
        ASSERT_EQ(dispatch->table()->rowCount(), 256) << kind;
        const int row = dispatch->findRow("Cmd", "#23");
        ASSERT_GE(row, 0);
        EXPECT_FALSE(dispatch->cell(row, "Name").isEmpty()) << "the profile names #23";
        EXPECT_TRUE(dispatch->cell(row, "Handler").startsWith('#')) << dispatch->cell(row, "Handler").toStdString();
        // The handlers carry the firmware's labels, all 256 on both cards: the core reads the tables where the
        // firmware keeps them (the ROM; a NeoGS's RAM copy), whatever window 3 maps now, against the firmware as it
        // started (a NeoGS's copy at its start): the stock firmware has nothing patched
        int labelled = 0;
        for (int r = 0; r < dispatch->table()->rowCount(); r++)
            labelled += dispatch->cell(r, "Label").isEmpty() ? 0 : 1;
        EXPECT_EQ(labelled, 256) << kind << ": " << dispatch->headline().toStdString();
        EXPECT_FALSE(dispatch->cell(row, "Baseline").isEmpty()) << kind << ": the stock split's baseline";
        EXPECT_EQ(dispatch->cell(row, "Baseline"), dispatch->cell(row, "Handler")) << kind;
        EXPECT_TRUE(dispatch->headline().contains(" · 0 patched · ")) << kind << ": " << dispatch->headline().toStdString();
        const int reset = dispatch->findRow("Cmd", "#00");
        ASSERT_GE(reset, 0);
        EXPECT_FALSE(dispatch->cell(reset, "Label").isEmpty()) << kind;
        EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        _emulator.reset();
    }
}

/// A5.2a Unknown (AU6): a command the firmware's profile does not name, sent twice, is listed with its count; Clear
/// empties the view (the log keeps its own counts), the next one counts from there
TEST_F(CardPanes_Test, TheUnknownTabCountsUntilClear)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsAnalysisPane* unknown = panes[13].analysis;
    ASSERT_NE(unknown, nullptr);
    unknown->refresh(_context, false);
    ASSERT_TRUE(unknown->status().isEmpty()) << unknown->status().toStdString();
    EXPECT_TRUE(unknown->turnOnButton()->isVisibleTo(unknown)) << "the command log is off by default";
    ASSERT_TRUE(unknown->turnOn(_context));
    unknown->refresh(_context, false);
    EXPECT_FALSE(unknown->turnOnButton()->isVisibleTo(unknown));
    EXPECT_EQ(unknown->table()->rowCount(), 0);

    const GSFirmwareProfile* profile = GSFirmwareProfileOf(*_context->pSoundManager->getGeneralSound());
    ASSERT_NE(profile, nullptr);
    int code = -1;
    for (int c = 0x50; c < 0xF0 && code < 0; c++)
        if (!profile->Command(static_cast<uint8_t>(c)).name)
            code = c;
    ASSERT_GE(code, 0) << "a command the profile does not name";
    const QString hex = QStringLiteral("#%1").arg(code, 2, 16, QLatin1Char('0')).toUpper();
    _context->pPortDecoder->DecodePortOut(0x00BB, static_cast<uint8_t>(code), 0);
    _context->pPortDecoder->DecodePortOut(0x00BB, static_cast<uint8_t>(code), 0);
    unknown->refresh(_context, false);
    const int row = unknown->findRow("Cmd", hex);
    ASSERT_GE(row, 0) << hex.toStdString() << " listed";
    EXPECT_EQ(unknown->cell(row, "Since Clear"), "2");
    EXPECT_EQ(unknown->cell(row, "Total"), "2");
    EXPECT_TRUE(unknown->cell(row, "Handler").startsWith('#')) << "the stub it lands in";
    EXPECT_TRUE(unknown->headline().startsWith("2 unknown commands")) << unknown->headline().toStdString();

    unknown->clearButton()->click();
    EXPECT_EQ(unknown->table()->rowCount(), 0);
    unknown->refresh(_context, false);
    EXPECT_EQ(unknown->table()->rowCount(), 0) << "read again: nothing since Clear";

    _context->pPortDecoder->DecodePortOut(0x00BB, static_cast<uint8_t>(code), 0);
    unknown->refresh(_context, false);
    ASSERT_EQ(unknown->table()->rowCount(), 1);
    EXPECT_EQ(unknown->cell(0, "Since Clear"), "1");
    EXPECT_EQ(unknown->cell(0, "Total"), "3");
}

/// A5.2a Objects (M3): the active profile's window variables; a profile with a struct (NeoTracker's `smp`) shows a
/// table with the struct's fields as columns and a row per instance
TEST_F(CardPanes_Test, TheObjectsTabShowsAStructTable)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsAnalysisPane* objects = panes[4].analysis;
    ASSERT_NE(objects, nullptr);
    objects->refresh(_context, false);
    ASSERT_TRUE(objects->status().isEmpty()) << objects->status().toStdString();
    EXPECT_TRUE(objects->headline().contains(" · cpu gs · ")) << objects->headline().toStdString();
    EXPECT_GT(objects->table()->rowCount(), 0) << "the window variables";
    EXPECT_GT(objects->profiles()->count(), 1) << "the profiles to pick";

    const GSFirmwareProfile* tracker = FindGSFirmwareProfileById("nedoos-neotracker");
    ASSERT_NE(tracker, nullptr) << "the shipped profile (symbols/gs/profiles)";
    ASSERT_FALSE(tracker->instances.empty());
    objects->selectProfile("nedoos-neotracker");
    objects->refresh(_context, false);
    ASSERT_TRUE(objects->status().isEmpty()) << objects->status().toStdString();
    EXPECT_TRUE(objects->headline().startsWith("nedoos-neotracker")) << objects->headline().toStdString();
    EXPECT_EQ(objects->objects()->currentData().toString().toStdString(), tracker->instances.front().name)
        << "the first instance shown";
    EXPECT_EQ(objects->table()->rowCount(), tracker->instances.front().count) << "a row per instance";
    const GSStructInfo* smp = tracker->Struct(tracker->instances.front().structName);
    ASSERT_NE(smp, nullptr);
    ASSERT_EQ(objects->table()->columnCount(), static_cast<int>(smp->fields.size()) + 2) << "#, At, the fields";
    EXPECT_EQ(objects->table()->horizontalHeaderItem(2)->text().toStdString(), smp->fields.front().name);
    EXPECT_EQ(objects->cell(1, "#"), "1");
    EXPECT_FALSE(objects->cell(1, QString::fromStdString(smp->fields.front().name)).isEmpty()) << "a value read";

    // Back to the active profile: its fixed window's variables
    objects->selectProfile("");
    objects->selectObject(":window");
    objects->refresh(_context, false);
    EXPECT_GT(objects->table()->rowCount(), 0);
    EXPECT_EQ(objects->cell(0, "Name").isEmpty(), false);
}

/// A5.2a Writers (X7): the host's #B3 / #BB writes with the main CPU's PC and frame (the command log), #33 from the
/// port trace; the card's own port writes with its PC once the firmware runs
TEST_F(CardPanes_Test, TheWritersTabShowsTheHostsWritesWithTheirPc)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsAnalysisPane* writers = panes[14].analysis;
    ASSERT_NE(writers, nullptr);
    writers->refresh(_context, false);
    ASSERT_TRUE(writers->status().isEmpty()) << writers->status().toStdString();
    EXPECT_TRUE(writers->turnOnButton()->isVisibleTo(writers)) << "the histories are off by default";
    ASSERT_GE(writers->findRow("Port", "host #BB"), 0);
    EXPECT_TRUE(writers->cell(writers->findRow("Port", "host #BB"), "Value").isEmpty()) << "nothing recorded yet";

    ASSERT_TRUE(writers->turnOn(_context));
    _emulator->RunNFrames(3);  // the firmware starts: its port writes
    Z80* z80 = _context->pCore->GetZ80();
    z80->m1_pc = 0x8003;
    _context->pPortDecoder->DecodePortOut(0x00B3, 0x01, 0);
    _context->pPortDecoder->DecodePortOut(0x00BB, 0x23, 0);
    const int64_t frame = static_cast<int64_t>(_context->emulatorState.frame_counter);
    writers->refresh(_context, false);
    EXPECT_FALSE(writers->turnOnButton()->isVisibleTo(writers));
    const int command = writers->findRow("Port", "host #BB");
    ASSERT_GE(command, 0);
    EXPECT_EQ(writers->cell(command, "Value"), "#23");
    EXPECT_EQ(writers->cell(command, "PC"), "#8003");
    EXPECT_EQ(writers->cell(command, "Frame"), QString::number(frame));
    const int data = writers->findRow("Port", "host #B3");
    ASSERT_GE(data, 0);
    EXPECT_EQ(writers->cell(data, "Value"), "#01");
    EXPECT_EQ(writers->cell(data, "PC"), "#8003");
    int cardRows = 0;
    for (int r = 0; r < writers->table()->rowCount(); r++)
        if (!writers->cell(r, "Port").startsWith("host") && writers->cell(r, "PC").startsWith('#'))
            cardRows++;
    EXPECT_GT(cardRows, 0) << "the firmware's own port writes with its PC";
}

// region <The card CPU analysis tabs: Profiler, Events, Coverage, Heat map (A5.2a; X2, X8-X11, X20)>

namespace
{
constexpr size_t kProfilerPane = 15;
constexpr size_t kEventsPane = 16;
constexpr size_t kCoveragePane = 17;
constexpr size_t kHeatMapPane = 18;

/// A classic GS ROM: DI, SP #5800, IM 1, EI, then NOP NOP JR back; the ISR at #0038: `nops` NOPs, EI, RET (IM 1
/// acknowledge 13 + 4 * nops + EI 4 + RET 10 cycles every 320)
std::vector<uint8_t> IsrRom(int nops)
{
    std::vector<uint8_t> rom(SoundChip_GeneralSound::ROM_SIZE, 0x00);
    const uint8_t start[] = {0xF3, 0x31, 0x00, 0x58, 0xED, 0x56, 0xFB, 0x00, 0x00, 0x18, 0xFC};
    std::copy(std::begin(start), std::end(start), rom.begin());
    rom[static_cast<size_t>(0x38 + nops)] = 0xFB;
    rom[static_cast<size_t>(0x38 + nops + 1)] = 0xC9;
    return rom;
}

/// `rom` as the classic card's ROM, then a power-on reset of the card
void LoadRom(GeneralSoundCard& card, const std::vector<uint8_t>& rom, const char* leaf)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath(leaf);
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    card.loadROM(path);
    card.reset();
}

}  // namespace

/// Profiler (X9, X20): off by default with its Start (and the slowdown said); started on the idle stock firmware: the
/// "interrupts off" state (no interrupt taken, the period requests lost) and "no routine called yet" instead of empty
/// tables, the activity per frame newest first; Stop keeps the data
TEST_F(CardPanes_Test, TheProfilerShowsTheInterruptsOffStateOnTheIdleFirmware)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    _emulator->RunNFrames(2);
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsCpuAnalysisPane* profiler = panes[kProfilerPane].cpuAnalysis;
    ASSERT_NE(profiler, nullptr);
    profiler->refresh(_context, false);
    ASSERT_TRUE(profiler->status().isEmpty()) << profiler->status().toStdString();
    EXPECT_TRUE(profiler->hint().contains("The profiler is off")) << profiler->hint().toStdString();
    EXPECT_TRUE(profiler->hint().contains("slower")) << "it says the card slows";
    ASSERT_NE(profiler->button("profile_start"), nullptr);
    EXPECT_TRUE(profiler->button("profile_start")->isEnabled());
    EXPECT_FALSE(profiler->button("profile_stop")->isEnabled());

    ASSERT_TRUE(profiler->runAction(_context, "profile_start"));
    _emulator->RunNFrames(3);
    profiler->refresh(_context, false);
    EXPECT_TRUE(profiler->headline().startsWith("Profiler on")) << profiler->headline().toStdString();
    EXPECT_FALSE(profiler->button("profile_start")->isEnabled());
    EXPECT_TRUE(profiler->hint().contains("Interrupts off")) << profiler->hint().toStdString();
    EXPECT_TRUE(profiler->hint().contains("request(s) lost")) << profiler->hint().toStdString();
    EXPECT_TRUE(profiler->hint().contains("No routine called yet")) << profiler->hint().toStdString();
    EXPECT_EQ(profiler->gauge()->format(), "ISR budget: no interrupt taken");
    EXPECT_EQ(profiler->routines()->rowCount(), 0);
    ASSERT_GT(profiler->activity()->rowCount(), 1);
    EXPECT_EQ(GsCpuAnalysisPane::Cell(profiler->activity(), 0, "INTs"), "0");
    EXPECT_GT(GsCpuAnalysisPane::Cell(profiler->activity(), 0, "Lost").toLongLong(), 0);
    EXPECT_GT(GsCpuAnalysisPane::Cell(profiler->activity(), 0, "Frame").toLongLong(),
              GsCpuAnalysisPane::Cell(profiler->activity(), 1, "Frame").toLongLong())
        << "newest first";

    ASSERT_TRUE(profiler->runAction(_context, "profile_stop"));
    _emulator->RunNFrames(1);
    profiler->refresh(_context, false);
    EXPECT_TRUE(profiler->hint().contains("stopped")) << profiler->hint().toStdString();
    EXPECT_GT(profiler->activity()->rowCount(), 0) << "the data kept";
    EXPECT_FALSE(profiler->runAction(_context, "profile_bogus"));
}

/// Profiler (X9): a hand-built program (main CALL #0100, which CALLs #0180; an ISR of 67 cycles every 320) gives the
/// gauge, the routines with their calls and inclusive cycles, sortable by address
TEST_F(CardPanes_Test, TheProfilerShowsTheRoutinesAndTheBudgetOfAProgram)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsCpuAnalysisPane* profiler = panes[kProfilerPane].cpuAnalysis;
    ASSERT_NE(profiler, nullptr);
    std::vector<uint8_t> rom = IsrRom(10);
    const uint8_t main[] = {0xCD, 0x00, 0x01, 0x18, 0xFB};  // #0007: CALL #0100; JR #0007
    std::copy(std::begin(main), std::end(main), rom.begin() + 0x07);
    rom[0x100] = 0xCD;  // CALL #0180, 20 NOPs, RET
    rom[0x101] = 0x80;
    rom[0x102] = 0x01;
    rom[0x103 + 20] = 0xC9;
    rom[0x180 + 30] = 0xC9;  // 30 NOPs, RET
    ASSERT_NO_FATAL_FAILURE(LoadRom(*_context->pSoundManager->getGeneralSound(), rom, "cardpanes-calls.rom"));
    ASSERT_TRUE(profiler->runAction(_context, "profile_start"));
    _emulator->RunNFrames(2);
    profiler->refresh(_context, false);
    EXPECT_TRUE(profiler->gauge()->format().startsWith("ISR 67 / 320 avg · 67 max")) << profiler->gauge()->format().toStdString();
    EXPECT_FALSE(profiler->hint().contains("Interrupts off")) << profiler->hint().toStdString();
    QTableWidget* routines = profiler->routines();
    const int inner = GsCpuAnalysisPane::FindRow(routines, "Address", "#0180");
    const int outer = GsCpuAnalysisPane::FindRow(routines, "Address", "#0100");
    const int isr = GsCpuAnalysisPane::FindRow(routines, "Address", "#0038");
    ASSERT_GE(inner, 0) << profiler->headline().toStdString();
    ASSERT_GE(outer, 0);
    ASSERT_GE(isr, 0);
    EXPECT_GT(GsCpuAnalysisPane::Cell(routines, inner, "Calls").toLongLong(), 100);
    EXPECT_EQ(GsCpuAnalysisPane::Cell(routines, inner, "Max"), "130") << "30 NOPs + RET, interrupts never counted";
    EXPECT_EQ(GsCpuAnalysisPane::Cell(routines, outer, "Max"), "237") << "CALL 17 + 130 + 20 NOPs + RET";
    EXPECT_EQ(GsCpuAnalysisPane::Cell(routines, isr, "Max"), "67");
    EXPECT_EQ(GsCpuAnalysisPane::Cell(routines, isr, "Page"), "ROM 0");
    routines->sortByColumn(0, Qt::AscendingOrder);
    EXPECT_EQ(GsCpuAnalysisPane::Cell(routines, 0, "Address"), "#0038");
    EXPECT_EQ(GsCpuAnalysisPane::Cell(routines, routines->rowCount() - 1, "Address"), "#0180");
}

/// Profiler (X9) on the stock firmware: a card reset's POST calls its routines (labelled by gs105a's symbols); they
/// show once they return, at the POST's end (about 120 frames; read every 10 frames to stay fast)
TEST_F(CardPanes_Test, TheProfilerShowsTheFirmwareRoutinesAfterACardReset)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    _emulator->RunNFrames(2);
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsCpuAnalysisPane* profiler = panes[kProfilerPane].cpuAnalysis;
    ASSERT_NE(profiler, nullptr);
    ASSERT_TRUE(profiler->runAction(_context, "profile_start"));
    _context->pPortDecoder->DecodePortOut(0x0033, 0x80, 0);  // the host resets the card: its POST calls its routines
    bool found = false;
    for (int f = 0; f < 400 && !found; f += 10)
    {
        _emulator->RunNFrames(10);
        profiler->refresh(_context, false);
        found = profiler->routines()->rowCount() > 0;
    }
    ASSERT_TRUE(found) << profiler->headline().toStdString() << "\n" << profiler->hint().toStdString();
    int labelled = 0;
    for (int r = 0; r < profiler->routines()->rowCount(); r++)
        labelled += GsCpuAnalysisPane::Cell(profiler->routines(), r, "Label").isEmpty() ? 0 : 1;
    EXPECT_GT(labelled, 0) << "gs105a's symbols name its routines";
    EXPECT_FALSE(profiler->hint().contains("No routine called yet")) << profiler->hint().toStdString();
}

/// Events (X2): without the profiler a hint and its Start; a hand-built ISR (67 cycles every 320) gives a row per
/// period with its bar, the host's #B3 write a dot; the stock firmware's frame has rows but no bars (interrupts off)
TEST_F(CardPanes_Test, TheEventsTabShowsThePeriodsAndTheIsrBars)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsCpuAnalysisPane* events = panes[kEventsPane].cpuAnalysis;
    ASSERT_NE(events, nullptr);
    events->refresh(_context, false);
    ASSERT_TRUE(events->status().isEmpty()) << events->status().toStdString();
    EXPECT_EQ(events->events()->rowCount(), 0);
    EXPECT_TRUE(events->hint().contains("the frame map needs the profiler")) << events->hint().toStdString();
    EXPECT_TRUE(events->hint().contains("port trace is off")) << events->hint().toStdString();

    // The stock firmware: periods, no ISR bars
    ASSERT_TRUE(events->runAction(_context, "profile_start"));
    _emulator->RunNFrames(2);
    events->refresh(_context, false);
    EXPECT_GT(events->events()->rowCount(), 700) << events->headline().toStdString();
    EXPECT_TRUE(events->hint().contains("No ISR bars")) << events->hint().toStdString();

    // A hand-built ISR
    GeneralSoundCard* card = _context->pSoundManager->getGeneralSound();
    ASSERT_NE(card, nullptr);
    ASSERT_NO_FATAL_FAILURE(LoadRom(*card, IsrRom(10), "cardpanes-isr.rom"));
    ASSERT_TRUE(events->runAction(_context, "trace_start"));
    _emulator->RunNFrames(1);
    _context->pPortDecoder->DecodePortOut(0x00B3, 0x5A, 0);
    _emulator->RunNFrames(1);
    events->refresh(_context, false);
    GsEventsCanvas* canvas = events->events();
    ASSERT_GT(canvas->rowCount(), 700) << events->headline().toStdString();
    EXPECT_LT(canvas->rowCount(), 800);
    EXPECT_EQ(canvas->periodCycles(), 320);
    EXPECT_TRUE(events->headline().contains("periods of 320 cycles")) << events->headline().toStdString();
    EXPECT_FALSE(events->hint().contains("No ISR bars")) << events->hint().toStdString();
    EXPECT_FALSE(events->hint().contains("port trace is off")) << events->hint().toStdString();
    int bars = 0;
    int row = -1;
    const GsEventsCanvas::Bar* bar = nullptr;
    for (int r = 0; r < canvas->rowCount(); r++)
        for (const GsEventsCanvas::Bar& b : canvas->rows()[static_cast<size_t>(r)].isrs)
        {
            bars++;
            if (!bar && b.end >= 0)
            {
                bar = &b;
                row = r;
            }
        }
    EXPECT_GT(bars, 700) << "an ISR each period";
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->end - bar->accept, 67);
    EXPECT_EQ(bar->entry, 0x38);
    const QString tip = canvas->describe(row, bar->accept + 10);
    EXPECT_TRUE(tip.contains("INT accepted at cycle")) << tip.toStdString();
    EXPECT_TRUE(tip.contains("67 cycles")) << tip.toStdString();
    // The host's #B3 write: a dot of its own kind, named by the tooltip
    bool found = false;
    for (int r = 0; r < canvas->rowCount() && !found; r++)
        for (const GsEventsCanvas::Dot& d : canvas->rows()[static_cast<size_t>(r)].events)
            if (d.kind == GsEventsCanvas::HostDataWrite && d.value == 0x5A)
            {
                found = true;
                EXPECT_TRUE(canvas->describe(r, d.cycle).contains("host writes #B3 = #5A")) << canvas->describe(r, d.cycle).toStdString();
            }
    EXPECT_TRUE(found) << events->headline().toStdString();
    canvas->setShown(GsEventsCanvas::IsrBars, false);
    EXPECT_FALSE(canvas->describe(row, bar->accept + 10).contains("INT accepted")) << "a hidden category says nothing";
}

/// Coverage (X8, X11): off with On; on, the firmware's code shows as executed ranges: ROM on the classic card, RAM on
/// a NeoGS (it runs a RAM copy of its flash); the handlers and the never-written list are captioned; no break on new
/// code in the core
TEST_F(CardPanes_Test, TheCoverageTabShowsTheExecutedCodeRanges)
{
    for (const char* kind : {"gs", "neogs"})
    {
        ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", kind}}));
        const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
        GsCpuAnalysisPane* coverage = panes[kCoveragePane].cpuAnalysis;
        ASSERT_NE(coverage, nullptr);
        coverage->refresh(_context, false);
        ASSERT_TRUE(coverage->status().isEmpty()) << kind << ": " << coverage->status().toStdString();
        EXPECT_TRUE(coverage->hint().contains("The analysis is off")) << coverage->hint().toStdString();
        EXPECT_TRUE(coverage->button("analysis_on")->isEnabled());
        EXPECT_FALSE(coverage->button("analysis_release")->isEnabled());
        ASSERT_NE(coverage->button("break_new_code"), nullptr);
        EXPECT_FALSE(coverage->button("break_new_code")->isEnabled()) << "not in the core";

        ASSERT_TRUE(coverage->runAction(_context, "analysis_on"));
        _emulator->RunNFrames(3);
        coverage->refresh(_context, false);
        EXPECT_TRUE(coverage->hint().isEmpty()) << coverage->hint().toStdString();
        EXPECT_TRUE(coverage->headline().startsWith("Analysis on")) << coverage->headline().toStdString();
        const bool neogs = std::string(kind) == "neogs";
        const QString space = neogs ? QStringLiteral("RAM") : QStringLiteral("ROM");
        int pages = 0;
        QString ranges;
        for (int r = 0; r < coverage->pages()->rowCount(); r++)
            if (GsCpuAnalysisPane::Cell(coverage->pages(), r, "Space") == space &&
                GsCpuAnalysisPane::Cell(coverage->pages(), r, "Page") != "all" &&
                !GsCpuAnalysisPane::Cell(coverage->pages(), r, "Code ranges").isEmpty())
            {
                pages++;
                if (ranges.isEmpty())
                    ranges = GsCpuAnalysisPane::Cell(coverage->pages(), r, "Code ranges");
            }
        EXPECT_GT(pages, 0) << kind << ": " << coverage->headline().toStdString();
        EXPECT_TRUE(ranges.startsWith('#')) << ranges.toStdString();
        const int total = GsCpuAnalysisPane::FindRow(coverage->pages(), "Space", space);
        ASSERT_GE(total, 0);
        EXPECT_EQ(GsCpuAnalysisPane::Cell(coverage->pages(), total, "Page"), "all");
        EXPECT_GT(GsCpuAnalysisPane::Cell(coverage->pages(), total, "Exec").toLongLong(), 50) << kind;
        EXPECT_TRUE(coverage->caption(coverage->handlers()).startsWith("The dispatcher's handlers"))
            << coverage->caption(coverage->handlers()).toStdString();
        EXPECT_TRUE(coverage->caption(coverage->neverWritten()).startsWith("Reads of RAM never written"))
            << coverage->caption(coverage->neverWritten()).toStdString();

        ASSERT_TRUE(coverage->runAction(_context, "analysis_release"));
        coverage->refresh(_context, false);
        EXPECT_TRUE(coverage->hint().contains("The analysis is off")) << coverage->hint().toStdString();
        EXPECT_EQ(coverage->pages()->rowCount(), 0);
        EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        _emulator.reset();
    }
}

/// Heat map (X10): a NeoGS runs from RAM, so the fetch channel glows; the decay set from the tab; a cell's tooltip
/// names its RAM bytes and heats; the DAC's recent ranges captioned
TEST_F(CardPanes_Test, TheHeatMapShowsTheFetchedCells)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsCpuAnalysisPane* heat = panes[kHeatMapPane].cpuAnalysis;
    ASSERT_NE(heat, nullptr);
    heat->refresh(_context, false);
    ASSERT_TRUE(heat->status().isEmpty()) << heat->status().toStdString();
    EXPECT_EQ(heat->heatmap()->cells(), 0);
    heat->decay()->setValue(2);
    ASSERT_TRUE(heat->runAction(_context, "analysis_on"));
    _emulator->RunNFrames(3);
    heat->refresh(_context, false);
    EXPECT_TRUE(heat->headline().contains("decay 2 per frame")) << heat->headline().toStdString();
    ASSERT_GT(heat->heatmap()->cells(), 0);
    EXPECT_LE(heat->heatmap()->cells(), static_cast<int>(GsCpuAnalysisPane::kHeatCells));
    heat->channel()->setCurrentIndex(1);
    heat->heatmap()->setChannel(0);  // fetch
    EXPECT_GT(heat->heatmap()->hotCells(), 0) << heat->headline().toStdString();
    int hot = -1;
    for (int cell = 0; cell < heat->heatmap()->cells() && hot < 0; cell++)
        if (!heat->heatmap()->describe(cell).contains("fetch 0 "))
            hot = cell;
    ASSERT_GE(hot, 0);
    const QString tip = heat->heatmap()->describe(hot);
    EXPECT_TRUE(tip.startsWith("RAM #")) << tip.toStdString();
    EXPECT_TRUE(tip.contains("(page #")) << tip.toStdString();
    EXPECT_TRUE(heat->caption(heat->recent()).startsWith("What the DAC played in the last 50 frames"))
        << heat->caption(heat->recent()).toStdString();

    heat->decay()->setValue(9);
    ASSERT_TRUE(heat->runAction(_context, "analysis_decay"));
    heat->refresh(_context, false);
    EXPECT_TRUE(heat->headline().contains("decay 9 per frame")) << heat->headline().toStdString();
}

/// The lightweight player has no card CPU: each of the four tabs says so
TEST_F(CardPanes_Test, TheCpuAnalysisTabsAreNotAvailableOnTheLightweightPlayer)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs-lw"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    for (size_t i = kProfilerPane; i <= kHeatMapPane; i++)
    {
        GsCpuAnalysisPane* pane = panes[i].cpuAnalysis;
        ASSERT_NE(pane, nullptr);
        EXPECT_TRUE(CardPanes::Shown(panes[i].card, _context));
        pane->refresh(_context, false);
        EXPECT_TRUE(pane->status().contains("not available")) << panes[i].title.toStdString() << ": "
                                                              << pane->status().toStdString();
    }
    QString error;
    EXPECT_FALSE(panes[kProfilerPane].cpuAnalysis->runAction(_context, "profile_start", &error));
    EXPECT_FALSE(error.isEmpty());
}

// endregion </The card CPU analysis tabs>

/// P5.6 (gui-card-debugger.md A5.3 Send console, C11): the GS send tab. The picker lists the profile's commands, the form
/// names and types their parameters; Send goes through the host driver (gs send) without waiting, the row follows the
/// send to its replies decoded by the profile; a bad field or a byte the profile does not name are said so
TEST_F(CardPanes_Test, TheSendTabSendsACommandAndShowsItsReply)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    GsSendPane* send = panes[3].send;
    ASSERT_NE(send, nullptr);
    EXPECT_TRUE(CardPanes::Shown(panes[3].card, _context));

    send->refresh(_context, false);
    EXPECT_TRUE(send->status().isEmpty()) << send->status().toStdString();
    EXPECT_FALSE(send->profileId().isEmpty());
    EXPECT_GT(send->commandCount(), 50);

    // A command with a pre byte: named and typed by the profile, its reply's meaning in the note
    send->selectCommand(0x2A);
    ASSERT_EQ(send->paramCount(), 1);
    EXPECT_EQ(send->paramLabel(0), "volume (before)");
    EXPECT_TRUE(send->noteText().contains("the old volume")) << send->noteText().toStdString();
    send->setParam(0, "#1G");
    QString error;
    EXPECT_FALSE(send->send(_context, &error));
    EXPECT_TRUE(error.contains("volume")) << error.toStdString();

    // A byte the profile does not name: the bytes and replies are the user's
    int unnamed = -1;
    for (int code = 0xFF; code >= 0 && unnamed < 0; code--)
        if (send->picker()->findData(code) < 0)
            unnamed = code;
    ASSERT_GE(unnamed, 0);
    send->picker()->setEditText(QStringLiteral("#%1").arg(unnamed, 2, 16, QLatin1Char('0')));
    EXPECT_EQ(send->paramCount(), 0);
    EXPECT_TRUE(send->noteText().contains("does not name")) << send->noteText().toStdString();
    GSSendRequest typed;
    ASSERT_TRUE(send->request(typed, error)) << error.toStdString();
    EXPECT_EQ(typed.command, unnamed);

    // #23 sent on a paused machine once the firmware booted: queued, then done once the firmware takes it, the reply
    // decoded
    GeneralSoundCard* card = _context->pSoundManager->getGeneralSound();
    ASSERT_NE(card, nullptr);
    for (int i = 0; i < 200 && !card->isReadyForCommands(); i++)
        _emulator->RunNFrames(5);
    ASSERT_TRUE(card->isReadyForCommands());
    send->selectCommand(0x23);
    EXPECT_EQ(send->paramCount(), 0);
    ASSERT_TRUE(send->send(_context, &error)) << error.toStdString();
    EXPECT_TRUE(send->rowsText().startsWith("queued")) << send->rowsText().toStdString();
    for (int i = 0; i < 100 && !send->rowsText().startsWith("done") && !send->rowsText().startsWith("failed"); i++)
    {
        _emulator->RunNFrames(5);
        send->refresh(_context, false);
    }
    EXPECT_TRUE(send->rowsText().startsWith("done #23 Get number of RAM pages -> #")) << send->rowsText().toStdString();
    EXPECT_TRUE(send->rowsText().contains("(pages)")) << send->rowsText().toStdString();
    EXPECT_EQ(send->table()->rowCount(), 1);

    // The monitor's Send... asks its owner for this tab
    int asked = 0;
    QObject::connect(panes[2].monitor, &GsMonitorPane::sendConsoleRequested, [&]() { asked++; });
    panes[2].monitor->sendButton()->click();
    EXPECT_EQ(asked, 1);
}

TEST(GsSendPane_Test, BytesParseAsTheConditionsWriteThem)
{
    EXPECT_EQ(GsSendPane::ParseByte("42"), 42);
    EXPECT_EQ(GsSendPane::ParseByte("#2A"), 0x2A);
    EXPECT_EQ(GsSendPane::ParseByte("$2a"), 0x2A);
    EXPECT_EQ(GsSendPane::ParseByte("0x2A"), 0x2A);
    EXPECT_EQ(GsSendPane::ParseByte(" 255 "), 255);
    EXPECT_FALSE(GsSendPane::ParseByte("256"));
    EXPECT_FALSE(GsSendPane::ParseByte("zz"));
    EXPECT_FALSE(GsSendPane::ParseByte(""));
}
