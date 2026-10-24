#pragma once

/// @file steampad.h
/// @brief Valve's Steam Controllers on their vendor HID interface: the 2015 one (28DE:1102 on the cable, 1142 the
/// wireless dongle) and the 2026 one, Triton (1302 on the cable, 1303 over Bluetooth LE, 1304 / 1305 its dongles).
/// Sources: SDL3's src/joystick/hidapi/SDL_hidapi_steam.c, SDL_hidapi_steam_triton.c and steam/controller_structs.h,
/// controller_constants.h (libsdl-org/SDL 3cffe56317b9).
///
/// - Out of the box both are in "lizard mode": a keyboard and a mouse, no game pad. The 2015 one leaves it with
///   feature reports (report id 0, 64 bytes: a message type, its length, its data): clear the digital mappings
///   (0x81), load the default settings (0x8E), set the settings (0x87: packet version 2, both trackpads to "none",
///   no absolute mouse smoothing); SDL's ResetSteamController. The dongle asks for the wireless state first (0xB4),
///   and a pad that connects to it later is set up again. Triton takes one setting, lizard mode off (feature report
///   1: 0x87, 3, setting 9 = 0), which its watchdog undoes unless it comes again: SDL sends it every 3 s.
/// - The 2015 input report (no id): version 0x0001, a type, a length, then for type 1 the state: a packet number,
///   64 button bits (bytes 3-4 of them the triggers 0-255), the left pad / stick and the right pad (16-bit signed
///   x, y each). The left pair is the stick when the left pad is not touched, the pad when it is; the right pad gives
///   the right stick, turned 15 degrees as SDL does. Type 3 is the dongle's wireless event (1 a pad went, 2 / 3 one
///   came). Bluetooth LE (1105 / 1106) uses other, segmented reports: not read here.
/// - Triton's input report 0x42 / 0x45 / 0x47: a sequence number, 32 button bits, the triggers 0-32767, two sticks
///   (16-bit signed, up positive), the trackpads; 0x79 / 0x46 the dongle's wireless state (1 gone, 2 there).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hid/drivers/padprotocol.h"
#include "hid/hidpad.h"

namespace controllers
{

enum class SteamModel : uint8_t
{
    Controller,        ///< the 2015 Steam Controller on the cable (28DE:1102)
    ControllerDongle,  ///< one of the four pads of its wireless dongle (1142)
    Triton,            ///< the 2026 Steam Controller on the cable or Bluetooth LE (1302, 1303)
    TritonDongle,      ///< one pad of its dongle (1304 Proteus, 1305 Nereid)
};

class SteamPad
{
public:
    static constexpr uint16_t kValve = 0x28DE;
    /// The models read here; nullopt for anything else (the 2015 pad over Bluetooth LE included)
    static std::optional<SteamModel> ModelOf(uint16_t vendor, uint16_t product);
    static bool IsDongle(SteamModel model)
    {
        return model == SteamModel::ControllerDongle || model == SteamModel::TritonDongle;
    }
    static bool IsTriton(SteamModel model)
    {
        return model == SteamModel::Triton || model == SteamModel::TritonDongle;
    }
    /// The controls a model gives (the POC 033 picture's names: misc.1 the left pad's click, paddles the grips)
    static std::vector<std::string> Controls(SteamModel model);

    enum class Event : uint8_t
    {
        None,          ///< not a report of this protocol
        State,         ///< a state report: its changes went to the sink
        Connected,     ///< a dongle says a pad came
        Disconnected,  ///< a dongle says the pad went
    };

    /// What a report is, without reading it
    static Event EventOf(SteamModel model, const uint8_t* report, size_t size);

    explicit SteamPad(SteamModel model);
    /// Reads one input report (bytes[0] the report id for Triton, none for the 2015 pad)
    Event Decode(const uint8_t* report, size_t size, const ControlSink& sink);
    SteamModel Model() const
    {
        return _model;
    }
    /// A pad that went: every control back at rest (the sink hears the ones that were not)
    void Release(const ControlSink& sink);

private:
    Event DecodeController(const uint8_t* report, size_t size, const ControlSink& sink);
    Event DecodeTriton(const uint8_t* report, size_t size, const ControlSink& sink);
    void Set(size_t slot, float value, const ControlSink& sink);

    SteamModel _model;
    std::vector<float> _values;
    std::vector<bool> _seen;
    int16_t _lastStick[2] = {};  ///< the 2015 pad's stick while the left pad is touched and both interleave
};

/// The set-up that turns lizard mode off (IPadProtocol, run by a PadPump): feature reports only; no rumble yet
class SteamProtocol : public IPadProtocol
{
public:
    static constexpr uint64_t kTritonRepeatMs = 3000;  ///< SDL: the watchdog brings lizard mode back otherwise

    explicit SteamProtocol(SteamModel model);

    /// The 2015 pad's set-up, in order (each 65 bytes: report id 0 and the 64 of the message)
    static std::vector<std::vector<uint8_t>> ControllerSetup();
    /// Triton's lizard mode off (64 bytes, report id 1 first)
    static std::vector<uint8_t> TritonLizardOff();

    std::optional<PadOutReport> Next(uint64_t nowMs, uint64_t& wakeMs) override;
    void OnInput(const uint8_t* report, size_t size, uint64_t nowMs) override;
    void SetRumble(const RumbleLevels& levels) override
    {
        (void)levels;
    }
    std::optional<PadOutReport> Final() override
    {
        return std::nullopt;
    }

private:
    void QueueSetup();

    SteamModel _model;
    std::vector<std::vector<uint8_t>> _queue;
    uint64_t _nextTritonMs = 0;
    bool _connected;  ///< a pad is there (a dongle's slot: after its connect event or a state report)
};

}  // namespace controllers
