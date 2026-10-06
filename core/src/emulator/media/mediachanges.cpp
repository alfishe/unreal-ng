#include "stdafx.h"

#include "mediachanges.h"

#include <array>
#include <map>
#include <memory>

#include "emulator/io/storage/compose/changeattributor.h"
#include "emulator/io/storage/compose/composedlayout.h"
#include "emulator/io/storage/partitioneddisk.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/io/storage/subrangedevice.h"
#include "emulator/media/compositemediumfactory.h"
#include "emulator/media/medium.h"

MediaResult ListMediumChanges(Medium& medium, MediumChanges& out)
{
    out = MediumChanges();
    if (!medium.Block() || medium.Kind() != MediaKind::Block)
        return MediaResult::Fail(MediaError::NotSupported, "the medium is not a disk or a card");
    SessionWriteMap* session = medium.Session();
    if (!session)
        return MediaResult::Fail(MediaError::NotSupported, std::string("the guest's writes are listed for media with session writes (this one is ") +
                                                               AccessModeName(medium.Access()) + ")");
    const CompositeInfo* composite = medium.Composite();
    auto add = [&](const ChangeSet& part, const std::string& prefix, size_t firstLayer, size_t layerCount) {
        for (const FileChange& change : part.changes)
        {
            MediumChange c;
            c.op = FileChange::OpName(change.op);
            c.path = prefix + change.path;
            if (change.op == FileChange::Op::Rename)
                c.oldPath = prefix + change.oldPath;
            const size_t index = firstLayer + static_cast<size_t>(change.layer);
            if (change.layer >= 0 && static_cast<size_t>(change.layer) < layerCount && composite && index < composite->layers.size())
                c.layer = composite->layers[index].name;
            c.sizeBefore = change.sizeBefore;
            c.sizeAfter = change.sizeAfter;
            out.changes.push_back(std::move(c));
        }
    };

    ChangeSet set;
    std::string error;
    bool ok = true;
    {
        if (auto* disk = dynamic_cast<PartitionedDisk*>(&session->Base()))
        {
            // Each FAT partition on its own: windows of the disk before and after the writes, the changes shifted
            std::shared_ptr<IBlockDevice> before(&session->Base(), [](IBlockDevice*) {});
            std::shared_ptr<IBlockDevice> after(session, [](IBlockDevice*) {});
            const auto& all = session->Changes();
            for (const uint64_t table : disk->TableSectors())
                if (all.count(table))
                    set.warnings.push_back("the partition table changed (LBA " + std::to_string(table) + ")");
            for (size_t i = 0; i < disk->Parts().size(); i++)
            {
                const PartitionedDisk::Part& p = disk->Parts()[i];
                std::map<uint64_t, std::array<uint8_t, IBlockDevice::kSectorSize>> shifted;
                for (auto it = all.lower_bound(p.start); it != all.end() && it->first < p.start + p.sectors; ++it)
                    shifted.emplace(it->first - p.start, it->second);
                if (shifted.empty())
                    continue;
                SubRangeDevice windowBefore(before, p.start, p.sectors);
                SubRangeDevice windowAfter(after, p.start, p.sectors);
                OffsetLayout layout(p.layout, p.layoutOffset);
                ChangeSet part;
                std::string why;
                if (!ChangeAttributor::Attribute(windowBefore, windowAfter, shifted, p.layout ? &layout : nullptr, part, &why))
                {
                    set.warnings.push_back(p.name + ": " + why);
                    continue;
                }
                const bool hasInfo = composite && i < composite->partitions.size();
                add(part, p.name + ":", hasInfo ? composite->partitions[i].firstLayer : 0, hasInfo ? composite->partitions[i].layerCount : 0);
                for (const std::string& w : part.warnings)
                    set.warnings.push_back(p.name + ": " + w);
                set.changedSectors += part.changedSectors;
                set.directoriesRead += part.directoriesRead;
                set.fullScan = set.fullScan || part.fullScan;
            }
        }
        else
        {
            const auto* layout = dynamic_cast<const IComposedLayout*>(&session->Base());
            ok = ChangeAttributor::Attribute(session->Base(), *session, session->Changes(), layout, set, &error);
            if (ok)
                add(set, "", 0, composite ? composite->layers.size() : 0);
        }
    }
    if (!ok)
        return MediaResult::Fail(MediaError::NotSupported, error);
    out.warnings.insert(out.warnings.end(), set.warnings.begin(), set.warnings.end());
    out.changedSectors = set.changedSectors;
    out.directoriesRead = set.directoriesRead;
    out.fullScan = set.fullScan;
    return MediaResult::Success();
}
