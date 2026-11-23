#include "cardpanes.h"

#include <QObject>

#include <cstring>
#include <memory>
#include <string>

#include "debugger/boardpane.h"
#include "debugger/gslwpane.h"
#include "debugger/reportpane.h"
#include "debugger/zxdmatraceview.h"
#include "debugger/targets/debugcpuid.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/card.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicecatalog.h"
#include "emulator/state/devicestate.h"

namespace CardPanes
{
namespace
{
/// The report of the catalog entry `id` (the one every plane reads by the entry's route and commands)
StateNode Catalogued(EmulatorContext* context, const char* id)
{
    for (const DeviceState::ReportEntry& e : DeviceState::CatalogEntries())
        if (std::strcmp(e.id, id) == 0)
            return e.build(context);
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = std::string("no report ") + id;
    return n;
}

/// The card the panes show now: `follow`'s CPU tab in front (none: the main CPU's)
Target Shows(EmulatorContext* context, const Follow& follow)
{
    return Front(context, follow ? *follow : DebugCpuId::Main);
}

/// The `gs` report of the card `target` names: the GS slot's (`state/audio/gs`) or the `multisound` report's `gs`
StateNode GsReport(EmulatorContext* context, Target target)
{
    if (target != Target::MultiSound)
        return DeviceState::Gs(context);
    const StateNode ms = DeviceState::MultiSound(context);
    if (const StateNode* gs = ms.find("gs"); gs && gs->isObject())
        return *gs;
    return ms;  // not available: its description says why
}

/// The catalog entry `id` of the GS slot's card, or `ms-<id>` of the MultiSound's
StateNode CatalogedOf(EmulatorContext* context, const Follow& follow, const char* id)
{
    if (Shows(context, follow) == Target::MultiSound)
        return Catalogued(context, (std::string("ms-") + id).c_str());
    return Catalogued(context, id);
}

/// What the host's conversation with the card is at: the `gs` report's protocol (the command log's decoder) and the
/// debugger's last send, with the mailbox they act on
StateNode Protocol(EmulatorContext* context, Target target)
{
    const StateNode gs = GsReport(context, target);
    if (const StateNode* available = gs.find("available"); !available || available->kind != StateNode::Kind::Bool || !available->b)
        return gs;
    StateNode out = StateNode::Object();
    out["available"] = true;
    for (const char* key : {"protocol", "last_send", "firmware_profile", "status", "command_pending", "data_pending",
                            "command_from_host", "data_from_host", "data_to_host"})
        if (const StateNode* value = gs.find(key))
            out[key] = *value;
    if (!out.find("protocol"))
        out["protocol"] = std::string("off: the command log does not record (history <slot>.commands mode ring)");
    return out;
}

/// The NeoGS part of the card's line (gui-card-debugger.md B2, the status bar): GSCFG0 with its mode and channels, the
/// ZX-DMA mode while it is not off ("GSCFG0 #05 (RAM mode, 8 ch) · ZX-DMA divert")
QString NeoGSStrip(const StateNode& gs)
{
    const StateNode* neogs = gs.find("neogs");
    const StateNode* gscfg0 = neogs ? neogs->find("gscfg0") : nullptr;
    if (!gscfg0 || gscfg0->kind != StateNode::Kind::Int)
        return QString();
    bool ram = false, eight = false, pan = false;
    if (const StateNode* flags = neogs->find("gscfg0_flags"); flags && flags->isArray())
        for (const StateNode& flag : flags->items)
        {
            ram = ram || flag.s == "ram_mode";
            eight = eight || flag.s == "8_channels";
            pan = pan || flag.s == "pan4ch";
        }
    QString line = QStringLiteral(" · GSCFG0 #%1 (%2, %3)")
                       .arg(QString::number(gscfg0->i, 16).toUpper().rightJustified(2, QLatin1Char('0')),
                            ram ? QStringLiteral("RAM mode") : QStringLiteral("ROM mode"),
                            eight ? QStringLiteral("8 ch") : pan ? QStringLiteral("pan4ch") : QStringLiteral("4 ch"));
    const StateNode* dma = neogs->find("dma");
    const StateNode* zx = dma ? dma->find("zx") : nullptr;
    const StateNode* mode = zx ? zx->find("mode") : nullptr;
    if (mode && mode->kind == StateNode::Kind::String && mode->s != "off")
        line += QStringLiteral(" · ZX-DMA ") + QString::fromStdString(mode->s);
    return line;
}

bool Anomaly(const StateNode& row)
{
    const StateNode* kind = row.find("kind");
    return kind && kind->kind == StateNode::Kind::String && kind->s == "anomaly";
}
}  // namespace

std::vector<Pane> Create(QWidget* parent, Follow follow)
{
    std::vector<Pane> panes;

    ReportPane* log = new ReportPane([follow](EmulatorContext* c) { return CatalogedOf(c, follow, "gs-commands"); }, "records",
                                     {"frame", "t", "kind", "command", "name", "text"}, parent);
    log->setRowMark(&Anomaly);
    panes.push_back({QObject::tr("GS log"),
                     QObject::tr("The card's command log decoded: the newest commands, data, replies and anomalies (anomalies tinted)"), log,
                     nullptr, Card::Any, nullptr});

    ReportPane* protocol =
        new ReportPane([follow](EmulatorContext* c) { return Protocol(c, Shows(c, follow)); }, std::string(), {}, parent);
    panes.push_back({QObject::tr("GS protocol"),
                     QObject::tr("Where the host's conversation with the card is: the decoder's state, the last send, the mailbox"),
                     protocol, nullptr, Card::Any, nullptr});

    ReportPane* firmware = new ReportPane([follow](EmulatorContext* c) { return CatalogedOf(c, follow, "gs-firmware"); }, "window_variables",
                                          {"name", "at", "size", "value", "meaning"}, parent);
    panes.push_back({QObject::tr("GS firmware"),
                     QObject::tr("The firmware's objects by its profile: instances, variables, the dispatch table, the unknown commands"),
                     firmware, nullptr, Card::Any, nullptr});

    ReportPane* hardware =
        new ReportPane([follow](EmulatorContext* c) { return GsReport(c, Shows(c, follow)); }, "channels", {}, parent);
    panes.push_back({QObject::tr("GS hardware"), QObject::tr("The card: ROM, MPAG, the mailbox, the four DAC channels, its CPU"), hardware,
                     nullptr, Card::NotNeoGS, nullptr});

    // NeoGS (gui-card-debugger.md B3.1): the board `neogs` with the card's counters (clock switches, resets)
    BoardPane* board = new BoardPane({{"neogs", "card", {}}}, BoardPane::Blocks::Columns, parent);
    panes.push_back({QObject::tr("NeoGS hardware"),
                     QObject::tr("The NeoGS board: GSCFG0, the four windows, the clock, the DAC, the interrupts, SPI and SSTAT, the "
                                 "LED, the flash, the mailbox"),
                     nullptr, board, Card::NeoGS, nullptr});

    // B3.2: the three DMA modules as a table (the statistics block's dma section), the ZX-DMA detail from the board
    BoardPane* dma = new BoardPane({{"neogs_dma", "", {"ZX module", "SD module", "MP3 module"}}}, BoardPane::Blocks::Columns, parent);
    dma->setTable({"dma",
                   {{"zx", QStringLiteral("ZX")}, {"sd", QStringLiteral("SD")}, {"mp3", QStringLiteral("MP3")}},
                   {{"selected", QObject::tr("Sel")}, {"running", QObject::tr("Run")}, {"address", QObject::tr("Address")},
                    {"page", QObject::tr("Page")}, {"starts", QObject::tr("Starts")}, {"completions", QObject::tr("Done")},
                    {"bytes", QObject::tr("Bytes")}, {"aborts", QObject::tr("Aborts")}, {"errors", QObject::tr("Errors")},
                    {"stall_clocks", QObject::tr("Stall clk")}, {"last.address", QObject::tr("Last at")},
                    {"last.bytes", QObject::tr("Last bytes")}}});
    // The ZX-DMA trace under the board (Trace ▸): it refreshes with the board, under the same lease
    ZxDmaTraceView* trace = new ZxDmaTraceView(dma);
    dma->addExtra(trace);
    QObject::connect(dma, &BoardPane::refreshed, trace, &ZxDmaTraceView::refresh);
    panes.push_back({QObject::tr("NeoGS DMA"),
                     QObject::tr("The NeoGS DMA modules (ZX, SD, MP3): selected, running, address, transactions; the ZX-DMA mode, "
                                 "latch, waits and bytes; Trace ▸: the bytes the Spectrum moved through the window"),
                     nullptr, dma, Card::NeoGS, nullptr, trace});

    // B3.3: three cards, side by side when wide; each with its counters from the statistics block
    BoardPane* media = new BoardPane({{"neogs_sd", "sd", {}}, {"neogs_mp3", "mp3", {}}, {"neogs_flash", "flash", {}}},
                                     BoardPane::Blocks::Cards, parent);
    panes.push_back({QObject::tr("NeoGS SD · MP3 · Flash"),
                     QObject::tr("The NeoGS SD card (slot sd.ngs: Insert, Eject), the MP3 decoder, the flash chip (Save flash)"),
                     nullptr, media, Card::NeoGS, nullptr});

    ReportPane* stats =
        new ReportPane([follow](EmulatorContext* c) { return CatalogedOf(c, follow, "gs-stats"); }, std::string(), {}, parent);
    panes.push_back({QObject::tr("GS stats"), QObject::tr("The card's statistics block: the mailbox, the CPU's load, the channels; on a NeoGS the DMA, SPI, SD, MP3 and flash"),
                     stats, nullptr, Card::Any, nullptr});

    // The lightweight player (A10): no CPU, so its interpreter and its player instead of registers and disassembly
    GsLwPane* interpreter = new GsLwPane(GsLwPane::View::Interpreter, parent);
    panes.push_back({QObject::tr("GS interpreter"),
                     QObject::tr("The lightweight player's command interpreter: Idle → Command → Params → Upload / Reply, the "
                                 "command, the byte counts, the params and replies queues, the upload"),
                     nullptr, nullptr, Card::Lightweight, interpreter});
    GsLwPane* player = new GsLwPane(GsLwPane::View::Player, parent);
    panes.push_back({QObject::tr("GS player"),
                     QObject::tr("The lightweight player's MOD player: song position, pattern, row, tick, speed, BPM, the four "
                                 "channels, the module, the samples"),
                     nullptr, nullptr, Card::Lightweight, player});
    return panes;
}

QString StripText(EmulatorContext* context, const Emulator::DebugStop& stop, DebugCpuId front)
{
    const Target target = Front(context, front);
    if (target == Target::None)
        return QString();
    const StateNode gs = GsReport(context, target);
    const StateNode* strip = gs.find("strip");
    QString line = strip && strip->kind == StateNode::Kind::String ? QString::fromStdString(strip->s) : QString();
    line += NeoGSStrip(gs);
    const bool cardStop = (stop.reason == Emulator::DebugStop::Reason::Breakpoint ||
                           (stop.reason == Emulator::DebugStop::Reason::Step && stop.breakpoint.step)) &&
                          stop.breakpoint.cpu != DebugCpuId::Main;
    if (cardStop)
    {
        line += QStringLiteral(" · paused by the card (%1)").arg(QString::fromLatin1(DebugCpuName(stop.breakpoint.cpu)));
        if (stop.breakpoint.main.known)
            line += QStringLiteral(" · ") + QString::fromStdString(execution::MainPositionText(stop.breakpoint.main));
    }
    return line;
}

namespace
{
GeneralSoundCard* SlotCard(EmulatorContext* context)
{
    return context && context->pSoundManager ? context->pSoundManager->getGeneralSound() : nullptr;
}
}  // namespace

bool Fitted(EmulatorContext* context)
{
    return SlotCard(context) || MultiSoundFitted(context);
}

bool NeoGSFitted(EmulatorContext* context)
{
    GeneralSoundCard* card = SlotCard(context);
    return card && card->implementation() == GSCardImplementation::NGS;
}

bool LightweightFitted(EmulatorContext* context)
{
    GeneralSoundCard* card = SlotCard(context);
    return card && card->implementation() == GSCardImplementation::LW;
}

bool MultiSoundFitted(EmulatorContext* context)
{
    if (!context || !context->pSlotManager)
        return false;
    for (const std::unique_ptr<ICard>& card : context->pSlotManager->Cards())
        if (card && card->DebugGeneralSound())
            return true;
    return false;
}

Target Front(EmulatorContext* context, DebugCpuId front)
{
    const bool slot = SlotCard(context) != nullptr;
    const bool ms = MultiSoundFitted(context);
    if (ms && (front == DebugCpuId::MultiSoundGS || !slot))
        return Target::MultiSound;
    return slot ? Target::Slot : Target::None;
}

std::optional<uint16_t> CardCpuAddress(EmulatorContext* context, uint32_t cardAddress)
{
    const auto* card = dynamic_cast<const SoundChip_NeoGS*>(SlotCard(context));
    if (!card)
        return std::nullopt;
    const NeoGSMemory& memory = card->memory();
    if (memory.ramSize() == 0)
        return std::nullopt;
    const uint32_t cell = static_cast<uint32_t>(cardAddress % memory.ramSize());
    for (int w = 0; w < 4; w++)
    {
        const auto base = static_cast<uint16_t>(w * 0x4000);
        if (!memory.isFlash(w) && memory.physical(base) == (cell & ~0x3FFFu))
            return static_cast<uint16_t>(base + (cell & 0x3FFF));
    }
    return std::nullopt;
}

bool Shown(Card card, EmulatorContext* context, DebugCpuId front)
{
    const Target target = Front(context, front);
    const bool slot = target == Target::Slot;
    switch (card)
    {
        case Card::Any: return target != Target::None;
        case Card::NotNeoGS: return target == Target::MultiSound || (slot && !NeoGSFitted(context));
        case Card::NeoGS: return slot && NeoGSFitted(context);
        case Card::Lightweight: return slot && LightweightFitted(context);
    }
    return false;
}

QWidget* Pane::widget() const
{
    if (pane)
        return pane;
    if (board)
        return board;
    return inspector;
}
}  // namespace CardPanes
