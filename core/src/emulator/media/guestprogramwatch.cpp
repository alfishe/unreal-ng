#include "stdafx.h"

#include "guestprogramwatch.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace
{
constexpr size_t kMaxDirectories = 4096;
constexpr size_t kMaxProgramFiles = 65536;
constexpr unsigned kMaxDepth = 12;
/// The same file read again within this time is the same load (a loader peeks at a file, then reads it)
constexpr std::chrono::seconds kRepeatGap{3};

std::string LowerExtension(const std::string& name)
{
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos)
        return {};
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}
}  // namespace

bool GuestProgramWatch::IsProgramName(const std::string& name)
{
    static const char* const kExtensions[] = {"nex", "snx", "sna", "z80", "szx", "tap", "tzx", "trd", "scl", "sp"};
    const std::string ext = LowerExtension(name);
    return std::any_of(std::begin(kExtensions), std::end(kExtensions), [&ext](const char* known) { return ext == known; });
}

void GuestProgramWatch::Forget(const std::string& slot)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto found = _slots.find(slot);
    if (found != _slots.end())
    {
        found->second.indexed = false;
        found->second.run = 0;
        found->second.nextLba = UINT64_MAX;
    }
}

void GuestProgramWatch::Reset(const std::string& slot)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _slots.erase(slot);
}

void GuestProgramWatch::BuildIndex(SlotState& state, IBlockDevice& below)
{
    state.files.clear();
    state.extents.clear();
    state.indexed = true;
    FatVolumeReader reader;
    if (!reader.Open(below))
        return;
    struct Pending
    {
        uint32_t cluster;
        std::string path;
        unsigned depth;
    };
    std::vector<Pending> pending{{0, std::string(), 0}};
    size_t directories = 0;
    while (!pending.empty() && directories < kMaxDirectories && state.files.size() < kMaxProgramFiles)
    {
        const Pending directory = pending.back();
        pending.pop_back();
        directories++;
        std::vector<FatDirEntryInfo> entries;
        if (!reader.ListDirectory(directory.cluster, entries))
            continue;
        for (const FatDirEntryInfo& entry : entries)
        {
            const std::string path = directory.path + "/" + entry.name;
            if (entry.isDirectory)
            {
                if (directory.depth < kMaxDepth && entry.firstCluster >= 2)
                    pending.push_back({entry.firstCluster, path, directory.depth + 1});
                continue;
            }
            if (entry.size == 0 || entry.size > kMaxFileBytes || !IsProgramName(entry.name))
                continue;
            std::vector<FatChainExtent> chain;
            if (!reader.ChainExtents(entry.firstCluster, entry.size, chain))
                continue;
            const size_t index = state.files.size();
            state.files.push_back({path, entry.size});
            for (const FatChainExtent& extent : chain)
                if (extent.sectors > 0)
                    state.extents.push_back({extent.lba, extent.lba + extent.sectors - 1, index});
        }
    }
    std::sort(state.extents.begin(), state.extents.end(),
              [](const Extent& a, const Extent& b) { return a.first < b.first; });
}

void GuestProgramWatch::OnSectorRead(const std::string& slot, uint64_t lba, IBlockDevice& below)
{
    std::unique_ptr<GuestProgramPayload> payload;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        SlotState& state = _slots[slot];
        if (lba == state.nextLba)
            state.run++;
        else
        {
            state.run = 1;
            state.runStart = lba;
        }
        state.nextLba = lba + 1;
        if (state.run < kRunSectors)
            return;
        if (!state.indexed)
            BuildIndex(state, below);
        auto after = std::upper_bound(state.extents.begin(), state.extents.end(), lba,
                                      [](uint64_t value, const Extent& extent) { return value < extent.first; });
        if (after == state.extents.begin())
            return;
        const Extent& extent = *(after - 1);
        if (lba > extent.last)
            return;
        // Sectors of this file in the run: a read that comes from the FAT or from the file before this one
        // does not count, so reading files one after another still names each
        const uint64_t inFile = lba - std::max(extent.first, state.runStart) + 1;
        if (inFile < kRunSectors)
            return;
        const auto now = std::chrono::steady_clock::now();
        const bool again = extent.file == state.lastReported;
        if (again && !(inFile == kRunSectors && now - state.lastReportedAt >= kRepeatGap))
            return;
        FatVolumeReader reader;
        std::vector<uint8_t> bytes;
        const File& file = state.files[extent.file];
        if (!reader.Open(below) || !reader.ReadFile(file.path, bytes) || bytes.empty())
            return;
        state.lastReported = extent.file;
        state.lastReportedAt = now;
        payload = std::make_unique<GuestProgramPayload>(std::string(), slot, file.path, std::move(bytes));
    }
    if (_context)
        payload->emulatorId = _context->emulatorId;
    MessageCenter::DefaultMessageCenter().Post(NC_GUEST_PROGRAM, payload.release(), true);
}
