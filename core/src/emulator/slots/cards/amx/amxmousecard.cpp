#include "amxmousecard.h"

#include <algorithm>
#include <string>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mousemanager.h"

namespace
{
void PutU64(uint8_t*& p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        *p++ = static_cast<uint8_t>(v >> (8 * i));
}

uint64_t GetU64(const uint8_t*& p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= static_cast<uint64_t>(*p++) << (8 * i);
    return v;
}

void PutU32(uint8_t*& p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        *p++ = static_cast<uint8_t>(v >> (8 * i));
}

uint32_t GetU32(const uint8_t*& p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= static_cast<uint32_t>(*p++) << (8 * i);
    return v;
}
} // namespace

/// The card's blob: u1 version, the PIO (Z80Pio::SaveState, 20 bytes), s2 pending X, s2 pending Y, u1 the buttons
/// (IMouseSink mask), then per axis u8 frame and u4 T-state of the next strobe's earliest time
class AmxMouseTtd final : public ttd::TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;

    explicit AmxMouseTtd(AmxMouseCard& card) : _card(card)
    {
    }

    size_t TTDStateSize() const override
    {
        return AmxMouseCard::kStateSize;
    }
    void TTDSaveState(uint8_t* dst) const override
    {
        _card.SaveState(dst);
    }
    void TTDLoadState(const uint8_t* src) override
    {
        _card.LoadState(src);
    }
    std::string TTDDeviceName() const override
    {
        return "AmxMouse";
    }
    ttd::TTDDeviceType TTDType() const override
    {
        return ttd::TTDDeviceType::AmxMouse;
    }
    uint64_t TTDHashState() const override
    {
        uint8_t blob[AmxMouseCard::kStateSize];
        TTDSaveState(blob);
        uint64_t hash = 0xcbf29ce484222325ull;
        for (uint8_t b : blob)
        {
            hash ^= b;
            hash *= 0x100000001b3ull;
        }
        return hash;
    }

private:
    AmxMouseCard& _card;
};

/// region <Construction>

std::unique_ptr<ICard> AmxMouseCard::Create(const CardContext& context)
{
    return std::make_unique<AmxMouseCard>(context);
}

AmxMouseCard::AmxMouseCard(const CardContext& context)
    : ICard(context), _context(context.emulator), _ttd(std::make_unique<AmxMouseTtd>(*this))
{
    if (_context != nullptr && _context->pMouseManager != nullptr)
        _context->pMouseManager->AddSink(this);
    Attach();
}

AmxMouseCard::~AmxMouseCard()
{
    // The cards go before the CPU and the mouse manager (Core::Release)
    if (_cpu != nullptr)
        _cpu->Im2Chain().Remove(this);
    if (_context != nullptr && _context->pMouseManager != nullptr)
        _context->pMouseManager->RemoveSink(this);
}

void AmxMouseCard::Attach()
{
    if (_cpu != nullptr || _context == nullptr || _context->pCore == nullptr)
        return;
    _cpu = _context->pCore->GetZ80();
    if (_cpu != nullptr)
        _cpu->Im2Chain().Add(this);
}

void AmxMouseCard::UpdateChain()
{
    Attach();
    if (_cpu != nullptr)
        _cpu->Im2Chain().Update();
}

uint8_t AmxMouseCard::ButtonLines(uint8_t activeLowMask)
{
    uint8_t lines = 0;
    if ((activeLowMask & 0x01) == 0)
        lines |= kButtonLeft;
    if ((activeLowMask & 0x02) == 0)
        lines |= kButtonRight;
    if ((activeLowMask & 0x04) == 0)
        lines |= kButtonMiddle;
    return lines;
}

/// endregion </Construction>

/// region <Time and strobes>

uint32_t AmxMouseCard::FrameT() const
{
    if (_context == nullptr)
        return 0;
    return _context->emulatorState.BaseToCpuT(_context->config.frame);
}

bool AmxMouseCard::CanStrobe(uint8_t axis) const
{
    const Z80Pio::Port& port = _pio.GetPort(axis);
    return _pending[axis] != 0 && !port.ip && !port.ius;
}

void AmxMouseCard::RunTo(uint32_t t)
{
    if (_context == nullptr || (_pending[0] == 0 && _pending[1] == 0))
        return;
    const uint64_t frame = _context->emulatorState.frame_counter;
    for (uint8_t axis = 0; axis < 2; axis++)
    {
        if (!CanStrobe(axis))
            continue;
        const Due& due = _due[axis];
        if (frame < due.frame || (frame == due.frame && t < due.t))
            continue;

        // X: XB high (bit 0 = 1) moving left; Y: YB low moving up (AMX Art's handlers, Lunter's sign bit)
        const bool negative = axis == 0 ? _pending[0] < 0 : _pending[1] > 0;
        const uint8_t lines = (axis == 0) ? (negative ? 0x01 : 0x00) : (negative ? 0x00 : 0x01);
        _pio.Strobe(axis, lines);
        _pending[axis] += _pending[axis] > 0 ? -1 : 1;
        _strobes++;

        const uint32_t frameT = FrameT();
        Due next{frame, t + std::max<uint32_t>(1, frameT / kStrobeDivider)};
        if (frameT != 0 && next.t >= frameT)
        {
            next.t -= frameT;
            next.frame++;
        }
        _due[axis] = next;
    }
}

/// endregion </Time and strobes>

/// region <Bus>

uint8_t AmxMouseCard::portDeviceInMethod(uint16_t port)
{
    bool drives = false;
    return portDeviceReadCycle(port, drives);
}

void AmxMouseCard::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    if (port & 0x0080)
        return;  // the buttons' select only reads
    const uint8_t axis = (port & 0x0020) ? 1 : 0;
    if (_cpu != nullptr)
        RunTo(_cpu->t);  // the strobes due before the write happen before it
    if (port & 0x0040)
        _pio.WriteControl(axis, value);
    else
        _pio.WriteData(axis, value);
    UpdateChain();  // an enable or a mask may arm or disarm the chain
}

uint8_t AmxMouseCard::portDeviceReadCycle(uint16_t port, bool& drives)
{
    if ((port & 0x00C0) == 0x0000)
    {
        // A data port: the strobes due before the read happen before it
        Attach();
        if (_cpu != nullptr)
            RunTo(_cpu->t);
        drives = true;
        const uint8_t value = _pio.ReadData((port & 0x0020) ? 1 : 0);
        UpdateChain();
        return value;
    }
    return Peek(port, drives);
}

uint8_t AmxMouseCard::Peek(uint16_t port, bool& drives) const
{
    drives = false;
    if ((port & 0x0080) == 0)
    {
        if (port & 0x0040)
            return 0xFF;  // the control registers are write-only (UM0081): the NMOS PIO leaves the bus alone
        drives = true;
        return _pio.PeekData((port & 0x0020) ? 1 : 0);
    }
    if ((port & 0x00E0) == 0x00C0)
    {
        // Each held button pulls its line low; the released ones drive nothing (the bus's idle level shows)
        const uint8_t pulled = ButtonLines(_buttons);
        drives = pulled != 0;
        return static_cast<uint8_t>(~pulled);
    }
    return 0xFF;
}

void AmxMouseCard::BusReset()
{
    _pio.Reset();
    _pending[0] = _pending[1] = 0;
    _due[0] = _due[1] = Due{};
    UpdateChain();
}

void AmxMouseCard::FrameStart()
{
    Attach();
}

/// endregion </Bus>

/// region <Mouse>

bool AmxMouseCard::IsMouseInUse() const
{
    for (uint8_t axis = 0; axis < 2; axis++)
    {
        const Z80Pio::Port& port = _pio.GetPort(axis);
        if (port.mode == Z80Pio::kModeInput && port.ie)
            return true;
    }
    return false;
}

void AmxMouseCard::OnMouseMotion(int dx, int dy)
{
    _pending[0] = std::clamp(_pending[0] + dx, -kMaxPending, kMaxPending);
    _pending[1] = std::clamp(_pending[1] + dy, -kMaxPending, kMaxPending);
    UpdateChain();
}

void AmxMouseCard::OnMouseButtons(uint8_t activeLowMask)
{
    _buttons = activeLowMask;
}

bool AmxMouseCard::HasUnreadMotion() const
{
    return IsMouseInUse() && (_pending[0] != 0 || _pending[1] != 0);
}

MouseDeviceStatus AmxMouseCard::DescribeMouse() const
{
    MouseDeviceStatus status;
    status.id = "amx";
    status.name = "AMX mouse (Z80 PIO interface, " + SlotId() + ")";
    status.kind = MouseDeviceKind::AmxPio;
    status.fitted = true;
    status.inUse = IsMouseInUse();
    status.wheel = false;
    status.buttons = 3;
    status.buttonMask = _buttons;
    status.hasPio = true;
    for (uint8_t axis = 0; axis < 2; axis++)
    {
        const Z80Pio::Port& port = _pio.GetPort(axis);
        MouseDeviceStatus::Pio::Port& out = status.pio.port[axis];
        out.mode = port.mode;
        out.vector = port.vector;
        out.data = _pio.PeekData(axis);
        out.ie = port.ie;
        out.ip = port.ip;
        out.ius = port.ius;
    }
    status.pio.pendingDx = _pending[0];
    status.pio.pendingDy = _pending[1];
    status.pio.buttonsPort = static_cast<uint8_t>(~ButtonLines(_buttons));
    status.pio.strobes = _strobes;
    return status;
}

/// endregion </Mouse>

/// region <Daisy chain>

uint8_t AmxMouseCard::DaisyState(uint32_t t)
{
    RunTo(t);
    return _pio.DaisyState();
}

uint8_t AmxMouseCard::DaisyAcknowledge()
{
    uint8_t vector = 0xFF;
    _pio.Acknowledge(vector);
    return vector;
}

void AmxMouseCard::DaisyReti()
{
    _pio.Reti();
}

bool AmxMouseCard::DaisyArmed() const
{
    for (uint8_t axis = 0; axis < 2; axis++)
    {
        const Z80Pio::Port& port = _pio.GetPort(axis);
        if ((port.ie && port.ip) || CanStrobe(axis))
            return true;
    }
    return false;
}

/// endregion </Daisy chain>

/// region <Controllers and time travel>

void AmxMouseCard::Connectors(std::vector<CardConnector>& out) const
{
    out.push_back({"amx", controllers::ConnectorKind::Amx, controllers::RetroDeviceKind::AmxMouse});
}

void AmxMouseCard::CollectTtdDevices(std::vector<CardTtdDevice>& out)
{
    out.push_back({_ttd.get(), SlotId() + "." + kCardId, nullptr});
}

void AmxMouseCard::SaveState(uint8_t* dst) const
{
    uint8_t* p = dst;
    *p++ = AmxMouseTtd::kVersion;
    _pio.SaveState(p);
    p += Z80Pio::kStateSize;
    for (int pending : _pending)
    {
        const auto v = static_cast<uint16_t>(static_cast<int16_t>(pending));
        *p++ = static_cast<uint8_t>(v);
        *p++ = static_cast<uint8_t>(v >> 8);
    }
    *p++ = _buttons;
    for (const Due& due : _due)
    {
        PutU64(p, due.frame);
        PutU32(p, due.t);
    }
}

void AmxMouseCard::LoadState(const uint8_t* src)
{
    const uint8_t* p = src;
    if (*p++ != AmxMouseTtd::kVersion)
        return;
    _pio.LoadState(p);
    p += Z80Pio::kStateSize;
    for (int& pending : _pending)
    {
        const auto v = static_cast<uint16_t>(p[0] | (p[1] << 8));
        p += 2;
        pending = std::clamp<int>(static_cast<int16_t>(v), -kMaxPending, kMaxPending);
    }
    _buttons = *p++;
    for (Due& due : _due)
    {
        due.frame = GetU64(p);
        due.t = GetU32(p);
    }
    UpdateChain();
}

/// endregion </Controllers and time travel>
