#include "cardcandidates.h"

#include <algorithm>

namespace slots
{

namespace
{
/// The card's claim answers an access in its direction (DOS-only claims are no board candidate)
bool Answers(const PortClaim& claim, uint16_t port, bool out)
{
    const Dir wanted = out ? Dir::Out : Dir::In;
    return (port & claim.mask) == claim.match && (claim.dir == Dir::InOut || claim.dir == wanted) &&
           claim.gate != Gate::DosOnly;
}

/// A dry-run plug of the card at the slot the planner suggests for it
SlotPlan PlanCandidate(MEM_MODEL model, const SlotSet& slots, const CardDef& card, const SlotPlanner& planner)
{
    const MachineDef* machine = planner.FindMachine(model);
    SlotRequest request;
    request.op = SlotRequest::Op::Plug;
    request.card = card.id;
    request.slot = machine != nullptr ? planner.SuggestSlot(*machine, card) : std::string();
    request.dryRun = true;
    if (request.slot.empty())
    {
        SlotPlan refused;
        refused.hardRefusal = true;
        return refused;
    }
    return planner.Plan(model, slots, request);
}
}  // namespace

std::vector<CardCandidate> CardCandidates(MEM_MODEL model, const SlotSet& slots, uint16_t port, bool out,
                                          const SlotPlanner& planner)
{
    std::vector<CardCandidate> candidates;
    if (planner.FindMachine(model) == nullptr)
        return candidates;
    for (const CardDef& card : planner.Data().cards)
    {
        if (card.socketDefault)
            continue;
        // A card already fitted answers through its own claims; the port is unclaimed because of its options
        if (std::any_of(slots.begin(), slots.end(), [&card](const SlotEntry& entry) { return entry.card == card.id; }))
            continue;
        const std::vector<PortClaim> claims = CardClaims(card, CardOptions{});
        const auto claim =
            std::find_if(claims.begin(), claims.end(), [&](const PortClaim& c) { return Answers(c, port, out); });
        if (claim == claims.end())
            continue;
        const SlotPlan plan = PlanCandidate(model, slots, card, planner);
        if (plan.hardRefusal || plan.slot.empty())
            continue;
        CardCandidate candidate;
        candidate.card = card.id;
        candidate.name = card.name;
        candidate.slot = plan.slot;
        candidate.input = claim->input;
        candidate.needsConfirmation = plan.needsConfirmation;
        candidates.push_back(std::move(candidate));
    }
    std::stable_partition(candidates.begin(), candidates.end(),
                          [](const CardCandidate& c) { return c.input != InputRole::None; });
    return candidates;
}

void RankCardCandidates(std::vector<BoardAccess>& accesses, const SlotPlanner& planner)
{
    // Per card once: the accesses it answers and the share of its claims they touch
    std::vector<CardCandidate> scored;
    for (const BoardAccess& access : accesses)
        for (const CardCandidate& candidate : access.candidates)
        {
            if (std::any_of(scored.begin(), scored.end(), [&](const CardCandidate& c) { return c.card == candidate.card; }))
                continue;
            CardCandidate score = candidate;
            if (const CardDef* card = planner.FindCard(candidate.card))
            {
                std::vector<PortClaim> claims = CardClaims(*card, CardOptions{});
                claims.erase(std::remove_if(claims.begin(), claims.end(),
                                            [](const PortClaim& c) { return c.gate == Gate::DosOnly; }),
                             claims.end());
                score.claims = static_cast<uint16_t>(claims.size());
                for (const PortClaim& claim : claims)
                    if (std::any_of(accesses.begin(), accesses.end(),
                                    [&](const BoardAccess& a) { return Answers(claim, a.port, a.out); }))
                        ++score.claimsUsed;
                score.explained = static_cast<uint16_t>(
                    std::count_if(accesses.begin(), accesses.end(), [&](const BoardAccess& a) {
                        return std::any_of(claims.begin(), claims.end(),
                                           [&](const PortClaim& c) { return Answers(c, a.port, a.out); });
                    }));
            }
            scored.push_back(std::move(score));
        }
    auto better = [](const CardCandidate& a, const CardCandidate& b) {
        const bool inputA = a.input != InputRole::None;
        const bool inputB = b.input != InputRole::None;
        if (inputA != inputB)
            return inputA;
        if (a.explained != b.explained)
            return a.explained > b.explained;
        // used / claims, compared without a division
        return uint32_t(a.claimsUsed) * std::max<uint32_t>(b.claims, 1) >
               uint32_t(b.claimsUsed) * std::max<uint32_t>(a.claims, 1);
    };
    for (BoardAccess& access : accesses)
    {
        for (CardCandidate& candidate : access.candidates)
            if (const auto it = std::find_if(scored.begin(), scored.end(),
                                             [&](const CardCandidate& c) { return c.card == candidate.card; });
                it != scored.end())
            {
                candidate.explained = it->explained;
                candidate.claimsUsed = it->claimsUsed;
                candidate.claims = it->claims;
            }
        std::stable_sort(access.candidates.begin(), access.candidates.end(), better);
    }
}

}  // namespace slots
