#include "debugger/targets/gscardtarget.h"

#include "debugger/breakpoints/breakpointmanager.h"
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
    GeneralSoundCard* want = _wanted ? card : nullptr;
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

void GSReportHostEvent(EmulatorContext* context, GSCardEvent kind, uint8_t value)
{
    DebugManager* debug = context ? context->pDebugManager : nullptr;
    if (!debug || !context->pEmulator || !context->pCore || !context->pCore->GetZ80()->isDebugMode)
        return;
    const uint16_t id = debug->GetBreakpointsManager()->HandleEvent(kind, value);
    if (id != BRK_INVALID) [[unlikely]]
        context->pEmulator->OnBreakpointHit(id, static_cast<uint16_t>(kind), BreakpointHitKind::Event);
}
