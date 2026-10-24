#pragma once

/// @file gamecubeadapter.h
/// @brief Nintendo's GameCube controller adapter for the Wii U / Switch (WUP-028, 057E:0337): one USB HID device, four
/// controller ports. Source: SDL3's src/joystick/hidapi/SDL_hidapi_gamecube.c (libsdl-org/SDL 3cffe56317b9).
///
/// - The adapter sends nothing until it gets output report 0x13 (one byte); SDL waits 10 ms after it. Rumble is
///   output 0x11 with one on / off byte per port, and only works with the adapter's second (power) cable in.
/// - Input 0x21, 37 bytes: four ports of 9 bytes after the id. Byte 0 the port's state (0x10 a wired controller,
///   0x20 a WaveBird, 0x04 the power cable), 1 the buttons A B X Y left right down up from bit 0, 2 Start Z R L
///   (the triggers' digital ends), then the stick x / y, the C stick x / y and the triggers L / R (0-255, the sticks'
///   up larger).
/// - The axes are scaled as SDL does: the range starts at 128 +- 88 (the triggers from 40) and widens to whatever a
///   controller reaches.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "hid/drivers/padprotocol.h"

namespace controllers
{

class GameCubeAdapter
{
public:
    static constexpr uint16_t kNintendo = 0x057E;
    static constexpr uint16_t kAdapter = 0x0337;
    static constexpr int kPorts = 4;
    static bool Handles(uint16_t vendor, uint16_t product)
    {
        return vendor == kNintendo && product == kAdapter;
    }
    /// The controls of a GameCube controller (the POC 033 picture's names: Z is rb, the C stick the right stick)
    static std::vector<std::string> Controls();

    /// A control of the controller in @p port (0-3)
    using PortSink = std::function<void(int port, const std::string& control, float value)>;
    struct PortChange
    {
        int port = 0;
        bool connected = false;
    };

    GameCubeAdapter();
    /// Reads input report 0x21: a controller that came or went goes to @p changes (before its controls), the
    /// controls that changed to @p sink. False for any other report
    bool Decode(const uint8_t* report, size_t size, const PortSink& sink, std::vector<PortChange>& changes);
    bool Connected(int port) const
    {
        return port >= 0 && port < kPorts && _ports[static_cast<size_t>(port)].connected;
    }

private:
    struct Port
    {
        bool connected = false;
        std::array<uint8_t, 6> low{};
        std::array<uint8_t, 6> high{};
        std::vector<float> values;
        std::vector<bool> seen;
    };
    void Reset(Port& port);
    float Axis(Port& port, size_t axis, uint8_t raw);

    std::array<Port, kPorts> _ports;
};

/// The adapter's start (IPadProtocol, run by a PadPump): output report 0x13 once; no rumble yet
class GameCubeProtocol : public IPadProtocol
{
public:
    std::optional<PadOutReport> Next(uint64_t nowMs, uint64_t& wakeMs) override;
    void OnWritten(bool ok, uint64_t nowMs) override;
    void SetRumble(const RumbleLevels& levels) override
    {
        (void)levels;
    }
    std::optional<PadOutReport> Final() override
    {
        return std::nullopt;
    }

private:
    bool _started = false;
    uint64_t _retryMs = 0;  ///< a refused start is tried again a second later
};

}  // namespace controllers
