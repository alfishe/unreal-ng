#include "debugger/markup/markupcontrol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace markup
{

namespace
{
constexpr size_t kDefaultLimit = 200;
constexpr size_t kMaxLimit = 4096;
constexpr uint16_t kDefaultSaveConfidence = 700;  ///< analysis::confidence::kReview: below it the review queue

std::mutex& SaverLock()
{
    static std::mutex lock;
    return lock;
}

ProfileSaver& Saver()
{
    static ProfileSaver saver;
    return saver;
}

Reply Refuse(Error error, const std::string& message)
{
    Reply reply;
    reply.error = error;
    reply.message = message;
    reply.text = message;
    return reply;
}

std::string Hex(uint16_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value);
    return text;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool ParseBool(const std::string& text, bool& value)
{
    const std::string v = Lower(text);
    if (v == "true" || v == "1" || v == "yes" || v == "on")
        value = true;
    else if (v == "false" || v == "0" || v == "no" || v == "off")
        value = false;
    else
        return false;
    return true;
}

std::string Option(const Options& options, const std::string& name)
{
    const auto it = options.find(name);
    return it != options.end() ? it->second : std::string();
}

MemoryMarkup* MarkupOf(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetMemoryMarkup()
                                                                   : nullptr;
}

/// An address or a label; false with the refusal
bool AddressOf(Emulator& emulator, const std::string& text, const char* what, uint16_t& address, Reply& refusal)
{
    if (analysis::ParseAddress(text, address))
        return true;
    EmulatorContext* context = emulator.GetContext();
    LabelManager* labels = context && context->pDebugManager ? context->pDebugManager->GetLabelManager() : nullptr;
    const std::shared_ptr<Label> label = labels != nullptr ? labels->GetLabelByName(text) : nullptr;
    if (!label)
    {
        refusal = Refuse(Error::NotFound, std::string(what) + ": no address or label '" + text + "'");
        return false;
    }
    address = label->address;
    return true;
}

/// range=, or from= [to=] (to defaults to from); neither: the whole 64K unless @p required. False with the refusal
bool RangeOf(Emulator& emulator, const Options& options, bool required, uint16_t& from, uint16_t& to,
             Reply& refusal)
{
    from = 0x0000;
    to = 0xFFFF;
    const std::string range = Option(options, "range");
    if (!range.empty())
    {
        analysis::ExportRange parsed;
        if (!analysis::ParseZ80Range(range, parsed))
        {
            refusal = Refuse(Error::BadRequest, "range: '" + range + "' is no range (8000-80FF, $8000-$80FF)");
            return false;
        }
        from = parsed.start;
        to = parsed.end;
        return true;
    }
    const std::string fromText = Option(options, "from");
    const std::string toText = Option(options, "to");
    if (fromText.empty())
    {
        if (required)
        {
            refusal = Refuse(Error::BadRequest, "give the addresses: from= [to=], or range=8000-80FF");
            return false;
        }
        return toText.empty() || AddressOf(emulator, toText, "to", to, refusal);
    }
    if (!AddressOf(emulator, fromText, "from", from, refusal))
        return false;
    to = from;
    if (!toText.empty() && !AddressOf(emulator, toText, "to", to, refusal))
        return false;
    if (to < from)
    {
        refusal = Refuse(Error::BadRequest, "the range " + Hex(from) + "-" + Hex(to) + " ends before it starts");
        return false;
    }
    return true;
}

bool LimitOf(const Options& options, size_t& limit, Reply& refusal)
{
    limit = kDefaultLimit;
    const std::string text = Option(options, "limit");
    if (text.empty())
        return true;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value == 0 || value > kMaxLimit)
    {
        refusal = Refuse(Error::BadRequest, "limit: 1-" + std::to_string(kMaxLimit));
        return false;
    }
    limit = value;
    return true;
}

StateNode LayersNode(const MemoryMarkup& markup)
{
    StateNode list = StateNode::Array();
    for (const LayerSummary& layer : markup.Summary())
    {
        StateNode node = StateNode::Object();
        node["layer"] = LayerName(layer.layer);
        node["source"] = layer.source;
        node["claims"] = static_cast<uint64_t>(layer.claims);
        node["entries"] = static_cast<uint64_t>(layer.entries);
        node["comments"] = static_cast<uint64_t>(layer.comments);
        list.push(std::move(node));
    }
    return list;
}

Reply Show(Emulator& emulator, MemoryMarkup& markup, const Options& options)
{
    Reply refusal;
    uint16_t from = 0;
    uint16_t to = 0;
    size_t limit = 0;
    if (!RangeOf(emulator, options, false, from, to, refusal) || !LimitOf(options, limit, refusal))
        return refusal;
    bool onlyLayer = false;
    Layer layer = Layer::User;
    if (const std::string text = Option(options, "layer"); !text.empty())
    {
        if (!ParseLayer(Lower(text), layer))
            return Refuse(Error::BadRequest, "layer: user, profile, analysis, runtime or bundle, not '" + text + "'");
        onlyLayer = true;
    }

    std::vector<Span> spans = markup.Ranges(from, to);
    if (onlyLayer)
        std::erase_if(spans, [layer](const Span& span) { return span.layer != layer; });
    const std::vector<uint16_t> anchors = markup.Anchors(from, to);
    const std::vector<CommentAt> comments = markup.Comments(from, to);

    Reply reply;
    StateNode& body = reply.body;
    body["from"] = Hex(from);
    body["to"] = Hex(to);
    body["revision"] = markup.Revision();
    body["count"] = static_cast<uint64_t>(spans.size());
    body["truncated"] = spans.size() > limit || anchors.size() > limit || comments.size() > limit;
    std::ostringstream text;
    text << "Markup " << Hex(from) << "-" << Hex(to) << ": " << spans.size() << " span(s), " << anchors.size()
         << " instruction start(s), " << comments.size() << " comment(s)";
    if (spans.empty())
        text << "; no layer claims anything here";

    StateNode& spanList = body["spans"];
    spanList = StateNode::Array();
    for (size_t i = 0; i < spans.size() && i < limit; i++)
    {
        const Span& span = spans[i];
        StateNode node = StateNode::Object();
        node["from"] = Hex(span.lo);
        node["to"] = Hex(span.hi);
        node["kind"] = analysis::RegionKindName(span.kind);
        node["layer"] = LayerName(span.layer);
        node["source"] = span.source;
        node["title"] = span.title;
        node["confidence"] = static_cast<uint64_t>(span.confidence);
        node["start"] = span.claimStart;
        spanList.push(std::move(node));
        text << "\n" << Hex(span.lo) << "-" << Hex(span.hi) << " " << analysis::RegionKindName(span.kind) << " ["
             << span.source << "]";
        if (!span.title.empty())
            text << " " << span.title;
        if (span.confidence < 1000)
            text << " (" << span.confidence << ")";
    }
    if (spans.size() > limit)
        text << "\n... " << (spans.size() - limit) << " more span(s): narrow the range or raise limit";

    StateNode& anchorList = body["anchors"];
    anchorList = StateNode::Array();
    for (size_t i = 0; i < anchors.size() && i < limit; i++)
        anchorList.push(Hex(anchors[i]));
    StateNode& commentList = body["comments"];
    commentList = StateNode::Array();
    for (size_t i = 0; i < comments.size() && i < limit; i++)
    {
        StateNode node = StateNode::Object();
        node["address"] = Hex(comments[i].address);
        node["layer"] = LayerName(comments[i].layer);
        node["text"] = comments[i].text;
        commentList.push(std::move(node));
        text << "\n; " << Hex(comments[i].address) << " " << comments[i].text;
    }
    reply.text = text.str();
    return reply;
}

Reply Layers(MemoryMarkup& markup)
{
    Reply reply;
    reply.body["layers"] = LayersNode(markup);
    reply.body["runtime"] = markup.RuntimeFeed();
    reply.body["revision"] = markup.Revision();
    std::ostringstream text;
    text << "Markup layers (revision " << markup.Revision() << ", highest first):";
    const std::vector<LayerSummary> summary = markup.Summary();
    for (auto it = summary.rbegin(); it != summary.rend(); ++it)
    {
        text << "\n  " << LayerName(it->layer) << ": ";
        if (it->layer == Layer::Runtime)
            text << "derived from " << markup.RuntimeFeed() << " at each query";
        else if (it->claims == 0 && it->entries == 0 && it->comments == 0)
            text << "empty";
        else
            text << it->source << ", " << it->claims << " claim(s), " << it->entries << " entries, "
                 << it->comments << " comment(s)";
    }
    reply.text = text.str();
    return reply;
}

Reply Mark(Emulator& emulator, MemoryMarkup& markup, const Options& options)
{
    Reply refusal;
    uint16_t from = 0;
    uint16_t to = 0;
    if (!RangeOf(emulator, options, true, from, to, refusal))
        return refusal;
    const std::string kindText = Lower(Option(options, "kind"));
    analysis::RegionKind kind = analysis::RegionKind::Data;
    if (kindText.empty() || !analysis::ParseRegionKind(kindText, kind))
        return Refuse(Error::BadRequest, "kind: code, data, text, font, sprite, tile, screen, buffer, ... (a region "
                                         "kind of the analysis), not '" + kindText + "'");
    bool cpu = false;
    if (const std::string text = Option(options, "cpu"); !text.empty() && !ParseBool(text, cpu))
        return Refuse(Error::BadRequest, "cpu: true or false");
    const std::string title = Option(options, "title");
    markup.Mark(from, to, kind, title, cpu);

    Reply reply;
    reply.body["from"] = Hex(from);
    reply.body["to"] = Hex(to);
    reply.body["kind"] = analysis::RegionKindName(kind);
    reply.body["cpu"] = cpu;
    reply.body["revision"] = markup.Revision();
    reply.text = "Marked " + Hex(from) + "-" + Hex(to) + " as " + analysis::RegionKindName(kind) +
                 (title.empty() ? std::string() : " '" + title + "'") +
                 (cpu ? " (on the CPU addresses)" : " (on the pages mapped now)");
    return reply;
}

Reply Unmark(Emulator& emulator, MemoryMarkup& markup, const Options& options)
{
    Reply refusal;
    uint16_t from = 0;
    uint16_t to = 0;
    if (!RangeOf(emulator, options, true, from, to, refusal))
        return refusal;
    markup.Unmark(from, to);
    Reply reply;
    reply.body["from"] = Hex(from);
    reply.body["to"] = Hex(to);
    reply.body["revision"] = markup.Revision();
    reply.text = "Unmarked " + Hex(from) + "-" + Hex(to) + " in the user layer";
    return reply;
}

Reply Comment(Emulator& emulator, MemoryMarkup& markup, const Options& options)
{
    const std::string addressText = Option(options, "address");
    if (addressText.empty())
        return Refuse(Error::BadRequest, "comment needs an address ($8000, 0x8000, decimal or a label)");
    Reply refusal;
    uint16_t address = 0;
    if (!AddressOf(emulator, addressText, "address", address, refusal))
        return refusal;
    const std::string text = Option(options, "text");
    markup.SetComment(address, text);
    Reply reply;
    reply.body["address"] = Hex(address);
    reply.body["text"] = text;
    reply.body["revision"] = markup.Revision();
    reply.text = text.empty() ? "Removed the comment at " + Hex(address) : "Comment at " + Hex(address) + ": " + text;
    return reply;
}

Reply Clear(MemoryMarkup& markup, const Options& options)
{
    Layer layer = Layer::User;
    if (const std::string text = Option(options, "layer"); !text.empty() && !ParseLayer(Lower(text), layer))
        return Refuse(Error::BadRequest, "layer: user, profile, analysis, runtime or bundle, not '" + text + "'");
    markup.ClearLayer(layer);
    Reply reply;
    reply.body["layer"] = LayerName(layer);
    reply.body["revision"] = markup.Revision();
    reply.text = std::string("Cleared the ") + LayerName(layer) + " layer" +
                 (layer == Layer::Analysis ? " (the last L2 build shows again)" : "");
    return reply;
}

Reply Save(Emulator& emulator, MemoryMarkup& markup, const Options& options)
{
    ProfileSaver saver;
    {
        std::lock_guard<std::mutex> lock(SaverLock());
        saver = Saver();
    }
    if (!saver)
        return Refuse(Error::NotAvailable, "this process keeps no software profiles (unreal-qt does)");

    SaveRequest request;
    request.profile = Option(options, "profile");
    request.user = markup.GetLayer(Layer::User);

    // The analysis claims chosen: all or the kinds named, of at least the confidence given
    if (const std::string kinds = Lower(Option(options, "analysis")); !kinds.empty() && kinds != "false")
    {
        std::vector<analysis::RegionKind> wanted;
        const bool all = kinds == "all" || kinds == "true";
        std::stringstream list(kinds);
        for (std::string name; !all && std::getline(list, name, ',');)
        {
            analysis::RegionKind kind = analysis::RegionKind::Data;
            if (!analysis::ParseRegionKind(name, kind))
                return Refuse(Error::BadRequest, "analysis: all or region kinds (code,sprite,...), not '" + name + "'");
            wanted.push_back(kind);
        }
        uint16_t minimum = kDefaultSaveConfidence;
        if (const std::string text = Option(options, "confidence"); !text.empty())
        {
            char* end = nullptr;
            const unsigned long value = std::strtoul(text.c_str(), &end, 10);
            if (end == text.c_str() || *end != '\0' || value > 1000)
                return Refuse(Error::BadRequest, "confidence: 0-1000");
            minimum = static_cast<uint16_t>(value);
        }
        for (const Claim& claim : markup.GetLayer(Layer::Analysis).claims)
            if (claim.confidence >= minimum &&
                (all || std::find(wanted.begin(), wanted.end(), claim.kind) != wanted.end()))
                request.analysis.push_back(claim);
    }
    bool keep = false;
    if (const std::string text = Option(options, "keep"); !text.empty() && !ParseBool(text, keep))
        return Refuse(Error::BadRequest, "keep: true or false");
    if (request.user.claims.empty() && request.user.comments.empty() && request.analysis.empty())
        return Refuse(Error::Conflict, "nothing to save: the user layer is empty and no analysis claims were chosen "
                                       "(analysis=all or kinds)");

    Reply reply = saver(emulator, request);
    if (!reply.Ok())
        return reply;
    // The marks live in the profile layer now; a profile that does not lie over the machine (the saver's applied:
    // false, another one matches more strongly) would take them out of sight, so they stay
    const StateNode* applied = reply.body.find("applied");
    keep = keep || (applied != nullptr && applied->kind == StateNode::Kind::Bool && !applied->b);
    if (!keep)
        markup.ClearLayer(Layer::User);
    reply.body["saved_user_claims"] = static_cast<uint64_t>(request.user.claims.size());
    reply.body["saved_comments"] = static_cast<uint64_t>(request.user.comments.size());
    reply.body["saved_analysis_claims"] = static_cast<uint64_t>(request.analysis.size());
    reply.body["kept_user_layer"] = keep;
    return reply;
}
}  // namespace

const std::vector<std::string>& Actions()
{
    static const std::vector<std::string> actions = {"show", "layers", "mark", "unmark", "comment", "clear", "save"};
    return actions;
}

const std::vector<std::string>& OptionsFor(const std::string& action)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"show", {"from", "to", "range", "layer", "limit"}},
        {"layers", {}},
        {"mark", {"from", "to", "range", "kind", "title", "cpu"}},
        {"unmark", {"from", "to", "range"}},
        {"comment", {"address", "text"}},
        {"clear", {"layer"}},
        {"save", {"profile", "analysis", "confidence", "keep"}},
    };
    static const std::vector<std::string> none;
    const auto it = options.find(action);
    return it != options.end() ? it->second : none;
}

Reply Execute(Emulator& emulator, const std::string& action, const Options& options)
{
    const std::vector<std::string>& actions = Actions();
    if (std::find(actions.begin(), actions.end(), action) == actions.end())
        return Refuse(Error::BadRequest, "unknown markup action '" + action +
                                             "' (show, layers, mark, unmark, comment, clear, save)");
    const std::vector<std::string>& known = OptionsFor(action);
    for (const auto& [name, value] : options)
        if (std::find(known.begin(), known.end(), name) == known.end())
        {
            std::string names;
            for (const std::string& option : known)
                names += (names.empty() ? "" : ", ") + option;
            return Refuse(Error::BadRequest, action + ": unknown option '" + name + "'" +
                                                 (names.empty() ? " (it takes none)" : " (known: " + names + ")"));
        }
    MemoryMarkup* markup = MarkupOf(emulator);
    if (markup == nullptr)
        return Refuse(Error::NotAvailable, "no memory markup on this instance");

    if (action == "show")
        return Show(emulator, *markup, options);
    if (action == "layers")
        return Layers(*markup);
    if (action == "mark")
        return Mark(emulator, *markup, options);
    if (action == "unmark")
        return Unmark(emulator, *markup, options);
    if (action == "comment")
        return Comment(emulator, *markup, options);
    if (action == "clear")
        return Clear(*markup, options);
    return Save(emulator, *markup, options);
}

void SetProfileSaver(ProfileSaver saver)
{
    std::lock_guard<std::mutex> lock(SaverLock());
    Saver() = std::move(saver);
}

}  // namespace markup
