#include "fdd.h"

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