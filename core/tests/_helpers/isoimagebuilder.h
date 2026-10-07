#pragma once

/// @file isoimagebuilder.h
/// @brief ISO 9660 images for reader tests, written here independently of
/// IsoSynthVolume (the production writer), so Iso9660Reader is checked against
/// something it does not share code with. Deliberately plain: names are given
/// (ISO names already in d-characters), files are laid out in the order given,
/// every directory is written breadth first, path tables only for the PVD.
///
///   IsoImageBuilder iso;
///   iso.joliet = true;
///   iso.Add("/README.TXT", "hello", "ReadMe.txt");      // ISO name, Joliet name
///   iso.Add("/GAMES/ELITE.TRD", bytes);
///   std::vector<uint8_t> image = iso.Build();
///   BytesDisk disk(image);                              // an IBlockDevice over it

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "emulator/io/storage/iblockdevice.h"

/// An IBlockDevice over bytes in memory (read-only), 512-byte sectors
class BytesDisk : public IBlockDevice
{
public:
    explicit BytesDisk(std::vector<uint8_t> bytes) : _bytes(std::move(bytes)) { _bytes.resize((_bytes.size() + 511) / 512 * 512); }
    uint64_t SectorCount() const override { return _bytes.size() / 512; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override
    {
        if (lba >= SectorCount())
            return false;
        std::memcpy(dst, _bytes.data() + lba * 512, 512);
        return true;
    }
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::string Describe() const override { return "bytes"; }
    uint64_t ContentId() const override { return 0xB17E5ULL; }

private:
    std::vector<uint8_t> _bytes;
};

class IsoImageBuilder
{
public:
    bool joliet = false;
    int64_t time = 1767268800;  ///< every date (2026-01-01 12:00:00 UTC)

    /// A file at an ISO path ("/DIR/NAME.EXT"); `jolietName` is its Joliet leaf name (default: the ISO leaf)
    void Add(const std::string& isoPath, const std::string& data, const std::string& jolietName = {}, bool hidden = false)
    {
        File f;
        f.path = isoPath;
        f.data = data;
        f.joliet = jolietName.empty() ? Leaf(isoPath) : jolietName;
        f.hidden = hidden;
        _files.push_back(f);
        // Every parent directory exists
        std::string parent = Parent(isoPath);
        while (parent != "/" && !_dirs.count(parent))
        {
            _dirs[parent] = Leaf(parent);
            parent = Parent(parent);
        }
    }

    /// Name a directory's Joliet leaf (default: its ISO leaf)
    void DirectoryJoliet(const std::string& isoPath, const std::string& jolietName) { _dirs[isoPath] = jolietName; }

    std::vector<uint8_t> Build()
    {
        // Directories: the root first, then by path (breadth first enough for these tests: shorter paths first)
        std::vector<std::string> dirs{"/"};
        for (const auto& [path, name] : _dirs)
            dirs.push_back(path);
        std::stable_sort(dirs.begin() + 1, dirs.end(), [](const std::string& a, const std::string& b) {
            return std::count(a.begin(), a.end(), '/') < std::count(b.begin(), b.end(), '/');
        });

        uint32_t block = 16 + (joliet ? 3 : 2);  // PVD, (SVD), terminator
        const uint32_t pathTableL = block++;
        // Directory extents: two passes (sizes first), each directory of each tree its own blocks
        std::map<std::string, uint32_t> isoBlock, isoSize, jolietBlock, jolietSize;
        for (int tree = 0; tree < (joliet ? 2 : 1); tree++)
        {
            for (const std::string& dir : dirs)
            {
                const uint32_t bytes = DirectoryBytes(dir, tree == 1);
                (tree ? jolietBlock : isoBlock)[dir] = block;
                (tree ? jolietSize : isoSize)[dir] = bytes;
                block += bytes / 2048;
            }
        }
        std::vector<uint32_t> fileBlock(_files.size());
        for (size_t i = 0; i < _files.size(); i++)
        {
            fileBlock[i] = _files[i].data.empty() ? 0 : block;
            block += static_cast<uint32_t>((_files[i].data.size() + 2047) / 2048);
        }
        std::vector<uint8_t> iso(static_cast<size_t>(block) * 2048, 0);

        // Volume descriptors
        for (int tree = 0; tree < (joliet ? 2 : 1); tree++)
        {
            uint8_t* p = iso.data() + (16 + tree) * 2048;
            p[0] = tree ? 2 : 1;
            std::memcpy(p + 1, "CD001", 5);
            p[6] = 1;
            std::memset(p + 8, ' ', 64);
            std::memcpy(p + 40, "TESTISO", 7);
            if (tree)
                std::memcpy(p + 88, "%/E", 3);
            Both32(p + 80, block);
            Both16(p + 120, 1);
            Both16(p + 124, 1);
            Both16(p + 128, 2048);
            Both32(p + 132, 10);
            p[140] = static_cast<uint8_t>(pathTableL);
            const std::map<std::string, uint32_t>& b = tree ? jolietBlock : isoBlock;
            const std::map<std::string, uint32_t>& s = tree ? jolietSize : isoSize;
            Record(p + 156, b.at("/"), s.at("/"), 0x02, std::string(1, '\0'));
            p[881] = 1;
        }
        uint8_t* term = iso.data() + (16 + (joliet ? 2 : 1)) * 2048;
        term[0] = 255;
        std::memcpy(term + 1, "CD001", 5);
        term[6] = 1;
        // A path table with the root only (the reader does not use path tables)
        uint8_t* pt = iso.data() + static_cast<size_t>(pathTableL) * 2048;
        pt[0] = 1;
        Le32(pt + 2, isoBlock.at("/"));
        pt[6] = 1;

        // Directories
        for (int tree = 0; tree < (joliet ? 2 : 1); tree++)
        {
            const std::map<std::string, uint32_t>& b = tree ? jolietBlock : isoBlock;
            const std::map<std::string, uint32_t>& s = tree ? jolietSize : isoSize;
            for (const std::string& dir : dirs)
            {
                uint8_t* base = iso.data() + static_cast<size_t>(b.at(dir)) * 2048;
                uint32_t at = 0;
                auto put = [&](uint32_t extent, uint32_t size, uint8_t flags, const std::string& name) {
                    const uint32_t length = static_cast<uint32_t>(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0));
                    if (at % 2048 + length > 2048)
                        at = (at / 2048 + 1) * 2048;
                    Record(base + at, extent, size, flags, name);
                    at += length;
                };
                const std::string parent = dir == "/" ? "/" : Parent(dir);
                put(b.at(dir), s.at(dir), 0x02, std::string(1, '\0'));
                put(b.at(parent), s.at(parent), 0x02, std::string(1, '\1'));
                for (const auto& [path, jname] : _dirs)
                {
                    if (Parent(path) == dir)
                        put(b.at(path), s.at(path), 0x02, tree ? Ucs2(jname) : Leaf(path));
                }
                for (size_t i = 0; i < _files.size(); i++)
                {
                    if (Parent(_files[i].path) == dir)
                        put(fileBlock[i], static_cast<uint32_t>(_files[i].data.size()), _files[i].hidden ? 0x01 : 0x00,
                            tree ? Ucs2(_files[i].joliet + ";1") : Leaf(_files[i].path) + ";1");
                }
            }
        }
        for (size_t i = 0; i < _files.size(); i++)
            std::memcpy(iso.data() + static_cast<size_t>(fileBlock[i]) * 2048, _files[i].data.data(), _files[i].data.size());
        return iso;
    }

private:
    struct File
    {
        std::string path;
        std::string data;
        std::string joliet;
        bool hidden = false;
    };

    static std::string Leaf(const std::string& path) { return path.substr(path.rfind('/') + 1); }
    static std::string Parent(const std::string& path)
    {
        const size_t slash = path.rfind('/');
        return slash == 0 ? "/" : path.substr(0, slash);
    }
    static std::string Ucs2(const std::string& ascii)
    {
        std::string out;
        for (char c : ascii)
        {
            out.push_back('\0');
            out.push_back(c);
        }
        return out;
    }
    static void Both16(uint8_t* p, uint16_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 8);
        p[3] = static_cast<uint8_t>(v);
    }
    static void Both32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
        {
            p[i] = static_cast<uint8_t>(v >> (8 * i));
            p[7 - i] = static_cast<uint8_t>(v >> (8 * i));
        }
    }
    static void Le32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void Record(uint8_t* p, uint32_t extent, uint32_t size, uint8_t flags, const std::string& name) const
    {
        p[0] = static_cast<uint8_t>(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0));
        Both32(p + 2, extent);
        Both32(p + 10, size);
        // 2026-01-01 12:00:00 UTC (the default `time`)
        p[18] = 126;
        p[19] = 1;
        p[20] = 1;
        p[21] = 12;
        p[25] = flags;
        Both16(p + 28, 1);
        p[32] = static_cast<uint8_t>(name.size());
        std::memcpy(p + 33, name.data(), name.size());
    }
    uint32_t DirectoryBytes(const std::string& dir, bool jolietTree) const
    {
        uint32_t at = 68;
        auto place = [&at](size_t nameBytes) {
            const uint32_t length = static_cast<uint32_t>(33 + nameBytes + (nameBytes % 2 == 0 ? 1 : 0));
            if (at % 2048 + length > 2048)
                at = (at / 2048 + 1) * 2048;
            at += length;
        };
        for (const auto& [path, jname] : _dirs)
        {
            if (Parent(path) == dir)
                place(jolietTree ? jname.size() * 2 : Leaf(path).size());
        }
        for (const File& f : _files)
        {
            if (Parent(f.path) == dir)
                place(jolietTree ? (f.joliet.size() + 2) * 2 : Leaf(f.path).size() + 2);
        }
        return (at + 2047) / 2048 * 2048;
    }

    std::vector<File> _files;
    std::map<std::string, std::string> _dirs;  ///< ISO path -> Joliet leaf
};
