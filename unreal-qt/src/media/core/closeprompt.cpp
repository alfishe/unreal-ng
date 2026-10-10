#include "closeprompt.h"

#include <algorithm>

std::string ReleaseFateText(const std::string& onRelease)
{
    if (onRelease == "delta")
        return "saved as a session delta (writes.save)";
    if (onRelease == "commit")
        return "committed into the base image (writes.save)";
    if (onRelease == "write-back")
        return "written back into the folders (writes.save)";
    if (onRelease == "discard")
        return "dropped (writes.save: discard)";
    if (onRelease == "ask")
        return "yours to choose (writes.save: ask)";
    if (onRelease == "journal")
        return "kept in the session journal for the next insert";
    return "lost";
}

ClosePrompt ClosePromptFor(const std::vector<SlotInfo>& unsaved)
{
    ClosePrompt prompt;
    for (const SlotInfo& info : unsaved)
    {
        if (!info.dirty)
            continue;
        UnsavedMediumLine line;
        line.slot = info.descriptor.id;
        line.source = info.source;
        line.changes = info.changes;
        line.onRelease = info.onRelease.empty() ? "lost" : info.onRelease;
        line.asks = line.onRelease == "ask" || line.onRelease == "lost";
        line.fate = ReleaseFateText(line.onRelease);
        prompt.ask = prompt.ask || line.asks;
        prompt.media.push_back(std::move(line));
    }
    std::stable_partition(prompt.media.begin(), prompt.media.end(), [](const UnsavedMediumLine& l) { return l.asks; });
    return prompt;
}
