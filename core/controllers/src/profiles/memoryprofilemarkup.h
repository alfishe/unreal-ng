#pragma once

/// @file memoryprofilemarkup.h
/// @brief A software profile's `memory` section laid over a machine: its regions, entries and comments become the
/// markup's profile layer (debugger/markup/memorymarkup.h), its labels and label files the label set
/// `profile:<id>` (between the ROM bundles and the loaded files). Header only: the controllers library uses no core
/// symbols, its owners (unreal-qt, core-tests) link both.

#include <filesystem>
#include <string>
#include <vector>

#include "debugger/labels/labelmanager.h"
#include "debugger/labels/symbolfiles.h"
#include "debugger/markup/memorymarkup.h"
#include "profiles/memoryprofile.h"
#include "unrealasm/symbols/symbol.h"

namespace controllers
{

/// The label set of a profile's labels
inline std::string MemoryProfileLabelSet(const std::string& id)
{
    return "profile:" + id;
}
/// Above the ROM bundles (50), below the files a user loads (100 on)
constexpr int kMemoryProfileLabelPriority = 75;

inline markup::Place ToMarkupPlace(const MemoryPlace& place)
{
    const markup::Space space = place.space == MemorySpace::Ram   ? markup::Space::Ram
                                : place.space == MemorySpace::Rom ? markup::Space::Rom
                                                                  : markup::Space::Cpu;
    return markup::Place{space, place.page, place.lo, place.hi};
}

inline unrealasm::symbols::Location ToSymbolLocation(const MemoryPlace& place)
{
    using namespace unrealasm::symbols;
    Location location;
    location.space.kind = place.space == MemorySpace::Ram   ? SpaceKind::Ram
                          : place.space == MemorySpace::Rom ? SpaceKind::Rom
                                                            : SpaceKind::CpuView;
    location.space.page = place.space == MemorySpace::Cpu ? 0 : place.page;
    location.offset = place.lo;
    return location;
}

/// The profile layer of @p profile (source `profile:<id>`); a region of a kind the analysis does not know is
/// skipped with a warning (ParseMemoryProfile refuses those, so only a hand-built profile has one)
inline markup::LayerContent MemoryProfileLayer(const MemoryProfile& profile, const std::string& id,
                                               std::vector<std::string>& warnings)
{
    markup::LayerContent layer;
    layer.source = MemoryProfileLabelSet(id);
    for (const MemoryRegionSpec& region : profile.regions)
    {
        markup::Claim claim;
        if (!analysis::ParseRegionKind(region.kind, claim.kind))
        {
            warnings.push_back(id + ": memory region kind '" + region.kind + "' is no analysis kind");
            continue;
        }
        claim.place = ToMarkupPlace(region.place);
        claim.title = region.title;
        claim.confidence = region.confidence;
        claim.sha256 = region.sha256;
        layer.claims.push_back(std::move(claim));
    }
    for (const MemoryLabelSpec& entry : profile.entries)
        layer.entries.push_back(ToMarkupPlace(entry.place));
    for (const MemoryCommentSpec& comment : profile.comments)
        layer.comments.push_back(markup::Note{ToMarkupPlace(comment.place), comment.text});
    return layer;
}

/// Take a profile's part back: its layer (when the markup's profile layer is still its) and its label set
inline void ClearMemoryProfile(const std::string& id, markup::MemoryMarkup& markup, LabelManager* labels)
{
    if (markup.GetLayer(markup::Layer::Profile).source == MemoryProfileLabelSet(id))
        markup.ClearLayer(markup::Layer::Profile);
    if (labels)
        labels->DropSymbolSet(MemoryProfileLabelSet(id));
}

/// Lay @p profile over a machine: the markup's profile layer, the labels, entries (labels of kind entry) and label
/// files (`symbols`, relative to @p profileDir) into the set `profile:<id>`. What could not be read is in @p warnings
inline void ApplyMemoryProfile(const MemoryProfile& profile, const std::string& id, const std::string& title,
                               const std::string& profileDir, markup::MemoryMarkup& markup, LabelManager* labels,
                               std::vector<std::string>& warnings)
{
    using namespace unrealasm::symbols;
    markup.SetLayer(markup::Layer::Profile, MemoryProfileLayer(profile, id, warnings));
    if (!labels)
        return;
    const std::string set = MemoryProfileLabelSet(id);
    labels->DropSymbolSet(set);
    std::vector<Symbol> records;
    auto add = [&records](const MemoryLabelSpec& spec, SymbolKind kind) {
        Symbol symbol;
        symbol.name = spec.name;
        symbol.location = ToSymbolLocation(spec.place);
        symbol.kind = kind;
        symbol.comment = spec.comment;
        records.push_back(std::move(symbol));
    };
    for (const MemoryLabelSpec& label : profile.labels)
        add(label, SymbolKind::Unknown);
    for (const MemoryLabelSpec& entry : profile.entries)
        add(entry, SymbolKind::Entry);
    SymbolImportRequest request;
    request.set = set;
    bool made = false;
    if (!records.empty())
    {
        const SymbolImportResult result =
            labels->ImportRecords(std::move(records), request, set, title, Origin{"profile", id, ""});
        if (!result.ok)
            warnings.push_back(id + ": labels: " + result.message);
        made = made || result.ok;
    }
    for (const std::string& file : profile.symbols)
    {
        const std::string path = (std::filesystem::path(profileDir) / file).string();
        const SymbolImportResult result = labels->ImportSymbols(path, request);
        if (!result.ok)
            warnings.push_back(id + ": " + file + ": " + result.message);
        made = made || result.ok;
    }
    if (made)
        labels->SetSymbolSetPriority(set, kMemoryProfileLabelPriority);
}

}  // namespace controllers
