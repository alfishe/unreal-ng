#pragma once

// ZEsarUX ZRCP protocol constants (wire-compatibility layer).
//
// Unreal-NG impersonates a ZEsarUX debug server so DeZog can connect with
// remoteType "zrcp" - the only DeZog remote profile that enables watchpoints,
// state save/restore and native reverse debugging (ZesaruxCpuHistory) over
// TCP. The wire formats are byte-compatible with ZEsarUX (verified against
// DeZog's zesaruxsocket/zesaruxremote and ZEsarUX src/zrcp/remote.c).

#include <cstdint>

namespace zrcp
{
// ZEsarUX default ZRCP port
constexpr uint16_t DEFAULT_PORT = 10000;

// Reported by get-version. DeZog gates:
//   - MIN_ZESARUX_VERSION 10.3 (session refuses to start below)
//   - breakpoint pass counts need >= 12.1
constexpr const char* SERVER_VERSION = "12.1";

// Every command response is terminated by this prompt line. DeZog's
// zesaruxsocket matches startsWith('command') && endsWith('> ').
constexpr const char* PROMPT = "command...> ";
} // namespace zrcp
