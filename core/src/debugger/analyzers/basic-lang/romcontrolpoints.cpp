#include "romcontrolpoints.h"

#include <cstring>

namespace ROMControlPoints
{
namespace
{
constexpr size_t PAGE_SIZE = 0x4000;

struct Signature
{
    uint16_t offset;
    std::vector<uint8_t> bytes;
};

bool Matches(const uint8_t* page, uint16_t offset, const std::vector<uint8_t>& bytes)
{
    if (offset + bytes.size() > PAGE_SIZE)
        return false;
    return std::memcmp(page + offset, bytes.data(), bytes.size()) == 0;
}

bool MatchesAll(const uint8_t* page, const std::vector<Signature>& signatures)
{
    for (const Signature& s : signatures)
    {
        if (!Matches(page, s.offset, s.bytes))
            return false;
    }
    return true;
}

// Identification signatures, checked against data/rom (2026-09-27)
const std::vector<Signature> BASIC48 = {
    { 0x0000, { 0xF3, 0xAF, 0x11, 0xFF, 0xFF, 0xC3 } },
    { 0x0F38, { 0xCD, 0xD4, 0x15 } },
};
// Not the reset vector: Scorpion's ROM0 is this editor behind its own reset
// code (#0000 = F3 C3 D1 08 instead of F3 01 2B 69)
const std::vector<Signature> EDITOR128 = {
    { 0x2653, { 0x31, 0xFF, 0x5B } },
    { 0x3683, { 0xCB, 0x6E, 0x28, 0xFC, 0xCB, 0xAE, 0x3A, 0x08, 0x5C } },
};
const std::vector<Signature> PLUS3ROM0 = {
    { 0x0000, { 0xF3, 0x01, 0x03, 0x6C } },
    { 0x1875, { 0xCB, 0x6E, 0x28, 0xFC } },
};
const std::vector<Signature> PLUS3ROM1 = {
    { 0x0000, { 'S', 'y', 'n', 't', 'a', 'x' } },
    { 0x24F0, { 0xFD, 0x36, 0x00, 0xFF, 0xFD, 0x36, 0x31, 0x02 } },
};
const std::vector<Signature> TRDOS = {
    { 0x3D00, { 0x00, 0x18, 0x2E } },
    { 0x3D2F, { 0x00, 0xC9 } },
};
} // anonymous namespace

const std::vector<PointDef>& All()
{
    // Addresses and bytes verified against the ROM files in data/rom with a
    // disassembly (input-verification.md §4). The byte string is a prefix of
    // the instruction at the address, enough to tell a moved routine.
    static const std::vector<PointDef> points = {
        // 48 BASIC: Sinclair 48K, 128K ROM1, Pentagon/Scorpion 48K, +3 ROM3
        { Point::EditorIdle,    RomKind::Basic48, 0x15DE, { 0xCD, 0xE6, 0x15 }, "WAIT-KEY" },
        { Point::KeyTaken,      RomKind::Basic48, 0x10B5, { 0x3A, 0x08, 0x5C }, "KEY-INPUT" },
        { Point::KeyAccepted,   RomKind::Basic48, 0x0F3B, { 0xF5 }, "ED-LOOP+3" },
        { Point::CharInserted,  RomKind::Basic48, 0x0F8B, { 0x12 }, "ADD-CHAR" },
        { Point::EditKey,       RomKind::Basic48, 0x0F92, { 0x5F }, "ED-KEYS" },
        { Point::Rasp,          RomKind::Basic48, 0x1091, { 0xCD, 0xB5, 0x03 }, "ED-ERROR" },
        { Point::LineFull,      RomKind::Basic48, 0x1167, { 0x16, 0x00, 0xFD, 0x5E, 0xFE }, "ED-FULL" },
        { Point::Enter,         RomKind::Basic48, 0x1024, { 0xE1, 0xE1 }, "ED-ENTER" },
        { Point::SyntaxResult,  RomKind::Basic48, 0x12B7, { 0xFD, 0xCB, 0x00, 0x7E }, "MAIN-2+11" },
        { Point::LineAccepted,  RomKind::Basic48, 0x12CF, { 0x2A, 0x59, 0x5C }, "MAIN-3" },
        { Point::LineStored,    RomKind::Basic48, 0x155D, { 0xED, 0x43, 0x49, 0x5C }, "MAIN-ADD" },
        { Point::ExecStart,     RomKind::Basic48, 0x1300, { 0xCD, 0x8A, 0x1B }, "MAIN-3+49" },
        { Point::ErrorRaised,   RomKind::Basic48, 0x0055, { 0xFD, 0x75, 0x00 }, "ERROR-3" },
        { Point::Report,        RomKind::Basic48, 0x1303, { 0x76, 0xFD, 0xCB, 0x01, 0xAE }, "MAIN-4" },
        { Point::TapeLoader,    RomKind::Basic48, 0x0556, { 0x14, 0x08, 0x15, 0xF3 }, "LD-BYTES" },

        // Spectrum 128 ROM0 editor (Pentagon and Scorpion ROM0 too)
        { Point::EditorInit,    RomKind::Editor128, 0x3371, { 0xED, 0xB0 }, "L3371" },
        { Point::EditorIdle,    RomKind::Editor128, 0x3683, { 0xCB, 0x6E, 0x28, 0xFC }, "L3683" },
        { Point::KeyTaken,      RomKind::Editor128, 0x3689, { 0x3A, 0x08, 0x5C }, "L3689" },
        { Point::KeyAccepted,   RomKind::Editor128, 0x265C, { 0xF5 }, "L265C" },
        { Point::CharInserted,  RomKind::Editor128, 0x28F1, { 0x21, 0x0D, 0xEC }, "L28F1" },
        // The error beep itself, not the CALL NC at #267C: a breakpoint fires on
        // a conditional call whether or not it is taken
        { Point::Rasp,          RomKind::Editor128, 0x26E7, { 0x3A, 0x38, 0x5C }, "L26E7" },
        { Point::Enter,         RomKind::Editor128, 0x2944, { 0xCD, 0xEC, 0x29 }, "L2944" },
        // The syntax pass of every line, direct or numbered, ends here (the
        // parser's error vector points at it too): ERR_NR tells the result
        { Point::SyntaxResult,  RomKind::Editor128, 0x02BA, { 0xFD, 0xCB, 0x00, 0x7E }, "L02BA" },
        { Point::LineStored,    RomKind::Editor128, 0x03F7, { 0xED, 0x43, 0x49, 0x5C }, "L03F7" },
        { Point::ExecStart,     RomKind::Editor128, 0x031E, { 0xC3, 0x38, 0x18 }, "L031E" },
        { Point::Report,        RomKind::Editor128, 0x0321, { 0xED, 0x7B, 0xB2, 0x5C }, "L0321" },

        // +2A/+3 v4.0 ROM0 editor
        { Point::EditorIdle,    RomKind::Plus3Rom0, 0x1875, { 0xCB, 0x6E, 0x28, 0xFC }, "L1875" },
        { Point::KeyTaken,      RomKind::Plus3Rom0, 0x187B, { 0x3A, 0x08, 0x5C }, "L187B" },
        { Point::KeyAccepted,   RomKind::Plus3Rom0, 0x0709, { 0xF5 }, "L0709" },
        { Point::CharInserted,  RomKind::Plus3Rom0, 0x09BC, { 0x21, 0x0D, 0xEC }, "L09BC" },
        { Point::Rasp,          RomKind::Plus3Rom0, 0x0794, { 0x3A, 0x38, 0x5C }, "L0794" },
        { Point::Enter,         RomKind::Plus3Rom0, 0x0A0F, { 0xCD, 0xB7, 0x0A }, "L0A0F" },
        { Point::SyntaxResult,  RomKind::Plus3Rom0, 0x0D9F, { 0x3A, 0x3A, 0x5C, 0x3C, 0x20, 0x18 }, "L0D9F" },
        { Point::LineAccepted,  RomKind::Plus3Rom0, 0x0DB5, { 0xCD, 0xA7, 0x07 }, "L0DB5" },
        // ...whose lines are checked and run by ROM1
        { Point::ExecStart,     RomKind::Plus3Rom1, 0x25C8, { 0xC3, 0x48, 0x10 }, "L25C8" },
        { Point::Report,        RomKind::Plus3Rom1, 0x25CB, { 0xED, 0x7B, 0xB2, 0x5C }, "L25CB" },

        // TR-DOS: the A> prompt runs the 48K editor through RST 20
        { Point::TrDosPrompt,      RomKind::TrDos, 0x02CB, { 0x2A, 0x1C, 0x5D }, "CMD-LOOP" },
        { Point::TrDosLineBack,    RomKind::TrDos, 0x3D2F, { 0x00, 0xC9 }, "RET-TRDOS" },
        { Point::TrDosDispatch,    RomKind::TrDos, 0x030A, { 0x7E, 0x47 }, "CMD-DISPATCH" },
        // #0325 JP C,#01D3 leaves the 21-entry command search; #01D3 is the
        // common error exit (also reached from #1D2C after a syntax error)
        { Point::TrDosError,       RomKind::TrDos, 0x01D3, { 0x21, 0x00, 0x00 }, "CMD-ERROR" },
        { Point::TrDosSyntaxError, RomKind::TrDos, 0x1D1A, { 0xFD, 0xCB, 0x00, 0x7E }, "CMD-SYNTAX" },
        // #0358 JP (HL) runs the handler's syntax pass, #035F its execute pass
        { Point::TrDosExecute,     RomKind::TrDos, 0x035F, { 0xE9 }, "CMD-EXECUTE" },
        // Reached only after #032B found the command in the table
        { Point::TrDosFound,       RomKind::TrDos, 0x0332, { 0x3E, 0x09, 0x32, 0x06, 0x5D }, "CMD-FOUND" },
    };
    return points;
}

std::vector<std::string> Mismatches(RomKind rom, const uint8_t* page)
{
    std::vector<std::string> result;
    for (const PointDef& p : All())
    {
        if (p.rom == rom && !Matches(page, p.address, p.expect))
        {
            char text[48];
            snprintf(text, sizeof(text), "#%04X %s", p.address, p.label);
            result.emplace_back(text);
        }
    }
    return result;
}

RomKind Identify(const uint8_t* page)
{
    if (page == nullptr)
        return RomKind::Unknown;

    RomKind rom = RomKind::Unknown;
    if (MatchesAll(page, BASIC48))
        rom = RomKind::Basic48;
    else if (MatchesAll(page, EDITOR128))
        rom = RomKind::Editor128;
    else if (MatchesAll(page, PLUS3ROM0))
        rom = RomKind::Plus3Rom0;
    else if (MatchesAll(page, PLUS3ROM1))
        rom = RomKind::Plus3Rom1;
    else if (MatchesAll(page, TRDOS))
        rom = RomKind::TrDos;

    // A signature is a few bytes; the control points are the real contract
    if (rom != RomKind::Unknown && !Mismatches(rom, page).empty())
        rom = RomKind::Unknown;

    return rom;
}

const char* RomName(RomKind rom)
{
    switch (rom)
    {
        case RomKind::Basic48:   return "48 BASIC";
        case RomKind::Editor128: return "128K editor";
        case RomKind::Plus3Rom0: return "+3 editor (ROM0)";
        case RomKind::Plus3Rom1: return "+3 syntax (ROM1)";
        case RomKind::TrDos:     return "TR-DOS";
        default:                 return "unknown";
    }
}

const char* PointName(Point point)
{
    static const char* names[] = {
        "editorIdle", "keyTaken", "keyAccepted", "charInserted", "editKey", "rasp", "lineFull",
        "enter", "syntaxResult", "lineAccepted", "lineStored", "execStart", "errorRaised", "report",
        "tapeLoader", "editorInit", "trdosPrompt", "trdosLineBack", "trdosDispatch", "trdosError",
        "trdosSyntaxError", "trdosExecute", "trdosFound",
    };
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(Point::Count), "PointName table drift");
    const size_t index = static_cast<size_t>(point);
    return index < static_cast<size_t>(Point::Count) ? names[index] : "?";
}
} // namespace ROMControlPoints
