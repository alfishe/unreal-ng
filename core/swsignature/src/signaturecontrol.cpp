#include "signaturecontrol.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "swsignature.h"

namespace unrealng::swsignature
{

namespace
{
SignatureReply Fail(const std::string& error, const std::string& message)
{
    SignatureReply reply;
    reply.error = error;
    reply.message = message;
    return reply;
}

std::string Option(const std::map<std::string, std::string>& options, const std::string& name)
{
    const auto it = options.find(name);
    return it != options.end() ? it->second : std::string();
}
}  // namespace

bool OptionFlag(const std::map<std::string, std::string>& options, const std::string& name)
{
    const auto it = options.find(name);
    if (it == options.end())
        return false;
    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.empty() || value == "1" || value == "true" || value == "yes" || value == "on";
}

StateNode SignatureReply::ToValue() const
{
    StateNode node = StateNode::Object();
    node["ok"] = Ok();
    if (!Ok())
    {
        node["error"] = error;
        node["message"] = message;
    }
    else if (!message.empty())
        node["message"] = message;
    for (const auto& [name, value] : body.members)
        node[name] = value;
    return node;
}

int SignatureReply::HttpStatus() const
{
    if (error.empty())
        return 200;
    if (error == "not-found")
        return 404;
    if (error == "unavailable")
        return 503;
    return 400;
}

const std::vector<std::string>& SignatureControl::Verbs()
{
    static const std::vector<std::string> verbs = {"file", "media", "compare"};
    return verbs;
}

SignatureReply SignatureControl::Execute(const SignatureRequest& request)
{
    if (request.verb == "file")
        return File(request);
    if (request.verb == "media")
        return Media(request);
    if (request.verb == "compare")
        return Compare(request);
    return Fail("bad-request", "unknown verb '" + request.verb + "' (file, media, compare)");
}

SignatureReply SignatureControl::File(const SignatureRequest& request)
{
    const std::string path = Option(request.options, "path");
    if (path.empty())
        return Fail("bad-request", "file needs a path");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return Fail("not-found", "no file '" + path + "'");
    Options options;
    options.externalHashes = OptionFlag(request.options, "external");
    Signatures signatures;
    std::string error;
    if (!ComputeFile(path, signatures, error, options))
        return Fail("bad-request", error);
    SignatureReply reply;
    reply.body["path"] = path;
    reply.body["signature"] = ToStateNode(signatures, OptionFlag(request.options, "details"));
    return reply;
}

SignatureReply SignatureControl::Media(const SignatureRequest& request)
{
    if (_context == nullptr || _context->pMediaManager == nullptr)
        return Fail("unavailable", "no machine to read the media of");
    Options options;
    options.externalHashes = OptionFlag(request.options, "external");
    const bool details = OptionFlag(request.options, "details");
    StateNode media = StateNode::Array();
    for (const SlotInfo& info : _context->pMediaManager->List())
    {
        if (!info.present || (info.descriptor.kind != ::MediaKind::Floppy && info.descriptor.kind != ::MediaKind::Tape))
            continue;
        StateNode item = StateNode::Object();
        item["slot"] = info.descriptor.id;
        item["source"] = info.source;
        item["dirty"] = info.dirty;
        std::error_code ec;
        Signatures signatures;
        std::string error;
        if (!std::filesystem::is_regular_file(info.source, ec))
            item["error"] = "the medium has no source file";
        else if (!ComputeFile(info.source, signatures, error, options))
            item["error"] = error;
        else
            item["signature"] = ToStateNode(signatures, details);
        media.push(item);
    }
    SignatureReply reply;
    reply.body["media"] = media;
    return reply;
}

SignatureReply SignatureControl::Compare(const SignatureRequest& request)
{
    const std::string path = Option(request.options, "path");
    const std::string with = Option(request.options, "with");
    if (path.empty() || with.empty())
        return Fail("bad-request", "compare needs path (the known title) and with (the candidate)");
    Signatures reference;
    Signatures candidate;
    std::string error;
    if (!ComputeFile(path, reference, error))
        return Fail("not-found", error);
    if (!ComputeFile(with, candidate, error))
        return Fail("not-found", error);
    SignatureReply reply;
    reply.body["path"] = path;
    reply.body["with"] = with;
    reply.body["match"] = ToStateNode(unrealng::swsignature::Compare(reference, candidate));
    return reply;
}

}  // namespace unrealng::swsignature
