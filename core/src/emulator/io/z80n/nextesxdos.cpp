#include "stdafx.h"

#include "nextesxdos.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>

#include "emulator/cpu/z80.h"
#include "unrealdisk/fatfs.h"

using unrealdisk::FatFs;
using unrealdisk::FatNode;

namespace
{
constexpr uint8_t kCarry = 0x01, kZero = 0x40;

// The calls ("NextZXOS and esxDOS APIs", page 37)
enum : uint8_t
{
    kDiskFilemap = 0x85,
    kDiskStreamStart = 0x86,
    kDiskStreamEnd = 0x87,
    kDosVersion = 0x88,
    kGetSetDrive = 0x89,
    kGetDate = 0x8E,
    kSetCaps = 0x91,
    kDriverApi = 0x92,
    kGetError = 0x93,
    kOpen = 0x9A,
    kClose = 0x9B,
    kSync = 0x9C,
    kRead = 0x9D,
    kWrite = 0x9E,
    kSeek = 0x9F,
    kGetPosition = 0xA0,
    kFileStat = 0xA1,
    kFileTruncate = 0xA2,
    kOpenDir = 0xA3,
    kReadDir = 0xA4,
    kTellDir = 0xA5,
    kSeekDir = 0xA6,
    kRewindDir = 0xA7,
    kGetCwd = 0xA8,
    kChangeDir = 0xA9,
    kMakeDir = 0xAA,
    kRemoveDir = 0xAB,
    kStat = 0xAC,
    kUnlink = 0xAD,
    kTruncate = 0xAE,
    kChmod = 0xAF,
    kRename = 0xB0,
    kGetFree = 0xB1,
};

// F_OPEN access modes
constexpr uint8_t kModeRead = 0x01, kModeWrite = 0x02, kModeHeader = 0x40;
constexpr uint8_t kModeCreateMask = 0x0C, kModeCreateNew = 0x04, kModeCreateTruncate = 0x0C;  // $08: open or create
// F_OPENDIR access modes and the sort / filter mode in C
constexpr uint8_t kDirLongName = 0x10, kDirBothNames = 0x18, kDirWildcards = 0x20, kDirHeader = 0x40, kDirSortFilter = 0x80;
constexpr uint8_t kSfNoFiles = 0x80, kSfNoDirs = 0x40, kSfNoDots = 0x20, kSfNoSystem = 0x10, kSfSort = 0x08,
                  kSfKeyMask = 0x03, kSfReverse = 0x04;

constexpr uint16_t kDosVersionBcd = 0x0207;  // the API of NextZXOS 2.07 (sorted / filtered directories)
constexpr uint8_t kDriveC = 2 << 3;          // M_GETSETDRV's encoding: bits 7..3 the letter
constexpr size_t kHeaderBytes = 128;         // the +3DOS header
constexpr uint32_t kMaxRunSectors = 0xFFFF;  // a DISK_FILEMAP entry's 16-bit count

/// The esxDOS messages (page 38), bit 7 set on the last character as M_GETERR returns them
const char* const kMessages[] = {
    "Unknown error", "OK", "Nonsense in esxDOS", "Statement end error", "Wrong file type", "No such file or dir",
    "I/O error", "Invalid filename", "Access denied", "Drive full", "Invalid i/o request", "No such drive",
    "Too many files open", "Bad file number", "No such device", "File pointer overflow", "Is a directory",
    "Not a directory", "Already exists", "Invalid path", "Missing system", "Path too long", "No such command",
    "In use", "Read only", "Verify failed", "Sys file load error", "Directory in use", "MAPRAM is active",
    "Drive busy", "Unknown filesystem", "Device busy",
};

uint8_t ErrorOf(FatFs::Error error)
{
    switch (error)
    {
        case FatFs::Error::NotFound: return NextEsxDos::kNoEntry;
        case FatFs::Error::Exists: return NextEsxDos::kExists;
        case FatFs::Error::NoSpace: return NextEsxDos::kNoSpace;
        case FatFs::Error::ReadOnly: return NextEsxDos::kReadOnly;
        case FatFs::Error::InvalidName: return NextEsxDos::kInvalid;
        case FatFs::Error::NotEmpty: return NextEsxDos::kAccess;
        case FatFs::Error::NotDirectory: return NextEsxDos::kNotDirectory;
        case FatFs::Error::IsDirectory: return NextEsxDos::kIsDirectory;
        default: return NextEsxDos::kIo;
    }
}

int Bcd(uint8_t v) { return ((v >> 4) & 0x0F) * 10 + (v & 0x0F); }
bool BcdValid(uint8_t v) { return (v & 0x0F) < 10 && (v >> 4) < 10; }

char Upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

/// `*` and `?` as DOS matches them, without case
bool Matches(const char* pattern, const char* name)
{
    for (; *pattern; pattern++, name++)
    {
        if (*pattern == '*')
        {
            for (const char* rest = name;; rest++)
            {
                if (Matches(pattern + 1, rest))
                    return true;
                if (!*rest)
                    return false;
            }
        }
        if (!*name || (*pattern != '?' && Upper(*pattern) != Upper(*name)))
            return false;
    }
    return !*name;
}

/// Wraps the card for one call: the writes it makes are noted once at the end
class CallDevice final : public IBlockDevice
{
public:
    explicit CallDevice(IBlockDevice& card) : _card(card) {}
    uint64_t SectorCount() const override { return _card.SectorCount(); }
    bool ReadSector(uint64_t lba, uint8_t* dst) override { return _card.ReadSector(lba, dst); }
    bool WriteSector(uint64_t lba, const uint8_t* src) override
    {
        written = true;
        return _card.WriteSector(lba, src);
    }
    bool IsWritable() const override { return _card.IsWritable(); }
    std::string Describe() const override { return _card.Describe(); }
    uint64_t ContentId() const override { return _card.ContentId(); }
    bool written = false;

private:
    IBlockDevice& _card;
};

/// A directory entry of F_READDIR (the synthesized "." and ".." have no node)
struct DirItem
{
    FatNode node;
    bool dot = false;
};
}  // namespace

/// One call: the registers in and out, the guest's memory, the volume opened on demand
struct NextEsxDos::Call
{
    Z80& cpu;
    Host& host;
    uint16_t pointer;  ///< IX, or HL from a dot command
    bool fromDot;
    std::unique_ptr<CallDevice> device;
    std::unique_ptr<FatFs> fs;
    bool failed = false;  ///< Fc = 1 without an error code in A (M_GETDATE without a date, M_DRVAPI without the driver)

    uint8_t Peek(uint16_t address) const { return host.Peek(address); }
    void Poke(uint16_t address, uint8_t value) const { host.Poke(address, value); }
    void Poke16(uint16_t address, uint16_t value) const
    {
        Poke(address, static_cast<uint8_t>(value));
        Poke(static_cast<uint16_t>(address + 1), static_cast<uint8_t>(value >> 8));
    }
    void Poke32(uint16_t address, uint32_t value) const
    {
        Poke16(address, static_cast<uint16_t>(value));
        Poke16(static_cast<uint16_t>(address + 2), static_cast<uint16_t>(value >> 16));
    }
    /// A null-terminated string (the API also ends a command line at $0D)
    std::string String(uint16_t address) const
    {
        std::string out;
        for (size_t i = 0; i < 512; i++)
        {
            const uint8_t c = Peek(static_cast<uint16_t>(address + i));
            if (c == 0 || c == 0x0D)
                break;
            out.push_back(static_cast<char>(c));
        }
        return out;
    }
    uint32_t Bcde() const { return (static_cast<uint32_t>(cpu.bc) << 16) | cpu.de; }
    void SetBcde(uint32_t value)
    {
        cpu.bc = static_cast<uint16_t>(value >> 16);
        cpu.de = static_cast<uint16_t>(value);
    }

    /// The volume on card 0, opened at the first call that needs it
    uint8_t Volume()
    {
        if (fs)
            return kOk;
        IBlockDevice* card = host.Card();
        if (!card)
            return kNoDrive;
        device = std::make_unique<CallDevice>(*card);
        fs = std::make_unique<FatFs>(*device);
        if (!fs->Open())
        {
            fs.reset();
            return kUnknownFs;
        }
        uint16_t date = 0, time = 0;
        if (Clock(date, time, nullptr))
            fs->SetClock(date, time);
        return kOk;
    }

    /// The DS1307 as MS-DOS date and time; false when it holds no valid date
    bool Clock(uint16_t& date, uint16_t& time, uint8_t* seconds) const
    {
        uint8_t r[7];
        for (unsigned i = 0; i < 7; i++)
            r[i] = host.RtcRegister(i);
        const uint8_t sec = r[0] & 0x7F, min = r[1] & 0x7F, day = r[4] & 0x3F, month = r[5] & 0x1F, year = r[6];
        int hour;
        if (r[2] & 0x40)  // 12-hour mode: bit 5 PM
        {
            const uint8_t h = r[2] & 0x1F;
            if (!BcdValid(h))
                return false;
            hour = Bcd(h) % 12 + ((r[2] & 0x20) ? 12 : 0);
        }
        else
        {
            if (!BcdValid(r[2] & 0x3F))
                return false;
            hour = Bcd(r[2] & 0x3F);
        }
        for (uint8_t v : {sec, min, day, month, year})
            if (!BcdValid(v))
                return false;
        if (Bcd(sec) > 59 || Bcd(min) > 59 || hour > 23 || Bcd(day) < 1 || Bcd(day) > 31 || Bcd(month) < 1 ||
            Bcd(month) > 12)
            return false;
        date = static_cast<uint16_t>(((Bcd(year) + 20) << 9) | (Bcd(month) << 5) | Bcd(day));
        time = static_cast<uint16_t>((hour << 11) | (Bcd(min) << 5) | (Bcd(sec) / 2));
        if (seconds)
            *seconds = static_cast<uint8_t>(Bcd(sec));
        return true;
    }
};

namespace
{
/// The path parts from the root: the drive letter taken off, `.` and `..` applied to the working directory
uint8_t PathParts(const std::string& cwd, uint8_t drive, std::string path, std::vector<std::string>& parts)
{
    // A drive in the call ('*' default, '$' system, a letter or M_GETSETDRV's encoding) is overridden by the path's
    const bool letter = (drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z');
    if (letter && Upper(static_cast<char>(drive)) != 'C')
        return NextEsxDos::kNoDrive;
    if (path.size() >= 2 && path[1] == ':')
    {
        if (Upper(path[0]) != 'C' && path[0] != '*' && path[0] != '$')
            return NextEsxDos::kNoDrive;
        path.erase(0, 2);
    }
    if (path.size() >= NextEsxDos::kPathMax)
        return NextEsxDos::kNameTooLong;
    parts.clear();
    const bool absolute = !path.empty() && (path[0] == '/' || path[0] == '\\');
    std::string all = absolute ? path : cwd + "/" + path;
    std::string part;
    auto flush = [&]() {
        if (part == "..")
        {
            if (!parts.empty())
                parts.pop_back();
        }
        else if (!part.empty() && part != ".")
            parts.push_back(part);
        part.clear();
    };
    for (char c : all)
    {
        if (c == '/' || c == '\\')
            flush();
        else
            part.push_back(c);
    }
    flush();
    return NextEsxDos::kOk;
}

/// Walk the parts from the root; `canonical` gets the names as stored
bool Walk(FatFs& fs, const std::vector<std::string>& parts, size_t count, FatNode& out, std::string* canonical)
{
    FatNode node = fs.Root();
    if (canonical)
        canonical->clear();
    for (size_t i = 0; i < count; i++)
    {
        if (!node.IsDirectory())
            return false;
        FatNode next;
        if (!fs.Find(node, parts[i], next))
            return false;
        if (canonical)
            *canonical += "/" + next.name;
        node = std::move(next);
    }
    out = std::move(node);
    return true;
}

/// The +3DOS header in the first 128 bytes of a file: its 8 BASIC bytes when the signature and the checksum hold
bool ReadHeader(FatFs& fs, const FatNode& file, uint8_t basic[8])
{
    uint8_t h[kHeaderBytes];
    if (fs.Read(file, 0, h, sizeof(h)) != sizeof(h) || std::memcmp(h, "PLUS3DOS\x1A", 9) != 0)
        return false;
    uint8_t sum = 0;
    for (size_t i = 0; i < 127; i++)
        sum = static_cast<uint8_t>(sum + h[i]);
    if (sum != h[127])
        return false;
    std::memcpy(basic, h + 15, 8);
    return true;
}

void BuildHeader(uint8_t h[kHeaderBytes], uint32_t length, const uint8_t basic[8])
{
    std::memset(h, 0, kHeaderBytes);
    std::memcpy(h, "PLUS3DOS\x1A", 9);
    h[9] = 1;   // issue
    h[10] = 0;  // version
    for (unsigned i = 0; i < 4; i++)
        h[11 + i] = static_cast<uint8_t>(length >> (8 * i));
    std::memcpy(h + 15, basic, 8);
    uint8_t sum = 0;
    for (size_t i = 0; i < 127; i++)
        sum = static_cast<uint8_t>(sum + h[i]);
    h[127] = sum;
}
}  // namespace

void NextEsxDos::Reset()
{
    for (Handle& h : _handles)
        h = Handle{};
    std::memset(_cwd, 0, sizeof(_cwd));
    _cwd[0] = '/';
    _caps = 0;
}

unsigned NextEsxDos::OpenHandles() const
{
    unsigned n = 0;
    for (const Handle& h : _handles)
        n += h.kind != kFree;
    return n;
}

NextEsxDos::Handle* NextEsxDos::HandleOf(uint8_t number, Kind kind)
{
    const unsigned slot = static_cast<uint8_t>(number - kFirstHandle);
    if (slot >= kHandles || _handles[slot].kind != kind)
        return nullptr;
    return &_handles[slot];
}

bool NextEsxDos::HandleRst8(Z80& cpu)
{
    // The RST pushed the address of the call byte that follows it
    const uint16_t ret = static_cast<uint16_t>(_host.Peek(cpu.sp) | (_host.Peek(static_cast<uint16_t>(cpu.sp + 1)) << 8));
    const uint8_t id = _host.Peek(ret);
    if (id < 0x80)
        return false;  // RST 8 : DEFB code, a 48K ROM error report: the ROM's own

    const bool fromDot = ret >= 0x2000 && ret < 0x4000;
    Call call{cpu, _host, fromDot ? cpu.hl : cpu.ix, fromDot, nullptr, nullptr};
    const uint8_t error = Serve(id, call);
    if (call.device && call.device->written)
        _host.CardWritten();

    if (error != kOk)
        cpu.a = error;
    cpu.f = static_cast<uint8_t>((cpu.f & ~kCarry) | (error != kOk || call.failed ? kCarry : 0));
    if (id == kDosVersion)
        cpu.f = static_cast<uint8_t>(cpu.f | kZero);  // A = 0: the NextZXOS mode
    // The DOS returns past the call byte
    cpu.sp = static_cast<uint16_t>(cpu.sp + 2);
    cpu.pc = static_cast<uint16_t>(ret + 1);
    return true;
}

uint8_t NextEsxDos::Serve(uint8_t id, Call& call)
{
    Z80& cpu = call.cpu;
    const uint16_t ptr = call.pointer;
    std::vector<std::string> parts;
    FatNode node;

    // The node a path call names (A = drive, the path at the pointer)
    auto resolve = [&](uint16_t address, FatNode& out, std::string* canonical = nullptr) -> uint8_t {
        if (const uint8_t e = PathParts(_cwd, cpu.a, call.String(address), parts); e != kOk)
            return e;
        if (const uint8_t e = call.Volume(); e != kOk)
            return e;
        if (!Walk(*call.fs, parts, parts.size(), out, canonical))
            return call.fs->LastError() == FatFs::Error::None ? kNoEntry : ErrorOf(call.fs->LastError());
        return kOk;
    };
    // The directory a new entry goes into, and its name
    auto parentOf = [&](uint16_t address, FatNode& dir, std::string& name) -> uint8_t {
        if (const uint8_t e = PathParts(_cwd, cpu.a, call.String(address), parts); e != kOk)
            return e;
        if (parts.empty())
            return kInvalid;
        if (const uint8_t e = call.Volume(); e != kOk)
            return e;
        if (!Walk(*call.fs, parts, parts.size() - 1, dir, nullptr))
            return kBadPath;
        if (!dir.IsDirectory())
            return kNotDirectory;
        name = parts.back();
        return kOk;
    };
    // The entry of an open handle, as it is now
    auto entryOf = [&](const Handle& h, FatNode& out) -> uint8_t {
        if (const uint8_t e = call.Volume(); e != kOk)
            return e;
        if (h.root)
        {
            out = call.fs->Root();
            return kOk;
        }
        return call.fs->NodeAt(h.entryLba, h.entryOffset, out) ? kOk : kBadHandle;
    };
    // An open handle moves with its entry (F_RENAME), and none survives its deletion
    // F_STAT / F_FSTAT's 11 bytes
    auto putStat = [&](uint16_t address, const FatNode& n) {
        call.Poke(address, '*');
        call.Poke(static_cast<uint16_t>(address + 1), 0x81);
        call.Poke(static_cast<uint16_t>(address + 2), n.root ? FatNode::kDirectory : n.attributes);
        call.Poke16(static_cast<uint16_t>(address + 3), n.time);
        call.Poke16(static_cast<uint16_t>(address + 5), n.date);
        call.Poke32(static_cast<uint16_t>(address + 7), n.size);
    };
    auto usedBy = [&](const FatNode& n) {
        for (const Handle& h : _handles)
            if (h.kind != kFree && !h.root && h.entryLba == n.entryLba && h.entryOffset == n.entryOffset)
                return true;
        return false;
    };

    switch (id)
    {
        case kDosVersion:
            cpu.b = 'N';
            cpu.c = 'X';
            cpu.de = kDosVersionBcd;
            cpu.l = 'e';
            cpu.h = 'n';
            cpu.a = 0;
            return kOk;

        case kGetSetDrive:
            // Drive C: only ('*' and '$' name it too)
            if (cpu.a != 0 && (cpu.a & 0xF8) != kDriveC)
                return kNoDrive;
            cpu.a = kDriveC;
            return kOk;

        case kGetDate:
        {
            uint16_t date = 0, time = 0;
            uint8_t seconds = 0;
            if (!call.Clock(date, time, &seconds))
            {
                cpu.bc = 0;
                cpu.de = 0;
                call.failed = true;
                return kOk;
            }
            cpu.bc = date;
            cpu.de = time;
            cpu.h = seconds;
            cpu.l = 0xFF;  // the DS1307 has no hundredths
            return kOk;
        }

        case kSetCaps:
            cpu.e = _caps;
            _caps = cpu.a;
            return kOk;

        case kDriverApi:
            if (cpu.c != 0)
            {
                cpu.a = 0;  // driver not found
                call.failed = true;
                return kOk;
            }
            if (cpu.b == 0)
                return Serve(kGetDate, call);
            return kNoCommand;

        case kGetError:
        {
            if (cpu.b != 1)
                return kNoCommand;  // B = 0 makes a BASIC error report: no NextZXOS to make it
            const uint16_t buffer = cpu.de;
            if (cpu.a == 0)
            {
                // A custom message (bit 7 ends it), up to the 32-byte buffer
                for (uint16_t i = 0; i < 32; i++)
                {
                    const uint8_t c = call.Peek(static_cast<uint16_t>(ptr + i));
                    call.Poke(static_cast<uint16_t>(buffer + i), c);
                    if (c & 0x80)
                        break;
                }
                return kOk;
            }
            const char* text = cpu.a < std::size(kMessages) ? kMessages[cpu.a] : kMessages[0];
            const size_t n = std::min<size_t>(std::strlen(text), 32);
            for (size_t i = 0; i < n; i++)
                call.Poke(static_cast<uint16_t>(buffer + i),
                          static_cast<uint8_t>(text[i] | (i + 1 == n ? 0x80 : 0)));
            return kOk;
        }

        case kDiskFilemap:
        {
            Handle* h = HandleOf(cpu.a, kFile);
            if (!h)
                return kBadHandle;
            if (const uint8_t e = entryOf(*h, node); e != kOk)
                return e;
            std::vector<std::pair<uint64_t, uint32_t>> runs;
            if (!call.fs->Extents(node, runs))
                return ErrorOf(call.fs->LastError());
            uint32_t skip = h->mapped;
            uint16_t room = cpu.de;
            uint16_t out = ptr;
            for (auto [lba, count] : runs)
            {
                if (skip >= count)
                {
                    skip -= count;
                    continue;
                }
                lba += skip;
                count -= skip;
                skip = 0;
                while (count > 0 && room > 0)
                {
                    const uint32_t n = std::min(count, kMaxRunSectors);
                    call.Poke32(out, static_cast<uint32_t>(lba));
                    call.Poke16(static_cast<uint16_t>(out + 4), static_cast<uint16_t>(n));
                    out = static_cast<uint16_t>(out + 6);
                    room--;
                    lba += n;
                    count -= n;
                    h->mapped += n;
                }
                if (room == 0)
                    break;
            }
            cpu.de = room;
            cpu.hl = out;
            cpu.a = 0x02;  // card 0, block addressing (SDHC)
            return kOk;
        }

        case kDiskStreamStart:
        {
            // IXDE (HLDE from a dot command): the card address
            const uint32_t block = (static_cast<uint32_t>(ptr) << 16) | cpu.de;
            if ((cpu.a & 0x01) != 0 || !_host.StreamStart(block, (cpu.a & 0x80) != 0))
                return kDeviceBusy;
            cpu.b = 0;     // the SD/MMC protocol
            cpu.c = 0xEB;  // its data port
            return kOk;
        }

        case kDiskStreamEnd:
            return _host.StreamEnd() ? kOk : kDeviceBusy;

        case kOpen:
        {
            const uint8_t mode = cpu.b;
            unsigned slot = 0;
            while (slot < kHandles && _handles[slot].kind != kFree)
                slot++;
            if (slot == kHandles)
                return kTooManyFiles;
            FatNode dir;
            std::string name;
            if (const uint8_t e = parentOf(ptr, dir, name); e != kOk)
                return e;
            FatFs& fs = *call.fs;
            const bool exists = fs.Find(dir, name, node);
            if (!exists && fs.LastError() != FatFs::Error::NotFound)
                return ErrorOf(fs.LastError());
            const uint8_t create = mode & kModeCreateMask;
            if (exists && create == kModeCreateNew)
                return kExists;
            if (!exists && (create == 0))
                return kNoEntry;
            if (exists && node.IsDirectory())
                return kIsDirectory;
            if (exists && (mode & kModeWrite) && (node.attributes & FatNode::kReadOnly))
                return kAccess;
            bool fresh = !exists;
            if (!exists && !fs.Create(dir, name, false, node))
                return ErrorOf(fs.LastError());
            if (exists && create == kModeCreateTruncate)
            {
                if (!fs.Resize(node, 0))
                    return ErrorOf(fs.LastError());
                fresh = true;
            }
            Handle h;
            h.kind = kFile;
            h.mode = mode;
            h.entryLba = node.entryLba;
            h.entryOffset = node.entryOffset;
            if (mode & kModeHeader)
            {
                uint8_t basic[8] = {};
                if (fresh && (mode & kModeWrite))
                {
                    // A new file starts with its header (the length and checksum follow at F_CLOSE)
                    for (unsigned i = 0; i < 8; i++)
                        basic[i] = call.Peek(static_cast<uint16_t>(cpu.de + i));
                    uint8_t header[kHeaderBytes];
                    BuildHeader(header, kHeaderBytes, basic);
                    size_t written = 0;
                    if (!fs.Write(node, 0, header, sizeof(header), written))
                        return ErrorOf(fs.LastError());
                    h.headerSize = kHeaderBytes;
                    h.headerDirty = 1;
                }
                else if (ReadHeader(fs, node, basic))
                {
                    h.headerSize = kHeaderBytes;
                    for (unsigned i = 0; i < 8; i++)
                        call.Poke(static_cast<uint16_t>(cpu.de + i), basic[i]);
                }
                else
                    call.Poke(cpu.de, 0xFF);  // a headerless file: its type reads $FF
            }
            _handles[slot] = h;
            cpu.a = static_cast<uint8_t>(kFirstHandle + slot);
            return kOk;
        }

        case kClose:
        {
            Handle* h = HandleOf(cpu.a, kFile);
            if (!h)
                h = HandleOf(cpu.a, kDirectory);
            if (!h)
                return kBadHandle;
            uint8_t result = kOk;
            if (h->kind == kFile && h->headerDirty)
            {
                // The header takes the file's length and its checksum
                result = entryOf(*h, node);
                uint8_t basic[8] = {};
                uint8_t header[kHeaderBytes];
                if (result == kOk && call.fs->Read(node, 0, header, sizeof(header)) == sizeof(header))
                {
                    std::memcpy(basic, header + 15, 8);
                    BuildHeader(header, node.size, basic);
                    size_t written = 0;
                    if (!call.fs->Write(node, 0, header, sizeof(header), written))
                        result = ErrorOf(call.fs->LastError());
                }
            }
            *h = Handle{};
            if (result == kOk)
                cpu.a = 0;
            return result;
        }

        case kSync:
            return HandleOf(cpu.a, kFile) ? kOk : kBadHandle;  // every write is on the card already

        case kRead:
        {
            Handle* h = HandleOf(cpu.a, kFile);
            const uint16_t count = cpu.bc;
            cpu.bc = 0;
            if (!h)
                return kBadHandle;
            if (!(h->mode & kModeRead))
                return kAccess;
            if (const uint8_t e = entryOf(*h, node); e != kOk)
                return e;
            std::vector<uint8_t> data(count);
            const size_t n = call.fs->Read(node, h->headerSize + h->position, data.data(), data.size());
            if (n == 0 && call.fs->LastError() != FatFs::Error::None)
                return ErrorOf(call.fs->LastError());
            for (size_t i = 0; i < n; i++)
                call.Poke(static_cast<uint16_t>(ptr + i), data[i]);
            h->position += static_cast<uint32_t>(n);
            cpu.bc = static_cast<uint16_t>(n);
            cpu.de = static_cast<uint16_t>(n);
            cpu.hl = static_cast<uint16_t>(ptr + n);
            return kOk;
        }

        case kWrite:
        {
            Handle* h = HandleOf(cpu.a, kFile);
            const uint16_t count = cpu.bc;
            cpu.bc = 0;
            if (!h)
                return kBadHandle;
            if (!(h->mode & kModeWrite))
                return kAccess;
            if (const uint8_t e = entryOf(*h, node); e != kOk)
                return e;
            std::vector<uint8_t> data(count);
            for (size_t i = 0; i < data.size(); i++)
                data[i] = call.Peek(static_cast<uint16_t>(ptr + i));
            size_t written = 0;
            const bool ok = call.fs->Write(node, h->headerSize + h->position, data.data(), data.size(), written);
            h->position += static_cast<uint32_t>(written);
            if (h->headerSize)
                h->headerDirty = 1;
            cpu.bc = static_cast<uint16_t>(written);
            return ok ? kOk : ErrorOf(call.fs->LastError());
        }

        case kSeek:
        case kGetPosition:
        {
            Handle* h = HandleOf(cpu.a, kFile);
            if (!h)
                return kBadHandle;
            if (id == kSeek)
            {
                if (const uint8_t e = entryOf(*h, node); e != kOk)
                    return e;
                const uint32_t end = node.size > h->headerSize ? node.size - h->headerSize : 0;
                const uint32_t offset = call.Bcde();
                // IXL (L from a dot command): set / forward / back; past either end stops at it
                const uint8_t how = call.fromDot ? cpu.l : cpu.xl;
                int64_t target = how == 1   ? int64_t{h->position} + offset
                                 : how == 2 ? int64_t{h->position} - offset
                                            : int64_t{offset};
                target = std::clamp<int64_t>(target, 0, end);
                h->position = static_cast<uint32_t>(target);
            }
            call.SetBcde(h->position);
            return kOk;
        }

        case kFileStat:
        {
            Handle* h = HandleOf(cpu.a, kFile);
            if (!h)
                return kBadHandle;
            if (const uint8_t e = entryOf(*h, node); e != kOk)
                return e;
            putStat(ptr, node);
            return kOk;
        }

        case kFileTruncate:
        {
            Handle* h = HandleOf(cpu.a, kFile);
            if (!h)
                return kBadHandle;
            if (!(h->mode & kModeWrite))
                return kAccess;
            if (const uint8_t e = entryOf(*h, node); e != kOk)
                return e;
            return call.fs->Resize(node, call.Bcde()) ? kOk : ErrorOf(call.fs->LastError());
        }

        case kOpenDir:
        {
            unsigned slot = 0;
            while (slot < kHandles && _handles[slot].kind != kFree)
                slot++;
            if (slot == kHandles)
                return kTooManyFiles;
            if (const uint8_t e = resolve(ptr, node); e != kOk)
                return e;
            if (!node.IsDirectory())
                return kNotDirectory;
            Handle h;
            h.kind = kDirectory;
            h.mode = cpu.b;
            h.filter = (cpu.b & kDirSortFilter) ? cpu.c : 0;
            h.root = node.root ? 1 : 0;
            h.entryLba = node.entryLba;
            h.entryOffset = node.entryOffset;
            _handles[slot] = h;
            cpu.a = static_cast<uint8_t>(kFirstHandle + slot);
            cpu.c = 1;  // sorted (when asked)
            return kOk;
        }

        case kReadDir:
        {
            Handle* h = HandleOf(cpu.a, kDirectory);
            if (!h)
                return kBadHandle;
            if (const uint8_t e = entryOf(*h, node); e != kOk)
                return e;
            FatFs& fs = *call.fs;
            std::vector<FatNode> entries;
            if (!fs.List(node, entries))
                return ErrorOf(fs.LastError());

            // What the handle shows: the dots of a subdirectory, then the entries its filter and wildcard keep
            const uint8_t filter = h->filter;
            const std::string wildcard = (h->mode & kDirWildcards) ? call.String(cpu.de) : std::string();
            std::vector<DirItem> items;
            if (!node.root && !(filter & kSfNoDots) && !(filter & kSfNoDirs))
            {
                for (const char* dot : {".", ".."})
                {
                    DirItem item;
                    item.dot = true;
                    item.node.name = item.node.shortName = dot;
                    item.node.attributes = FatNode::kDirectory;
                    item.node.date = node.date;
                    item.node.time = node.time;
                    items.push_back(std::move(item));
                }
            }
            const size_t dots = items.size();
            for (FatNode& e : entries)
            {
                const bool isDir = e.IsDirectory();
                if ((isDir && (filter & kSfNoDirs)) || (!isDir && (filter & kSfNoFiles)))
                    continue;
                if ((filter & kSfNoSystem) && (e.attributes & (FatNode::kHidden | FatNode::kSystem)))
                    continue;
                if (!wildcard.empty() && !Matches(wildcard.c_str(), e.name.c_str()) &&
                    !Matches(wildcard.c_str(), e.shortName.c_str()))
                    continue;
                DirItem item;
                item.node = std::move(e);
                items.push_back(std::move(item));
            }
            if (filter & kSfSort)
            {
                auto upper = [](std::string s) {
                    for (char& c : s)
                        c = Upper(c);
                    return s;
                };
                const uint8_t key = filter & kSfKeyMask;
                const bool reverse = (filter & kSfReverse) != 0;
                std::stable_sort(items.begin() + static_cast<ptrdiff_t>(dots), items.end(),
                                 [&](const DirItem& x, const DirItem& y) {
                                     const FatNode& a = reverse ? y.node : x.node;
                                     const FatNode& b = reverse ? x.node : y.node;
                                     if (key == 2 && (a.date != b.date || a.time != b.time))
                                         return a.date != b.date ? a.date < b.date : a.time < b.time;
                                     if (key == 3 && a.size != b.size)
                                         return a.size < b.size;
                                     if (key == 1)
                                         return upper(a.shortName) < upper(b.shortName);
                                     return upper(a.name) < upper(b.name);
                                 });
            }
            if (h->position >= items.size())
            {
                cpu.a = 0;  // no more entries
                return kOk;
            }
            const DirItem& item = items[h->position++];
            uint16_t out = ptr;
            auto put = [&](uint8_t b) {
                call.Poke(out, b);
                out = static_cast<uint16_t>(out + 1);
            };
            auto putName = [&](const std::string& s) {
                for (char c : s)
                    put(static_cast<uint8_t>(c));
                put(0);
            };
            put(item.node.attributes);
            if ((h->mode & kDirBothNames) == kDirBothNames)
            {
                putName(item.node.name);
                putName(item.node.shortName);
            }
            else
                putName((h->mode & kDirLongName) ? item.node.name : item.node.shortName);
            for (uint16_t v : {item.node.time, item.node.date})
            {
                put(static_cast<uint8_t>(v));
                put(static_cast<uint8_t>(v >> 8));
            }
            for (unsigned i = 0; i < 4; i++)
                put(static_cast<uint8_t>(item.node.size >> (8 * i)));
            if (h->mode & kDirHeader)
            {
                uint8_t basic[8] = {0xFF};  // headerless: type $FF, the rest zero
                if (item.dot || item.node.IsDirectory() || !ReadHeader(fs, item.node, basic))
                {
                    std::memset(basic, 0, sizeof(basic));
                    basic[0] = 0xFF;
                }
                for (uint8_t b : basic)
                    put(b);
            }
            cpu.a = 1;
            return kOk;
        }

        case kTellDir:
        case kSeekDir:
        case kRewindDir:
        {
            Handle* h = HandleOf(cpu.a, kDirectory);
            if (!h)
                return kBadHandle;
            if (id == kTellDir)
                call.SetBcde(h->position);
            else
                h->position = id == kSeekDir ? call.Bcde() : 0;
            return kOk;
        }

        case kGetCwd:
        {
            std::string path = _cwd;
            if (cpu.a == 0xFF)
            {
                // The directory of the filespec at DE: what follows its last '/', '\' or ':' is not looked at
                std::string spec = call.String(cpu.de);
                const size_t cut = spec.find_last_of("/\\:");
                spec = cut == std::string::npos ? std::string() : spec.substr(0, cut + 1);
                if (const uint8_t e = PathParts(_cwd, '*', spec, parts); e != kOk)
                    return e;
                if (const uint8_t v = call.Volume(); v != kOk)
                    return v;
                if (!Walk(*call.fs, parts, parts.size(), node, &path))
                    return kBadPath;
                if (!node.IsDirectory())
                    return kNotDirectory;
            }
            else if (const uint8_t e = PathParts(_cwd, cpu.a, "", parts); e != kOk)
                return e;
            if (path.empty() || path.back() != '/')
                path += '/';
            for (size_t i = 0; i < path.size(); i++)
                call.Poke(static_cast<uint16_t>(ptr + i), static_cast<uint8_t>(path[i]));
            call.Poke(static_cast<uint16_t>(ptr + path.size()), 0);
            return kOk;
        }

        case kChangeDir:
        {
            std::string canonical;
            if (const uint8_t e = resolve(ptr, node, &canonical); e != kOk)
                return e;
            if (!node.IsDirectory())
                return kNotDirectory;
            if (canonical.empty())
                canonical = "/";
            if (canonical.size() >= kPathMax)
                return kNameTooLong;
            std::memset(_cwd, 0, sizeof(_cwd));
            std::memcpy(_cwd, canonical.data(), canonical.size());
            return kOk;
        }

        case kMakeDir:
        {
            FatNode dir;
            std::string name;
            if (const uint8_t e = parentOf(ptr, dir, name); e != kOk)
                return e;
            return call.fs->Create(dir, name, true, node) ? kOk : ErrorOf(call.fs->LastError());
        }

        case kRemoveDir:
        {
            std::string canonical;
            if (const uint8_t e = resolve(ptr, node, &canonical); e != kOk)
                return e;
            if (!node.IsDirectory())
                return kNotDirectory;
            if (node.root || usedBy(node))
                return kDirInUse;
            const std::string cwd = _cwd;
            if (cwd == canonical || cwd.rfind(canonical + "/", 0) == 0)
                return kDirInUse;  // the working directory, or one it is in
            return call.fs->Remove(node) ? kOk : ErrorOf(call.fs->LastError());
        }

        case kStat:
            if (const uint8_t e = resolve(ptr, node); e != kOk)
                return e;
            putStat(cpu.de, node);
            return kOk;

        case kUnlink:
            if (const uint8_t e = resolve(ptr, node); e != kOk)
                return e;
            if (node.IsDirectory())
                return kIsDirectory;
            if (usedBy(node))
                return kInUse;
            return call.fs->Remove(node) ? kOk : ErrorOf(call.fs->LastError());

        case kTruncate:
            if (const uint8_t e = resolve(ptr, node); e != kOk)
                return e;
            if (node.IsDirectory())
                return kIsDirectory;
            return call.fs->Resize(node, call.Bcde()) ? kOk : ErrorOf(call.fs->LastError());

        case kChmod:
        {
            if (const uint8_t e = resolve(ptr, node); e != kOk)
                return e;
            if (node.root)
                return kAccess;
            // A_WRITE (bit 0) is the inverse of the read-only bit; hidden, system and archive are the FAT bits
            const uint8_t values = cpu.b, change = cpu.c;
            uint8_t attributes = node.attributes;
            for (uint8_t bit : {FatNode::kHidden, FatNode::kSystem, FatNode::kArchive})
                if (change & bit)
                    attributes = static_cast<uint8_t>((attributes & ~bit) | (values & bit));
            if (change & 0x01)
                attributes = static_cast<uint8_t>((attributes & ~FatNode::kReadOnly) |
                                                  ((values & 0x01) ? 0 : FatNode::kReadOnly));
            return call.fs->SetAttributes(node, attributes) ? kOk : ErrorOf(call.fs->LastError());
        }

        case kRename:
        {
            if (const uint8_t e = resolve(ptr, node); e != kOk)
                return e;
            if (node.root)
                return kAccess;
            // The destination: into an existing directory under the same name, else the path's directory and name
            std::vector<std::string> source = parts;
            FatNode dir;
            std::string name;
            if (const uint8_t e = PathParts(_cwd, cpu.a, call.String(cpu.de), parts); e != kOk)
                return e;
            FatNode target;
            if (!parts.empty() && Walk(*call.fs, parts, parts.size(), target, nullptr) && target.IsDirectory() &&
                !(target.entryLba == node.entryLba && target.entryOffset == node.entryOffset))
            {
                dir = target;
                name = node.name;
            }
            else
            {
                if (parts.empty())
                    return kInvalid;
                if (!Walk(*call.fs, parts, parts.size() - 1, dir, nullptr))
                    return kBadPath;
                if (!dir.IsDirectory())
                    return kNotDirectory;
                name = parts.back();
            }
            FatNode moved;
            if (!call.fs->Rename(node, dir, name, moved))
                return ErrorOf(call.fs->LastError());
            for (Handle& h : _handles)
                if (h.kind != kFree && !h.root && h.entryLba == node.entryLba && h.entryOffset == node.entryOffset)
                {
                    h.entryLba = moved.entryLba;
                    h.entryOffset = moved.entryOffset;
                }
            return kOk;
        }

        case kGetFree:
        {
            if (const uint8_t e = PathParts(_cwd, cpu.a, "", parts); e != kOk)
                return e;
            if (const uint8_t e = call.Volume(); e != kOk)
                return e;
            const uint64_t blocks = uint64_t{call.fs->FreeClusters()} * (call.fs->ClusterBytes() / 512);
            call.SetBcde(static_cast<uint32_t>(std::min<uint64_t>(blocks, UINT32_MAX)));
            return kOk;
        }

        default:
            // Tape redirection, dot commands, +3DOS, BASIC autoload, error handlers: NextZXOS's own
            return kNoCommand;
    }
}
