/// @file cardcandidates_test.cpp
/// @brief The catalog cards that would answer a board access (docs/inprogress/2026-11-02-device-access-watch): a
/// Kempston interface for #1F reads on a bare 48K, on the slot the planner suggests; nothing for a direction the card
/// does not answer; a card already fitted is no candidate.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "emulator/platform.h"
#include "emulator/slots/cardcandidates.h"

using namespace slots;

TEST(CardCandidates_Test, KempstonForTheBoardReadsOfA48K)
{
    const std::vector<CardCandidate> reads = CardCandidates(MM_SPECTRUM48, {}, 0x001F, false);
    ASSERT_FALSE(reads.empty());
    EXPECT_NE(reads.front().input, InputRole::None) << "input claims first";
    const auto kempston = std::find_if(reads.begin(), reads.end(), [](const CardCandidate& c) { return c.card == "kempston"; });
    ASSERT_NE(kempston, reads.end());
    EXPECT_EQ(kempston->slot, "edge.1");
    EXPECT_EQ(kempston->input, InputRole::KempstonJoystick);
    EXPECT_FALSE(kempston->needsConfirmation);

    const std::vector<CardCandidate> writes = CardCandidates(MM_SPECTRUM48, {}, 0x001F, true);
    EXPECT_TRUE(std::none_of(writes.begin(), writes.end(), [](const CardCandidate& c) { return c.card == "kempston"; }))
        << "the interface answers reads only";

    const SlotSet fitted = { SlotEntry{ .slot = "edge.1", .card = "kempston" } };
    const std::vector<CardCandidate> again = CardCandidates(MM_SPECTRUM48, fitted, 0x001F, false);
    EXPECT_TRUE(std::none_of(again.begin(), again.end(), [](const CardCandidate& c) { return c.card == "kempston"; }));
}

// The same #1F reads, two cards that answer them: the one the whole window explains best comes first. A program that
// only reads #1F wants the one-port Kempston interface; one that also programs the PIO (#5F / #7F) wants the AMX mouse
TEST(CardCandidates_Test, RankedByWhatTheWindowExplains)
{
    auto first = [](std::vector<BoardAccess> accesses) {
        for (BoardAccess& access : accesses)
            access.candidates = CardCandidates(MM_SPECTRUM48, {}, access.port, access.out);
        RankCardCandidates(accesses);
        return accesses.front().candidates.empty() ? std::string() : accesses.front().candidates.front().card;
    };
    const std::vector<CardCandidate> raw = CardCandidates(MM_SPECTRUM48, {}, 0x001F, false);
    ASSERT_TRUE(std::any_of(raw.begin(), raw.end(), [](const CardCandidate& c) { return c.card == "amx-mouse"; }))
        << "the AMX PIO's data A is #1F too";
    EXPECT_EQ(first({ { .port = 0x001F } }), "kempston");
    EXPECT_EQ(first({ { .port = 0x001F }, { .port = 0x005F, .out = true }, { .port = 0x007F, .out = true },
                      { .port = 0x003F } }),
              "amx-mouse");
}
