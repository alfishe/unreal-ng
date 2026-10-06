#pragma once

/// @file slotconfig.h
/// @brief The machine INI's [SLOTS] section as configured (docs/inprogress/2026-10-03-zx-bus-slots/architecture.md
/// §6): one entry per slot, the built-in switches, and the legacy card keys the INI still carried. Plain data, part
/// of CONFIG (platform.h), so it names nothing Qt defines as a macro (`slots`, `signals`): global types, no
/// namespace. SlotManager (slotmanager.h) plans it when the instance is created.
///
/// Key form:
///   ay-socket = tsfm                  the AY socket: ay | ts | tsfm | none (no chip)
///   zxbus.1 = neogs                   a card in slot <bus>.<n> (n >= 1): <bus> is a bus id of the machine
///   zxbus.1.ram = 4m                  a card option: <slot>.<option> = value (comma list for a set option)
///   cpu-socket.1.adapter = atm-cpu-socket-zxbus     an adapter in the slot (the card sits behind it)
///   edge.1.fit = unrealistic          the fit override for this slot (Q5): the card works as if the bus carried
///                                     every signal it needs, and every report says `unrealistic`
///   builtin.covox = off               a switchable built-in device: on | off

#include <string>
#include <utility>
#include <vector>

/// One configured slot
struct SlotConfigEntry
{
    std::string slot;           ///< "ay-socket", "zxbus.1", "edge.1"
    std::string card;           ///< card id ("neogs"); "none" in the AY socket = no chip
    std::string options;        ///< "ram=4m mode=both" (slots::ParseCardOptions form)
    std::string adapter;        ///< adapter id in the slot, empty for none
    bool fitOverride = false;   ///< "<slot>.fit = unrealistic"; set on every translated legacy key
    std::string source;         ///< where it came from: "[SLOTS] zxbus.1", "[SOUND] GSType=NGS"
};

/// One switchable built-in device switched on or off ("builtin.<id> = on | off")
struct SlotBuiltInSwitch
{
    std::string id;             ///< built-in id of the machine declaration ("covox")
    bool on = true;
    std::string source;
};

/// The [SLOTS] section of one machine INI
struct SlotConfig
{
    bool section = false;                       ///< the INI has [SLOTS]: it is the single source of the cards
    std::vector<SlotConfigEntry> entries;       ///< in file order (the plan sorts them into slot order)
    std::vector<SlotBuiltInSwitch> builtIns;
    std::vector<std::string> legacyKeys;        ///< legacy card keys present in the INI ("[SOUND] GSType=NGS")
    std::vector<std::string> errors;            ///< [SLOTS] lines that could not be read, with the reason

    void Clear()
    {
        *this = SlotConfig{};
    }
};

/// Reads the [SLOTS] key / value pairs (file order) into `out` (cleared first, `section` set). A malformed line is
/// skipped and named in out.errors; the rest are kept. Options are collected per slot whatever the line order
void ParseSlotsSection(const std::vector<std::pair<std::string, std::string>>& keyValues, SlotConfig& out);

/// The [SLOTS] section text of a slot configuration (the inverse of ParseSlotsSection, one key per line, entries in
/// the given order): for tests, tools and the shipped-config conversion
std::string FormatSlotsSection(const SlotConfig& config);
