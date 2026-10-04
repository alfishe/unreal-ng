#include "stdafx.h"

#include "sprinterinput.h"

#include "3rdparty/z84c15/z84c15.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"
#include "emulator/video/sprinter/sprinterintsource.h"

SprinterInput::SprinterInput(EmulatorContext* context, Z84Lib::Z84C15& chip, SprinterIntSource& intSource,
                             const SprinterPldState& pld)
    : _context(context), _chip(chip), _intSource(intSource), _pld(pld)
{
    const uint32_t baseHz = _context && _context->emulatorState.base_z80_frequency ? _context->emulatorState.base_z80_frequency : 3500000u;
    _keyboard.SetBaseClock(baseHz);
    _mouse.SetBaseClock(baseHz);
    _keyboard.SetClock([this]() { return Now(); });

    // The keyboard's clock and data reach SIO A; with ALL_MODE bits 0 and 3 the PLD raises an INT per byte
    _keyboard.SetByteSink([this](uint8_t value, [[maybe_unused]] uint64_t at) {
        if (!_chip.sio.Receive(0, value))
            _keyboardOverruns++;
        if (KeyboardIntEnabled())
            _intSource.LatchKeyboardInt();
        OnWireByte(value);
    });

    _mouse.SetSampler([this](uint8_t& x, uint8_t& y, uint8_t& buttons) { SampleMouse(x, y, buttons); });
    // SIO B samples the mouse's 1 200 baud line with the clock CTC ZC/TO0 gives it: off by more than the
    // tolerance (or no clock at all), the character is lost (a framing error, not modeled further)
    _mouse.SetByteSink([this](uint8_t value, [[maybe_unused]] uint64_t at) {
        if (MouseReceiverInTune())
        {
            _chip.sio.Receive(1, value);
            _mouseBytesReceived++;
        }
        else
            _mouseFramingErrors++;
    });

    if (_context && _context->pMouseManager)
        _context->pMouseManager->AddSink(this);
}

SprinterInput::~SprinterInput()
{
    if (_context && _context->pMouseManager)
        _context->pMouseManager->RemoveSink(this);
}

double SprinterInput::MouseReceiverBaud() const
{
    // WR4 bits 7-6: the clock mode (x1, x16, x32, x64); ZC/TO0 is SIO B's receive and transmit clock
    // (MAME sprinter.cpp:2006-2007). DSS 1.71: CTC 0 counts 875 kHz / 45, SIO B x16 = 1 215 baud
    static constexpr uint32_t kClockMode[4] = {1, 16, 32, 64};
    return _chip.ctc.OutputHz(0) / kClockMode[(_chip.sio.GetChannel(1).wr[4] >> 6) & 3];
}

bool SprinterInput::MouseReceiverInTune() const
{
    const double baud = MouseReceiverBaud();
    const double error = (baud - MsSerialMouse::kBaud) / MsSerialMouse::kBaud;
    return error <= kBaudTolerance && error >= -kBaudTolerance;
}

void SprinterInput::SampleMouse(uint8_t& x, uint8_t& y, uint8_t& buttons) const
{
    x = _mouseX.load(std::memory_order_relaxed);
    y = _mouseY.load(std::memory_order_relaxed);
    buttons = _mouseButtons.load(std::memory_order_relaxed);
}

void SprinterInput::OnMouseMotion(int dx, int dy)
{
    // Compare-and-swap: a host move and an automation move must both land
    uint8_t old = _mouseX.load(std::memory_order_relaxed);
    while (!_mouseX.compare_exchange_weak(old, static_cast<uint8_t>(old + dx), std::memory_order_relaxed))
    {
    }
    old = _mouseY.load(std::memory_order_relaxed);
    while (!_mouseY.compare_exchange_weak(old, static_cast<uint8_t>(old + dy), std::memory_order_relaxed))
    {
    }
    _viewUnread.fetch_or(static_cast<uint8_t>((dx ? 0x02 : 0) | (dy ? 0x04 : 0)), std::memory_order_relaxed);
}

uint64_t SprinterInput::Frame() const
{
    return _context ? _context->emulatorState.frame_counter : 0;
}

bool SprinterInput::PolledLately(uint64_t pollFrame) const
{
    const uint64_t frame = Frame();
    return pollFrame != kNeverPolled && frame >= pollFrame && frame - pollFrame <= kPolledWithinFrames;
}

bool SprinterInput::IsMouseInUse() const
{
    if (!_context)
        return true;  // no frame source (unit-test contexts): fitted is in use
    return PolledLately(_viewPollFrame.load(std::memory_order_relaxed)) ||
           PolledLately(_serialPollFrame.load(std::memory_order_relaxed));
}

bool SprinterInput::HasUnreadMotion() const
{
    if (PolledLately(_viewPollFrame.load(std::memory_order_relaxed)) && _viewUnread.load(std::memory_order_relaxed))
        return true;
    if (!PolledLately(_serialPollFrame.load(std::memory_order_relaxed)))
        return false;
    int dx = 0, up = 0;
    _mouse.PendingMotion(dx, up);
    return _mouse.GetState().sent < 3 || dx != 0 || up != 0;
}

MouseDeviceStatus SprinterInput::DescribeMouse() const
{
    MouseDeviceStatus status;
    status.id = "sprinter";
    status.name = "Sprinter board mouse: Microsoft serial mouse on SIO B + the PLD's Kempston view (#58)";
    status.kind = MouseDeviceKind::SerialMicrosoft;
    status.fitted = IsMouseFitted();
    status.inUse = IsMouseInUse();
    status.wheel = false;
    status.buttons = 3;  // the Kempston view shows three, the serial packet left and right
    status.hasPorts = true;
    status.portButtons = PeekMouseView(0xFADF);
    status.portX = PeekMouseView(0xFBDF);
    status.portY = PeekMouseView(0xFFDF);
    const BoardMouse board = GetBoardMouse();
    status.x = board.x;
    status.y = board.y;
    status.buttonMask = board.buttons;

    status.hasSerial = true;
    MouseDeviceStatus::Serial& serial = status.serial;
    const MsSerialMouse::State& line = _mouse.GetState();
    const Z84Lib::Z84Sio::Channel& sioB = _chip.sio.GetChannel(1);
    serial.baud = MsSerialMouse::kBaud;
    serial.receiverBaud = MouseReceiverBaud();
    serial.receiverInTune = MouseReceiverInTune();
    serial.receiverEnabled = (sioB.wr[3] & 0x01) != 0;
    serial.packetInFlight = line.sent < 3;
    for (int i = 0; i < 3; i++)
        serial.packet[i] = line.packet[i];
    serial.packetBytesSent = line.sent;
    _mouse.PendingMotion(serial.pendingDx, serial.pendingDy);
    serial.packetsSent = _mouse.PacketsSent();
    serial.bytesReceived = _mouseBytesReceived;
    serial.framingErrors = _mouseFramingErrors;
    serial.fifoCount = sioB.fifoCount;
    for (int i = 0; i < 3; i++)
        serial.fifo[i] = sioB.fifo[i];
    serial.overrun = _chip.sio.OverrunLatched(1);
    return status;
}

void SprinterInput::OnMouseButtons(uint8_t activeLowMask)
{
    _mouseButtons.store(activeLowMask, std::memory_order_relaxed);
}

void SprinterInput::OnMouseCounters(uint8_t x, uint8_t y)
{
    _mouseX.store(x, std::memory_order_relaxed);
    _mouseY.store(y, std::memory_order_relaxed);
    _viewUnread.store(0x06, std::memory_order_relaxed);
}

SprinterInput::BoardMouse SprinterInput::GetBoardMouse() const
{
    BoardMouse mouse{};
    SampleMouse(mouse.x, mouse.y, mouse.buttons);
    return mouse;
}

void SprinterInput::SetBoardMouse(const BoardMouse& mouse)
{
    _mouseX.store(mouse.x, std::memory_order_relaxed);
    _mouseY.store(mouse.y, std::memory_order_relaxed);
    _mouseButtons.store(mouse.buttons, std::memory_order_relaxed);
}

uint8_t SprinterInput::ReadMouseView(uint16_t port) const
{
    // A program's read: polling (IsMouseInUse), and X (A10 = 0) or Y taken
    _viewPollFrame.store(Frame(), std::memory_order_relaxed);
    if (port & 0x0100)
        _viewUnread.fetch_and(static_cast<uint8_t>((port & 0x0400) == 0 ? ~0x02u : ~0x04u), std::memory_order_relaxed);
    return PeekMouseView(port);
}

uint8_t SprinterInput::PeekMouseView(uint16_t port) const
{
    // MAME sprinter.cpp case 0x58: #FADF buttons, #FBDF X, #FFDF Y (the Kempston address bits A8, A10)
    uint8_t x = 0, y = 0, buttons = 0xFF;
    SampleMouse(x, y, buttons);
    if ((port & 0x0100) == 0)
        return static_cast<uint8_t>(0xF8 | (buttons & 0x07));
    return (port & 0x0400) == 0 ? x : y;
}

uint64_t SprinterInput::Now() const
{
    if (!_context)
        return 0;
    const Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    const uint32_t multiplier = _context->emulatorState.current_z80_frequency_multiplier ? _context->emulatorState.current_z80_frequency_multiplier : 1u;
    return _context->emulatorState.t_states + (z80 ? z80->t / multiplier : 0);
}

void SprinterInput::OnPcKey(PcKey key, bool pressed)
{
    // The board's actions (reset, turbo) come from the bytes on the wire (OnWireByte), not from the host key
    _keyboard.OnPcKey(key, pressed);
    if (_onStepHookChange)
        _onStepHookChange();
}

void SprinterInput::OnWireByte(uint8_t value)
{
    // KBD.TDF, at the end of each 11-bit frame (KB_CT counting the idle clock down): at KB_CT 3 the modifier
    // flags, KB_F12 and KB_RESET take the byte with the KB_OFF of the byte before; at KB_CT 1 KB_EXT takes "this
    // was #E0" and the KB_F12 / KB_RESET preset makes the edge; at KB_CT 0 KB_OFF takes "this was #F0" unless
    // KB_EXT. The byte patterns: KB_CTRL_X & KB_XXX = #14, KB_ALT_X & KB_XXX = #11, KB_SH_X = #12 or #59,
    // KB_F12 = #07, KB_RESET = KB_ALT_X & #x11xxxx0x = #71
    const uint8_t flags = _pldKeyboard;
    const bool off = (flags & kPldOff) != 0;
    const bool ctrl = (flags & kPldCtrl) != 0;
    const bool alt = (flags & kPldAlt) != 0;
    const bool f12 = value == 0x07 && !off;
    const bool reset = value == 0x71 && !off && ctrl && alt;

    uint8_t next = flags;
    auto update = [&](uint8_t bit, bool match) {
        if (match)
            next = static_cast<uint8_t>(off ? (next & ~bit) : (next | bit));
    };
    update(kPldCtrl, value == 0x14);
    update(kPldAlt, value == 0x11);
    update(kPldShift, value == 0x12 || value == 0x59);
    if (value == 0xE0)
        next |= kPldExt;
    else
    {
        next = static_cast<uint8_t>(next & ~kPldExt);
        next = static_cast<uint8_t>(value == 0xF0 ? (next | kPldOff) : (next & ~kPldOff));
    }
    _pldKeyboard = next;

    // TEST_SWITCH = TFF(!KB_SH & !KB_CTRL & !KB_ALT, KB_F12) -> TURBO_HAND; /RESET = KB_RESET & SOFT_RESET
    if (f12 && !(next & (kPldShift | kPldCtrl | kPldAlt)) && _onTurboSwitch)
        _onTurboSwitch();
    if (reset && _onReset)
        _onReset();
}

bool SprinterInput::PldActionPending() const
{
    const Ps2KeyboardStream::State& s = _keyboard.GetState();
    for (uint8_t i = 0; i < s.count; i++)
    {
        const uint8_t value = s.queue[(s.head + i) % Ps2KeyboardStream::kQueueSize];
        if (value == 0x07 || value == 0x71)
            return true;
    }
    const auto repeat = static_cast<PcKey>(s.repeatKey);
    return repeat == PcKey::Function12 || repeat == PcKey::Delete || repeat == PcKey::KeypadDecimal;
}

void SprinterInput::ReleaseAllPcKeys()
{
    _keyboard.ReleaseAllPcKeys();
    if (_onStepHookChange)
        _onStepHookChange();
}

bool SprinterInput::KeyboardIntEnabled() const
{
    return (_pld.allMode & 0x09) == 0x09;
}

void SprinterInput::BeforeAllModeWrite()
{
    _keyboard.Advance(Now());
}

void SprinterInput::BeforeChipAccess(uint8_t lowByte)
{
    switch (lowByte)
    {
        case 0x18:
        case 0x19:
            _keyboard.Advance(Now());
            break;
        case 0x1A:
        case 0x1B:
            _serialPollFrame.store(Frame(), std::memory_order_relaxed);
            _mouse.Advance(Now());
            break;
        default:
            break;
    }
}

void SprinterInput::Advance()
{
    const uint64_t now = Now();
    _keyboard.Advance(now);
}

void SprinterInput::Rebase()
{
    const uint64_t now = Now();
    _keyboard.Rebase(now);
    _mouse.Rebase(now);
}

void SprinterInput::Clear()
{
    _keyboard.Clear();
    _mouse.Clear();
    _keyboardOverruns = 0;
    _pldKeyboard = 0;
    _mouseBytesReceived = 0;
    _viewPollFrame.store(kNeverPolled, std::memory_order_relaxed);
    _serialPollFrame.store(kNeverPolled, std::memory_order_relaxed);
    _viewUnread.store(0, std::memory_order_relaxed);
}
