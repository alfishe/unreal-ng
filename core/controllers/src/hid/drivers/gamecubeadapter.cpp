#include "hid/drivers/gamecubeadapter.h"

#include <algorithm>

namespace controllers
{

namespace
{
enum Slot : size_t
{
    kSouth,  ///< A
    kWest,   ///< B
    kEast,   ///< X
    kNorth,  ///< Y
    kDpadLeft,
    kDpadRight,
    kDpadDown,
    kDpadUp,
    kMenu,  ///< Start
    kRb,    ///< Z
    kLstickX,
    kLstickY,
    kRstickX,  ///< the C stick
    kRstickY,
    kLt,
    kRt,
    kSlotCount,
};

const char* const kSlotNames[kSlotCount] = {
    "south",    "west",     "east",     "north",    "dpad.left", "dpad.right", "dpad.down", "dpad.up",
    "menu",     "rb",       "lstick.x", "lstick.y", "rstick.x",  "rstick.y",   "lt",        "rt",
};

constexpr uint8_t kInputReport = 0x21;
constexpr size_t kInputSize = 37;
constexpr size_t kPortBytes = 9;
constexpr uint8_t kStart = 0x13;
constexpr uint64_t kRetryMs = 1000;
}  // namespace

/// region <GameCubeAdapter>

std::vector<std::string> GameCubeAdapter::Controls()
{
    return {std::begin(kSlotNames), std::end(kSlotNames)};
}

GameCubeAdapter::GameCubeAdapter()
{
    for (Port& port : _ports)
        Reset(port);
}

void GameCubeAdapter::Reset(Port& port)
{
    // SDL's ResetAxisRange: 128 +- 88, the triggers from 40 (they rest higher)
    port.low.fill(128 - 88);
    port.high.fill(128 + 88);
    port.low[4] = port.low[5] = 40;
    port.values.assign(kSlotCount, 0.0f);
    port.seen.assign(kSlotCount, false);
}

float GameCubeAdapter::Axis(Port& port, size_t axis, uint8_t raw)
{
    port.low[axis] = std::min(port.low[axis], raw);
    port.high[axis] = std::max(port.high[axis], raw);
    const int span = port.high[axis] - port.low[axis];
    const float unit = span == 0 ? 0.0f : static_cast<float>(raw - port.low[axis]) / static_cast<float>(span);
    return axis >= 4 ? unit : unit * 2.0f - 1.0f;  // the triggers 0..1, the sticks -1..1
}

bool GameCubeAdapter::Decode(const uint8_t* report, size_t size, const PortSink& sink, std::vector<PortChange>& changes)
{
    if (report == nullptr || size < kInputSize || report[0] != kInputReport)
        return false;
    for (int index = 0; index < kPorts; index++)
    {
        Port& port = _ports[static_cast<size_t>(index)];
        const uint8_t* at = report + 1 + static_cast<size_t>(index) * kPortBytes;
        const bool connected = (at[0] & 0x30) != 0;  // 0x10 wired, 0x20 wireless
        if (connected != port.connected)
        {
            port.connected = connected;
            Reset(port);
            changes.push_back({index, connected});
        }
        if (!connected)
            continue;
        const auto set = [&](size_t slot, float value) {
            if (port.seen[slot] && port.values[slot] == value)
                return;
            port.seen[slot] = true;
            port.values[slot] = value;
            if (sink)
                sink(index, kSlotNames[slot], value);
        };
        for (size_t bit = 0; bit < 8; bit++)
            set(kSouth + bit, (at[1] >> bit) & 1 ? 1.0f : 0.0f);  // A B X Y left right down up
        set(kMenu, (at[2] & 0x01) != 0 ? 1.0f : 0.0f);
        set(kRb, (at[2] & 0x02) != 0 ? 1.0f : 0.0f);
        // The stick's up is larger; -1 is up here (hostcontrols.h)
        set(kLstickX, Axis(port, 0, at[3]));
        set(kLstickY, -Axis(port, 1, at[4]));
        set(kRstickX, Axis(port, 2, at[5]));
        set(kRstickY, -Axis(port, 3, at[6]));
        set(kLt, Axis(port, 4, at[7]));
        set(kRt, Axis(port, 5, at[8]));
    }
    return true;
}

/// endregion </GameCubeAdapter>

/// region <GameCubeProtocol>

std::optional<PadOutReport> GameCubeProtocol::Next(uint64_t nowMs, uint64_t& wakeMs)
{
    wakeMs = 0;
    if (_started)
        return std::nullopt;
    if (nowMs < _retryMs)
    {
        wakeMs = _retryMs;
        return std::nullopt;
    }
    _retryMs = nowMs + kRetryMs;
    PadOutReport out;
    out.bytes = {kStart};
    return out;
}

void GameCubeProtocol::OnWritten(bool ok, uint64_t nowMs)
{
    (void)nowMs;
    _started = ok;
}

/// endregion </GameCubeProtocol>

}  // namespace controllers
