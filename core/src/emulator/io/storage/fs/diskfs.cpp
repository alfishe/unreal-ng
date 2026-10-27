#include "emulator/io/storage/fs/diskfs.h"

#include "emulator/io/storage/iblockdevice.h"
#include "emulator/io/storage/mediareadtap.h"
#include "emulator/media/medium.h"
#include "unrealdisk/diskimage.h"

std::unique_ptr<DiskFs> OpenDiskFs(Medium& medium)
{
    if (unrealdisk::DiskImage* floppy = medium.Floppy())
        return std::make_unique<DiskFs>(*floppy);
    if (medium.Kind() != MediaKind::Block || !medium.Block())
        return nullptr;
    // Under the read tap: a host read is no guest read, time travel keeps nothing of it
    IBlockDevice& device = medium.ReadTap() ? medium.ReadTap()->Base() : *medium.Block();
    return std::make_unique<DiskFs>(device, medium.Options().codePage.value_or(CodePage::Cp866));
}
