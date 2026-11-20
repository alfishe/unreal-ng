#include "debugger/targets/gscardtarget.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

#include "common/filehelper.h"
#include "common/signaturecache.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
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
    _labels = std::make_unique<LabelManager>(context, DebugCpuName(id), [this](uint8_t window, bool& rom, uint16_t& page) {
        GeneralSoundCard* card = Card();
        if (!card)
            return false;
        const GeneralSoundCard::CardPage where = card->mapCardAddress(static_cast<uint16_t>(window << 14));
        if (where.kind == GeneralSoundCard::CardPage::None)
            return false;
        rom = where.kind == GeneralSoundCard::CardPage::Rom;
        page = where.page;
        return true;
    });
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
    ApplySymbolBundles();
}

std::vector<std::string> GSCardTarget::ApplySymbolBundles()
{
    // The card's ROM pages by number, as for the main ROM (Emulator::ApplySymbolBundles); the card bundles are a
    // folder of their own, so a main ROM's bundle never lands in a card's store
    const std::string folder = Emulator::SymbolBundleFolder();
    GeneralSoundCard* card = Card();
    if (folder.empty() || !card || !_labels)
        return {};
    std::vector<std::string> pages;
    const uint32_t count = card->cardPageCount(GeneralSoundCard::CardPage::Rom);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t* page = card->cardRomPage(i);
        pages.push_back(page ? SignatureCache::Sha256Hex(page, PAGE_SIZE) : std::string());
    }
    return _labels->ApplyBundles(FileHelper::PathCombine(folder, "gs"), pages);
}

void GSCardTarget::Attach()
{
    GeneralSoundCard* card = Card();
    // A waiting edit is made at a boundary of the debug loop too, and an armed step parks at one
    GeneralSoundCard* want = _wanted || !_live.empty() || !_replay.empty() || StepArmed() || Scanning() ? card : nullptr;
    if (want == _attached)
        return;
    if (_attached && _attached == card)
        _attached->setDebuggerHooks(nullptr);
    _attached = nullptr;
    _pending = Stop{};
    if (want && want->setDebuggerHooks(this))
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

bool GSCardTarget::HitClock(bool event, DebugTargetHitClock& out) const
{
    const GeneralSoundCard* card = Card();
    if (!card)
        return false;
    out.frame = CardFrame(card);
    out.t = static_cast<uint32_t>(std::max<int64_t>(0, card->cardFrameTime()));
    out.cpuTime = card->cardTime();
    out.pc = _accessPc;
    if (event || _busMasterAccess)
    {
        // An event or a bus master's write comes with no instruction of its own: the one the card CPU stands at
        Z80CpuRegisters regs{};
        out.pc = card->cardRegisters(regs) ? regs.pc : 0;
    }
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
    _stopWanted.store(demand.stop, std::memory_order_release);
    Attach();
    UpdateTight();
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
    // Any stop ends an armed step: a breakpoint on the way is where it ended
    _stepKind.store(StepKind::None, std::memory_order_release);
    if (!_context || !_context->pEmulator)
        return;
    _parkedInside.store(true, std::memory_order_release);
    if (stop.step)
        _context->pEmulator->OnTargetStep(_id, boundaryPc);
    else
        _context->pEmulator->OnTargetBreakpointHit(_id, *_breakpoints, stop.breakpointId, stop.address, stop.kind,
                                                   stop.pc);
    _parkedInside.store(false, std::memory_order_release);
}

std::string GSCardTarget::ArmStep(StepKind kind, uint32_t count, uint16_t target)
{
    GeneralSoundCard* card = Card();
    Z80CpuRegisters regs{};
    if (!card || !card->cardRegisters(regs))
        return std::string("cpu ") + Name() + " not available: no card with a CPU in its slot";
    if (StepArmed())
        return "a step of cpu " + std::string(Name()) + " is already under way";
    if (kind == StepKind::None)
        return "no step";
    // Parked inside the card's run, the instruction at PC runs first and the next boundary is after it; else the
    // next boundary is that instruction's own
    const bool inside = ParkedInside();
    const uint16_t pc = regs.pc;
    _stepSp = regs.sp;
    _stepReturning = false;
    _stepSkip = (inside ? 0u : 1u) + std::max<uint32_t>(count, 1) - 1;
    if (kind == StepKind::Over)
    {
        if (StepOverTarget(card, pc, _stepTarget))
            _stepSkip = inside ? 0u : 1u;
        else
        {
            kind = StepKind::In;
            _stepSkip = inside ? 0u : 1u;
        }
    }
    else if (kind == StepKind::Out)
    {
        // Parked inside, the instruction at PC runs without a boundary of its own first
        _stepSkip = 0;
        _stepReturning = inside && IsReturnAt(card, pc);
        _stepReturnSp = regs.sp;
    }
    else if (kind == StepKind::To)
    {
        // The boundary of the instruction at PC is passed: a cursor at PC means its next run
        _stepTarget = target;
        _stepSkip = inside ? 0u : 1u;
    }
    _stepKind.store(kind, std::memory_order_release);
    Attach();
    UpdateTight();
    return {};
}

void GSCardTarget::CancelStep()
{
    _stepKind.store(StepKind::None, std::memory_order_release);
    UpdateTight();  // a step that ended inside the card's run left tight mode on for this call
}

void GSCardTarget::UpdateTight()
{
    if (_context && _context->pDebugManager)
        _context->pDebugManager->UpdateCardTight();
}

bool GSCardTarget::StepOverTarget(GeneralSoundCard* card, uint16_t pc, uint16_t& target) const
{
    std::vector<uint8_t> bytes(Z80Disassembler::MAX_INSTRUCTION_LENGTH, 0);
    for (size_t i = 0; i < bytes.size(); i++)
        card->peekCardMemory(static_cast<uint16_t>(pc + i), bytes[i]);
    // The machine's disassembler, or one of its own for a card without a machine (tests)
    std::unique_ptr<Z80Disassembler> own;
    Z80Disassembler* disassembler =
        _context && _context->pDebugManager ? _context->pDebugManager->GetDisassembler().get() : nullptr;
    if (!disassembler)
        disassembler = (own = std::make_unique<Z80Disassembler>(_context)).get();
    if (!disassembler->shouldStepOver(bytes))
        return false;
    uint8_t length = 0;
    disassembler->disassembleSingleCommand(bytes, pc, &length);
    target = static_cast<uint16_t>(pc + std::max<uint8_t>(length, 1));
    return true;
}

bool GSCardTarget::IsReturnAt(GeneralSoundCard* card, uint16_t pc) const
{
    uint8_t opcode = 0;
    uint8_t next = 0;
    if (!card || !card->peekCardMemory(pc, opcode))
        return false;
    if (opcode == 0xC9 || (opcode & 0xC7) == 0xC0)  // RET, RET cc
        return true;
    if (opcode != 0xED || !card->peekCardMemory(static_cast<uint16_t>(pc + 1), next))
        return false;
    // RETN, RETI and their aliases: ED 45 / 4D / 55 / 5D / 65 / 6D / 75 / 7D
    return (next & 0xC7) == 0x45;
}

bool GSCardTarget::StepBoundary(uint16_t pc)
{
    // A direct run of the main CPU (its step) takes the card along: the card's step waits for the machine's own run
    if (_context && _context->pEmulator && _context->pEmulator->IsDirectStepping())
        return false;
    const StepKind kind = _stepKind.load(std::memory_order_acquire);
    GeneralSoundCard* card = Card();
    Z80CpuRegisters regs{};
    switch (kind)
    {
        case StepKind::In:
            if (_stepSkip > 0)
            {
                _stepSkip--;
                return false;
            }
            break;
        case StepKind::Over:
            if (_stepSkip > 0)
            {
                _stepSkip--;
                return false;
            }
            if (pc != _stepTarget || !card || !card->cardRegisters(regs) || regs.sp < _stepSp)
                return false;
            break;
        case StepKind::Out:
            if (!card || !card->cardRegisters(regs))
                return false;
            // A return ran when SP rose over the return's own (RET cc not taken leaves it): else this boundary may
            // be the next return
            if (_stepReturning && regs.sp > _stepReturnSp)
                break;
            _stepReturning = regs.sp >= _stepSp && IsReturnAt(card, pc);
            _stepReturnSp = regs.sp;
            return false;
        case StepKind::To:
            if (_stepSkip > 0)
            {
                _stepSkip--;
                return false;
            }
            if (pc != _stepTarget)
                return false;
            break;
        default:
            return false;
    }
    Stop stop;
    stop.step = true;
    stop.address = pc;
    stop.pc = pc;
    Park(stop, pc);
    return true;
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
    // A TTD scan: nothing parks, no step ends; its hits and its capture
    if (_scanning.load(std::memory_order_relaxed)) [[unlikely]]
    {
        if (GeneralSoundCard* card = Card())
            ScanBoundary(card, pc);
        return;
    }
    bool parked = false;
    if (_pending.hit) [[unlikely]]
    {
        const Stop stop = _pending;
        _pending = Stop{};
        Park(stop, pc);
        parked = true;
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
        parked = true;
    }
    // An armed step last: a stop at this boundary already ended it
    if (!parked && _stepKind.load(std::memory_order_relaxed) != StepKind::None) [[unlikely]]
        StepBoundary(pc);
}

void GSCardTarget::endRun(uint16_t pc)
{
    // A scan's pending hit waits for the next boundary (or the scan's end): where a live run would park
    if (_scanning.load(std::memory_order_relaxed)) [[unlikely]]
        return;
    if (_pending.hit)
    {
        const Stop stop = _pending;
        _pending = Stop{};
        Park(stop, pc);
    }
    // Out of the card's loop: the hooks a step alone asked for come off
    if (!StepArmed() && !_wanted && _attached && _live.empty() && _replay.empty()) [[unlikely]]
        Attach();
}

uint8_t GSCardTarget::onMemoryRead(uint16_t pc, uint16_t addr, uint8_t value)
{
    if (!_replay.empty()) [[unlikely]]
        ReplayRead(ttd::TTDCardEdit::Read, addr, value);
    _accessPc = pc;
    // A read tap's sync callback may hand the card another byte (fault injection, design §8A.7)
    Note(_breakpoints->HandleReplaceableRead(addr, value), addr, value, BreakpointHitKind::MemoryRead, pc);
    return value;
}

void GSCardTarget::onMemoryWrite(uint16_t pc, uint16_t addr, uint8_t value)
{
    _accessPc = pc;
    Note(_breakpoints->HandleMemoryWrite(addr, value), addr, value, BreakpointHitKind::MemoryWrite, pc);
}

uint8_t GSCardTarget::onPortIn(uint16_t pc, uint16_t port, uint8_t value)
{
    if (!_replay.empty()) [[unlikely]]
        ReplayRead(ttd::TTDCardEdit::PortIn, port, value);
    _accessPc = pc;
    Note(_breakpoints->HandleReplaceablePortIn(port, value), port, value, BreakpointHitKind::PortIn, pc);
    return value;
}

void GSCardTarget::onPortOut(uint16_t pc, uint16_t port, uint8_t value)
{
    _accessPc = pc;
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
    _busMasterAccess = true;
    const uint16_t id = _breakpoints->HandlePageWrite(BANK_RAM, ramPage, offset, value);
    _busMasterAccess = false;
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

/// region <TTD card scans>

bool GSCardTarget::SaveCardState(GeneralSoundCard* card, CardState& out)
{
    out.valid = false;
    if (!card)
        return false;
    out.blob.resize(card->TTDStateSize());
    if (!out.blob.empty())
        card->TTDSaveState(out.blob.data());
    out.regions.clear();
    if (auto* source = dynamic_cast<ttd::ITTDRegionSource*>(card))
    {
        std::vector<ttd::TTDDeviceRegion> regions;
        source->TTDRegions(regions);
        for (const ttd::TTDDeviceRegion& region : regions)
        {
            const uint8_t* memory = region.desc.memory;
            out.regions.emplace_back(memory ? memory : nullptr, memory ? memory + region.desc.bytes : nullptr);
        }
    }
    out.valid = true;
    return true;
}

bool GSCardTarget::LoadCardState(GeneralSoundCard* card, const CardState& state)
{
    if (!card || !state.valid || state.blob.size() != card->TTDStateSize())
        return false;
    if (auto* source = dynamic_cast<ttd::ITTDRegionSource*>(card))
    {
        std::vector<ttd::TTDDeviceRegion> regions;
        source->TTDRegions(regions);
        if (regions.size() != state.regions.size())
            return false;
        for (size_t i = 0; i < regions.size(); i++)
        {
            uint8_t* memory = regions[i].desc.memory;
            if (!memory || state.regions[i].size() != regions[i].desc.bytes)
                continue;
            std::memcpy(memory, state.regions[i].data(), state.regions[i].size());
            // The engine's next capture stores what the copy wrote (a restore after it decodes it again)
            if (regions[i].tracker)
                regions[i].tracker->MarkAll();
        }
    }
    if (!state.blob.empty())
        card->TTDLoadState(state.blob.data());
    return true;
}

void GSCardTarget::BeginScan(uint64_t captureAt, bool breakpoints, uint64_t hitsBefore)
{
    _scanCaptureAt = captureAt;
    _scanHitsBefore = hitsBefore;
    _scanBreakpoints = breakpoints;
    _scanStartedPast = false;
    _scanSawBoundary = false;
    _capture = ScanCapture{};
    _scanLastHit = ScanHit{};
    _scanHits = 0;
    _scanStack = false;
    _scanStackFound = false;
    _scanStackCount = 0;
    _scanTrace = nullptr;
    _scanStepKind = StepKind::None;
    _scanStepFound = false;
    _scanStepStarted = false;
    _scanFirstAfter = UINT64_MAX;
    _scanFirstHit = ScanHit{};
    _pending = Stop{};
    if (breakpoints)
        _breakpoints->BeginReplayEvaluate();
    _scanning.store(true, std::memory_order_release);
    Attach();
    UpdateTight();
}

void GSCardTarget::EndScan()
{
    if (!_scanning.load(std::memory_order_acquire))
        return;
    // A bus or event hit of the last instruction run: the card parks for it at its next boundary, which has this count
    if (_scanBreakpoints && _pending.hit)
    {
        uint64_t count = 0;
        GeneralSoundCard* card = Card();
        if (card && card->cardInstructionCount(count))
            ScanNoteHit(_pending, count);
    }
    _pending = Stop{};
    if (_scanBreakpoints)
        _breakpoints->EndReplayEvaluate();
    _scanBreakpoints = false;
    _scanStack = false;
    _scanTrace = nullptr;
    _scanStepKind = StepKind::None;
    _scanFirstAfter = UINT64_MAX;
    _scanning.store(false, std::memory_order_release);
    Attach();
    UpdateTight();
}

void GSCardTarget::ScanNoteHit(const Stop& stop, uint64_t count)
{
    _scanHits++;
    if (count > _scanFirstAfter && (!_scanFirstHit.hit || count < _scanFirstHit.count))
    {
        _scanFirstHit.hit = true;
        _scanFirstHit.count = count;
        _scanFirstHit.breakpointId = stop.breakpointId;
        _scanFirstHit.address = stop.address;
        _scanFirstHit.value = stop.value;
        _scanFirstHit.kind = stop.kind;
        _scanFirstHit.pc = stop.pc;
    }
    if (count >= _scanHitsBefore || (_scanLastHit.hit && count < _scanLastHit.count))
        return;
    _scanLastHit.hit = true;
    _scanLastHit.count = count;
    _scanLastHit.breakpointId = stop.breakpointId;
    _scanLastHit.address = stop.address;
    _scanLastHit.value = stop.value;
    _scanLastHit.kind = stop.kind;
    _scanLastHit.pc = stop.pc;
}

void GSCardTarget::ScanStackLevel(uint16_t level, uint64_t before)
{
    _scanStack = true;
    _scanStackLevel = level;
    _scanStackBefore = before;
    _scanStackFound = false;
    _scanStackCount = 0;
}

void GSCardTarget::ScanStep(StepKind kind, uint64_t after)
{
    _scanStepKind = kind;
    _scanStepAfter = after;
    _scanStepStarted = false;
    _scanStepLast = after;
    _scanStepTarget = 0;
    _scanStepSp = 0;
    _scanStepReturning = false;
    _scanStepReturnSp = 0;
    _scanStepFound = false;
    _scanStepCount = 0;
}

void GSCardTarget::ScanFirstHitAfter(uint64_t after)
{
    _scanFirstAfter = after;
    _scanFirstHit = ScanHit{};
}

void GSCardTarget::ScanStepBoundary(GeneralSoundCard* card, uint16_t pc, uint64_t count)
{
    if (_scanStepFound || count < _scanStepAfter)
        return;
    Z80CpuRegisters regs{};
    if (!card->cardRegisters(regs))
        return;
    // The step's start: the card standing at `after` (as ArmStep arms a step from a boundary, not parked inside)
    if (!_scanStepStarted)
    {
        if (count != _scanStepAfter)
            return;
        _scanStepStarted = true;
        _scanStepSp = regs.sp;
        if (_scanStepKind == StepKind::Over && !StepOverTarget(card, pc, _scanStepTarget))
            _scanStepKind = StepKind::In;   // nothing to pass as a whole: a step in
        else if (_scanStepKind == StepKind::Out)
        {
            _scanStepReturning = IsReturnAt(card, pc);
            _scanStepReturnSp = regs.sp;
        }
        return;
    }
    // Each later boundary once, in order, decided as StepBoundary decides it for a live step
    if (count <= _scanStepLast)
        return;
    _scanStepLast = count;
    bool ends = false;
    if (_scanStepKind == StepKind::In)
        ends = true;
    else if (_scanStepKind == StepKind::Over)
        ends = pc == _scanStepTarget && regs.sp >= _scanStepSp;
    else if (_scanStepKind == StepKind::Out)
    {
        ends = _scanStepReturning && regs.sp > _scanStepReturnSp;
        if (!ends)
        {
            _scanStepReturning = regs.sp >= _scanStepSp && IsReturnAt(card, pc);
            _scanStepReturnSp = regs.sp;
        }
    }
    if (ends)
    {
        _scanStepFound = true;
        _scanStepCount = count;
    }
}

void GSCardTarget::ScanBoundary(GeneralSoundCard* card, uint16_t pc)
{
    uint64_t count = 0;
    if (!card->cardInstructionCount(count))
        return;
    if (_scanTrace)
        _scanTrace(card, count, pc);
    if (_scanStepKind != StepKind::None)
        ScanStepBoundary(card, pc, count);
    if (_scanStack && count < _scanStackBefore && (!_scanStackFound || count > _scanStackCount))
    {
        Z80CpuRegisters regs{};
        if (card->cardRegisters(regs) &&
            static_cast<int16_t>(static_cast<uint16_t>(regs.sp - _scanStackLevel)) >= 0)
        {
            _scanStackFound = true;
            _scanStackCount = count;
        }
    }
    if (_scanBreakpoints)
    {
        // A bus or event hit of the instruction just run parks here, then an execution breakpoint at this PC
        if (_pending.hit)
        {
            ScanNoteHit(_pending, count);
            _pending = Stop{};
        }
        const uint16_t id = _breakpoints->HandlePCChange(pc);
        if (id != BRK_INVALID)
        {
            Stop stop;
            stop.hit = true;
            stop.breakpointId = id;
            stop.address = pc;
            stop.kind = BreakpointHitKind::Execute;
            stop.pc = pc;
            ScanNoteHit(stop, count);
        }
    }
    if (!_scanSawBoundary)
    {
        _scanSawBoundary = true;
        _scanStartedPast = _scanCaptureAt != UINT64_MAX && count > _scanCaptureAt;
    }
    if (_capture.captured || count != _scanCaptureAt)
        return;
    _capture.captured = true;
    _capture.count = count;
    _capture.pc = pc;
    _capture.cardTime = card->cardTime();
    SaveCardState(card, _capture.state);
    if (_context)
    {
        const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
        _capture.frameCounter = _context->emulatorState.frame_counter;
        if (z80)
        {
            _capture.t = z80->t;
            _capture.instrStartT = z80->instrStartT;
            _capture.ttdInstrStart = _context->emulatorState.TtdTInFrame(z80->instrStartT);
            _capture.m1pc = z80->m1_pc;
        }
        _capture.catchingUp = _context->pDebugManager && _context->pDebugManager->CatchingUp();
    }
    _capture.frameEnd = card->inFrameEndCatchUp();
}

/// endregion </TTD card scans>
