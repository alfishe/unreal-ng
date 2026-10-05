#include "stdafx.h"

#include "sourcepool.h"

#include <algorithm>
#include <cstring>

#include "emulator/io/storage/iblockdevice.h"

uint32_t SourcePool::AddHostFile(const std::filesystem::path& hostPath, uint64_t size)
{
    _hostFiles.push_back(HostFile{hostPath, size, false});
    return static_cast<uint32_t>(_hostFiles.size() - 1);
}

uint16_t SourcePool::AddDevice(std::shared_ptr<IBlockDevice> device)
{
    _devices.push_back(std::move(device));
    return static_cast<uint16_t>(_devices.size() - 1);
}

size_t SourcePool::ReadHost(uint32_t file, uint64_t offset, uint8_t* dst, size_t wanted)
{
    HostFile& host = _hostFiles[file];

    // A small LRU of open host streams
    auto it = std::find_if(_open.begin(), _open.end(), [file](const OpenFile& f) { return f.file == file; });
    if (it == _open.end())
    {
        _open.push_front(OpenFile{file, std::ifstream(host.path, std::ios::binary)});
        if (_open.size() > kMaxOpenFiles)
            _open.pop_back();
        it = _open.begin();
    }
    else if (it != _open.begin())
    {
        _open.splice(_open.begin(), _open, it);
        it = _open.begin();
    }

    std::ifstream& in = it->stream;
    size_t got = 0;
    if (in.is_open())
    {
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(wanted));
        got = static_cast<size_t>(in.gcount());
        in.clear();
    }
    if (got < wanted && !host.warned)
    {
        host.warned = true;
        const auto display = host.path.u8string();
        _warnings.push_back(std::string(display.begin(), display.end()) +
                            ": shorter than when the folder was scanned, or gone; reads as zeros");
    }
    return got;
}
