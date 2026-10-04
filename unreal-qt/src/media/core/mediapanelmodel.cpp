#include "mediapanelmodel.h"

namespace
{
    std::string Text(const StateNode* node)
    {
        return node && node->kind == StateNode::Kind::String ? node->s : std::string();
    }

    bool Flag(const StateNode* node)
    {
        return node && node->kind == StateNode::Kind::Bool && node->b;
    }

    MediaPanelRow Row(const StateNode& slot)
    {
        MediaPanelRow row;
        row.slot = Text(slot.find("id"));
        row.kind = Text(slot.find("kind"));
        row.label = Text(slot.find("label"));
        row.state = Text(slot.find("state"));
        row.detached = Flag(slot.find("detached"));
        row.writeProtect = Flag(slot.find("writeProtect"));
        row.acceptsFolder = Flag(slot.find("acceptsFolder"));
        if (const StateNode* aliases = slot.find("aliases"); aliases && !aliases->items.empty())
            row.alias = aliases->items.front().s;
        if (const StateNode* tags = slot.find("tags"))
        {
            for (const StateNode& tag : tags->items)
            {
                row.ideUnit = row.ideUnit || tag.s == "ide";
                row.compactFlash = row.compactFlash || tag.s == "cf";
            }
        }

        const StateNode* medium = slot.find("medium");
        if (medium && medium->isObject())
        {
            row.present = !row.detached;
            row.medium = Text(medium->find("source"));
            row.format = Text(medium->find("format"));
            row.access = Text(medium->find("access"));
            row.isDirty = Flag(medium->find("dirty"));
            row.dirty = Text(medium->find("changes"));  // the core words it, for every surface
            const StateNode* units = medium->find("dirtyUnits");
            if (row.isDirty && row.dirty.empty() && units)
                row.dirty = std::to_string(units->i) + " changed";
        }
        return row;
    }
}  // namespace

std::vector<MediaPanelRow> MediaPanelRows(const StateNode& listReply)
{
    std::vector<MediaPanelRow> rows;
    if (const StateNode* slots = listReply.find("slots"))
    {
        for (const StateNode& slot : slots->items)
            rows.push_back(Row(slot));
    }
    if (const StateNode* detached = listReply.find("detached"))
    {
        for (const StateNode& slot : detached->items)
            rows.push_back(Row(slot));
    }
    return rows;
}

std::string MediaFileFilter(const StateNode& formatsReply, const std::string& kind)
{
    const StateNode* formats = formatsReply.find("formats");
    const StateNode* list = formats ? formats->find(kind) : nullptr;
    std::string patterns;
    if (list)
    {
        for (const StateNode& extension : list->items)
            patterns += (patterns.empty() ? "*." : " *.") + extension.s;
    }
    const std::string name = kind == "floppy" ? "Disk images" : kind == "block" ? "Card / disk images"
                             : kind == "tape"  ? "Tape images" : "Images";
    return patterns.empty() ? "All files (*)" : name + " (" + patterns + ");;All files (*)";
}

bool MediaNeedsDisposition(const StateNode& reply)
{
    return !Flag(reply.find("ok")) && Text(reply.find("error")) == "dirty";
}
