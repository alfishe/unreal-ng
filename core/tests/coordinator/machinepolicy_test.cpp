/// @file machinepolicy_test.cpp
/// @brief The coordinator's rules, every cell: operation kind x machine mode x "end the recording". The expected
/// table is written here from today's behaviour (current-state.md §2-3), not copied from machinepolicy.cpp, so a
/// changed cell fails until both agree.

#include <gtest/gtest.h>

#include <string>

#include "emulator/coordinator/machinepolicy.h"

using namespace coordinator;

namespace
{
constexpr MachineMode kModes[] = {MachineMode::Live, MachineMode::Recording, MachineMode::BlackBox,
                                  MachineMode::Replaying, MachineMode::Detached};

struct Expected
{
    bool refused;
    SessionEffect effect;
    bool hold;
    bool machineResetAfter;
};

Expected ExpectedCell(OpKind kind, MachineMode mode, bool endRecording)
{
    const bool live = mode == MachineMode::Live;
    switch (kind)
    {
        case OpKind::Reset:
        case OpKind::Autostart:
            return {false, SessionEffect::EndKeepHistory, true, true};
        case OpKind::SnapshotLoad:
            return {false, SessionEffect::EndKeepHistory, true, false};
        case OpKind::MediaInsert:
        case OpKind::MediaEject:
        case OpKind::DiskCreate:
            if (mode == MachineMode::Recording)
                return endRecording ? Expected{false, SessionEffect::StopThenMediaChanged, true, false}
                                    : Expected{true, SessionEffect::None, false, false};
            return {false, SessionEffect::MediaChanged, !live, false};
        case OpKind::MediaDiscard:
            if (mode == MachineMode::Recording)
                return {true, SessionEffect::None, false, false};   // no "end the recording" for a discard today
            return {false, SessionEffect::MediaChanged, !live, false};
        case OpKind::Count:
            break;
    }
    ADD_FAILURE() << "no expectation for kind " << static_cast<int>(kind);
    return {};
}
}  // namespace

TEST(MachinePolicy_Test, EveryCellIsTodaysBehaviour)
{
    int cells = 0;
    for (int k = 0; k < static_cast<int>(OpKind::Count); ++k)
        for (MachineMode mode : kModes)
            for (bool endRecording : {false, true})
            {
                MachineOp op;
                op.kind = static_cast<OpKind>(k);
                op.endRecording = endRecording;
                const PolicyDecision d = Decide(op, mode);
                const Expected e = ExpectedCell(op.kind, mode, endRecording);
                const std::string where = std::string(OpKindName(op.kind)) + " in " + MachineModeName(mode) +
                                          (endRecording ? " (end recording)" : "");
                EXPECT_EQ(d.refused, e.refused) << where;
                EXPECT_EQ(d.reason.empty(), !e.refused) << where;
                EXPECT_EQ(d.effect, e.effect) << where;
                EXPECT_EQ(d.hold, e.hold) << where;
                EXPECT_EQ(d.machineResetAfter, e.machineResetAfter) << where;
                ++cells;
            }
    EXPECT_EQ(cells, static_cast<int>(OpKind::Count) * 5 * 2);
}

TEST(MachinePolicy_Test, MediaRefusalsSayWhatTheSurfacesSaidBefore)
{
    MachineOp op;
    op.kind = OpKind::MediaInsert;
    op.media = MediaClass::Tape;
    EXPECT_EQ(Decide(op, MachineMode::Recording).reason.rfind("Cannot insert a tape while TTD is recording", 0), 0u);
    op.media = MediaClass::Disk;
    EXPECT_EQ(Decide(op, MachineMode::Recording).reason.rfind("Cannot insert a disk while TTD is recording", 0), 0u);
    op.media = MediaClass::Any;
    EXPECT_EQ(Decide(op, MachineMode::Recording).reason,
              "the media set is fixed while a TTD recording runs; end the recording first");
    op.kind = OpKind::DiskCreate;
    op.media = MediaClass::Disk;
    EXPECT_EQ(Decide(op, MachineMode::Recording).reason.rfind("Cannot create a disk while TTD is recording", 0), 0u);
}

TEST(MachinePolicy_Test, EveryKindAndModeHasAName)
{
    for (int k = 0; k < static_cast<int>(OpKind::Count); ++k)
        EXPECT_STRNE(OpKindName(static_cast<OpKind>(k)), "unknown") << k;
    for (MachineMode mode : kModes)
        EXPECT_STRNE(MachineModeName(mode), "unknown");
    for (int s = 0; s < static_cast<int>(OpSource::Count); ++s)
    {
        if (static_cast<OpSource>(s) != OpSource::Unknown)
        {
            EXPECT_STRNE(OpSourceName(static_cast<OpSource>(s)), "unknown") << s;
        }
    }
}

TEST(MachinePolicy_Test, AnUnknownKindIsRefused)
{
    MachineOp op;
    op.kind = OpKind::Count;
    EXPECT_TRUE(Decide(op, MachineMode::Live).refused);
}
