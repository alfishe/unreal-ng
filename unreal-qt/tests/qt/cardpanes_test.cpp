// The debugger's General Sound panes (gs-debugger TODO P5.1): each a ReportPane over a report of the device catalog the
// automation planes serve; a fitted card fills them, the command log shows the host's command decoded, a machine
// without the card says why each is empty. On a NeoGS (P5.5) the board panes over the core's device boards: the NeoGS
// board replacing the Hardware pane, DMA, SD · MP3 · Flash. On the lightweight player (A10) its interpreter and player
// panes; with a ZX-MultiSound the panes over its General Sound (CPU `ms`).

#include <QProgressBar>
#include <QTableWidget>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "debugger/boardpane.h"
#include "debugger/boards/debugboards.h"
#include "debugger/cardpanes.h"
#include "debugger/debugmanager.h"
#include "debugger/gslwpane.h"
#include "debugger/history/historycontrol.h"
#include "debugger/reportpane.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

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
    ASSERT_EQ(panes.size(), 10u);
    const char* titles[] = {"GS log",    "GS protocol", "GS firmware",           "GS hardware", "NeoGS hardware",
                            "NeoGS DMA", "NeoGS SD · MP3 · Flash", "GS stats", "GS interpreter", "GS player"};
    const CardPanes::Card cards[] = {CardPanes::Card::Any,   CardPanes::Card::Any,   CardPanes::Card::Any,
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

    ReportPane* hardware = panes[3].pane;
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
    BoardPane* board = panes[4].board;
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
    ReportPane* stats = panes[7].pane;
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
    BoardPane* dma = panes[5].board;
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

/// The SD · MP3 · Flash pane (B3.3): three cards with their counters, side by side when wide, stacked when narrow; the
/// boards' actions as buttons, carried out by the core's board action
TEST_F(CardPanes_Test, TheMediaPaneShowsThreeCards)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "neogs"}}));
    const std::vector<CardPanes::Pane> panes = CardPanes::Create(&_parent);
    BoardPane* media = panes[6].board;
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
    ReportPane* firmware = panes[2].pane;
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
    GsLwPane* interpreter = panes[8].inspector;
    GsLwPane* player = panes[9].inspector;
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
    ReportPane* hardware = panes[3].pane;
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

    ReportPane* firmware = panes[2].pane;
    firmware->refresh(_context, false);
    ASSERT_TRUE(firmware->status().isEmpty()) << firmware->status().toStdString();
    QTreeWidgetItem* cpu = Field(firmware->fields(), "cpu");
    ASSERT_NE(cpu, nullptr);
    EXPECT_EQ(cpu->text(1), "ms");

    ReportPane* stats = panes[7].pane;
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
