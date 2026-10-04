// z84c15.cpp - the Z84C15's on-chip block: system control registers, chip
// selects, watchdog, the daisy chain (z84c15.h).

#include "z84c15.h"

#include "z84cpu-internal.h"

namespace Z84Lib
{

namespace
{
/// #F4: the order of the on-chip devices on the daisy chain, highest first
/// (0 CTC, 1 SIO, 2 PIO; MAME tmpz84c015.cpp:144-150; values 6 and 7 are
/// undefined in the data sheets, taken & 3 as MAME does). The board's own
/// /INT comes after them
constexpr uint8_t kPriority[6][3] = {
    {0, 1, 2}, {1, 0, 2}, {0, 2, 1}, {2, 1, 0}, {2, 0, 1}, {1, 2, 0},
};
constexpr uint8_t kSourcesPerDevice[3] = {4, 2, 2};  // CTC channels, SIO A / B receive, PIO A / B
constexpr int kMaxSources = 8;
}  // namespace

/// region <System registers>

uint32_t Z84SystemRegs::Cs0End() const
{
    if (!(mcr & 0x01))
        return 0;
    return ((csbr & 0x0Fu) + 1u) * 0x1000u;
}

bool Z84SystemRegs::Cs1Selects(uint16_t addr) const
{
    if (!(mcr & 0x02))
        return false;
    const uint8_t block = static_cast<uint8_t>(addr >> 12);
    return block <= (csbr >> 4) && block > (csbr & 0x0F);
}

/// endregion </System registers>

Z84C15::Z84C15()
{
    _cpu = Z84CpuCreate();
    _cpu->chipReti = [](void* chip) { static_cast<Z84C15*>(chip)->OnReti(); };
    _cpu->chipData = this;
    sio.onReturnFromInt = [this]() { ReturnFromIntInSio(); };
    PowerOn();
}

Z84C15::~Z84C15()
{
    Z84CpuDestroy(_cpu);
}

void Z84C15::PowerOn()
{
    // Reset values (PS0182 p. 316-321; MAME z84c015 / tmpz84c015 device_reset)
    system = Z84SystemRegs{};

    // The wait generator: WCR acts as #FF for the first 15 M1 cycles (p. 318)
    Z84CPU::Z84WaitGen& w = _cpu->wait;
    w.wcrProgrammed = 0;
    w.wcr = 0xFF;
    w.mwbr = system.mwbr;
    w.powerOnM1Left = 15;
    w.afterEd = 0;
    w.active = true;

    _wdtRunning = (system.wdtmr & 0x80) != 0;
    _wdtFired = false;
    _wdtStart = Now();
    _wdtClocksBefore = 0;

    Reset();
}

void Z84C15::Reset()
{
    ctc.Reset();
    sio.Reset();
    pio.Reset();
}

void Z84C15::SetClock(std::function<uint64_t()> clock)
{
    _clock = clock;
    ctc.SetClock(std::move(clock));
}

void Z84C15::SetSystemClockPeriod(uint32_t num, uint32_t den)
{
    if (!num || !den || (num == ctc.SystemClockNum() && den == ctc.SystemClockDen()))
        return;
    // The watchdog's clocks so far at the old rate
    const uint64_t now = Now();
    if (now > _wdtStart)
        _wdtClocksBefore += Detail::MulDivFloor(now - _wdtStart, ctc.SystemClockDen(), ctc.SystemClockNum());
    _wdtStart = now;
    ctc.SetSystemClockPeriod(num, den);
}

bool Z84C15::Owns(uint16_t port)
{
    return Z84OnChipPort(port);
}

uint8_t Z84C15::Read(uint8_t lowByte)
{
    if (lowByte >= 0x10 && lowByte <= 0x13)
        return ctc.Read(lowByte & 3);
    if (lowByte >= 0x18 && lowByte <= 0x1B)
        return sio.Read(lowByte);
    if (lowByte >= 0x1C && lowByte <= 0x1F)
        return pio.Read(lowByte & 3);

    switch (lowByte)
    {
        case 0xEE:
            return system.scrp;
        case 0xEF:
            switch (system.scrp)
            {
                case 0: return _cpu->wait.powerOnM1Left ? 0xFF : system.wcr;  // #FF in the power-on window
                case 1: return system.mwbr;
                case 2: return system.csbr;
                case 3: return system.mcr;
                default: return 0xFF;
            }
        case 0xF0:
            return system.wdtmr;
        default:
            return 0xFF;  // #F1 and #F4 are write-only
    }
}

void Z84C15::Write(uint8_t lowByte, uint8_t value)
{
    if (lowByte >= 0x10 && lowByte <= 0x13)
    {
        ctc.Write(lowByte & 3, value);
        return;
    }
    if (lowByte >= 0x18 && lowByte <= 0x1B)
    {
        sio.Write(lowByte, value);
        return;
    }
    if (lowByte >= 0x1C && lowByte <= 0x1F)
    {
        pio.Write(lowByte & 3, value);
        return;
    }

    Z84CPU::Z84WaitGen& w = _cpu->wait;
    switch (lowByte)
    {
        case 0xEE:
            system.scrp = value;
            break;
        case 0xEF:
            if (system.scrp == 0)
            {
                // A write ends the power-on window (p. 318) and programs the waits at once
                system.wcr = value;
                w.wcrProgrammed = value;
                w.wcr = value;
                w.powerOnM1Left = 0;
                w.afterEd = 0;
                w.active = value != 0;
            }
            else if (system.scrp == 1)
            {
                system.mwbr = value;
                w.mwbr = value;
            }
            else if (system.scrp == 2)
            {
                system.csbr = value;
            }
            else if (system.scrp == 3)
            {
                system.mcr = value;  // D4 (clock divider) is stored only: CLKIN boards ignore it
            }
            break;
        case 0xF0:
        {
            PollWatchdog();
            const bool wasEnabled = (system.wdtmr & 0x80) != 0;
            system.wdtmr = value;
            if (!wasEnabled && (value & 0x80))
            {
                _wdtRunning = true;
                ClearWatchdog();
            }
            break;
        }
        case 0xF1:
            PollWatchdog();
            system.wdtcr = value;
            if (value == 0x4E)
                ClearWatchdog();
            else if (value == 0xB1 && !(system.wdtmr & 0x80))
                _wdtRunning = false;  // disable: only after WDTE was cleared
            break;  // #DB unlocks the halt-mode bits: stored only (no halt modes modeled)
        case 0xF4:
            system.irqPriority = value;
            break;
        default:
            break;
    }
}

/// region <Watchdog>

uint64_t Z84C15::WatchdogDeadline() const
{
    const uint32_t shift = 16u + 2u * ((system.wdtmr >> 5) & 0x03);
    const uint64_t period = uint64_t{1} << shift;
    if (_wdtClocksBefore >= period)
        return _wdtStart;
    return _wdtStart + Detail::MulDivCeil(period - _wdtClocksBefore, ctc.SystemClockNum(), ctc.SystemClockDen());
}

void Z84C15::ClearWatchdog()
{
    _wdtStart = Now();
    _wdtClocksBefore = 0;
    _wdtFired = false;
}

void Z84C15::PollWatchdog()
{
    if (!_watchdogHandler || !_wdtRunning || _wdtFired)
        return;
    if (Now() >= WatchdogDeadline())
    {
        _wdtFired = true;
        _watchdogHandler();
    }
}

/// endregion </Watchdog>

/// region <State>

namespace
{
void Put64(uint8_t*& p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        *p++ = static_cast<uint8_t>(v >> (8 * i));
}

uint64_t Get64(const uint8_t*& p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= static_cast<uint64_t>(*p++) << (8 * i);
    return v;
}

void Put32(uint8_t*& p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        *p++ = static_cast<uint8_t>(v >> (8 * i));
}

uint32_t Get32(const uint8_t*& p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= static_cast<uint32_t>(*p++) << (8 * i);
    return v;
}
}  // namespace

void Z84C15::SaveState(uint8_t* dst) const
{
    uint8_t* p = dst;
    const uint8_t sys[8] = {system.scrp, system.wcr,   system.mwbr,  system.csbr,
                            system.mcr,  system.wdtmr, system.wdtcr, system.irqPriority};
    for (uint8_t b : sys)
        *p++ = b;

    const Z84CPU::Z84WaitGen& w = _cpu->wait;
    *p++ = w.wcrProgrammed;
    *p++ = w.wcr;
    *p++ = w.mwbr;
    *p++ = w.powerOnM1Left;
    *p++ = w.afterEd;
    *p++ = w.active ? 1 : 0;

    *p++ = _wdtRunning ? 1 : 0;
    *p++ = _wdtFired ? 1 : 0;
    Put64(p, _wdtStart);
    Put64(p, _wdtClocksBefore);

    Put32(p, ctc.SystemClockNum());
    Put32(p, ctc.SystemClockDen());
    *p++ = ctc.Vector();
    for (uint8_t i = 0; i < 4; i++)
    {
        const Z84Ctc::ChannelState& ch = ctc.GetChannel(i);
        *p++ = ch.control;
        *p++ = ch.timeConstant;
        *p++ = ch.awaitingConstant;
        *p++ = ch.running;
        *p++ = ch.waitingTrigger;
        *p++ = ch.down;
        Put64(p, ch.anchor);
        Put64(p, ch.zeroBase);
        Put64(p, ch.zeroSeen);
        *p++ = ch.ip;
        *p++ = ch.ius;
    }

    for (uint8_t i = 0; i < 2; i++)
    {
        const Z84Sio::Channel& ch = sio.GetChannel(i);
        for (uint8_t r : ch.wr)
            *p++ = r;
        *p++ = ch.pointer;
        for (uint8_t f : ch.fifo)
            *p++ = f;
        *p++ = ch.fifoCount;
        *p++ = ch.lastData;
        *p++ = ch.overrun;
        *p++ = ch.rxFirstArmed;
        *p++ = ch.rxFirstIp;
        *p++ = ch.rxIus;
    }

    for (uint8_t i = 0; i < 2; i++)
    {
        const Z84Pio::Port& port = pio.GetPort(i);
        const uint8_t bytes[11] = {port.mode, port.direction, port.output,    port.vector, port.intControl, port.mask,
                                   port.next, port.inputs,    port.condition, port.ip,     port.ius};
        for (uint8_t b : bytes)
            *p++ = b;
    }
}

void Z84C15::LoadState(const uint8_t* src)
{
    const uint8_t* p = src;
    system.scrp = *p++;
    system.wcr = *p++;
    system.mwbr = *p++;
    system.csbr = *p++;
    system.mcr = *p++;
    system.wdtmr = *p++;
    system.wdtcr = *p++;
    system.irqPriority = *p++;

    Z84CPU::Z84WaitGen& w = _cpu->wait;
    w.wcrProgrammed = *p++;
    w.wcr = *p++;
    w.mwbr = *p++;
    w.powerOnM1Left = *p++;
    w.afterEd = *p++;
    w.active = *p++ != 0;

    _wdtRunning = *p++ != 0;
    _wdtFired = *p++ != 0;
    _wdtStart = Get64(p);
    _wdtClocksBefore = Get64(p);

    const uint32_t clkNum = Get32(p);
    const uint32_t clkDen = Get32(p);
    ctc.RestoreSystemClockPeriod(clkNum, clkDen);
    ctc.SetVector(*p++);
    for (uint8_t i = 0; i < 4; i++)
    {
        Z84Ctc::ChannelState& ch = ctc.Channel(i);
        ch.control = *p++;
        ch.timeConstant = *p++;
        ch.awaitingConstant = *p++;
        ch.running = *p++;
        ch.waitingTrigger = *p++;
        ch.down = *p++;
        ch.anchor = Get64(p);
        ch.zeroBase = Get64(p);
        ch.zeroSeen = Get64(p);
        ch.ip = *p++;
        ch.ius = *p++;
    }

    for (uint8_t i = 0; i < 2; i++)
    {
        Z84Sio::Channel& ch = sio.ChannelState(i);
        for (uint8_t& r : ch.wr)
            r = *p++;
        ch.pointer = *p++;
        for (uint8_t& f : ch.fifo)
            f = *p++;
        ch.fifoCount = *p++;
        ch.lastData = *p++;
        ch.overrun = *p++;
        ch.rxFirstArmed = *p++;
        ch.rxFirstIp = *p++;
        ch.rxIus = *p++;
    }

    for (uint8_t i = 0; i < 2; i++)
    {
        Z84Pio::Port& port = pio.PortState(i);
        port.mode = *p++;
        port.direction = *p++;
        port.output = *p++;
        port.vector = *p++;
        port.intControl = *p++;
        port.mask = *p++;
        port.next = *p++;
        port.inputs = *p++;
        port.condition = *p++;
        port.ip = *p++;
        port.ius = *p++;
    }

    ctc.Refresh();
}

/// endregion </State>

/// region <Daisy chain>

int Z84C15::Order(Source* out) const
{
    uint8_t priority = static_cast<uint8_t>(system.irqPriority & 0x07);
    if (priority > 5)
        priority &= 0x03;
    static_assert(kSourcesPerDevice[0] + kSourcesPerDevice[1] + kSourcesPerDevice[2] == kMaxSources,
                  "every priority order lists all sources: callers pass kMaxSources entries");
    int n = 0;
    for (uint8_t device : kPriority[priority])
    {
        // n < kMaxSources never cuts anything off (static_assert above); it is the
        // bound gcc cannot derive through the tables (-Wstringop-overflow)
        for (uint8_t i = 0; i < kSourcesPerDevice[device] && n < kMaxSources; i++)
            out[n++] = Source{device, i};
    }
    return n;
}

bool Z84C15::Ip(Source s) const
{
    switch (s.device)
    {
        case 0: return ctc.GetChannel(s.index).ip != 0;
        case 1: return sio.RxIp(s.index);
        default: return pio.GetPort(s.index).ip != 0;
    }
}

bool Z84C15::Ius(Source s) const
{
    switch (s.device)
    {
        case 0: return ctc.GetChannel(s.index).ius != 0;
        case 1: return sio.GetChannel(s.index).rxIus != 0;
        default: return pio.GetPort(s.index).ius != 0;
    }
}

void Z84C15::SetIus(Source s, bool value)
{
    const uint8_t v = value ? 1 : 0;
    switch (s.device)
    {
        case 0: ctc.Channel(s.index).ius = v; break;
        case 1: sio.ChannelState(s.index).rxIus = v; break;
        default: pio.PortState(s.index).ius = v; break;
    }
}

uint8_t Z84C15::Vector(Source s) const
{
    switch (s.device)
    {
        case 0: return static_cast<uint8_t>(ctc.Vector() | (s.index << 1));
        case 1: return sio.RxVector(s.index);
        default: return pio.GetPort(s.index).vector;
    }
}

void Z84C15::ClearIpOnAcknowledge(Source s)
{
    // The CTC and the PIO latch a request until it is acknowledged; the SIO's
    // receive request lasts while the character waits (the read clears it)
    if (s.device == 0)
        ctc.Channel(s.index).ip = 0;
    else if (s.device == 2)
        pio.PortState(s.index).ip = 0;
}

bool Z84C15::IntPending()
{
    PollWatchdog();

    // Fast exit, asked at every instruction boundary: no source can request
    // (no CTC channel or SIO receiver with its interrupt enabled, no PIO
    // request latched) and none is under service
    bool possible = false;
    for (uint8_t i = 0; i < 4; i++)
        possible = possible || (ctc.GetChannel(i).control & 0x80) || ctc.GetChannel(i).ius;
    for (uint8_t i = 0; i < 2; i++)
        possible = possible || (sio.GetChannel(i).wr[1] & 0x18) || sio.GetChannel(i).rxIus || pio.GetPort(i).ip ||
                   pio.GetPort(i).ius;
    if (!possible)
        return false;

    ctc.Poll();

    Source order[kMaxSources];
    const int n = Order(order);
    for (int i = 0; i < n; i++)
    {
        if (Ius(order[i]))
            return false;  // a higher-priority service blocks the rest of the chain (IEO low)
        if (Ip(order[i]))
            return true;
    }
    return false;
}

uint8_t Z84C15::AcknowledgeInterrupt()
{
    Source order[kMaxSources];
    const int n = Order(order);
    for (int i = 0; i < n; i++)
    {
        if (Ius(order[i]))
            break;
        if (Ip(order[i]))
        {
            SetIus(order[i], true);
            ClearIpOnAcknowledge(order[i]);
            return Vector(order[i]);
        }
    }
    return 0xFF;  // nobody on the chip drives the bus
}

void Z84C15::OnReti()
{
    Source order[kMaxSources];
    const int n = Order(order);
    for (int i = 0; i < n; i++)
    {
        if (Ius(order[i]))
        {
            SetIus(order[i], false);
            return;
        }
    }
}

void Z84C15::ReturnFromIntInSio()
{
    for (uint8_t ch = 0; ch < 2; ch++)
    {
        if (sio.GetChannel(ch).rxIus)
        {
            sio.ChannelState(ch).rxIus = 0;
            return;
        }
    }
}

bool Z84C15::AnyUnderService() const
{
    Source order[kMaxSources];
    const int n = Order(order);
    for (int i = 0; i < n; i++)
    {
        if (Ius(order[i]))
            return true;
    }
    return false;
}

/// endregion </Daisy chain>

}  // namespace Z84Lib
