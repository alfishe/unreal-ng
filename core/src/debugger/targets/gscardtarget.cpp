#include "debugger/targets/gscardtarget.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

GSCardTarget::GSCardTarget(EmulatorContext* context) : GSCardTarget(context, nullptr) {}

GSCardTarget::GSCardTarget(EmulatorContext* context, std::function<GeneralSoundCard*()> cardSource)
    : GSCardTarget(context, DebugCpuId::GS, std::move(cardSource))
{
}

GSCardTarget::GSCardTarget(EmulatorContext* context, DebugCpuId id, std::function<GeneralSoundCard*()> cardSource)
    : _context(context), _id(id), _cardSource(std::move(cardSource))
{
    _breakpoints = std::make_unique<BreakpointManager>(context, this);
}

GSCardTarget::~GSCardTarget()
{
    // The breakpoints go first (their teardown tells the demand), then the hooks leave the card
    _breakpoints.reset();
    _wanted = false;
    Attach();
}

GeneralSoundCard* GSCardTarget::Card() const
{
    GeneralSoundCard* card = nullptr;
    if (_cardSource)
        card = _cardSource();
    else if (SoundManager* sound = _context ? _context->pSoundManager : nullptr)
        card = sound->getGeneralSound();
    return card && card->hasCoprocessor() ? card : nullptr;
}

void GSCardTarget::OnCardCreated()
{
    // The card the hooks were on is gone (the switch deleted it): never touched again
    _attached = nullptr;
    _pending = Stop{};
    Attach();
}

void GSCardTarget::Attach()
{
    GeneralSoundCard* card = Card();
    // A waiting edit is made at a boundary of the debug loop too
    GeneralSoundCard* want = _wanted || !_live.empty() || !_replay.empty() ? card : nullptr;
    if (want == _attached)
        return;
    if (_attached && _attached == card)
        _attached->setDebugHooks(nullptr);
    _attached = nullptr;
    _pending = Stop{};
    if (want && want->setDebugHooks(this))
        _attached = want;
}

MemoryPageDescriptor GSCardTarget::MapAddress(uint16_t address) const
{
    MemoryPageDescriptor where;
    GeneralSoundCard* card = Card();
    if (!card)
        return where;
    const GeneralSoundCard::CardPage page = card->mapCardAddress(address);
    if (page.kind == GeneralSoundCard::CardPage::None)
        return where;
    where.mode = static_cast<MemoryBankModeEnum>(page.kind);
    where.page = page.page;
    where.addressInPage = page.offset;
    where.slotShift = 14;
    return where;
}

uint32_t GSCardTarget::PageCount(MemoryBankModeEnum kind) const
{
    GeneralSoundCard* card = Card();
    if (!card || (kind != BANK_ROM && kind != BANK_RAM))
        return 0;
    return card->cardPageCount(static_cast<GeneralSoundCard::CardPage::Kind>(kind));
}

bool GSCardTarget::Registers(Z80Registers& out) const
{
    GeneralSoundCard* card = Card();
    Z80CpuRegisters regs{};
    if (!card || !card->cardRegisters(regs))
        return false;
    out = Z80Registers{};
    out.af = regs.af;
    out.bc = regs.bc;
    out.de = regs.de;
    out.hl = regs.hl;
    out.alt.af = regs.afAlt;
    out.alt.bc = regs.bcAlt;
    out.alt.de = regs.deAlt;
    out.alt.hl = regs.hlAlt;
    out.ix = regs.ix;
    out.iy = regs.iy;
    out.pc = regs.pc;
    out.sp = regs.sp;
    out.memptr = regs.memptr;
    out.i = regs.i;
    out.r_low = regs.r & 0x7F;
    out.r_hi = regs.r & 0x80;
    out.im = regs.im;
    out.iff1 = regs.iff1;
    out.iff2 = regs.iff2;
    out.halted = regs.halted;
    return true;
}

bool GSCardTarget::Peek(uint16_t address, uint8_t& out) const
{
    GeneralSoundCard* card = Card();
    return card && card->peekCardMemory(address, out);
}

void GSCardTarget::OnBreakpointDemand(const BreakpointTapDemand& demand)
{
    // Every active breakpoint of the card (a stop or a tap) needs the debug loop
    _wanted = demand.stop || demand.exec || demand.read || demand.write || demand.other;
    Attach();
}

void GSCardTarget::Note(uint16_t id, uint16_t address, uint8_t value, BreakpointHitKind kind, uint16_t pc)
{
    if (id == BRK_INVALID || _pending.hit)
        return;
    _pending.hit = true;
    _pending.breakpointId = id;
    _pending.address = address;
    _pending.value = value;
    _pending.kind = kind;
    _pending.pc = pc;
}

void GSCardTarget::Park(Stop stop, uint16_t boundaryPc)
{
    GeneralSoundCard* card = Card();
    stop.boundaryPc = boundaryPc;
    stop.cardTime = card ? card->cardTime() : 0;
    _lastStop = stop;
    _stopCount++;
    if (_context && _context->pEmulator)
        _context->pEmulator->OnTargetBreakpointHit(_id, *_breakpoints, stop.breakpointId, stop.address,
                                                   stop.kind, stop.pc);
}

void GSCardTarget::beforeStep(uint16_t pc)
{
    if (!_live.empty() || !_replay.empty()) [[unlikely]]
    {
        if (GeneralSoundCard* card = Card())
        {
            ApplyEdits(card);
            Z80CpuRegisters regs{};
            if (card->cardRegisters(regs))
                pc = regs.pc;  // a set_reg moved it
        }
    }
    if (_pending.hit) [[unlikely]]
    {
        const Stop stop = _pending;
        _pending = Stop{};
        Park(stop, pc);
    }
    const uint16_t id = _breakpoints->HandlePCChange(pc);
    if (id != BRK_INVALID) [[unlikely]]
    {
        Stop stop;
        stop.hit = true;
        stop.breakpointId = id;
        stop.address = pc;
        stop.kind = BreakpointHitKind::Execute;
        stop.pc = pc;
        Park(stop, pc);
    }
}

void GSCardTarget::endRun(uint16_t pc)
{
    if (!_pending.hit)
        return;
    const Stop stop = _pending;
    _pending = Stop{};
    Park(stop, pc);
}

uint8_t GSCardTarget::onMemoryRead(uint16_t pc, uint16_t addr, uint8_t value)
{
    if (!_replay.empty()) [[unlikely]]
        ReplayRead(ttd::TTDCardEdit::Read, addr, value);
    // A read tap's sync callback may hand the card another byte (fault injection, design §8A.7)
    Note(_breakpoints->HandleReplaceableRead(addr, value), addr, value, BreakpointHitKind::MemoryRead, pc);
    return value;
}

void GSCardTarget::onMemoryWrite(uint16_t pc, uint16_t addr, uint8_t value)
{
    Note(_breakpoints->HandleMemoryWrite(addr, value), addr, value, BreakpointHitKind::MemoryWrite, pc);
}

uint8_t GSCardTarget::onPortIn(uint16_t pc, uint16_t port, uint8_t value)
{
    if (!_replay.empty()) [[unlikely]]
        ReplayRead(ttd::TTDCardEdit::PortIn, port, value);
    Note(_breakpoints->HandleReplaceablePortIn(port, value), port, value, BreakpointHitKind::PortIn, pc);
    return value;
}

void GSCardTarget::onPortOut(uint16_t pc, uint16_t port, uint8_t value)
{
    Note(_breakpoints->HandlePortOut(port, value), port, value, BreakpointHitKind::PortOut, pc);
}

void GSCardTarget::onEvent(GSCardEvent kind, uint16_t port, uint8_t value)
{
    // The card hardware's events carry their detail in `port` (gsdebughooks.h)
    const uint16_t id = _breakpoints->HandleEvent(kind, value, kind >= GSCardEvent::DacFetch ? port : -1);
    if (id == BRK_INVALID)
        return;
    // The instruction the event came with (an accepted INT or NMI: the CPU already at the handler)
    Z80CpuRegisters regs{};
    const uint16_t pc = _attached && _attached->cardRegisters(regs) ? regs.pc : 0;
    Note(id, static_cast<uint16_t>(kind), value, BreakpointHitKind::Event, pc);
}

void GSCardTarget::onBusMasterWrite(uint8_t master, uint8_t ramPage, uint16_t offset, uint8_t value)
{
    const uint16_t id = _breakpoints->HandlePageWrite(BANK_RAM, ramPage, offset, value);
    if (id == BRK_INVALID || _pending.hit)
        return;
    // The card CPU was off the bus: the instruction it stands at
    Z80CpuRegisters regs{};
    const uint16_t pc = _attached && _attached->cardRegisters(regs) ? regs.pc : 0;
    Note(id, offset, value, BreakpointHitKind::DmaWrite, pc);
    _pending.master = master;
    _pending.page = ramPage;
}

void GSCardTarget::QueueHookEdits(uint32_t callbackId, bool view, HookEdits edits)
{
    if (edits.Empty())
        return;
    _live.push_back(LiveEdit{callbackId, view, std::move(edits)});
    Attach();
}

uint64_t GSCardTarget::CardFrame(const GeneralSoundCard* card) const
{
    const uint64_t frame = _context ? _context->emulatorState.frame_counter : 0;
    return card && card->inFrameEndCatchUp() && frame > 0 ? frame - 1 : frame;
}

void GSCardTarget::RecordReadEdit(uint32_t callbackId, bool port, uint16_t address, uint8_t value)
{
    ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    GeneralSoundCard* card = Card();
    if (!ttd || !card || !ttd->IsRecording() || ttd->IsReplayActive())
        return;
    ttd::TTDCardEdit edit;
    edit.cpu = static_cast<uint8_t>(_id);
    edit.kind = port ? ttd::TTDCardEdit::PortIn : ttd::TTDCardEdit::Read;
    edit.frame = CardFrame(card);
    edit.at = card->cardFrameTime();  // the step's start: the card's clock moves when the step is done
    edit.address = address;
    edit.bytes.push_back(value);
    const std::string source = "callback " + std::to_string(callbackId);
    ttd->RecordCardEdits(source.c_str(), {edit});
}

void GSCardTarget::ApplyEdits(GeneralSoundCard* card)
{
    const int64_t now = card->cardFrameTime();
    // The replay's edits: those the card reached; a read of a time already past was not met and goes
    size_t done = 0;
    while (done < _replay.size())
    {
        const ttd::TTDCardEdit& edit = _replay[done];
        const bool read = edit.kind == ttd::TTDCardEdit::Read || edit.kind == ttd::TTDCardEdit::PortIn;
        if (edit.at > now || (read && edit.at == now))
            break;
        if (edit.kind == ttd::TTDCardEdit::Bytes)
        {
            for (size_t k = 0; k < edit.bytes.size(); k++)
                card->pokeCardMemory(static_cast<uint16_t>(edit.address + k), edit.bytes[k]);
        }
        else if (edit.kind == ttd::TTDCardEdit::Cpu && edit.bytes.size() == sizeof(Z80CpuRegisters))
        {
            Z80CpuRegisters regs{};
            std::memcpy(&regs, edit.bytes.data(), sizeof(regs));
            card->setCardRegisters(regs);
        }
        done++;
    }
    _replay.erase(_replay.begin(), _replay.begin() + static_cast<std::ptrdiff_t>(done));
    if (_live.empty())
        return;

    // The hooks' writes: a session that opened since the call takes none (a view's) or keeps them (a logic one's)
    std::vector<LiveEdit> live;
    live.swap(_live);
    ttd::ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    if (ttd && ttd->IsReplayActive())
        return;
    const bool record = ttd && ttd->IsRecording();
    std::vector<ttd::TTDCardEdit> recorded;
    for (LiveEdit& item : live)
    {
        const CallbackRole role = item.view ? CallbackRole::View : CallbackRole::Logic;
        if (!ScriptCallbackManager::EditRefusal(_context, role).empty())
            continue;
        ttd::TTDCardEdit bytes;
        bytes.kind = ttd::TTDCardEdit::Bytes;
        auto flush = [&]() {
            if (!bytes.bytes.empty())
                recorded.push_back(bytes);
            bytes.bytes.clear();
        };
        for (const auto& [address, value] : item.edits.pokes)
        {
            card->pokeCardMemory(address, value);
            if (!record)
                continue;
            if (bytes.bytes.empty() || address != static_cast<uint16_t>(bytes.address + bytes.bytes.size()) ||
                address == 0)
            {
                flush();
                bytes.address = address;
            }
            bytes.bytes.push_back(value);
        }
        flush();
        Z80CpuRegisters regs{};
        if (!item.edits.regs.empty() && card->cardRegisters(regs))
        {
            for (const auto& [name, value] : item.edits.regs)
                SetRegister(regs, name, value);
            card->setCardRegisters(regs);
            if (record)
            {
                ttd::TTDCardEdit cpu;
                cpu.kind = ttd::TTDCardEdit::Cpu;
                cpu.bytes.resize(sizeof(regs));
                std::memcpy(cpu.bytes.data(), &regs, sizeof(regs));
                recorded.push_back(std::move(cpu));
            }
        }
        if (record && !recorded.empty())
        {
            for (ttd::TTDCardEdit& edit : recorded)
            {
                edit.cpu = static_cast<uint8_t>(_id);
                edit.frame = CardFrame(card);
                edit.at = now;
            }
            const std::string source = "callback " + std::to_string(item.callbackId);
            ttd->RecordCardEdits(source.c_str(), recorded);
            recorded.clear();
        }
    }
}

bool GSCardTarget::ReplayRead(ttd::TTDCardEdit::Kind kind, uint16_t address, uint8_t& value)
{
    GeneralSoundCard* card = Card();
    if (!card)
        return false;
    const int64_t now = card->cardFrameTime();
    for (auto it = _replay.begin(); it != _replay.end() && it->at <= now; ++it)
        if (it->kind == kind && it->at == now && it->address == address && !it->bytes.empty())
        {
            value = it->bytes[0];
            _replay.erase(it);
            return true;
        }
    return false;
}

void GSCardTarget::ClearReplayEdits()
{
    _replay.clear();
}

void GSCardTarget::AddReplayEdit(ttd::TTDCardEdit edit)
{
    _replay.push_back(std::move(edit));
}

void GSCardTarget::CommitReplayEdits()
{
    Attach();
}

bool GSCardTarget::SetRegister(Z80CpuRegisters& r, std::string name, uint16_t value)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    struct Wide
    {
        const char* name;
        uint16_t* field;
    };
    const Wide wide[] = {{"af", &r.af},       {"bc", &r.bc},       {"de", &r.de}, {"hl", &r.hl}, {"af'", &r.afAlt},
                         {"bc'", &r.bcAlt},   {"de'", &r.deAlt},   {"hl'", &r.hlAlt}, {"ix", &r.ix}, {"iy", &r.iy},
                         {"sp", &r.sp},       {"pc", &r.pc},       {"memptr", &r.memptr}};
    for (const Wide& w : wide)
        if (name == w.name)
        {
            *w.field = value;
            return true;
        }
    struct Half
    {
        const char* name;
        uint16_t* field;
        bool high;
    };
    const Half halves[] = {{"a", &r.af, true},   {"f", &r.af, false},  {"b", &r.bc, true},   {"c", &r.bc, false},
                           {"d", &r.de, true},   {"e", &r.de, false},  {"h", &r.hl, true},   {"l", &r.hl, false},
                           {"ixh", &r.ix, true}, {"ixl", &r.ix, false}, {"iyh", &r.iy, true}, {"iyl", &r.iy, false}};
    for (const Half& h : halves)
        if (name == h.name)
        {
            if (value > 0xFF)
                return false;
            *h.field = h.high ? static_cast<uint16_t>((*h.field & 0x00FF) | (value << 8))
                              : static_cast<uint16_t>((*h.field & 0xFF00) | value);
            return true;
        }
    struct Byte
    {
        const char* name;
        uint8_t* field;
        uint16_t max;
    };
    const Byte bytes[] = {{"i", &r.i, 0xFF}, {"r", &r.r, 0xFF}, {"im", &r.im, 2}, {"iff1", &r.iff1, 1}, {"iff2", &r.iff2, 1}};
    for (const Byte& b : bytes)
        if (name == b.name)
        {
            if (value > b.max)
                return false;
            *b.field = static_cast<uint8_t>(value);
            return true;
        }
    return false;
}

void GSReportHostEvent(EmulatorContext* context, GSCardEvent kind, uint8_t value, int arg)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    if (!debug || !context->pEmulator || !context->pCore)
        return;
    // Out of the debug mode only the taps hear it (a tap never needs the debug mode)
    BreakpointManager* breakpoints = debug->GetBreakpointsManager();
    const bool stops = context->pCore->GetZ80()->isDebugMode;
    if (!stops && !breakpoints->HasEventTaps())
        return;
    const uint16_t id = breakpoints->HandleEvent(kind, value, arg, stops);
    if (id != BRK_INVALID) [[unlikely]]
        context->pEmulator->OnBreakpointHit(id, static_cast<uint16_t>(kind), BreakpointHitKind::Event);
}
