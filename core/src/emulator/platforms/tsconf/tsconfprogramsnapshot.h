#pragma once

/// @file tsconfprogramsnapshot.h
/// @brief The TS-Conf machine's snapshot commit policy (snapshot pipeline, PLAN #84): a TS-Conf SDK program (an SPG,
/// physically addressed blocks) goes into the machine from the neutral snapshot::Image the plan decided on.
///
/// The image carries what the format says: the blocks as physical runs, the CPU (PC, SP, the interrupt flag, the registers
/// the SDK shell leaves), and in the "spg:header" extension's payload the two TS-Conf registers the file names:
/// byte 0 = the RAM page at #C000, byte 1 = SYS_CONFIG[1:0] (the CPU clock). The machine's decoder hands the instance out as its
/// snapshot policy (PortDecoder::GetSnapshotPolicy); nothing outside the TS-Conf files names this class.
///
/// Worked example: wc.spg (blocks at #0A0000 ...; page 3 = 0; clock = 2): the machine is reset, the SD card left idle, BASIC-48 ROM
/// at #0000 (mapped mode), RAM 5 / 2 / page 0, the blocks copied to RAM, IY = #5C3A, HL' = #2758, I = #3F, IM 1, PC / SP from the file.

#include "loaders/snapshot/snapshotpolicy.h"

class TsConfProgramSnapshot : public snapshot::ISnapshotCommitPolicy
{
public:
    /// The one instance every TS-Conf shares (stateless); also known by name for `commit=tsconf-program`
    static TsConfProgramSnapshot& Instance();

    std::string Name() const override { return "tsconf-program"; }
    snapshot::Verdict Examine(const snapshot::Image& image, EmulatorContext& context) const override;
    bool Commit(const snapshot::Image& image, EmulatorContext& context, snapshot::Report& report) override;
};
