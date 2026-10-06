#include "flattenchoice.h"

#include <algorithm>

namespace
{
    const char* Name(FlattenStrategy s)
    {
        switch (s)
        {
            case FlattenStrategy::Flat: return "flat";
            case FlattenStrategy::Delta: return "delta";
            case FlattenStrategy::Commit: return "commit";
            case FlattenStrategy::WriteBack: return "write-back";
        }
        return "delta";
    }

    std::string Text(const StateNode& node, const std::string& key)
    {
        const StateNode* v = node.find(key);
        return v && v->kind == StateNode::Kind::String ? v->s : std::string();
    }

    /// The composite body of a `layers` reply (the reply's "layers" object)
    const StateNode* Composite(const StateNode& reply)
    {
        const StateNode* body = reply.find("layers");
        return body && body->isObject() ? body : nullptr;
    }
}  // namespace

bool IsCompositeRow(const MediaPanelRow& row)
{
    return row.format.rfind("compose-", 0) == 0 || row.format.rfind("graft-", 0) == 0;
}

std::vector<FlattenOption> FlattenOptionsFor(const StateNode& layersReply)
{
    const StateNode* composite = Composite(layersReply);
    const std::string build = composite ? Text(*composite, "build") : std::string();
    const std::string descriptor = composite ? Text(*composite, "descriptor") : std::string();
    bool writable = false;
    if (composite)
        if (const StateNode* layers = composite->find("layers"))
            for (const StateNode& layer : layers->items)
                if (const StateNode* w = layer.find("writable"); w && w->kind == StateNode::Kind::Bool && w->b)
                    writable = true;
    const bool inlineDescriptor = descriptor == "(inline)";

    std::vector<FlattenOption> options(4);
    options[0] = {FlattenStrategy::Flat, Name(FlattenStrategy::Flat), "One image file (S1)",
                  "Write the disk as the guest sees it to a new .img / .vhd / .chd; the slot then holds that image", true, {}};
    options[1] = {FlattenStrategy::Delta, Name(FlattenStrategy::Delta), "Keep the session (S2)",
                  "Save the guest's writes next to the descriptor; inserting it again restores them. Nothing else is written",
                  !inlineDescriptor, inlineDescriptor ? "an inline descriptor has no file to keep them next to" : ""};
    options[2] = {FlattenStrategy::Commit, Name(FlattenStrategy::Commit), "Commit into the base image (S3)",
                  "Write the grafted files and the guest's writes into the bottom image (journaled); the slot then holds it",
                  build == "graft", build == "graft" ? "" : "only a graft (a FAT image at the bottom) has a base image"};
    options[3] = {FlattenStrategy::WriteBack, Name(FlattenStrategy::WriteBack), "Write back into the folders (S4)",
                  "Carry the guest's file changes into the layers marked writable; changed host files are conflicts",
                  writable && !inlineDescriptor,
                  inlineDescriptor ? "an inline descriptor cannot take write-back"
                  : !writable      ? "no layer is marked writable: true"
                                   : ""};
    return options;
}

FlattenStrategy PreselectedStrategy(const std::vector<FlattenOption>& options, const StateNode& layersReply)
{
    const StateNode* composite = Composite(layersReply);
    const std::string wanted = composite ? Text(*composite, "writesSave") : std::string();
    for (const FlattenOption& o : options)
        if (o.name == wanted && o.available)
            return o.strategy;
    for (const FlattenOption& o : options)
        if (o.strategy == FlattenStrategy::Delta && o.available)
            return o.strategy;
    return FlattenStrategy::Flat;
}

std::map<std::string, std::string> FlattenRequestOptions(const FlattenChoice& choice, bool plan)
{
    std::map<std::string, std::string> options = {{"strategy", Name(choice.strategy)}};
    if (plan && (choice.strategy == FlattenStrategy::Commit || choice.strategy == FlattenStrategy::WriteBack))
        options["plan"] = "true";
    if (choice.strategy == FlattenStrategy::Flat && choice.compact)
        options["compact"] = "true";
    if (choice.strategy == FlattenStrategy::WriteBack && choice.keepBoth)
        options["onConflict"] = "keep-both";
    if ((choice.strategy == FlattenStrategy::Commit || choice.strategy == FlattenStrategy::WriteBack) && choice.force)
        options["force"] = "true";
    return options;
}

std::vector<std::string> ReplyReport(const StateNode& reply)
{
    std::vector<std::string> lines;
    if (const StateNode* report = reply.find("report"))
        for (const StateNode& line : report->items)
            if (line.kind == StateNode::Kind::String)
                lines.push_back(line.s);
    return lines;
}
