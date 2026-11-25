#pragma once

/// @file portplan.h
/// @brief The automation ports of one application instance, derived from a port base (`--port-base N | auto`,
/// `--ports-file <path>`).
///
/// Every automation server reads its port from an environment variable of its own (UNREAL_WEBAPI_PORT, ...). A port
/// base B gives all of them at once: WebAPI B, CLI B+100, MCP B+200, GDB B+300, DeZog B+400, ZRCP B+500 (the layout
/// the verification scripts use). `auto` searches for a base whose every port can be bound right now, so parallel
/// instances (agents, tests) need no hand-assigned bases. The application applies the plan by setting those
/// variables before the servers start; the servers' own "port is in use" paths stay as they are.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace portplan
{

/// One automation server's port: the name in the ports file, its environment variable, its offset from the base
struct PortRole
{
    const char* name;
    const char* envVar;
    int offset;
};

/// The WebSocket endpoints are served on the WebAPI port
inline constexpr std::array<PortRole, 6> kRoles = {{
    {"webapi", "UNREAL_WEBAPI_PORT", 0},
    {"cli", "UNREAL_CLI_PORT", 100},
    {"mcp", "UNREAL_MCP_PORT", 200},
    {"gdb", "UNREAL_GDB_PORT", 300},
    {"dezog", "UNREAL_DEZOG_PORT", 400},
    {"zrcp", "UNREAL_ZRCP_PORT", 500},
}};

/// The largest offset of kRoles
inline constexpr int kLastOffset = 500;

/// Where `auto` looks. The bases are `firstBase + k * step`, k = 0 .. : a step above kLastOffset, so two candidates
/// never share a port. The range stays below the ephemeral ports of Linux (32768+) and macOS / Windows (49152+),
/// which the OS hands out to outgoing connections, and above the servers' default ports
struct SearchRange
{
    int firstBase = 20000;
    int lastBase = 32000;   ///< the last candidate (inclusive)
    int step = 600;
    /// The candidate the search starts from (taken modulo the count); the rest follow upward and wrap around.
    /// Instances started at the same moment pass different values (their process ids), so they do not probe the same
    /// base first and race for it
    size_t startSlot = 0;
};

/// True when a listener can take the port now
using CanBind = std::function<bool(int port)>;

/// The first base of the range whose every port (kRoles) can be bound. 0 when there is none or the range is not
/// valid; `error` (optional) then says why
int FindFreePortBase(const SearchRange& range, const CanBind& canBind, std::string* error = nullptr);

/// True when a base gives valid ports for every role (1 .. 65535)
bool IsValidPortBase(long long base);

/// The port of kRoles[index] for a base
inline int PortOf(int base, size_t index) { return base + kRoles[index].offset; }

/// Binds a TCP socket to the port on all interfaces (the address the servers listen on) and closes it
bool CanBindTcpPort(int port);

/// The ports file: {"port_base": B, "webapi": .., "cli": .., "mcp": .., "gdb": .., "dezog": .., "zrcp": .., "pid": P}
std::string PortsFileJson(int base, int64_t pid);

/// Writes the ports file through a temporary file beside it and a rename: a reader sees no file or the whole one.
/// `path` is UTF-8. False with `error` (optional) when it cannot be written
bool WritePortsFile(const std::string& path, int base, int64_t pid, std::string* error = nullptr);

}  // namespace portplan
