#pragma once

/// @file snapshotdigest.h
/// @brief A digest of the machine state a snapshot load leaves behind (snapshot pipeline P0, PLAN #84): RAM, paging
/// latches and the bank mapping, CPU, AY 0 and the border, each its own hash so a changed row says WHICH area moved.
/// Used by the golden commit test (loaders/snapshot/snapshotgolden_test.cpp) and by every later pipeline step that has
/// to prove it reproduces today's commit.

#include <cstdint>
#include <string>

class Emulator;

namespace SnapshotDigest
{
struct Digest
{
    uint64_t ram = 0;      ///< every RAM page of the configured size, in page order
    uint64_t ports = 0;    ///< the paging / system latches of EmulatorState and the bank mapping Memory resolved
    uint64_t cpu = 0;      ///< registers incl. the alternate set, IR, IFF, IM, HALT, MEMPTR and the frame position
    uint64_t ay = 0;       ///< AY 0 registers (0 when the machine has none)
    uint64_t misc = 0;     ///< border, execution flags
};

/// What the machine looks like now
Digest Capture(Emulator* emulator);

/// "ram=1a2b3c4d ports=... cpu=... ay=... misc=..." (the low 32 bits of each hash, enough to see a change)
std::string ToText(const Digest& digest);
}  // namespace SnapshotDigest
