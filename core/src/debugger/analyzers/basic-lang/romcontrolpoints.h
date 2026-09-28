#pragma once

#include "stdafx.h"

#include <cstdint>
#include <string>
#include <vector>

/// region <Documentation>

/// ROM control points for verified command input (design: input-verification.md,
/// next to this file). A control point is a ROM address whose execution means
/// a known editor event: waiting for a key, key taken, character inserted,
/// syntax error, command started, report.
///
/// Several ROMs share #0000-#3FFF, so an address means nothing on its own. A
/// 16K ROM page is first identified by its signature, then every control point
/// of that ROM is checked against the page's bytes. A page that fails either
/// check is Unknown and gets no hooks: a changed ROM image fails loudly instead
/// of hooking the wrong code.

/// endregion </Documentation>

namespace ROMControlPoints
{
    /// ROM families with a known editor. The 48 BASIC family covers the
    /// Sinclair 48K ROM, the 128K ROM1, the Pentagon and Scorpion 48K ROMs and
    /// the +2A/+3 ROM3: their editor and main loop sit at the same addresses.
    enum class RomKind : uint8_t
    {
        Unknown = 0,
        Basic48,        ///< 48K editor and BASIC
        Editor128,      ///< Spectrum 128 ROM0 editor (also Pentagon and Scorpion ROM0)
        Plus3Rom0,      ///< +2A/+3 v4.0 ROM0 editor
        Plus3Rom1,      ///< +2A/+3 v4.0 ROM1 syntax checker and command runner
        TrDos           ///< TR-DOS 5.03 / 5.04T / 5.04TM
    };

    /// Editor events; each ROM defines the subset it has
    enum class Point : uint8_t
    {
        EditorIdle = 0,   ///< editor waiting for a key
        KeyTaken,         ///< new key flag seen, LAST_K being read
        KeyAccepted,      ///< back in the editor loop, A = key code
        CharInserted,     ///< character or token inserted, A = byte
        EditKey,          ///< editing key (cursor, delete), A = code
        Rasp,             ///< key rejected: error beep (interrupts off inside)
        LineFull,         ///< edit line full: error beep
        Enter,            ///< ENTER taken
        SyntaxResult,     ///< syntax check done: ERR_NR == #FF means OK
        LineAccepted,     ///< syntax OK
        LineStored,       ///< numbered line stored in the program
        ExecStart,        ///< direct command starts running
        ErrorRaised,      ///< runtime error, L = code (48 BASIC)
        Report,           ///< command finished or stopped, report = ERR_NR + 1
        TapeLoader,       ///< LD-BYTES entered
        EditorInit,       ///< 128K editor set-up: keys pressed now are lost
        TrDosPrompt,      ///< TR-DOS command loop top
        TrDosLineBack,    ///< the 48K editor returned a line to TR-DOS
        TrDosDispatch,    ///< TR-DOS looks the command up, A = first byte
        TrDosError,       ///< TR-DOS error exit: not a command, syntax, or a command failed
        TrDosSyntaxError, ///< TR-DOS syntax error message
        TrDosExecute,     ///< a TR-DOS command handler runs (execute pass)
        TrDosFound,       ///< the line's first byte matched a TR-DOS command
        ReportShown,      ///< a report is up: the next key clears it, then goes into an empty line (128K, +3)
        Count
    };

    struct PointDef
    {
        Point point;
        RomKind rom;
        uint16_t address;
        std::vector<uint8_t> expect;  ///< bytes at the address (a prefix of the instruction)
        const char* label;            ///< ROM listing label, for logs
    };

    /// Every control point of every known ROM
    const std::vector<PointDef>& All();

    /// Identifies a 16K ROM page by signature, then checks all its control
    /// points. Unknown when either fails.
    RomKind Identify(const uint8_t* page);

    /// Control points of `rom` whose bytes differ from `page` ("#0F38 ED-LOOP"...)
    std::vector<std::string> Mismatches(RomKind rom, const uint8_t* page);

    const char* RomName(RomKind rom);
    const char* PointName(Point point);
}
