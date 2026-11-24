// The debugger's General Sound panes (gs-debugger TODO P5.1): each a ReportPane over a report of the device catalog the
// automation planes serve; a fitted card fills them, the command log shows the host's command decoded, a machine
// without the card says why each is empty. On a NeoGS (P5.5) the board panes over the core's device boards: the NeoGS
// board replacing the Hardware pane, DMA, SD · MP3 · Flash. On the lightweight player (A10) its interpreter and player
// panes; with a ZX-MultiSound the panes over its General Sound (CPU `ms`).

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "debugger/boardpane.h"
#include "debugger/boards/debugboards.h"
#include "debugger/cardpanes.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80mcycles.h"
#include "debugger/gslwpane.h"
#include "debugger/gsmonitorpane.h"
#include "debugger/history/historycontrol.h"
#include "debugger/reportpane.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/zxdmatraceview.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gshostdriver.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"
#include "media/mediapanelwindow.h"
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
    ASSERT_EQ(panes.size(), 11u);
    const char* titles[] = {"GS log",    "GS protocol", "GS monitor", "GS firmware",           "GS hardware", "NeoGS hardware",
                            "NeoGS DMA", "NeoGS SD · MP3 · Flash", "GS stats", "GS interpreter", "GS player"};
    const CardPanes::Card cards[] = {CardPanes::Card::Any,   CardPanes::Card::Any,   CardPanes::Card::Any, CardPanes::Card::Any,
                                     CardPanes::Card::NotNeoGS, CardPanes::Card::NeoGS, CardPanes::Card::NeoGS,
                                     CardPanes::Card::NeoGS, CardPanes::Card::Any, CardPanes::Card::Lightweight,
                                     CardPanes::Card::Lightweight};
    for (size_t i = 0; i < panes.size(); i++)
    {
        EXPECT_EQ(panes[i].title, QString::fromUtf8(titles[i]));
        EXPECT_NE(panes[i].widget(), nullptr);
        EXPECT_EQ(panes[i].card, cards[i]) << titles[i];
        EXPECT_EQ(panes[i].board != nullptr, panes[i].card == CardPanes::Card::NeoGS) << titles[i];
        EXPECT_EQ(panes[i].inspector != nullptr, panes[i].card == CardPanes::Card::Lightweight) << titles[i];
        EXPECT_EQ(panes[i].monitor != nullptr, i == 2) << titles[i];
        EXPECT_FALSE(panes[i].tooltip.isEmpty());
    }
}

/// The card fitted: every pane has its report; the hardware pane lists the DAC channels, the log the host's command
/// once its history records
TEST_F(CardPanes_Test, AFittedCardFillsThePanes)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    EXPECT_TRUE(CardPanes::Fitted(_context));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);

    ReportPane* hardware = panes[4].pane;
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
    BoardPane* board = panes[5].board;
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
    ReportPane* stats = panes[8].pane;
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
    BoardPane* dma = panes[6].board;
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
    BoardPane* dma = panes[6].board;
    ZxDmaTraceView* trace = panes[6].trace;
    ASSERT_NE(dma, nullptr);
    ASSERT_NE(trace, nullptr);
    for (const CardPanes::Pane& pane : panes)
        if (&pane != &panes[6])
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
    BoardPane* dma = panes[6].board;
    ZxDmaTraceView* trace = panes[6].trace;
    ASSERT_NE(trace, nullptr);
    dma->refresh(_context, false);
    EXPECT_FALSE(dma->status().isEmpty());
    EXPECT_FALSE(trace->toggle()->isVisibleTo(dma)) << "no board, no trace";
    EXPECT_FALSE(CardPanes::Shown(panes[6].card, _context)) << "the DMA tab itself is hidden";
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
    BoardPane* media = panes[7].board;
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
    ReportPane* firmware = panes[3].pane;
    firmware->refresh(_context, false);
    EXPECT_TRUE(firmware->status().isEmpty()) << firmware->status().toStdString();
    ASSERT_NE(firmware->table(), nullptr);
    EXPECT_GE(firmware->table()->rowCount(), 1) << "the window variables";
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
    GsLwPane* interpreter = panes[9].inspector;
    GsLwPane* player = panes[10].inspector;
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
    ReportPane* hardware = panes[4].pane;
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

    ReportPane* firmware = panes[3].pane;
    firmware->refresh(_context, false);
    ASSERT_TRUE(firmware->status().isEmpty()) << firmware->status().toStdString();
    QTreeWidgetItem* cpu = Field(firmware->fields(), "cpu");
    ASSERT_NE(cpu, nullptr);
    EXPECT_EQ(cpu->text(1), "ms");

    ReportPane* stats = panes[8].pane;
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
    BoardPane* media = panes[7].board;
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
    // Sends until the log is taller than its view, the view at its end
    QElapsedTimer clock;
    clock.start();
    while ((monitor->table()->rowCount() < 40 || bar->maximum() == 0) && clock.elapsed() < 900)
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
    while (monitor->table()->rowCount() == rows && clock.elapsed() < 500)
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
