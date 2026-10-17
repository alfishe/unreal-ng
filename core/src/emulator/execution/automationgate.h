#pragma once

/// @file automationgate.h
/// @brief The automation gate (owner decision 2026-10-11): every automation surface - WebAPI, MCP (through the
/// WebAPI), CLI, Lua / Python - opens a run-control session for a client that changes the machine without having
/// taken control (EmulatorContext::EnterImplicitRunControl). The GUI then shows the remote control and locks, and
/// the user can take control back, as for a session the client took itself. Reads, a pause, profilers, captures and
/// display settings open none. The client is told and advised to take and release control explicitly.
///
/// This file holds what the surfaces share: which HTTP requests change the machine, and the advice text.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace automation_gate
{
/// The implicit session's idle lease (owner 2026-10-11: 15 s; an explicit HTTP session keeps 60 s). A client that
/// never releases it holds the window this long after its last request
constexpr std::chrono::seconds kImplicitLease{15};

/// What the gate tells a client it opened a session for
constexpr const char* kAdvice =
    "Session opened implicitly: this client changed the emulator without taking control. Take control first "
    "(POST /control/session, MCP take_control, CLI session take, take_control()) and release it when done.";

/// Does a request to /api/v1/emulator/{id}<subpath> change the machine? `method`: "GET", "POST", ...; `subpath`: the
/// path after the instance id ("" for the instance itself, "/run_frames", "/memory/write", ...). Reads never do;
/// of the rest, all but the pause, the session itself, profilers, analyzers, coverage, captures, labels and symbols,
/// audio mixing and capture, display and recording settings, and the debugger's own journals
bool ChangesMachine(const std::string& method, const std::string& subpath);

/// Does a CLI command change the machine? `command` as typed ("run_frames", "memory"), `args` after it. Commands that
/// always do (runs, steps, resume, reset, breakpoints, input, port writes) and the changing forms of the others
/// (memory write / fill / load, tape load / play / ..., snapshot load, TTD seeks and steps, disk insert / write, ...)
bool CliCommandChangesMachine(const std::string& command, const std::vector<std::string>& args);

/// A short client name from an HTTP User-Agent: its first product token ("python-urllib/3.12", "curl/8.7.1");
/// "HTTP client" when there is none
std::string ClientFromUserAgent(const std::string& userAgent);

/// Companion clients (docs/inprogress/2026-10-16-companion-client): a paired device acting as the user's hands passes
/// the gate without a session; it may do anything the user may (owner 2026-10-16), within the scopes the user granted
/// at pairing. Every changing action belongs to exactly one scope
enum CompanionScope : uint32_t
{
    ScopeRun = 1u << 0,       ///< resume, reset, NMI, turbo, speed
    ScopeInput = 1u << 1,     ///< keyboard, joystick, mouse
    ScopeMedia = 1u << 2,     ///< tape, disk, snapshot load, media slots, RZX playback, CD audio, fast loading
    ScopeMachine = 1u << 3,   ///< model, configuration: settings, features, switches, slots, network, start / stop
    ScopeRewind = 1u << 4,    ///< time-travel seek and frame steps
    ScopeTakeBack = 1u << 5,  ///< take control back from a session, allow re-takes
    ScopeDebug = 1u << 6,     ///< the debugger: memory, registers, ports, breakpoints, steps, runs, TTD analysis, ...
};
constexpr uint32_t kCompanionAllScopes =
    ScopeRun | ScopeInput | ScopeMedia | ScopeMachine | ScopeRewind | ScopeTakeBack | ScopeDebug;
/// Granted when a device asks for nothing: every scope (the user may untick some at pairing)
constexpr uint32_t kCompanionDefaultScopes = kCompanionAllScopes;

/// The scope a changing request to /api/v1/emulator/{id}<subpath> needs from a companion: the user actions of their
/// scope, ScopeDebug for the rest. Meaningful for the requests ChangesMachine() is true for; the rest pass for everybody
uint32_t CompanionScopeOf(const std::string& method, const std::string& subpath);

/// The same for a CLI command (`command` as typed, `args` after it); meaningful when CliCommandChangesMachine() is true
uint32_t CompanionCliScopeOf(const std::string& command, const std::vector<std::string>& args);

/// Does this changing request need a paused machine (owner 2026-10-16: stepping without a pause makes no sense)?
/// Steps, direct runs, register writes, TTD instruction steps: a companion is refused `not_now` while it runs
bool CompanionNeedsPause(const std::string& subpath);
bool CompanionCliNeedsPause(const std::string& command, const std::vector<std::string>& args);

/// "run", "input", ...; "" for 0 or several bits
std::string CompanionScopeName(uint32_t scope);
/// The names of the set bits, in the order of the enum
std::vector<std::string> CompanionScopeNames(uint32_t scopes);
/// "run" -> ScopeRun, "all" -> every scope; 0 for an unknown name
uint32_t CompanionScopeFromName(const std::string& name);
}  // namespace automation_gate
