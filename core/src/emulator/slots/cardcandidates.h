#pragma once
/// @file cardcandidates.h
/// @brief The catalog cards that would answer a port access no device of the machine claims (a board access of the
/// device access watch, docs/inprogress/2026-11-02-device-access-watch): a program reading #1F every frame on a bare
/// 48K wants a Kempston interface on the edge. Each candidate is the card the planner would fit on the machine as it
/// is now, at the slot it suggests; a card the planner refuses outright is no candidate.
///
/// Control path only (an auto decision, a report): one plan per catalog card that claims the port.

// Qt defines `slots` as a macro; this header reaches Qt translation units through the device access evidence
#pragma push_macro("slots")
#undef slots

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/slots/slotplanner.h"

namespace slots
{

/// The catalog cards whose claims (with their default options) answer @p port in the direction @p out, not in a
/// DOS-only claim, that the planner would fit on @p model with @p slots. Input claims first, then catalog order
std::vector<CardCandidate> CardCandidates(MEM_MODEL model, const SlotSet& slots, uint16_t port, bool out,
                                          const SlotPlanner& planner = SlotPlanner());

/// One board access of a window with its candidates (CardCandidates)
struct BoardAccess
{
    uint16_t port = 0;
    bool out = false;
    std::vector<CardCandidate> candidates;
};

/// Orders each access's candidates by how well the card explains the whole window, the same rule for every card: input
/// claims first, then the most board accesses the card answers, then the largest share of its own claims those
/// accesses touch (a Kempston interface, one port read, before an AMX mouse whose PIO is read on one of six; an AMX
/// program writing #5F / #7F puts the mouse first), then catalog order. Fills `explained`, `claimsUsed`, `claims`
void RankCardCandidates(std::vector<BoardAccess>& accesses, const SlotPlanner& planner = SlotPlanner());

}  // namespace slots

#pragma pop_macro("slots")
