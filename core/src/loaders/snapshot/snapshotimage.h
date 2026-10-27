#pragma once

/// @file snapshotimage.h
/// @brief SnapshotImage - what a snapshot file says, in one format-neutral record (snapshot pipeline P1, PLAN #84).
///
/// Every format reader (SNA, Z80, SZX, SPG, ZXP) fills one from its staging; nothing here touches a machine. Memory is
/// kept as LOGICAL banks in the Spectrum 128K sense (a 48K file holds banks 5, 2, 0), never as physical pages, so the
/// plan step can ask a machine what "bank n" means on it. Formats that only know absolute addresses (SPG) say so with
/// MemoryModel::Physical and only a policy that understands them may commit them.
/// Design: docs/inprogress/2026-10-02-snapshot-pipeline/proposal.md section 4.2.

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

namespace snapshot
{
/// How the file lays out RAM
enum class MemoryModel : uint8_t
{
    Mem48k,     ///< banks 5, 2, 0 only
    Mem128k,    ///< banks 0-7
    Extended,   ///< more than 8 banks (Pentagon 512 / 1024, Scorpion 256, ...)
    Physical,   ///< absolute addresses (SPG): banks hold nothing, `physical` does
};

const char* ToText(MemoryModel model);

struct Cpu
{
    uint16_t af = 0, bc = 0, de = 0, hl = 0, ix = 0, iy = 0, sp = 0, pc = 0;
    uint16_t af2 = 0, bc2 = 0, de2 = 0, hl2 = 0;
    uint8_t i = 0, r = 0;
    bool iff1 = false, iff2 = false;
    uint8_t im = 1;
    // Only the formats that store them
    std::optional<uint16_t> memptr;
    std::optional<uint8_t> q;
    std::optional<bool> halted;
    std::optional<bool> eiShadow;   ///< the instruction before the snapshot was EI (INT is not accepted yet)
    /// SZX: T-states left in which the frame INT is still accepted (Z80R chHoldIntReqCycles)
    std::optional<uint8_t> holdIntCycles;
    /// A 48K SNA keeps the PC on the stack. The image reads it off the stack when the stack is in the file's RAM (pc is then
    /// the popped word and sp is past it); when it is not (SP in the ROM or at the top of memory) the commit pops it from
    /// the machine's memory at `sp`, as it always has: pcOnMachineStack is set and pc / sp are the raw header values
    bool pcOnMachineStack = false;
};

/// The paging / system latches a file carries; absent = the format cannot say
struct Paging
{
    std::optional<uint8_t> p7FFD, p1FFD, pEFF7, pDFFD;
};

struct Ay
{
    std::array<uint8_t, 16> registers{};
    uint8_t selected = 0;
};

/// Anything else a file carries, with where it came from. P1 records WHAT is there (origin, kind, size, a note); the
/// payload is filled when a commit starts to read the image instead of its private staging (P9)
struct Extension
{
    std::string origin;               ///< "szx:BDSK", "z80:v3", "zxp:module2", ...
    std::string kind;                 ///< "beta128", "disk", "tape", "general-sound", ...
    size_t size = 0;
    std::string note;
    std::vector<uint8_t> payload;
};

/// One 16 KB page of a physically addressed file (SPG)
struct PhysicalRun
{
    uint32_t address = 0;             ///< byte address in the machine's RAM
    std::vector<uint8_t> data;
};

struct Image
{
    // Where it came from
    std::string format;               ///< "sna", "z80", "szx", "spg", "zxp"
    std::string formatVersion;        ///< "48", "128", "v1", "v2", "v3", "1.5", ...
    std::string sourcePath;

    /// What the file says it was made on: "48k", "128k", "128k-family" (an SNA says no more), "plus2", "plus2a",
    /// "plus3", "pentagon128", "pentagon512", "pentagon1024", "scorpion256", "tsconf", "zxpoly", "unknown"
    std::string machineHint;
    std::string rawMachineId;         ///< the format's own id, as text ("z80 hw 3", "szx 5", ...)

    MemoryModel memoryModel = MemoryModel::Mem48k;
    std::map<uint16_t, std::vector<uint8_t>> banks;   ///< logical bank -> 16384 bytes
    std::vector<PhysicalRun> physical;                ///< MemoryModel::Physical only

    Paging paging;
    bool trdosPaged = false;          ///< the TR-DOS ROM was paged in
    Cpu cpu;
    std::optional<uint32_t> framePosition;   ///< T-state in the frame, when the format stores it
    uint8_t border = 0;
    /// The whole ULA latch (#FE) when the format stores it apart from the border colour (SZX 1.1+: EAR, MIC, bit 3)
    std::optional<uint8_t> portFE;
    std::vector<Ay> ay;
    std::string timingHint;           ///< "48k", "128k", "pentagon", "" when unknown

    /// The last #FFFD write (the selected AY register) when the format stores it apart from the AY registers (Z80 on a 48K)
    std::optional<uint8_t> ayAddressLatch;

    std::vector<Extension> extensions;
    std::vector<std::string> warnings;
    /// Why no machine can take this snapshot, whatever it is ("a SAM Coupe snapshot", "a ROM block"); empty = it can. The plan
    /// refuses it before anything is written
    std::string unsupported;

    /// Number of bytes of RAM the file carries
    size_t RamBytes() const;
};

/// A dump for tests, the report and `inspect`: every field, banks as {bank, size, hash} (never the bytes)
StateNode ToStateNode(const Image& image);

/// FNV-1a 64 of a bank's bytes, as the dump prints it (16 hex digits)
std::string HashText(const std::vector<uint8_t>& bytes);
}  // namespace snapshot
