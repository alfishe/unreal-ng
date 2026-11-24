#pragma once

/// @file gsfirmwareobjects.h
/// @brief The firmware objects of a General Sound card, read through its profile (gs-debugger design §8A.9, M1-M3,
/// M7, F5): struct instances bound to a physical page (rows of fields), variables in memory, in a register over a PC
/// range, or in an instruction's operand, and the fixed-window variables of the built-in tables.
///
/// The profile has the layout; this file has no firmware-specific code. Reads are plain byte reads of the card's
/// memory pages (mapped or not, M2); a register variable is read from the card CPU's registers and is valid only
/// while its PC is in range. Any thread: a running card may change a value between two bytes, as any memory view.

#include <cstdint>
#include <string>

#include "emulator/state/statenode.h"

class EmulatorContext;
class GeneralSoundCard;
struct GSFirmwareProfile;

/// One read of the objects
struct GSFirmwareObjectsQuery
{
    std::string profile; ///< a profile id ("" = the card's active one)
    std::string object;  ///< one instance or variable by name ("" = all)
    std::string cpu;     ///< the card by its debugger CPU: "" or "gs" the GS slot's, "ms" the ZX-MultiSound's
};

struct GSFirmwareObjectsReply
{
    int status = 200;    ///< as HTTP: 404 no card, no profile, no such object
    StateNode body;      ///< {profile, title, source, instances[], variables[], window_variables[]} or {error}
    std::string text;    ///< the same for people
};

/// The objects of `profile` on `card`: what the query asks for
StateNode GSReadFirmwareObjects(const GeneralSoundCard& card, const GSFirmwareProfile& profile,
                                const std::string& object = {});

/// The objects of the machine's GS slot card, or of the card `query.cpu` names (every plane: WebAPI `/state/audio/gs/firmware`, CLI `gs firmware`, MCP,
/// Lua, Python)
GSFirmwareObjectsReply GSFirmwareObjectsRead(EmulatorContext* context, const GSFirmwareObjectsQuery& query);
/// The profiles known (built-in and files): {profiles: [{id, title, source, sha256, matches[], commands, structs,
/// instances, variables, dispatch}]}, and their lines in `text`
StateNode GSFirmwareProfilesState(std::string* text = nullptr);
/// A read's body as lines: "smp[0] @ram 2:#0100: SamplAdr #4A00, Vol 64"
std::string GSFirmwareObjectsText(const StateNode& body);
