#pragma once

#include <cstdint>

/// The CPUs the debugger can stop on (gs-debugger design §3.1)
enum class DebugCpuId : uint8_t
{
    Main = 0,
    GS = 1,  ///< the CPU of the card in the General Sound slot (classic GS or NeoGS)
};

/// The surfaces' name of a CPU ("main", "gs")
inline const char* DebugCpuName(DebugCpuId cpu)
{
    return cpu == DebugCpuId::GS ? "gs" : "main";
}
