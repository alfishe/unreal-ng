#include "cardpanes.h"

#include <QLabel>
#include <QObject>
#include <QTimer>
#include <QWidget>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "debugger/boardpane.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/disassembler/z80mcycles.h"
#include "debugger/gslwpane.h"
#include "debugger/gsmonitorpane.h"
#include "debugger/reportpane.h"
#include "debugger/zxdmatraceview.h"
#include "debugger/targets/debugcpuid.h"
#include "emulator/execution/runcontrol.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
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

    // P5.6 (gui-card-debugger.md A5.3, C10): the protocol monitor, watched next to the game while it plays
    Pane monitor{QObject::tr("GS monitor"),
                 QObject::tr("The protocol monitor: where the conversation is, the anomalies and unknown commands since Clear, "
                             "the command log following its tail, Pause / Continue"),
                 nullptr, nullptr, Card::Any, nullptr};
    monitor.monitor = new GsMonitorPane(follow, parent);
    panes.push_back(monitor);

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
    // N8: the SD card's slot in the media panel (its owner): the window opens it on uiActionRequested
    media->addUiAction(QStringLiteral("neogs_sd"), QString::fromLatin1(kOpenMediaPanel), QObject::tr("Open media panel"),
                       QObject::tr("The media panel on slot %1: insert, eject, save, protect, create").arg(QString::fromLatin1(kSdSlot)));
    panes.push_back({QObject::tr("NeoGS SD · MP3 · Flash"),
                     QObject::tr("The NeoGS SD card (slot sd.ngs: Insert, Eject, Open media panel), the MP3 decoder, the flash chip (Save flash)"),
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

QString StripText(EmulatorContext* context, const Emulator::DebugStop& stop, DebugCpuId front, bool withMainPosition)
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
        if (withMainPosition && stop.breakpoint.main.known)
            line += QStringLiteral(" · ") + QString::fromStdString(execution::MainPositionText(stop.breakpoint.main));
    }
    return line;
}

namespace
{
/// The main CPU's instruction at `pc` as the disassembler reads it ("LD A,(HL)"); empty without a disassembler
QString MainInstruction(EmulatorContext* context, uint16_t pc)
{
    Memory* memory = context ? context->pMemory : nullptr;
    DebugManager* debugManager = context ? context->pDebugManager : nullptr;
    Z80Disassembler* disassembler = debugManager ? debugManager->GetDisassembler().get() : nullptr;
    if (!memory || !disassembler)
        return QString();
    std::vector<uint8_t> bytes(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (size_t i = 0; i < bytes.size(); i++)
        bytes[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(pc + i));  // the memory map: no device read
    try
    {
        return QString::fromStdString(disassembler->disassembleSingleCommand(bytes, pc)).trimmed();
    }
    catch (const std::exception&)
    {
        return QString();
    }
}
}  // namespace

QString MainPositionStrip(EmulatorContext* context, const Emulator::DebugStop& stop)
{
    const bool cardStop = (stop.reason == Emulator::DebugStop::Reason::Breakpoint ||
                           (stop.reason == Emulator::DebugStop::Reason::Step && stop.breakpoint.step)) &&
                          stop.breakpoint.cpu != DebugCpuId::Main;
    if (!cardStop || !stop.breakpoint.main.known)
        return QString();
    return MainPositionStrip(context, stop.breakpoint.main);
}

QString MainPositionStrip(EmulatorContext* context, const MainPosition& main)
{
    const QString gap = QStringLiteral("%1 T past the card").arg(main.gapT);
    if (main.frameEnd)
        return QStringLiteral("main between frames · ") + gap;
    QString line = QStringLiteral("in #%1").arg(QString::number(main.pc, 16).toUpper().rightJustified(4, QLatin1Char('0')));
    if (const QString instruction = MainInstruction(context, main.pc); !instruction.isEmpty())
        line += QLatin1Char(' ') + instruction;
    if (main.onBoundary)
        line += QStringLiteral(" (finished)");
    const QString wait = main.waitT ? QStringLiteral(" + %1 T wait").arg(main.waitT) : QString();
    if (main.timed)
    {
        QString total = QString::number(main.totalT);
        if (main.totalTAlt)
            total += QLatin1Char('/') + QString::number(main.totalTAlt);
        line += QStringLiteral(" · T %1 of %2").arg(main.elapsedT).arg(total) + wait;
        if (main.contended)
            line += QStringLiteral(" contended");
        if (main.busCycle >= 0 && main.busCycle < main.cycles.count)
            line += QStringLiteral(" · ") +
                    QString::fromLatin1(Z80MCycleDecoder::Description(main.cycles.cycles[main.busCycle].kind));
    }
    else
        line += wait;
    return line + QStringLiteral(" · ") + gap;
}

QString ZxDmaBanner(EmulatorContext* context)
{
    return QString::fromStdString(DeviceState::ZxDmaBanner(context));
}

bool ZxDmaDivertsReads(EmulatorContext* context)
{
    GeneralSoundCard* card = context && context->pSoundManager ? context->pSoundManager->getGeneralSound() : nullptr;
    NeoGSStateInfo state;
    if (!card || !card->neogsState(state) || !state.zxOverlayInstalled || std::strcmp(state.zxMode, "divert") != 0)
        return false;
    return context->pMemory && context->pMemory->IsBank0ROM();
}

namespace
{
void Show(QLabel* label, const QString& text)
{
    if (!label)
        return;
    label->setText(text);
    label->setVisible(!text.isEmpty());
}
}  // namespace

bool FillLiveLines(const Lines& lines, EmulatorContext* context, DebugCpuId front)
{
    static Emulator::DebugStop none;
    return FillLines(lines, context, none, front, true);
}

bool FillLines(const Lines& lines, EmulatorContext* context, const Emulator::DebugStop& stop, DebugCpuId front, bool live)
{
    static Emulator::DebugStop none;  // named: g++ 14 crashes on a temporary of it (z80mcycles.h)
    const Emulator::DebugStop& at = live ? none : stop;
    const bool main = context && front == DebugCpuId::Main;
    // S3 above the main registers while they are in front; then the card line leaves the position out (one view, once)
    const QString position = main ? MainPositionStrip(context, at) : QString();
    Show(lines.mainPosition, position);
    Show(lines.card, context ? StripText(context, at, front, position.isEmpty()) : QString());
    // S4: the main CPU's views only (a card's tab shows the card's memory)
    const QString banner = main ? ZxDmaBanner(context) : QString();
    const bool diverts = !banner.isEmpty() && ZxDmaDivertsReads(context);
    Show(lines.memoryBanner, banner);
    Show(lines.disasmBanner, diverts ? banner + QObject::tr(" · marked rows #0000-#3FFF: the CPU's bytes come from the card "
                                                            "at run time, the rows show the host ROM")
                                     : banner);
    return diverts;
}

LiveLine::LiveLine(QWidget* window, std::function<std::shared_ptr<Emulator>()> emulator,
                   std::function<void(EmulatorContext*)> read, int intervalMs)
    : QObject(window), _window(window), _emulator(std::move(emulator)), _read(std::move(read))
{
    _timer = new QTimer(this);
    _timer->setInterval(intervalMs);
    connect(_timer, &QTimer::timeout, this, &LiveLine::tick);
}

void LiveLine::update()
{
    if (_window && _window->isVisible())
        _timer->start();
    else
        _timer->stop();
}

bool LiveLine::active() const
{
    return _timer->isActive();
}

void LiveLine::tick()
{
    if (!_window || !_window->isVisible())
    {
        _timer->stop();
        return;
    }
    const std::shared_ptr<Emulator> emulator = _emulator ? _emulator() : nullptr;
    // A paused machine: its stop's refresh fills the line; automation stepping on its own thread: its end refreshes
    if (!emulator || emulator->IsPaused() || !emulator->IsRunning() || emulator->IsDirectStepping())
        return;
    Emulator::ContextLease lease = emulator->LeaseContext();
    if (!lease)
        return;
    EmulatorContext* context = lease.get();
    if (!Fitted(context))
        return;
    _reads++;
    _read(context);
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
    if (monitor)
        return monitor;
    return inspector;
}
}  // namespace CardPanes
