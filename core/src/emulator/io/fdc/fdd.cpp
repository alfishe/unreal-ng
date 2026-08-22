#include "fdd.h"

#include <cstring>
#include <random>
#include "common/filehelper.h"
#include "loaders/disk/loader_trd.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulator.h"
#include "emulator/cpu/core.h"
#include "emulator/notifications.h"

/// region <Constructors / destructors>
FDD::FDD(EmulatorContext* context) : _context(context)
{
    /// region <Random track number on init>
    // Initialize random numbers generator
    std::random_device rd;
    std::mt19937 generator(rd());

    // Set distribution range within standard valid track number [0:80]
    std::uniform_int_distribution<size_t> trackValueDistribution(0, 80);

    // setTrack will set flags as well
    setTrack(trackValueDistribution(generator));

    /// endregion </Random track number on init>

    // TODO: remove debug
    /// region <Debug image initialization>
    if (false)
    {
        std::string filepath = "../../../tests/loaders/trd/EyeAche.trd";
        filepath = FileHelper::AbsolutePath(filepath);
        LoaderTRD trdLoader(_context, filepath);
        [[maybe_unused]] bool imageLoaded = trdLoader.loadImage();

        DiskImage *diskImage = trdLoader.getImage();
        /// endregion </Load disk image>

        _diskImage = diskImage;
        _diskInserted = true;
    }
    /// endregion </Debug Image initialization>
}

FDD::~FDD()
{
    // Note: FDD does not own the DiskImage, just holds a pointer
    // DiskImage is managed by Emulator/CoreState and deleted elsewhere
}
/// endregion </Constructors / destructors>

/// region <Methods>

/// Method must be called at
void FDD::process()
{
    uint64_t frame = _context->emulatorState.frame_counter;
    uint64_t time = _context->pCore->GetZ80()->t;




    _lastFrame = frame;
    _lastTime = time;
}

void FDD::insertDisk(DiskImage* diskImage)
{
    if (diskImage)
    {
        _diskImage = diskImage;
        _diskInserted = true;
        
        // Notify subscribers about disk insertion with full context
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        std::string emulatorId = (_context && _context->pEmulator) ? _context->pEmulator->GetId() : "";
        std::string path = diskImage->getFilePath();
        messageCenter.Post(NC_FDD_DISK_INSERTED, new FDDDiskPayload(emulatorId, _driveID, path), true);
    }
}

void FDD::ejectDisk()
{
    // Capture path before clearing pointer
    std::string path;
    if (_diskImage)
    {
        path = _diskImage->getFilePath();
    }
    
    // Get emulator ID before any cleanup
    std::string emulatorId = (_context && _context->pEmulator) ? _context->pEmulator->GetId() : "";
    
    // Note: FDD does not own the DiskImage, just holds a pointer to it
    // The DiskImage is owned and managed by the Emulator/CoreState
    _diskImage = nullptr;
    _diskInserted = false;
    
    // Notify subscribers about disk ejection with full context
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_FDD_DISK_EJECTED, new FDDDiskPayload(emulatorId, _driveID, path), true);
}

/// endregion </Methods>

/// region <TTDSerializable (P1.5 — parent TDD §6.4, §4 row 4)>
//
// Cursor-packed layout (27 bytes):
//   0   1   _driveID
//   1   1   _sideTop
//   2   1   _motorOn
//   3   1   _direction
//   4   1   _headLoad
//   5   1   _diskInserted
//   6   1   _track
//   7   1   _index (cached output signal)
//   8   1   _ready
//   9   1   _writeProtect
//   10  8   _motorStopTimeoutMs (size_t → uint64)
//   18  8   _motorRotationCounter (size_t → uint64)
//   26  1   reserved (alignment / future use)
//
// Note: total is 27 bytes; the trailing reserved byte keeps the format
// at a round size for snapshot-version bump headroom.

namespace
{
inline void put_u8 (uint8_t*& cur, uint8_t v)        { *cur++ = v; }
inline void put_u64(uint8_t*& cur, uint64_t v)      { std::memcpy(cur, &v, 8); cur += 8; }
inline uint8_t  get_u8 (const uint8_t*& cur)        { return *cur++; }
inline uint64_t get_u64(const uint8_t*& cur)        { uint64_t v; std::memcpy(&v, cur, 8); cur += 8; return v; }
} // anonymous namespace

static constexpr size_t kFddStateSize = 27;
static_assert(kFddStateSize == 27, "FDD state size drift");

size_t FDD::TTDStateSize() const
{
    return kFddStateSize;
}

void FDD::TTDSaveState(uint8_t* dst) const
{
    uint8_t* cur = dst;
    put_u8 (cur, _driveID);
    put_u8 (cur, _sideTop          ? 1 : 0);
    put_u8 (cur, _motorOn          ? 1 : 0);
    put_u8 (cur, _direction        ? 1 : 0);
    put_u8 (cur, _headLoad         ? 1 : 0);
    put_u8 (cur, _diskInserted     ? 1 : 0);
    put_u8 (cur, _track);
    put_u8 (cur, _index            ? 1 : 0);
    put_u8 (cur, _ready            ? 1 : 0);
    put_u8 (cur, _writeProtect     ? 1 : 0);
    put_u64(cur, static_cast<uint64_t>(_motorStopTimeoutMs));
    put_u64(cur, static_cast<uint64_t>(_motorRotationCounter));
    put_u8 (cur, 0);  // reserved
}

void FDD::TTDLoadState(const uint8_t* src)
{
    const uint8_t* cur = src;
    _driveID           = get_u8(cur);
    _sideTop           = get_u8(cur) != 0;
    _motorOn           = get_u8(cur) != 0;
    _direction         = get_u8(cur) != 0;
    _headLoad          = get_u8(cur) != 0;
    _diskInserted      = get_u8(cur) != 0;
    _track             = get_u8(cur);
    _index             = get_u8(cur) != 0;
    _ready             = get_u8(cur) != 0;
    _writeProtect      = get_u8(cur) != 0;
    _motorStopTimeoutMs   = static_cast<size_t>(get_u64(cur));
    _motorRotationCounter = static_cast<size_t>(get_u64(cur));
    (void)get_u8(cur);  // reserved

    // _diskImage pointer is intentionally not restored — disk image identity
    // is session-scoped per TDD §12.2; the loader layer (Emulator::LoadDisk)
    // re-establishes the pointer from coreState.diskImages[_driveID] before
    // any FDD operation. _context, _readDataBit/_writeDataBit/_step, and
    // sync counters (_lastFrame/_lastTime) are host-side / transient.
}

/// endregion </TTDSerializable>