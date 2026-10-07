#include "stdafx.h"

#include "hostfolderfat.h"

#include "emulator/io/storage/compose/hostfoldersource.h"
#include "emulator/io/storage/compose/sourcepool.h"

std::unique_ptr<HostFolderFat> HostFolderFat::Build(const FolderSnapshot& snapshot, const FatVolumeOptions& options,
                                                    std::string* error, std::vector<std::string>* report)
{
    // One layer mounted at the root: the union is the layer itself
    auto pool = std::make_shared<SourcePool>();
    auto tree = std::make_shared<FileTree>();
    if (!HostFolderSource::Enumerate(snapshot, {}, *pool, *tree, report, error))
        return nullptr;

    const auto folder = snapshot.Root().hostPath.u8string();
    std::unique_ptr<HostFolderFat> volume(new HostFolderFat());
    if (!volume->Init(std::move(tree), std::move(pool), options, snapshot.Identity(),
                      std::string(folder.begin(), folder.end()), error, report))
        return nullptr;
    return volume;
}
