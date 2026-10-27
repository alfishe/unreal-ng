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

namespace
{
/// Reads from the stack under the tap, writes through the tap
class ToolEdit : public IBlockDevice
{
public:
    ToolEdit(IBlockDevice& reads, IBlockDevice& writes) : _reads(reads), _writes(writes) {}

    uint64_t SectorCount() const override { return _reads.SectorCount(); }
    bool ReadSector(uint64_t lba, uint8_t* dst) override { return _reads.ReadSector(lba, dst); }
    bool WriteSector(uint64_t lba, const uint8_t* src) override { return _writes.WriteSector(lba, src); }
    bool IsWritable() const override { return _writes.IsWritable(); }
    std::optional<BlockGeometry> NativeGeometry() const override { return _reads.NativeGeometry(); }
    std::string Describe() const override { return _reads.Describe(); }
    uint64_t ContentId() const override { return _reads.ContentId(); }

private:
    IBlockDevice& _reads;
    IBlockDevice& _writes;
};
}  // namespace

std::unique_ptr<IBlockDevice> ToolEditDevice(Medium& medium)
{
    if (medium.Kind() != MediaKind::Block || !medium.Block())
        return nullptr;
    IBlockDevice& top = *medium.Block();
    return std::make_unique<ToolEdit>(medium.ReadTap() ? medium.ReadTap()->Base() : top, top);
}
