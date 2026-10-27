#pragma once

/// @file controllertypes.h
/// @brief The emulated side of the controller manager: retro device kinds, connector kinds, the control bits
/// (docs/inprogress/2026-10-17-controller-manager/retro-devices.md §1-§2).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace controllers
{

/// What a connector can hold. Kempston and the Next pad keep the devices that exist (Joystick, the Next decoder's
/// pads) and their journal kinds; the keyboard joysticks are overlays on the matrix, journaled as ConnectorState
enum class RetroDeviceKind : uint8_t
{
    None = 0,
    KempstonJoystick,  ///< #1F, the existing Joystick device
    Md6Pad,            ///< the Next's DB9 connectors: 12 buttons, PortDecoder::PadState layout
    SinclairJoystick,  ///< overlay: Sinclair 1 = 6 L, 7 R, 8 D, 9 U, 0 F; Sinclair 2 = 1 L, 2 R, 3 D, 4 U, 5 F
    CursorJoystick,    ///< overlay: 5 L, 6 D, 7 U, 8 R, 0 F
    KeysJoystick,      ///< overlay: a user key per control (QAOP + Space by default)
    FullerJoystick,    ///< the Fuller Box's stick: #7F active low, F---RLDU (a card's connector, journaled as ConnectorState)
    AmxMouse,          ///< the AMX mouse on its interface card: a mouse (the mouse journal kinds), not ConnectorState
};

enum class ConnectorKind : uint8_t
{
    Kempston,          ///< the board's Kempston port: fixed device
    NextDb9,           ///< a ZX Next DB9 connector: fixed md6-pad, projection by NR #05
    KeyboardJoystick,  ///< a joystick wired to the keyboard matrix: built-in sockets (+2 / +2A / +3) and the
                       ///< keyjoy sockets every machine has for keys-only games
    Fuller,            ///< the Fuller Box card's joystick socket: fixed fuller-joystick, while the card is fitted
    Amx,               ///< the AMX mouse interface card's mouse socket: fixed amx-mouse, while the card is fitted
};
constexpr unsigned kConnectorKinds = 5;

/// Retro control bits, the Kempston order every connector device shares (the Md6Pad adds bits 4-11 of PadState)
constexpr uint16_t kRight = 0x01;
constexpr uint16_t kLeft = 0x02;
constexpr uint16_t kDown = 0x04;
constexpr uint16_t kUp = 0x08;
constexpr uint16_t kFire = 0x10;
constexpr uint16_t kFire2 = 0x20;
constexpr uint16_t kFire3 = 0x40;
constexpr uint16_t kFire4 = 0x80;
/// The controls a keyboard joystick has: four directions and up to four fire buttons
constexpr unsigned kOverlayControls = 8;

/// kempston-joystick, md6-pad, sinclair-joystick, cursor-joystick, keys-joystick, fuller-joystick, amx-mouse ("none"
/// for None)
const char* RetroDeviceKindName(RetroDeviceKind kind);
/// The reverse; false when the name is unknown
bool RetroDeviceKindFromName(const std::string& name, RetroDeviceKind& kind);
/// kempston, next-db9, keyboard-joystick, fuller, amx
const char* ConnectorKindName(ConnectorKind kind);

/// The error of a connector operation; the text codes and HTTP statuses are the surfaces' (surfaces.md §4)
enum class ControllerError : uint8_t
{
    None,
    UnknownConnector,  ///< the selector matched nothing (404)
    Incompatible,      ///< the connector does not take this retro device (409)
    Fixed,             ///< the device is part of the board: cannot be unplugged or replaced (409)
    Recording,         ///< plugs are configuration: refused while TTD records (409)
    OwnedByReplay,     ///< TTD replay or RZX playback owns input (409)
    BadRequest,        ///< an unknown verb, option or device kind, a malformed value (400)
};

/// "unknown-connector", "incompatible", "fixed", "recording", "owned-by-replay", "bad-request" ("" for None)
const char* ControllerErrorCode(ControllerError error);
/// 200 for None
int ControllerErrorHttpStatus(ControllerError error);

struct ControllerResult
{
    ControllerError error = ControllerError::None;
    std::string message;

    bool Ok() const
    {
        return error == ControllerError::None;
    }
    static ControllerResult Success()
    {
        return {};
    }
    static ControllerResult Fail(ControllerError code, std::string text)
    {
        return {code, std::move(text)};
    }
};

/// One host-side sample of a connector: the retro controls a host device holds now (IHostInputSource)
struct ConnectorSample
{
    std::string connector;  ///< a connector selector (id, alias or kind:index)
    uint16_t mask = 0;      ///< the held controls, kRight ... bit order (Md6Pad: the 12 PadState bits)
};

/// The mouse a host source drives this frame (a pad's mouse targets, mapping-and-profiles.md §5)
struct MouseSample
{
    int dx = 0;           ///< counts this frame, + = right
    int dy = 0;           ///< counts this frame, + = up (MouseEvent)
    int wheel = 0;        ///< notches this frame
    uint8_t buttons = 0;  ///< held, active high: D0 left, D1 right, D2 middle
};

/// One frame of a host source (IHostInputSource::Sample)
struct HostSample
{
    /// The connectors it drives; a connector left out keeps its state
    std::vector<ConnectorSample> connectors;
    /// Every ZX key it holds (ZXKeysEnum values, the ZXKEY_EXT_* composites too): the whole set each frame, held on
    /// the host key overlay of the matrix, independent of the keys the user holds
    std::vector<uint8_t> keys;
    MouseSample mouse;

    void Clear()
    {
        connectors.clear();
        keys.clear();
        mouse = MouseSample{};
    }
};

}  // namespace controllers
