#pragma once

/// @file szxformat.h
/// @brief SZX (ZX-State) snapshot format: block ids, the parsed state
/// ("stage"), machine ids and page sets, and the per-block load report.
/// Byte layouts: docs/inprogress/2026-09-29-szx-snapshots/szx-format-reference.md;
/// design: design.md in the same folder.
///
/// A file is an 8-byte header ("ZXST", version, machine id, flags) followed by
/// blocks, each a 4-character id, a 32-bit size and that many bytes. Worked
/// example: a 48K snapshot is ZXST 1.5 id 1, then CRTR, Z80R (37 bytes), SPCR
/// (8 bytes) and three RAMP blocks for pages 5, 2 and 0.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emulator/platform.h"

namespace szx
{
/// A block id from its four characters in file order
constexpr uint32_t BlockId(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kMagic = BlockId('Z', 'X', 'S', 'T');
constexpr uint32_t kCreator = BlockId('C', 'R', 'T', 'R');
constexpr uint32_t kZ80Regs = BlockId('Z', '8', '0', 'R');
constexpr uint32_t kSpecRegs = BlockId('S', 'P', 'C', 'R');
constexpr uint32_t kRamPage = BlockId('R', 'A', 'M', 'P');
constexpr uint32_t kAy = BlockId('A', 'Y', '\0', '\0');
constexpr uint32_t kBeta128 = BlockId('B', '1', '2', '8');
constexpr uint32_t kBetaDisk = BlockId('B', 'D', 'S', 'K');
constexpr uint32_t kKeyboard = BlockId('K', 'E', 'Y', 'B');
constexpr uint32_t kJoystick = BlockId('J', 'O', 'Y', '\0');
constexpr uint32_t kMouse = BlockId('A', 'M', 'X', 'M');
constexpr uint32_t kPlus3 = BlockId('+', '3', '\0', '\0');
constexpr uint32_t kDskFile = BlockId('D', 'S', 'K', '\0');
constexpr uint32_t kTape = BlockId('T', 'A', 'P', 'E');
constexpr uint32_t kGs = BlockId('G', 'S', '\0', '\0');
constexpr uint32_t kGsRamPage = BlockId('G', 'S', 'R', 'P');
constexpr uint32_t kCovox = BlockId('C', 'O', 'V', 'X');
constexpr uint32_t kSpecDrum = BlockId('D', 'R', 'U', 'M');

constexpr uint8_t kVersionMajor = 1;
constexpr uint8_t kVersionMinor = 5;
constexpr size_t kHeaderSize = 8;
constexpr size_t kBlockHeaderSize = 8;
constexpr size_t kCreatorSize = 36;  ///< without the creator's own data
constexpr size_t kZ80RegsSize = 37;
constexpr size_t kSpecRegsSize = 8;
constexpr size_t kAySize = 18;
constexpr size_t kPageSize = 16384;

/// Header flags
constexpr uint8_t kAlternateTimings = 0x01;
/// Z80R chFlags
constexpr uint8_t kSuppressInts = 0x01;  ///< ZXSTZF_EILAST before 1.5
constexpr uint8_t kHalted = 0x02;
constexpr uint8_t kFset = 0x04;
/// RAMP wFlags
constexpr uint16_t kPageCompressed = 0x0001;
/// B128 dwFlags
constexpr uint32_t kBetaConnected = 0x01;
constexpr uint32_t kBetaCustomRom = 0x02;
constexpr uint32_t kBetaPaged = 0x04;
constexpr uint32_t kBetaAutoboot = 0x08;
constexpr uint32_t kBetaSeekLower = 0x10;
constexpr size_t kBeta128Size = 10;  ///< without a custom ROM
/// BDSK dwFlags, chDiskType
constexpr uint32_t kDiskEmbedded = 0x01;
constexpr uint32_t kDiskCompressed = 0x02;
constexpr uint32_t kDiskWriteProtect = 0x04;
enum BetaDiskType : uint8_t
{
    DiskTrd = 0,
    DiskScl = 1,
    DiskFdi = 2,
    DiskUdi = 3
};
/// TAPE wFlags
constexpr uint16_t kTapeEmbedded = 0x01;
constexpr uint16_t kTapeCompressed = 0x02;
/// KEYB
constexpr uint32_t kKeyboardIssue2 = 0x01;
constexpr uint8_t kKeyboardJoystickNone = 8;
/// AMXM chType
constexpr uint8_t kMouseNone = 0;
constexpr uint8_t kMouseAmx = 1;
constexpr uint8_t kMouseKempston = 2;
/// GS chFlags (the Z80 flags plus ROM flags)
constexpr uint8_t kGsCustomRom = 0x40;
constexpr size_t kGsSize = 46;  ///< without a custom ROM
constexpr size_t kGsPageSize = 32768;
/// Largest embedded disk or tape image we inflate (no decompression bombs)
constexpr size_t kMaxEmbeddedImage = 16 * 1024 * 1024;

/// Machine ids of the header
enum MachineId : uint8_t
{
    Mid16K = 0,
    Mid48K = 1,
    Mid128K = 2,
    MidPlus2 = 3,
    MidPlus2A = 4,
    MidPlus3 = 5,
    MidPlus3E = 6,
    MidPentagon128 = 7,
    MidTc2048 = 8,
    MidTc2068 = 9,
    MidScorpion = 10,
    MidSe = 11,
    MidTs2068 = 12,
    MidPentagon512 = 13,
    MidPentagon1024 = 14,
    MidNtsc48K = 15,
    Mid128Ke = 16
};

/// The unreal-ng machine a machine id loads as
struct Machine
{
    MEM_MODEL model = MM_SPECTRUM48;
    uint32_t ramKb = 48;
    std::string note;  ///< set when the id is read as a close model (16K as 48K, ...)
};

/// False for ids we do not emulate (Timex, SE, unknown)
bool MachineFor(uint8_t id, Machine& machine, std::string& error);
/// "Pentagon 512K", "ZX-Spectrum 48k", "ZS Scorpion 256K": a model for messages
std::string DescribeModel(MEM_MODEL model, uint32_t ramKb);
/// The id a model is written as; nullopt: no SZX id (design §11)
std::optional<uint8_t> IdFor(MEM_MODEL model, uint32_t ramKb);
/// The RAM pages a machine id stores, in file order
std::vector<uint8_t> PagesOf(uint8_t id);
/// SPCR byte 2 holds #1FFD (+2A, +3, Scorpion) or #EFF7 (Pentagon 1024)
bool HasPort1FFD(uint8_t id);
bool HasPortEFF7(uint8_t id);
/// The machine has the 128-style AY
bool HasAy(uint8_t id);

struct Creator
{
    std::string name;
    uint16_t major = 0;
    uint16_t minor = 0;
    std::vector<uint8_t> data;
};

/// Z80R, decoded
struct Z80Regs
{
    uint16_t af = 0, bc = 0, de = 0, hl = 0;
    uint16_t af1 = 0, bc1 = 0, de1 = 0, hl1 = 0;
    uint16_t ix = 0, iy = 0, sp = 0, pc = 0;
    uint8_t i = 0, r = 0, iff1 = 0, iff2 = 0, im = 0;
    uint32_t cyclesStart = 0;       ///< T-states since the frame's INT
    uint8_t holdIntReqCycles = 0;   ///< T-states left in which the INT is still accepted
    uint8_t flags = 0;              ///< kSuppressInts | kHalted | kFset
    uint16_t memptr = 0;            ///< 1.4+; from chBitReg (high byte) in 1.1-1.3
};

/// SPCR
struct SpecRegs
{
    uint8_t border = 0;
    uint8_t port7FFD = 0;
    uint8_t port1FFDorEFF7 = 0;
    uint8_t portFE = 0;
};

/// B128: the Beta 128 interface and its WD1793
struct Beta128
{
    uint32_t flags = kBetaConnected;
    uint8_t drives = 4;
    uint8_t system = 0;  ///< the last #FF write
    uint8_t track = 0;
    uint8_t sector = 0;
    uint8_t data = 0;
    uint8_t status = 0;
};

/// BDSK: a Beta 128 drive and its disk, linked or embedded
struct BetaDisk
{
    uint32_t flags = 0;
    uint8_t drive = 0;
    uint8_t cylinder = 0;
    uint8_t type = DiskTrd;
    std::string fileName;         ///< linked
    std::vector<uint8_t> image;   ///< embedded, inflated
};

/// +3 and DSK: the uPD765 drives; images are always links
struct Plus3
{
    uint8_t drives = 2;
    uint8_t motorOn = 0;
};
struct DskFile
{
    uint16_t flags = 0;
    uint8_t drive = 0;
    std::string fileName;
};

/// TAPE: the recorder's image, linked or embedded, and the head block
struct Tape
{
    uint16_t block = 0;
    uint16_t flags = 0;
    std::string extension;       ///< of an embedded image ("tzx", "tap")
    std::string fileName;        ///< linked
    std::vector<uint8_t> image;  ///< embedded, inflated
};

/// GS: the General Sound card's CPU and latches (its RAM in gsPages)
struct GeneralSound
{
    uint8_t model = 0;       ///< 0 = 128 KB, 1 = 512 KB
    uint8_t upperPage = 0;   ///< the page at #8000-#FFFF (port #00; 0 = ROM)
    std::array<uint8_t, 4> volume{};
    std::array<uint8_t, 4> output{};
    uint8_t flags = 0;       ///< kSuppressInts | kHalted | kGsCustomRom
    Z80Regs cpu;             ///< registers; cyclesStart / hold as in Z80R
};

struct Keyboard
{
    uint32_t flags = 0;
    uint8_t joystick = kKeyboardJoystickNone;
};

struct Mouse
{
    uint8_t type = kMouseNone;
    std::array<uint8_t, 3> ctrlA{};
    std::array<uint8_t, 3> ctrlB{};
};

struct Ay
{
    uint8_t flags = 0;
    uint8_t currentRegister = 0;
    std::array<uint8_t, 16> registers{};
};

/// What a load did with one block
enum class Outcome : uint8_t
{
    Applied,       ///< its state is in the machine
    Approximated,  ///< applied as far as this machine can hold it
    Ignored,       ///< understood, not applied (hardware we do not emulate, ...)
    Unknown        ///< an id we do not know: skipped, as the format asks
};

struct ReportEntry
{
    std::string block;  ///< "Z80R", "RAMP 5", "IF1", ...
    Outcome outcome = Outcome::Applied;
    std::string note;
};

struct Report
{
    std::vector<ReportEntry> entries;
    std::vector<std::string> warnings;

    void Add(std::string block, Outcome outcome, std::string note = {});
    std::string ToText() const;
};

/// A parsed file: everything the loader needs, nothing applied yet
struct Stage
{
    uint8_t versionMajor = kVersionMajor;
    uint8_t versionMinor = kVersionMinor;
    uint8_t machineId = Mid48K;
    uint8_t headerFlags = 0;
    std::optional<Creator> creator;
    std::optional<Z80Regs> z80;
    std::optional<SpecRegs> spec;
    std::map<uint8_t, std::vector<uint8_t>> pages;  ///< page number -> 16384 bytes
    std::optional<Ay> ay;
    std::optional<Beta128> beta;
    std::vector<BetaDisk> betaDisks;
    std::optional<Plus3> plus3;
    std::vector<DskFile> dskFiles;
    std::optional<Tape> tape;
    std::optional<GeneralSound> gs;
    std::map<uint8_t, std::vector<uint8_t>> gsPages;  ///< 32 KB GS RAM pages
    std::optional<Keyboard> keyboard;
    std::optional<std::array<uint8_t, 2>> joysticks;   ///< JOY player 1, 2
    std::optional<Mouse> mouse;
    std::optional<uint8_t> covox;
    std::optional<int8_t> specDrum;
    /// The file's folder: linked media are looked up there first
    std::string folder;
    /// Blocks found and not taken into the stage: id text, size
    std::vector<std::pair<std::string, uint32_t>> otherBlocks;
    std::vector<std::string> warnings;
};

/// "Z80R", "AY", "B128" ...: the id's printable characters
std::string BlockName(uint32_t id);

/// zlib stream (RFC 1950) to exactly `expected` bytes; false on any error,
/// including output that would exceed `expected` (no decompression bombs)
bool Inflate(const uint8_t* data, size_t size, size_t expected, std::vector<uint8_t>& out);
/// zlib stream of unknown length, at most `limit` bytes out
bool InflateBounded(const uint8_t* data, size_t size, size_t limit, std::vector<uint8_t>& out);
/// zlib stream at the best compression
std::vector<uint8_t> Deflate(const uint8_t* data, size_t size);

/// Little-endian helpers
inline uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t Get32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
inline void Put16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
inline void Put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
}  // namespace szx
