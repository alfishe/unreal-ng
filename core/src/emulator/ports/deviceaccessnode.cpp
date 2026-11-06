#include "deviceaccessnode.h"

namespace
{
/// The bits of a tested mask as a list ([0, 1, 4]): the sampled AND n / BIT b / shift into carry after the read
StateNode Bits(uint8_t tested)
{
    StateNode bits = StateNode::Array();
    for (unsigned bit = 0; bit < 8; ++bit)
        if ((tested >> bit) & 1u)
            bits.push(static_cast<uint64_t>(bit));
    return bits;
}
}  // namespace

StateNode DeviceAccessEvidenceNode(const DeviceAccessEvidence& evidence)
{
    StateNode node = StateNode::Object();
    node["active"] = evidence.active;
    node["frames_observed"] = static_cast<uint64_t>(evidence.framesObserved);
    node["window"] = static_cast<uint64_t>(evidence.window);
    node["dropped_channels"] = static_cast<uint64_t>(evidence.droppedChannels);
    // Per device of the slot registry and channel: what the machine's own decode says each access is
    StateNode channels = StateNode::Array();
    for (const DeviceAccessChannel& channel : evidence.channels)
    {
        if (channel.frames == 0)
            continue;  // seen earlier, not in the window
        StateNode entry = StateNode::Object();
        entry["device"] = channel.device;
        entry["channel"] = channel.channel;
        entry["dir"] = channel.out ? "out" : "in";
        entry["port"] = static_cast<uint64_t>(channel.port);
        if (channel.input != slots::InputRole::None)
            entry["input"] = InputRoleName(channel.input);
        entry["frames"] = static_cast<uint64_t>(channel.frames);
        entry["reads_per_frame"] = static_cast<double>(channel.perFrame);
        if (channel.tested != 0)
            entry["tested_bits"] = Bits(channel.tested);
        StateNode sites = StateNode::Array();
        for (const DeviceAccessSite& site : channel.sites)
        {
            StateNode s = StateNode::Object();
            s["pc"] = static_cast<uint64_t>(site.pc);
            s["count"] = static_cast<uint64_t>(site.count);
            if (site.tested != 0)
                s["tested_bits"] = Bits(site.tested);
            sites.push(std::move(s));
        }
        entry["sites"] = sites;
        entry["other_sites"] = static_cast<uint64_t>(channel.otherSites);
        // A board access: the catalog cards that would answer it on this machine (where the planner fits each)
        if (!channel.candidates.empty())
        {
            StateNode candidates = StateNode::Array();
            for (const slots::CardCandidate& candidate : channel.candidates)
            {
                StateNode c = StateNode::Object();
                c["card"] = candidate.card;
                c["name"] = candidate.name;
                c["slot"] = candidate.slot;
                if (candidate.input != slots::InputRole::None)
                    c["input"] = InputRoleName(candidate.input);
                c["needs_confirmation"] = candidate.needsConfirmation;
                candidates.push(std::move(c));
            }
            entry["candidates"] = candidates;
        }
        channels.push(std::move(entry));
    }
    node["channels"] = channels;
    return node;
}
