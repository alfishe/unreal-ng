#pragma once

/// @file signaturecontrol.h
/// @brief The signature verbs for every surface (CLI, WebAPI and MCP through it, Python): a surface turns its input
/// into a SignatureRequest and the reply's StateNode into its output, as MediaControl does for the media verbs.
///
/// Verbs:
/// - `file`: the signatures of a file (`path`; `details` adds the blocks / files, `external` SHA-1 and MD5)
/// - `media`: the signatures of every medium in the machine's tape and floppy slots, from their source files
/// - `compare`: `path` (the known title) against `with` (the candidate): level exact / content / fuzzy / memory / none
///
/// @code
///   SignatureReply reply = SignatureControl(context).Execute({"file", {{"path", "Elite.tzx"}}});
///   http.status = reply.HttpStatus();  http.body = reply.ToValue();
/// @endcode

#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

namespace unrealng::swsignature
{

struct SignatureRequest
{
    std::string verb;
    /// path, with, details, external; a flag given without a value ("details") is true
    std::map<std::string, std::string> options;
};

struct SignatureReply
{
    std::string error;    ///< empty = ok; bad-request, not-found, unavailable
    std::string message;
    StateNode body = StateNode::Object();
    bool Ok() const
    {
        return error.empty();
    }
    /// {ok, error, message, ...body}
    StateNode ToValue() const;
    int HttpStatus() const;
};

class SignatureControl
{
public:
    /// @p context: the machine `media` reads; nullptr for `file` / `compare` alone
    explicit SignatureControl(EmulatorContext* context = nullptr) : _context(context)
    {
    }
    SignatureReply Execute(const SignatureRequest& request);
    static const std::vector<std::string>& Verbs();

private:
    SignatureReply File(const SignatureRequest& request);
    SignatureReply Media(const SignatureRequest& request);
    SignatureReply Compare(const SignatureRequest& request);

    EmulatorContext* _context = nullptr;
};

/// "", "1", "true", "yes", "on" are true
bool OptionFlag(const std::map<std::string, std::string>& options, const std::string& name);

}  // namespace unrealng::swsignature
