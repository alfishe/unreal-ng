#pragma once

#include <cstddef>
#include <cstdint>

/// The CPUs the debugger can stop on (gs-debugger design §3.1)
enum class DebugCpuId : uint8_t
{
    Main = 0,
    GS = 1,            ///< the CPU of the card in the General Sound slot (classic GS or NeoGS)
    MultiSoundGS = 2,  ///< the General Sound on a MultiSound slot card
};

/// Every CPU id, for the tables indexed by one
inline constexpr size_t kDebugCpuCount = 3;

/// A sound card's CPU (a GSCardTarget), not the machine's
inline constexpr bool IsCardCpu(DebugCpuId cpu)
{
    return cpu != DebugCpuId::Main;
}

/// The surfaces' name of a CPU ("main", "gs", "ms")
inline const char* DebugCpuName(DebugCpuId cpu)
{
    switch (cpu)
    {
        case DebugCpuId::GS:
            return "gs";
        case DebugCpuId::MultiSoundGS:
            return "ms";
        default:
            return "main";
    }
}
