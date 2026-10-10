#pragma once

/// @file asmcontrol.h
/// @brief The one place that implements the assembler-source verbs for every surface (unreal-asm tdd §7,
/// architecture.md §8): WebAPI (and MCP through it), CLI, Lua, Python and the Qt disk browser only turn their input into
/// an AsmRequest and the AsmReply into their output. The same pattern as SymbolControl and TTDControl.
///
/// A source is `path` (a host file; a .trd / .tap / .tzx image with `file` NAME.T inside it; a hobeta $X file) or
/// "disk:A/NAME.T" (the file on the disk in drive A), or `data` (the file as base64) with `name`. An output is `output`
/// (a host file or "disk:A/NAME.T"); without one the reply carries the text (decode, convert) or the bytes as base64
/// (encode).
///
/// Verbs:
///   formats                                  the source codecs and their versions
///   dialects                                 the dialects that can be read and written
///   files    [drive]                         the files on a disk, each with the format detected
///   detect   source                          the codecs that could read it, by score, and the one chosen
///   decode   source [codec version codepage output]            the text (UTF-8)
///   encode   text | input  codec [version codepage lineend output start]   the bytes in a format
///   convert  source to [codec version from z80n output]        the source in another dialect
///   sync-status / sync-probe / sync-extract   the source an assembler running in the machine holds in RAM
///                                            (sync/synccontrol.h, asm-synchronizer.md)

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

enum class AsmControlError : uint8_t
{
    None,
    BadRequest,    ///< unknown verb or option, a value that does not parse, no source (HTTP 400)
    NotFound,      ///< no such file, codec or dialect (HTTP 404)
    Refused,       ///< the disk is write-protected or full (HTTP 409)
    Failed,        ///< the bytes are no source of the format, the conversion failed (HTTP 422)
};

int AsmControlErrorHttpStatus(AsmControlError error);
const char* AsmControlErrorPhrase(AsmControlError error);

struct AsmRequest
{
    std::string verb;
    std::map<std::string, std::string> options;
};

struct AsmReply
{
    AsmControlError error = AsmControlError::None;
    std::string message;
    StateNode body = StateNode::Object();

    bool Ok() const { return error == AsmControlError::None; }
    StateNode ToValue() const;
    int HttpStatus() const { return AsmControlErrorHttpStatus(error); }
};

class AsmControl
{
public:
    /// `context` may be null: then disk:A/... sources and outputs are refused
    explicit AsmControl(EmulatorContext* context);

    AsmReply Execute(const AsmRequest& request);

    static const std::vector<std::string>& Verbs();
    static const std::vector<std::string>& OptionsFor(const std::string& verb);

private:
    AsmReply Formats();
    AsmReply Dialects();
    AsmReply Files(const AsmRequest& request);
    AsmReply Detect(const AsmRequest& request);
    AsmReply Decode(const AsmRequest& request);
    AsmReply Encode(const AsmRequest& request);
    AsmReply Convert(const AsmRequest& request);

    EmulatorContext* _context = nullptr;
};
