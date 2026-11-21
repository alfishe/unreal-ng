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
/// Revision 3 (design §8A.9) adds, as data: the profile's matches (a ROM or an uploaded block by digest), its
/// command dispatcher, the handshake per command, structs and their instances bound to a physical page, and variables
/// held in memory, in a register over a PC range, or in an instruction's operand. Profiles also come from JSON files
/// (`<symbols>/gs/profiles/*.json`, ParseGSFirmwareProfile); the built-in tables use the same structure.
///
/// Protocol words used below:
///   pre   the byte the host writes to the data register before the command; the handler takes it on entry
///   post  bytes the host sends after the firmware took the command (it waits for each one)
///   reply bytes the firmware writes to the output register

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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
    FixedBlock,   // a block of GSCommandInfo::transferLength data bytes after the parameters
    PagedBlock,   // the pre byte's count of 256-byte pages after the parameters (NeoTracker's #EA)
    Sectors,      // sectors of GSCommandInfo::transferLength bytes after the parameters, each opened by a #BB strobe,
                  // read or written as a parameter selects (the NeoGS SD driver's #1E, M12)
};

/// How the host confirms a command (M6)
enum class GSHandshakeAck : uint8_t
{
    Command, // the host waits for status bit 0 to clear (the card's RSCOM)
    Data,    // the host waits for a data byte (status bit 7): a command flag still set is not "unconfirmed"
};

/// The handshake of one command, as the firmware expects it (M6); the F4 rules read it to avoid false positives
struct GSCommandHandshake
{
    GSHandshakeAck ack = GSHandshakeAck::Command;
    bool barrier = false; // the command resets the conversation (the decoder starts over)
    bool strobe = false;  // inside its transfer a #BB write is a strobe, part of the transfer, not a new command
    bool replyWait = false; // the handler spins until the host reads its reply (an unread one stalls the card)
    bool exit = false;      // the handler leaves the uploaded code for the ROM's (GP's #00: JP 0): the profile stack
                            // returns to the ROM's
    bool valuePoll = false; // the host reads the reply until it is not 0 (#F4: INIT writes 0, then the page count):
                            // a 0 read is the poll, the reply still due
};

/// A sector transfer (GSTransferKind::Sectors, M12): which parameters count the sectors and select the direction
struct GSSectorTransfer
{
    uint8_t countParam = 0;   ///< the parameter with the sector count (0: 256)
    uint8_t selectParam = 0;  ///< the parameter whose value selects the direction
    int16_t read = -1;        ///< its value for a read (the host reads the sectors), -1 none
    int16_t write = -1;       ///< its value for a write; any other value moves no sectors
};

/// A firmware state that accepts only some commands (M6): NeoTracker's player takes only #E4 / #E6 while it runs.
/// Entered by an `enter` command, left by a `leave` one (or a card reset); any other command not in `accepts` is
/// flagged `command_rejected_in_state`. A mode may take commands as its own selections (`selects`: the stock test
/// mode #FA takes #00-#1F as test numbers, TCOMTB) and end at any command from `leaveFrom` on (#20: COMINT2 runs it)
struct GSProfileMode
{
    std::string name;
    std::vector<uint8_t> enter;
    std::vector<uint8_t> leave;
    std::vector<uint8_t> accepts;
    std::vector<uint8_t> selects;
    uint16_t leaveFrom = 0x100;  // a command >= this leaves the mode and is decoded as usual; #100: none
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
    static constexpr size_t kMaxParams = 6;  ///< the pre byte and the bytes after it (#1E: sub-command, LBA, count)

    const char* name = nullptr;
    uint8_t pre = 0;          // 0 or 1
    uint8_t post = 0;         // 0..3 or kVariablePost
    GSParamInfo params[kMaxParams]; // pre first, then post, in the order sent
    uint8_t reply = 0;        // bytes written to the output register
    const char* replyMeaning = nullptr;
    GSTransferKind transfer = GSTransferKind::None;
    bool documented = false;  // in the programming guide without the (*) mark
    const char* note = nullptr;
    /// The handler writes the reply, then reads a data byte before RSCOM (OUT (OUTRG) / IN A,(DATRG)): that read
    /// clears status bit 7, so the host reads the reply once bit 0 clears, without the data flag
    bool replyUnflagged = false;
    GSCommandHandshake handshake;
    uint16_t transferLength = 0; // FixedBlock: the block's bytes; Sectors: a sector's
    GSSectorTransfer sectors;    // Sectors: the count and the direction
    const char* handler = nullptr; // the handler's source label (M4: `card_label` of the command's log entries)
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

/// Where a firmware object lives (M2): a physical page, or the card CPU's address through its mapping now
enum class GSPlace : uint8_t
{
    Window, // the CPU address as mapped now (the fixed RAM window #4000-#7FFF never moves)
    Ram,    // RAM page `page`, offset = address & #3FFF, mapped or not
    Rom,    // ROM (NeoGS: flash) page `page`
};

/// A field type of a firmware struct (M1); multi-byte values are little-endian
enum class GSFieldType : uint8_t
{
    U8,
    U16,
    U24,
    U32,
    S8,
    S16,
    Bytes,  // `size` raw bytes
    Struct, // a nested struct (`structName`)
};

struct GSStructField
{
    std::string name;
    uint16_t offset = 0;
    GSFieldType type = GSFieldType::U8;
    uint16_t size = 1;      // Bytes: the byte count; other types: their size (set by the parser)
    uint16_t count = 1;     // an array of `count` elements
    uint16_t stride = 0;    // between elements (0: the element's size)
    std::string structName; // Struct: the nested struct's name
    std::string meaning;
};

struct GSStructInfo
{
    std::string name;
    uint16_t size = 0;
    std::vector<GSStructField> fields;
    std::string meaning;
};

/// A struct at an address, `count` times (M1, M2)
struct GSInstanceInfo
{
    std::string name;       // the instance's name (default: the struct's)
    std::string structName;
    std::string at;         // the symbol or the address as written
    uint16_t address = 0;
    GSPlace place = GSPlace::Window;
    uint16_t page = 0;
    uint16_t count = 1;
    uint16_t stride = 0;    // between instances (0: the struct's size)
};

enum class GSVariableKind : uint8_t
{
    Memory,     // `size` bytes at the address
    Register,   // a register, valid only while the card PC is in [pcFrom, pcTo] (M7)
    SmcOperand, // the operand byte(s) of the instruction at the address: self-modifying code, `label = $+1` (M7)
};

struct GSProfileVariable
{
    std::string name;
    GSVariableKind kind = GSVariableKind::Memory;
    std::string at;          // Memory / SmcOperand: the symbol or address as written
    uint16_t address = 0;
    uint8_t size = 1;
    GSPlace place = GSPlace::Window;
    uint16_t page = 0;
    std::string reg;         // Register: A, HL, DE, BC, IX, IY, SP, ...
    uint16_t pcFrom = 0;
    uint16_t pcTo = 0;
    std::string meaning;
};

/// The firmware's command dispatcher (M4)
enum class GSDispatchKind : uint8_t
{
    None,
    Words,      // `entries` handler addresses (little-endian words) at `table`
    StockSplit, // the stock firmware: COMTAB words for #00-#1F and #F0-#FF, COMTABH split bytes for #20-#EF
};

struct GSDispatchInfo
{
    GSDispatchKind kind = GSDispatchKind::None;
    uint16_t pc = 0;          // the dispatch code: a probe there sees each command
    std::string reg = "A";    // the register holding the command at `pc`
    uint16_t table = 0;
    uint16_t entries = 256;
    GSPlace place = GSPlace::Window;
    uint16_t page = 0;
    bool hasUnknownStub = false;
    uint16_t unknownStub = 0; // the handler of an unknown command (AU6)
    uint16_t highFrom = 0x20; // Words: the command of entry #20 on (NeoTracker: #E0, entries #20-#3F = #E0-#FF)
};

/// What selects a profile: the card's ROM, or a block the host uploaded (#14) (M5)
enum class GSMatchKind : uint8_t
{
    Rom,
    Upload,
};

struct GSProfileMatch
{
    GSMatchKind kind = GSMatchKind::Rom;
    std::string sha256;     // Rom: of the 32 KB image; Upload: of the uploaded bytes
    uint16_t address = 0;   // Upload: where the block lands
    uint32_t length = 0;    // Upload: its length (0: any)
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

    // Revision 3 (design §8A.9)
    std::vector<GSProfileMatch> matches; // besides `sha256`: the uploads that select this profile
    GSDispatchInfo dispatch;
    std::vector<GSStructInfo> structs;
    std::vector<GSInstanceInfo> instances;
    std::vector<GSProfileVariable> objectVariables; // variables beyond the fixed-window table (`variables`)
    std::string source;                             // "built-in" or the JSON file it came from
    std::vector<GSProfileMode> modes;               // states that accept only some commands (M6)
    bool extends = false;            // code that adds commands to the firmware below it (the NedoOS SD driver's
                                     // #1E): a command it does not name is the lower profile's (M5 stack)
    bool unsolicitedReplies = false; // the card sends data bytes nobody asked for (NeoTracker's DataToZX): a read of
                                     // an unasked byte is no `stale_reply_drain`
    bool hostDataStream = false;     // the code takes data bytes outside its commands as a stream (GP's MP3 streamer):
                                     // they count into one stream block that the code's polls do not end (F1a)

    const GSCommandInfo& Command(uint8_t code) const { return (*commands)[code]; }
    const GSVariableInfo* Variable(const std::string& name) const;
    const GSStructInfo* Struct(const std::string& name) const;
    /// The upload match of a block (its digest, address and length); nullptr when none
    const GSProfileMatch* UploadMatch(const std::string& sha256, uint16_t address, uint32_t length) const;
};

/// A profile read from its JSON form (design §8A.9). `symbols` resolves names to addresses (the profile's symbol
/// file, `[ROMn:|RAMn:]ADDR NAME` lines as in data/symbols/gs/*.map); an address is also written as hex ("#4000",
/// "0x4000", "4000") or a decimal number. Null with `error` set on a fault
std::unique_ptr<GSFirmwareProfile> ParseGSFirmwareProfile(const std::string& json, const std::string& symbols,
                                                          std::string& error);

/// The profiles: the built-in v1.04, v1.05a, v1.05b and NeoGS v1.11, then the JSON files of `<symbols>/gs/profiles/`
/// in name order (read once; a broken file is skipped with a warning). The order is fixed for the process: the command
/// log stores an index
const std::vector<const GSFirmwareProfile*>& GSFirmwareProfiles();
/// The profile of an id; nullptr when none
const GSFirmwareProfile* FindGSFirmwareProfileById(const std::string& id);
/// The profile an uploaded block selects (M5); nullptr when none
const GSFirmwareProfile* FindGSUploadProfile(const std::string& sha256, uint16_t address, uint32_t length);

/// The profile of a 32 KB image digest (lower-case hex); nullptr when unknown
const GSFirmwareProfile* FindGSFirmwareProfile(const std::string& sha256);

/// A card's firmware: the digests of its ROM and the profile they select
struct GSFirmwareIdentity
{
    bool hasRom = false;                // the card has a firmware to hash (LLE cards: firmwarePage 0-1)
    std::string sha256;                 // of the first 32 KB
    std::string sha256Pages[2];
    const GSFirmwareProfile* profile = nullptr;
};

GSFirmwareIdentity IdentifyGSFirmware(const GeneralSoundCard& card);
/// The profile a card answers with: the lightweight card mimics gs105a (soundchip_gslw.h), any other card its
/// ROM's (IdentifyGSFirmware); nullptr: none (an unknown ROM)
const GSFirmwareProfile* GSFirmwareProfileOf(const GeneralSoundCard& card);
