#pragma once

/// @file symbolcontrol.h
/// @brief The one place that implements the symbol verbs for every surface (symbols/tdd.md section 8): WebAPI (and MCP
/// through it), CLI, Lua and Python only turn their input into a SymbolRequest and the SymbolReply into their output.
/// Option names and parsing, defaults, refusals and their texts and the reply's fields live here, so the surfaces
/// cannot drift apart.
///
/// Same pattern as TTDControl: a verb, options by name as strings, a reply with an error code and a StateNode body
/// every surface already converts (JSON, sol::table, py::dict).
///
/// Verbs:
///   formats                    the symbol codecs: id, title, family, extensions, pages, comment
///   detect   path              the codecs that could read the file, by score, and the one chosen
///   sets                       the symbol sets: id, title, origin, priority, enabled, symbols; the label count
///   import   path | data name [format set space base policy]   a file (or its bytes as base64) into the store
///   export   path [format sets pages space from to kinds name]   write symbols, filtered (LabelManager::ExportSymbols)
///   set      id [enabled priority]                 switch a set on / off, change its priority
///   drop     id                                    remove a set
///   scan                                           label tables of assemblers in RAM (ALASM, XAS): the candidates
///   import-live [scanner page offset set policy]   read a label table (the best candidate, or the one named) into a set
///   import-source path [main set policy generated]   the labels a source defines with their values (a source, an image,
///                                                  a hobeta file, or disk:A/NAME.T: the disk in a drive as the project)
///
/// @code
///   SymbolReply reply = SymbolControl(context).Execute({"import", {{"path", "game.sym"}, {"policy", "replace"}}});
///   http.status = reply.HttpStatus();  http.body = StateNodeToJson(reply.ToValue());
/// @endcode

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;
class LabelManager;

enum class SymbolControlError : uint8_t
{
    None,
    NotAvailable,  ///< no debug manager / label manager (HTTP 503)
    BadRequest,    ///< unknown verb or option, a value that does not parse, a file that cannot be read (HTTP 400)
    NotFound,      ///< no such set (HTTP 404)
    Conflict,      ///< the merge stopped at a conflict (policy fail) (HTTP 409)
};

int SymbolControlErrorHttpStatus(SymbolControlError error);
const char* SymbolControlErrorPhrase(SymbolControlError error);

struct SymbolRequest
{
    std::string verb;
    std::map<std::string, std::string> options;
};

struct SymbolReply
{
    SymbolControlError error = SymbolControlError::None;
    std::string message;  ///< one sentence a user can act on; every surface shows it verbatim
    StateNode body = StateNode::Object();

    bool Ok() const { return error == SymbolControlError::None; }
    /// Success: the body. Failure: {"error": <phrase>, "message": ...} then the body's fields
    StateNode ToValue() const;
    int HttpStatus() const { return SymbolControlErrorHttpStatus(error); }
};

class SymbolControl
{
public:
    explicit SymbolControl(EmulatorContext* context);
    explicit SymbolControl(LabelManager* labels);

    /// Check the verb and its options, then run it
    SymbolReply Execute(const SymbolRequest& request);

    static const std::vector<std::string>& Verbs();
    /// The option names a verb takes
    static const std::vector<std::string>& OptionsFor(const std::string& verb);

private:
    SymbolReply Formats();
    SymbolReply Detect(const SymbolRequest& request);
    SymbolReply Sets();
    SymbolReply Import(const SymbolRequest& request);
    SymbolReply Export(const SymbolRequest& request);
    SymbolReply Set(const SymbolRequest& request);
    SymbolReply Drop(const SymbolRequest& request);
    SymbolReply Scan();
    SymbolReply ImportLive(const SymbolRequest& request);
    SymbolReply ImportSource(const SymbolRequest& request);
    /// The machine's RAM pages, copied at a coherent moment (false with the reason)
    bool CopyRam(std::vector<std::vector<uint8_t>>& pages, std::string& error) const;

    EmulatorContext* _context = nullptr;
    LabelManager* _labels = nullptr;
};
