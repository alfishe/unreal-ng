#include "mouse.h"

#include <cstring>
#include <type_traits>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/debugmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "stdafx.h"

const char* const MC_MOUSE_MOVE = "MC_MOUSE_MOVE";
const char* const MC_MOUSE_BUTTON = "MC_MOUSE_BUTTON";
const char* const MC_MOUSE_WHEEL = "MC_MOUSE_WHEEL";

Mouse::Mouse(EmulatorContext* context)
{
    _context = context;
    if (_context)
    {
        _logger = _context->pModuleLogger;
    }

    Reset();
    ApplyConfiguration();

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observer = static_cast<Observer*>(this);
    messageCenter.AddObserver(MC_MOUSE_MOVE, observer, static_cast<ObserverCallbackMethod>(&Mouse::OnMouseMove));
    messageCenter.AddObserver(MC_MOUSE_BUTTON, observer, static_cast<ObserverCallbackMethod>(&Mouse::OnMouseButton));
    messageCenter.AddObserver(MC_MOUSE_WHEEL, observer, static_cast<ObserverCallbackMethod>(&Mouse::OnMouseWheel));
}

Mouse::~Mouse()
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observer = static_cast<Observer*>(this);
    messageCenter.RemoveObserver(MC_MOUSE_MOVE, observer, static_cast<ObserverCallbackMethod>(&Mouse::OnMouseMove));
    messageCenter.RemoveObserver(MC_MOUSE_BUTTON, observer, static_cast<ObserverCallbackMethod>(&Mouse::OnMouseButton));
    messageCenter.RemoveObserver(MC_MOUSE_WHEEL, observer, static_cast<ObserverCallbackMethod>(&Mouse::OnMouseWheel));
}

void Mouse::Reset()
{
    // Power-on values. Fitting (present, wheel) is configuration, not state - kept.
    _x.store(RESET_X, std::memory_order_relaxed);
    _y.store(RESET_Y, std::memory_order_relaxed);
    _buttons.store(0xFF, std::memory_order_relaxed);  // All buttons released (active-low)
    _wheel.store(0x00, std::memory_order_relaxed);
}

void Mouse::ApplyConfiguration()
{
    if (!_context)
        return;

    const CONFIG& config = _context->config;

    // Config Mouse= type selector (design §7.4). A config that never parsed the key
    // (unit-test contexts without an ini) keeps the device fitted.
    const bool fittedByConfig = !config.input.mouseConfigured || config.input.mouse == MOUSE_TYPE_KEMPSTON;
    const bool enabledByFeature =
        !_context->pFeatureManager || _context->pFeatureManager->isEnabled(Features::kKempstonMouse);
    SetPresent(fittedByConfig && enabledByFeature);

    // Config Wheel= (design §7.2): only KEMPSTON puts the counter on the bus
    SetWheelEnabled(config.input.mousewheel == MOUSE_WHEEL_KEMPSTON);
}

uint8_t Mouse::ReadRegister(uint8_t selectRegister) const
{
    if (!IsPresent())
    {
        return 0xFF;
    }

    switch (selectRegister)
    {
        case 0: {  // Buttons (+ wheel) register
            // Bits 2..0: active-low buttons (D2=Middle, D1=Right, D0=Left)
            // Bit 3: constant 1
            // Upper nibble: wheel counter when a wheel is fitted ({wheel[3:0], 1'b1, buttons[2:0]},
            // ZX Evo FPGA); otherwise part of the #FF base (hardware-reference §8 "wheel vs no wheel")
            const uint8_t buttons = GetButtons() & 0x07;
            if (IsWheelEnabled())
                return static_cast<uint8_t>(((GetWheel() & 0x0F) << 4) | 0x08 | buttons);
            return static_cast<uint8_t>(0xF8 | buttons);
        }

        case 1:  // X axis counter
            return GetX();

        case 2:  // Y axis counter
            return GetY();

        default:
            return 0xFF;
    }
}

void Mouse::Move(int dx, int dy)
{
    // Compare-and-swap: concurrent host and automation moves must both land
    uint8_t old = _x.load(std::memory_order_relaxed);
    while (!_x.compare_exchange_weak(old, static_cast<uint8_t>(old + dx), std::memory_order_relaxed))
    {
    }

    old = _y.load(std::memory_order_relaxed);
    while (!_y.compare_exchange_weak(old, static_cast<uint8_t>(old + dy), std::memory_order_relaxed))
    {
    }
}

void Mouse::SetButtons(uint8_t mask)
{
    _buttons.store(mask, std::memory_order_relaxed);
}

void Mouse::SetWheel(int delta)
{
    uint8_t old = _wheel.load(std::memory_order_relaxed);
    while (!_wheel.compare_exchange_weak(old, static_cast<uint8_t>((old + delta) & 0x0F), std::memory_order_relaxed))
    {
    }
}

void Mouse::SetCounters(uint8_t x, uint8_t y)
{
    _x.store(x, std::memory_order_relaxed);
    _y.store(y, std::memory_order_relaxed);
}

/// Event for this instance of the given kind, or nullptr (wrong payload, wrong
/// kind for the topic, or tagged for another emulator instance)
MouseEvent* Mouse::AcceptEvent(Message* message, MouseEventKind kind) const
{
    if (!message || !message->obj)
        return nullptr;

    auto* event = dynamic_cast<MouseEvent*>(message->obj);
    if (!event || event->kind != kind)
        return nullptr;

    if (!event->targetId.empty() && _context && _context->pEmulator && event->targetId != _context->pEmulator->GetId())
        return nullptr;

    return event;
}

/// Host input goes through the debug mouse manager when there is one (replay guard +
/// TTD journal); bare contexts (unit tests without a DebugManager) apply directly
static DebugMouseManager* HostInputFunnel(EmulatorContext* context)
{
    if (context && context->pDebugManager)
        return context->pDebugManager->GetMouseManager();
    return nullptr;
}

void Mouse::OnMouseMove([[maybe_unused]] int id, Message* message)
{
    MouseEvent* event = AcceptEvent(message, MouseEventKind::Move);
    if (!event)
        return;

    if (DebugMouseManager* funnel = HostInputFunnel(_context))
        funnel->ApplyHostMove(event->dx, event->dy);
    else
        Move(event->dx, event->dy);
}

void Mouse::OnMouseButton([[maybe_unused]] int id, Message* message)
{
    MouseEvent* event = AcceptEvent(message, MouseEventKind::Buttons);
    if (!event)
        return;

    if (DebugMouseManager* funnel = HostInputFunnel(_context))
        funnel->ApplyHostButtons(event->buttonMask);
    else
        SetButtons(event->buttonMask);
}

void Mouse::OnMouseWheel([[maybe_unused]] int id, Message* message)
{
    MouseEvent* event = AcceptEvent(message, MouseEventKind::Wheel);
    if (!event)
        return;

    if (DebugMouseManager* funnel = HostInputFunnel(_context))
        funnel->ApplyHostWheel(event->wheelSteps);
    else
        SetWheel(event->wheelSteps);
}

/// region <TTD>

namespace
{
/// Packed, padding-free blob (design §6.1): hashed byte-wise, so no implicit padding
struct MouseTTDBlob
{
    uint8_t version;
    uint8_t x;
    uint8_t y;
    uint8_t buttons;
    uint8_t wheel;
    uint8_t reserved[3];
};
static_assert(sizeof(MouseTTDBlob) == 8, "Kempston Mouse TTD blob size drift");
static_assert(std::is_trivially_copyable_v<MouseTTDBlob>, "Kempston Mouse TTD blob must be POD");

constexpr uint8_t kMouseTTDVersion = 1;
}  // namespace

size_t Mouse::TTDStateSize() const
{
    return sizeof(MouseTTDBlob);
}

void Mouse::TTDSaveState(uint8_t* dst) const
{
    MouseTTDBlob blob{};
    blob.version = kMouseTTDVersion;
    blob.x = GetX();
    blob.y = GetY();
    blob.buttons = GetButtons();
    blob.wheel = GetWheel();
    std::memcpy(dst, &blob, sizeof(blob));
}

void Mouse::TTDLoadState(const uint8_t* src)
{
    MouseTTDBlob blob{};
    std::memcpy(&blob, src, sizeof(blob));
    _x.store(blob.x, std::memory_order_relaxed);
    _y.store(blob.y, std::memory_order_relaxed);
    _buttons.store(blob.buttons, std::memory_order_relaxed);
    _wheel.store(blob.wheel & 0x0F, std::memory_order_relaxed);
}

uint64_t Mouse::TTDHashState() const
{
    // FNV-1a over the state fields (no padding, no version byte)
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint8_t byte : {GetX(), GetY(), GetButtons(), GetWheel()})
    {
        hash ^= byte;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

/// endregion </TTD>
