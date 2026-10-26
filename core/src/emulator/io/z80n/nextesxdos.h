#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/iblockdevice.h"

/// The esxDOS API of the Next without its firmware (requirements.md Q11, esxdos-and-sd.md E1): with the feature "esxdos"
/// on and no boot ROM (the bare personality), a `RST $08 : DEFB call` with a call of #80 and up is served here against
/// the FAT volume of SD card 0 as NextZXOS serves it ("NextZXOS and esxDOS APIs", pages 37-58): the files, the
/// directories, the +3DOS headers, the file map and the stream of DISK_FILEMAP / DISK_STRMSTART, the date from the
/// board's RTC. A call below #80 is a 48K ROM error report and runs the ROM; a call the mode does not serve (the
/// tape redirection, dot commands, +3DOS, drivers, a BASIC error report) answers "no such command".
///
/// The guest sees NextZXOS 2.07 in its NextZXOS mode (M_DOSVERSION), drive C: only. Parameters come in IX, or in HL when
/// the call comes from a dot command's page (#2000-#3FFF), as on the board. A call takes no time: the RST's 11 T only.
///
/// The state is a fixed block (the TTD state, nextstatecodec.cpp): the open handles keep the places of their directory
/// entries and their positions, never a host object, so every call reads the volume again and a restored machine goes on
/// with the card it has.
class NextEsxDos final : public IRst8Trap
{
public:
    static constexpr unsigned kHandles = 16;
    static constexpr uint8_t kFirstHandle = 1;  ///< handle numbers are kFirstHandle + slot
    static constexpr size_t kPathMax = 256;

    /// esxDOS error codes (esx_e*)
    enum Error : uint8_t
    {
        kOk = 0,
        kNoEntry = 5,
        kIo = 6,
        kInvalid = 7,
        kAccess = 8,
        kNoSpace = 9,
        kNoDrive = 11,
        kTooManyFiles = 12,
        kBadHandle = 13,
        kIsDirectory = 16,
        kNotDirectory = 17,
        kExists = 18,
        kBadPath = 19,
        kNameTooLong = 21,
        kNoCommand = 22,
        kInUse = 23,
        kReadOnly = 24,
        kDirInUse = 27,
        kUnknownFs = 30,
        kDeviceBusy = 31,
    };

    /// How the machine reaches its card: card 0's medium, the write note, the stream
    class Host
    {
    public:
        virtual ~Host() = default;
        virtual IBlockDevice* Card() = 0;
        virtual void CardWritten() = 0;
        /// DISK_STRMSTART: CMD18 at `block` on the card's SPI line, with the first data token waited for unless
        /// `userWaits`; false when the card does not answer
        virtual bool StreamStart(uint32_t block, bool userWaits) = 0;
        virtual bool StreamEnd() = 0;
        /// The DS1307's registers 0-6 (BCD)
        virtual uint8_t RtcRegister(unsigned index) = 0;
        /// The CPU's view of memory: a read without side effects, a write that reaches TTD as a CPU write does
        virtual uint8_t Peek(uint16_t address) = 0;
        virtual void Poke(uint16_t address, uint8_t value) = 0;
    };

    explicit NextEsxDos(Host& host) : _host(host) { Reset(); }

    /// Every handle closed, the working directory the root
    void Reset();
    bool HandleRst8(Z80& cpu) override;

    /// The working directory ("/" or "/GAMES/SUB", names as stored) and the open handles, for tests and reports
    std::string WorkingDirectory() const { return _cwd; }
    unsigned OpenHandles() const;

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    struct Call;

    enum Kind : uint8_t
    {
        kFree = 0,
        kFile = 1,
        kDirectory = 2,
    };
    struct Handle
    {
        uint8_t kind = kFree;
        uint8_t mode = 0;         ///< F_OPEN's access mode / F_OPENDIR's
        uint8_t filter = 0;       ///< F_OPENDIR's sort / filter mode
        uint8_t headerSize = 0;   ///< 128: a +3DOS header precedes the data the position counts
        uint8_t headerDirty = 0;  ///< written since the open: F_CLOSE sets the header's length and checksum
        uint8_t root = 0;         ///< a directory handle on the root (no entry of its own)
        uint16_t entryOffset = 0;
        uint64_t entryLba = 0;    ///< the place of the directory entry
        uint32_t position = 0;    ///< the data position / the index of the next directory entry
        uint32_t mapped = 0;      ///< sectors DISK_FILEMAP has handed out
    };

    Handle* HandleOf(uint8_t number, Kind kind);
    uint8_t Serve(uint8_t id, Call& call);

    Host& _host;
    Handle _handles[kHandles];
    char _cwd[kPathMax] = {};
    uint8_t _caps = 0;  ///< M_SETCAPS
};
