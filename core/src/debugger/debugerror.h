#pragma once

/// @file debugerror.h
/// @brief The one error `code` of the debugger's commands and reads on every surface (debugger model protocol §4.5):
/// a short machine-readable name beside the surface's own error text, which stays as it was. REST and WebSocket
/// bodies carry it as "code" beside "error" and "message", MCP in its error text and structured content, Lua and
/// Python in the error tables / dicts they return, the CLI after the message.

#include <string>

namespace DebugError
{
inline constexpr const char* kCpuUnavailable = "cpu_unavailable";    ///< the CPU named is not there now (no card CPU)
inline constexpr const char* kInvalidArgument = "invalid_argument";  ///< a bad name, address, range or value
inline constexpr const char* kNotSupported = "not_supported";        ///< the capability is not this CPU's (main only)
inline constexpr const char* kNotFound = "not_found";                ///< no such instance, breakpoint, label, ...
inline constexpr const char* kNotPaused = "not_paused";              ///< the command needs the machine paused
inline constexpr const char* kRunControlHeld = "run_control_held";   ///< another surface or client holds run control
inline constexpr const char* kRevoked = "revoked";                   ///< the user took control back: do not retry
inline constexpr const char* kRefused = "refused";                   ///< the machine's state refuses it (the recorded past)
inline constexpr const char* kBusy = "busy";                         ///< no coherent moment now: try again
inline constexpr const char* kInternal = "internal";                 ///< the emulator could not do it

/// The code an HTTP status stands for where the route names none: 400 invalid_argument, 404 not_found, 409 refused,
/// 503 busy, any other 4xx invalid_argument, 5xx internal; "" below 400
inline const char* OfHttpStatus(int status)
{
    if (status < 400)
        return "";
    switch (status)
    {
        case 404: return kNotFound;
        case 409: return kRefused;
        case 503: return kBusy;
        default: return status < 500 ? kInvalidArgument : kInternal;
    }
}

/// "message (code: name)": the CLI's line for an error with a code
inline std::string WithCode(const std::string& message, const char* code)
{
    return code && *code ? message + " (code: " + code + ")" : message;
}
}  // namespace DebugError
