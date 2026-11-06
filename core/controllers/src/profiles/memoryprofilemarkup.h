#pragma once

/// @file memoryprofilemarkup.h
/// @brief A software profile's `memory` section laid over a machine: its regions, entries and comments become the
/// markup's profile layer (debugger/markup/memorymarkup.h), its labels and label files the label set
/// `profile:<id>` (between the ROM bundles and the loaded files). Header only: the controllers library uses no core
/// symbols, its owners (unreal-qt, core-tests) link both.

#include <algorithm>
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

/// The place a markup claim is written at in a profile; false for a cache page (a profile names the CPU, RAM and ROM)
inline bool FromMarkupPlace(const markup::Place& place, MemoryPlace& out)
{
    if (place.space != markup::Space::Cpu && place.space != markup::Space::Ram && place.space != markup::Space::Rom)
        return false;
    out.space = place.space == markup::Space::Ram   ? MemorySpace::Ram
                : place.space == markup::Space::Rom ? MemorySpace::Rom
                                                    : MemorySpace::Cpu;
    out.page = place.space == markup::Space::Cpu ? 0 : place.page;
    out.lo = place.lo;
    out.hi = place.hi;
    return true;
}

inline bool SameMemoryPlace(const MemoryPlace& a, const MemoryPlace& b)
{
    return a.space == b.space && a.page == b.page && a.lo == b.lo && a.hi == b.hi;
}

/// What MergeIntoMemoryProfile added
struct MemoryProfileMerge
{
    size_t regions = 0;   ///< regions added
    size_t comments = 0;  ///< comments added or replaced
    size_t skipped = 0;   ///< claims and comments on a cache page (a profile cannot name one)
};

/// A save to a profile (markup `save`, markupcontrol.h): the user layer's claims and comments and the analysis claims
/// chosen go into @p profile so that its layer alone shows what the layers showed. A later region wins where regions
/// overlap, so the analysis claims go first (the profile's own regions were above them), then the regions there were,
/// then the user's claims; a region already there as it is (place, kind, title) is not repeated. A user comment
/// replaces the one at its address
inline MemoryProfileMerge MergeIntoMemoryProfile(const markup::LayerContent& user,
                                                 const std::vector<markup::Claim>& analysisClaims,
                                                 MemoryProfile& profile)
{
    MemoryProfileMerge merge;
    auto toRegion = [&merge](const markup::Claim& claim, MemoryRegionSpec& region) {
        if (!FromMarkupPlace(claim.place, region.place))
        {
            merge.skipped++;
            return false;
        }
        region.kind = analysis::RegionKindName(claim.kind);
        region.title = claim.title;
        region.sha256 = claim.sha256;
        region.confidence = claim.confidence;
        return true;
    };
    auto known = [](const std::vector<MemoryRegionSpec>& regions, const MemoryRegionSpec& region) {
        return std::any_of(regions.begin(), regions.end(), [&region](const MemoryRegionSpec& other) {
            return SameMemoryPlace(other.place, region.place) && other.kind == region.kind &&
                   other.title == region.title;
        });
    };
    std::vector<MemoryRegionSpec> below;
    for (const markup::Claim& claim : analysisClaims)
    {
        MemoryRegionSpec region;
        if (toRegion(claim, region) && !known(profile.regions, region) && !known(below, region))
            below.push_back(std::move(region));
    }
    merge.regions += below.size();
    for (MemoryRegionSpec& region : profile.regions)
        below.push_back(std::move(region));
    profile.regions = std::move(below);
    for (const markup::Claim& claim : user.claims)
    {
        MemoryRegionSpec region;
        if (!toRegion(claim, region))
            continue;
        // Already the last word there (the topmost region over its place is this one): nothing to add. Otherwise it
        // goes on top, even when an equal one lies lower
        auto top = std::find_if(profile.regions.rbegin(), profile.regions.rend(), [&region](const MemoryRegionSpec& r) {
            return r.place.space == region.place.space && r.place.page == region.place.page &&
                   r.place.lo <= region.place.hi && region.place.lo <= r.place.hi;
        });
        if (top != profile.regions.rend() && SameMemoryPlace(top->place, region.place) && top->kind == region.kind &&
            top->title == region.title)
            continue;
        profile.regions.push_back(std::move(region));
        merge.regions++;
    }
    for (const markup::Note& note : user.comments)
    {
        MemoryCommentSpec comment;
        if (!FromMarkupPlace(note.place, comment.place))
        {
            merge.skipped++;
            continue;
        }
        comment.text = note.text;
        auto same = std::find_if(profile.comments.begin(), profile.comments.end(), [&comment](const MemoryCommentSpec& c) {
            return SameMemoryPlace(c.place, comment.place);
        });
        if (same != profile.comments.end())
            same->text = comment.text;
        else
            profile.comments.push_back(std::move(comment));
        merge.comments++;
    }
    return merge;
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
