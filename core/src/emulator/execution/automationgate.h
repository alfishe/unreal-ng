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
}  // namespace automation_gate
