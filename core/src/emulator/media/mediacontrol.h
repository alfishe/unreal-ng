#pragma once

/// @file mediacontrol.h
/// @brief The one place that implements the media verbs for every surface:
/// the Qt GUI, WebAPI (and MCP through it), CLI, Lua and Python only turn their
/// input into a MediaRequest and the MediaReply into their output. Replies
/// are StateNode trees, the tree every surface already converts (JSON,
/// sol::table, py::dict). Selector
/// resolution, option names and parsing, defaults, the automatic slot choice,
/// dispositions, sync / async and the error codes live here, so the surfaces
/// cannot drift apart.
/// Design: docs/inprogress/2026-09-28-storage-manager/media-control-design.md
/// (§3.3 selectors, §3.4 verbs and options, §3.5 auto, §3.8 this layer).
///
/// Verbs: list, info, formats, insert, eject, swap, save, export, discard,
/// rescan, create, protect, compose (build a composition descriptor without
/// inserting it: report and layout), layers (a slot's composite medium).
///
/// Example (what a surface does):
/// @code
///   MediaRequest request{"swap", "A", "Elite (disk 2).trd", {{"save", ""}}};
///   MediaReply reply = MediaControl(context).Execute(request);
///   http.status = reply.HttpStatus();  http.body = reply.ToJson();
/// @endcode

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "emulator/media/mediamanager.h"
#include "emulator/media/mediatypes.h"
#include "emulator/state/statenode.h"

class EmulatorContext;

struct MediaRequest
{
    std::string verb;
    std::string selector;  ///< a slot: id, alias, kind:index or tag:a+b ("auto" for insert)
    std::string path;      ///< insert / swap: the source; save / export: the target
    /// Options by name (media-control-design.md §3.4). A flag given without a
    /// value ("--save") is an empty string and means true
    std::map<std::string, std::string> options;
    /// `path` is a staged upload (WebAPI multipart): the manager deletes the
    /// file when the medium leaves
    bool upload = false;

    /// Insert of a folder volume only (BUGS.md #3), set directly by an
    /// in-process C++ caller - never serialized, so the wire protocols
    /// (WebAPI/MCP's JSON, the CLI's strings) have no way to set these and
    /// keep running Insert synchronously on their own calling thread, as
    /// before. The GUI's async insert worker is the one caller that sets
    /// them, to abort a large or slow/network folder scan off the UI thread
    /// and report progress back to it
    std::function<bool()> cancelRequested;
    std::function<void(uint64_t entriesScanned, uint64_t bytesScanned)> onProgress;
};

struct MediaReply
{
    MediaResult result;
    std::string slot;      ///< the resolved slot id
    bool pending = false;  ///< the change waits for the frame boundary (async, or a sync timeout)
    uint64_t revision = 0;
    /// Verb-specific fields, merged into the envelope: "slots", "detached",
    /// "info", "formats", "savedPath", "retargeted"
    StateNode body = StateNode::Object();

    /// The envelope every surface returns: ok, error, message, slot, pending,
    /// revision, report, then the body's fields
    StateNode ToValue() const;
    /// The envelope as JSON text (CLI --json; the WebAPI converts ToValue())
    std::string ToJson() const;
    int HttpStatus() const { return MediaErrorHttpStatus(result.error); }
};

class MediaControl
{
public:
    /// How long a sync request waits for the frame boundary and the swap delay
    static constexpr uint32_t kSyncTimeoutMs = 5000;

    explicit MediaControl(EmulatorContext* context);

    MediaReply Execute(const MediaRequest& request);

    static const std::vector<std::string>& Verbs();
    /// The options a verb accepts (help texts, the OpenAPI spec, tests)
    static const std::vector<std::string>& OptionsFor(const std::string& verb);

    /// Selector → slot id. Unknown and ambiguous selectors fail with a message
    /// that lists the candidates. `allowDetached` also accepts a detached
    /// medium's slot id (save, export, discard)
    static MediaResult ResolveSelector(const MediaManager& manager, const std::string& selector, std::string& slotId,
                                       bool allowDetached = false);

    /// One slot as the surfaces show it (media-control-design.md MC-1)
    static StateNode SlotValue(const SlotInfo& info);

private:
    MediaReply Run(const MediaRequest& request);
    MediaReply List();
    MediaReply Info(const MediaRequest& request);
    MediaReply Formats(const MediaRequest& request);
    MediaReply Targets(const MediaRequest& request);
    MediaReply Insert(const MediaRequest& request, bool swap);
    MediaReply Eject(const MediaRequest& request);
    MediaReply Save(const MediaRequest& request);
    MediaReply Export(const MediaRequest& request);
    MediaReply Discard(const MediaRequest& request);
    MediaReply Rescan(const MediaRequest& request);
    MediaReply Create(const MediaRequest& request);
    MediaReply Protect(const MediaRequest& request);
    MediaReply Compose(const MediaRequest& request);
    MediaReply Layers(const MediaRequest& request);

    /// The slot `insert auto` picks for `path` (§3.5)
    MediaResult ChooseSlot(const std::string& path, const std::map<std::string, std::string>& options, std::string& slotId);
    /// save / export a dirty medium before it leaves (the emulator parked for the write)
    MediaResult ApplyDisposition(const std::string& slotId, const std::map<std::string, std::string>& options,
                                 Disposition& remaining);
    /// Sync: wait for the change to reach the slot
    void Finish(MediaReply& reply, const std::map<std::string, std::string>& options);

    EmulatorContext* _context = nullptr;
    MediaManager* _manager = nullptr;
};
