#pragma once

/// @file gsfirmwareprofile.h
/// @brief What the debugger knows about a General Sound firmware, as data (gs-debugger design §6.1, F2, L4).
///
/// A profile names the firmware's host commands (their parameters, replies and transfers), its variables in the
/// card's fixed RAM window and its command tables. It is selected by the SHA-256 of the card's 32 KB ROM image. The
/// debugger has no firmware-specific code: the command log, the protocol view and the card's report read these
/// tables.
///
/// The tables are written from the firmware sources (COMTAB, COMTABH and every handler in COM_L.a80 / COM_H.a80,
/// docs/inprogress/2026-09-19-general-sound/materials/gs/gs-firmware), not from the programming guide: the guide
/// misses commands (#2C, #2D, #2F, #34, #3B, #3C, #50) and has some wrong (#02 / #03 swapped, #3E's format byte).
/// The self-check test (gsfirmwareprofile_test.cpp) reads each shipped ROM's tables and checks every handler that is
/// not the "unknown command" stub has a named command here, and no stub has one.
///
/// Protocol words used below:
///   pre   the byte the host writes to the data register before the command; the handler takes it on entry
///   post  bytes the host sends after the firmware took the command (it waits for each one)
///   reply bytes the firmware writes to the output register

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class GeneralSoundCard;

/// What a parameter byte means (for decoding; every parameter is one byte)
enum class GSParamKind : uint8_t
{
    Byte,        // a raw value
    Channel,     // a channel number
    ChannelMask, // a bit per channel
    Volume,      // #00..#40 (#3F for a volume port)
    Note,        // 0..95
    Handle,      // a module or sample handle (0: the current one)
    Port,        // a card port number
    Low,         // the low byte of a word or of a 24-bit value
    Middle,      // the middle byte of a 24-bit value
    High,        // the high byte
    Mode,        // a switch or format code
};

/// What a command starts after it is taken
enum class GSTransferKind : uint8_t
{
    None,
    ModuleUpload, // the host streams a module: #D1 opens, data bytes, #D2 closes
    SampleUpload, // the same for a sample
    BlockUpload,  // #14: length and address, then the bytes
    BlockDownload,// #15: length and address, then the firmware sends the bytes
    Stream,       // a Covox mode: data bytes go to the DACs until a command ends it
};

struct GSParamInfo
{
    const char* name = nullptr;
    GSParamKind kind = GSParamKind::Byte;
};

/// One host command. name == nullptr: the firmware does not know it (its table entry is the stub)
struct GSCommandInfo
{
    /// post == kVariablePost: the count depends on the values (the note says how)
    static constexpr uint8_t kVariablePost = 0xFF;

    const char* name = nullptr;
    uint8_t pre = 0;          // 0 or 1
    uint8_t post = 0;         // 0..3 or kVariablePost
    GSParamInfo params[4];    // pre first, then post, in the order sent
    uint8_t reply = 0;        // bytes written to the output register
    const char* replyMeaning = nullptr;
    GSTransferKind transfer = GSTransferKind::None;
    bool documented = false;  // in the programming guide without the (*) mark
    const char* note = nullptr;
};

/// A firmware variable in the card's fixed RAM window (#4000-#7FFF)
struct GSVariableInfo
{
    const char* name;    // the source label
    uint16_t address;
    uint8_t size;        // bytes
    const char* meaning;
    bool equ;            // defined with EQU (no label in the symbol file)
};

/// A command the firmware takes while it receives a module or sample (LOAD in LOAD_L.a80)
struct GSStreamCommandInfo
{
    uint8_t code;
    const char* name;
};

struct GSFirmwareProfile
{
    const char* id;              // "gs105a", the ROM file name and the symbol bundle gs:<id>
    const char* title;           // "General Sound firmware v1.05a"
    const char* sha256;          // hex SHA-256 of the 32 KB image
    const char* sha256Pages[2];  // of each 16 KB page
    uint16_t comtab;             // COMTAB: words for #00-#1F, #F0-#FF (ROM page 0)
    uint16_t comtabh;            // COMTABH: low bytes for #20-#EF, high bytes 256 later (window 3, ROM page 1)
    const std::array<GSCommandInfo, 256>* commands;
    const GSVariableInfo* variables;
    size_t variableCount;
    const GSStreamCommandInfo* streamCommands;
    size_t streamCommandCount;

    const GSCommandInfo& Command(uint8_t code) const { return (*commands)[code]; }
    const GSVariableInfo* Variable(const std::string& name) const;
};

/// The built-in profiles: v1.04, v1.05a, v1.05b
const std::vector<const GSFirmwareProfile*>& GSFirmwareProfiles();

/// The profile of a 32 KB image digest (lower-case hex); nullptr when unknown
const GSFirmwareProfile* FindGSFirmwareProfile(const std::string& sha256);

/// A card's firmware: the digests of its ROM and the profile they select
struct GSFirmwareIdentity
{
    bool hasRom = false;                // the card has a ROM to hash (LLE cards; NeoGS: its flash)
    std::string sha256;                 // of the first 32 KB
    std::string sha256Pages[2];
    const GSFirmwareProfile* profile = nullptr;
};

GSFirmwareIdentity IdentifyGSFirmware(const GeneralSoundCard& card);
