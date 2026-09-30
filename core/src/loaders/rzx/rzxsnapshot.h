#pragma once

/// @file rzxsnapshot.h
/// @brief Which machine an RZX start snapshot was taken on, read from the
/// image's own header (design §7): the recording only replays on that
/// machine, and the SNA / Z80 loaders do not check it themselves.
///
/// Examples: a Z80 v3 image with hardware byte 4 is a 128K; the same byte
/// with the "modified hardware" bit (byte 37 bit 7) is a +2; an SNA of 49179
/// bytes is a 48K; an SZX with machine id 7 is a Pentagon 128.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/platform.h"

namespace rzx
{
    struct SnapshotMachine
    {
        MEM_MODEL model = MM_SPECTRUM48;
        uint32_t ramKb = 48;
        /// True when the image says only "a 128K-class machine" (a 128K SNA):
        /// any 128K-compatible running model is accepted
        bool family128 = false;
        std::string description;  ///< "Z80 v3, 128K", "SNA 48K"
    };

    /// False with `error` set for an image whose machine we do not emulate or
    /// cannot tell
    bool DetectSnapshotMachine(const std::string& extension, const std::vector<uint8_t>& data,
                               SnapshotMachine& machine, std::string& error);

    /// True when a machine running `model` with `ramKb` can replay a
    /// recording taken on `machine`
    bool MachineMatches(const SnapshotMachine& machine, MEM_MODEL model, uint32_t ramKb);
}  // namespace rzx
