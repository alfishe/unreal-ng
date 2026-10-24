#include "controllertypes.h"

namespace controllers
{

namespace
{
struct KindName
{
    RetroDeviceKind kind;
    const char* name;
};
constexpr KindName kKindNames[] = {
    {RetroDeviceKind::None, "none"},
    {RetroDeviceKind::KempstonJoystick, "kempston-joystick"},
    {RetroDeviceKind::Md6Pad, "md6-pad"},
    {RetroDeviceKind::SinclairJoystick, "sinclair-joystick"},
    {RetroDeviceKind::CursorJoystick, "cursor-joystick"},
    {RetroDeviceKind::KeysJoystick, "keys-joystick"},
    {RetroDeviceKind::FullerJoystick, "fuller-joystick"},
    {RetroDeviceKind::AmxMouse, "amx-mouse"},
};
}  // namespace

const char* RetroDeviceKindName(RetroDeviceKind kind)
{
    for (const KindName& entry : kKindNames)
    {
        if (entry.kind == kind)
            return entry.name;
    }
    return "none";
}

bool RetroDeviceKindFromName(const std::string& name, RetroDeviceKind& kind)
{
    for (const KindName& entry : kKindNames)
    {
        if (name == entry.name)
        {
            kind = entry.kind;
            return true;
        }
    }
    return false;
}

const char* ConnectorKindName(ConnectorKind kind)
{
    switch (kind)
    {
        case ConnectorKind::Kempston:
            return "kempston";
        case ConnectorKind::NextDb9:
            return "next-db9";
        case ConnectorKind::KeyboardJoystick:
            return "keyboard-joystick";
        case ConnectorKind::Fuller:
            return "fuller";
        case ConnectorKind::Amx:
            return "amx";
    }
    return "keyboard-joystick";
}

const char* ControllerErrorCode(ControllerError error)
{
    switch (error)
    {
        case ControllerError::None:
            return "";
        case ControllerError::UnknownConnector:
            return "unknown-connector";
        case ControllerError::Incompatible:
            return "incompatible";
        case ControllerError::Fixed:
            return "fixed";
        case ControllerError::Recording:
            return "recording";
        case ControllerError::OwnedByReplay:
            return "owned-by-replay";
        case ControllerError::BadRequest:
            return "bad-request";
    }
    return "bad-request";
}

int ControllerErrorHttpStatus(ControllerError error)
{
    switch (error)
    {
        case ControllerError::None:
            return 200;
        case ControllerError::UnknownConnector:
            return 404;
        case ControllerError::BadRequest:
            return 400;
        case ControllerError::Incompatible:
        case ControllerError::Fixed:
        case ControllerError::Recording:
        case ControllerError::OwnedByReplay:
            return 409;
    }
    return 400;
}

}  // namespace controllers
