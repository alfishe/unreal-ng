#pragma once

/// @file markupcontrol.h
/// @brief The `markup` verbs of the memory markup (memorymarkup.h; docs/inprogress/2026-11-05-software-metadata-profile
/// §4-§5) for every surface: the WebAPI (and MCP and the WebSocket `call` through it), the CLI, Lua and Python turn
/// their input into an action plus options and show the reply. Options, defaults, refusals, the answer's fields and
/// its text live here, so the surfaces cannot drift apart (the analysiscontrol.h pattern, whose Reply it shares).
///
/// Actions:
///   show    [from to range layer limit]  what the CPU range (default the whole 64K; range: "8000-80FF") holds, as
///                          spans of one winning claim each (kind, layer, source, title, confidence, start: the
///                          claim's first byte), the instruction starts known (anchors) and the comments; layer: only
///                          the spans that layer wins; at most limit (200, <= 4096) rows of each list
///   layers                 every layer counted (source, claims, entries, comments), what the runtime layer is derived
///                          from (facts, facts-physical, coverage, none) and the revision
///   mark    from|range kind [to title cpu]   the user layer: the range as a region kind (code, data, text, font,
///                          sprite, ...), on the pages the CPU shows now (cpu=true: on the CPU addresses instead)
///   unmark  from|range [to] the user layer forgets its claims there (the lower layers show again)
///   comment address [text] the user layer's comment at an address (empty text removes it)
///   clear   [layer]        empty a layer (user, the default; analysis: back to the last L2 build)
///   save    [profile analysis confidence keep]   the user layer, and with analysis (all or kinds, comma separated)
///                          the analysis claims of at least that confidence (700), into a software profile's
///                          `memory:` section: the profile named, else the matched one, else a new one keyed by the
///                          loaded program. The process's profile owner does the writing (SetProfileSaver; unreal-qt);
///                          the user layer is emptied after (keep=true keeps it)
///
/// Addresses: $8000, #8000, 0x8000, 8000h, decimal or a label.
///
/// @code
///   markup::Reply reply = markup::Execute(emulator, "mark", {{"range", "8000-80FF"}, {"kind", "data"}});
///   http.status = reply.HttpStatus();  http.body = StateNodeToJson(reply.ToValue());
/// @endcode

#include <functional>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "debugger/markup/memorymarkup.h"

class Emulator;

namespace markup
{

using Error = analysis::Error;
using Reply = analysis::Reply;
using Options = analysis::Options;

/// "show", "layers", "mark", "unmark", "comment", "clear", "save"
const std::vector<std::string>& Actions();
/// The option names an action takes (empty for an unknown action)
const std::vector<std::string>& OptionsFor(const std::string& action);

/// Check the action and its options, then run it
Reply Execute(Emulator& emulator, const std::string& action, const Options& options = {});

/// What `save` hands the profile owner
struct SaveRequest
{
    std::string profile;          ///< the profile to write into; empty: the matched one, else a new one
    LayerContent user;            ///< the user layer
    std::vector<Claim> analysis;  ///< the analysis claims chosen
};

/// Writes a SaveRequest into a profile file and lays the profile over the machine again. The body of its reply:
/// profile (the id), path (the file), regions, comments (what the section holds now), created (a new profile),
/// applied (false: the profile does not lie over the machine, so the user layer is kept)
using ProfileSaver = std::function<Reply(Emulator& emulator, const SaveRequest& request)>;
/// The process's software profile owner registers it (unreal-qt's HostControllers); empty: `save` answers that this
/// process keeps no software profiles
void SetProfileSaver(ProfileSaver saver);

}  // namespace markup
