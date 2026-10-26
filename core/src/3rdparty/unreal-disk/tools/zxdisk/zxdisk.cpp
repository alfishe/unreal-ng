// zxdisk - the files of any ZX Spectrum disk image through unreal-disk's DiskFs (unrealdisk/diskfs.h).
//
//   zxdisk volumes <image>                           the file systems on the disk: id, what it reads as, label, size, free
//   zxdisk ls <image> [dir] [-r] [--volume id]       a directory's entries (-r: the whole tree under it)
//   zxdisk cat <image> <file> [-o out] [--volume id] a file's bytes (to stdout without -o)
//   zxdisk put <image> <host-file> [name] [--start n] [-o out-image] [--volume id]
//                                                    a host file onto a TR-DOS (NAME.T) or +3DOS (NAME.TYP) floppy, an
//                                                    older file of that name deleted first; the image is written back
//                                                    (or to -o, its extension picking the format)
//
// <image>: a floppy in any format unreal-disk reads (TRD, SCL, FDI, UDI, TD0, DSK, MGT, IMG, HFE, SCP, ...) with
// TR-DOS, +3DOS, iS-DOS / TASiS or FAT12 (Sprinter DSS); else a raw hard disk or card image (512-byte sectors): MBR or
// superfloppy FAT partitions (Estex DSS, NedoOS), iS-DOS devices (the whole disk or at cylinder boundaries).
// `--offset n` skips n bytes of a hard disk image's header (HDF: 22 + the identify block).
//
// Exit code 0 on success, 1 on errors, 2 on bad usage.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "unrealdisk/blockdevice.h"
#include "unrealdisk/diskfs.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/floppyformats.h"

using namespace unrealdisk;

namespace
{
int Usage()
{
    std::cerr << "usage: zxdisk volumes <image>\n"
                 "       zxdisk ls <image> [dir] [-r] [--volume id]\n"
                 "       zxdisk cat <image> <file> [-o out] [--volume id]\n"
                 "       zxdisk put <image> <host-file> [name] [--start n] [-o out-image] [--volume id]\n"
                 "       (hard disk images: --offset n skips a header)\n";
    return 2;
}

/// A raw hard disk image, read only: 512-byte sectors from `offset` bytes into the file
class FileBlockDevice : public BlockDevice
{
public:
    FileBlockDevice(const std::string& path, uint64_t offset) : _in(path, std::ios::binary), _offset(offset), _path(path)
    {
        _in.seekg(0, std::ios::end);
        const auto size = static_cast<uint64_t>(_in.tellg());
        _count = size > offset ? (size - offset) / kSectorSize : 0;
    }

    bool IsOpen() const { return _in.good() && _count > 0; }
    uint64_t SectorCount() const override { return _count; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override
    {
        if (lba >= _count)
            return false;
        _in.clear();
        _in.seekg(static_cast<std::streamoff>(_offset + lba * kSectorSize));
        _in.read(reinterpret_cast<char*>(dst), kSectorSize);
        return _in.gcount() == static_cast<std::streamsize>(kSectorSize);
    }
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::string Describe() const override { return _path; }
    uint64_t ContentId() const override { return 0; }

private:
    std::ifstream _in;
    uint64_t _offset = 0;
    uint64_t _count = 0;
    std::string _path;
};

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::string Extension(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : Lower(path.substr(dot + 1));
}

/// The image opened: a floppy when unreal-disk reads it as one, else a raw block image
struct Opened
{
    std::unique_ptr<DiskImage> floppy;
    std::unique_ptr<FileBlockDevice> device;
    std::unique_ptr<DiskFs> fs;
};

bool Open(const std::string& path, uint64_t offset, Opened& out, std::string& error)
{
    std::string format;
    if (offset == 0)
    {
        const FormatResult loaded = FloppyFormats::Load({}, path, out.floppy, format);
        if (loaded.Ok() && out.floppy)
        {
            out.fs = std::make_unique<DiskFs>(*out.floppy);
            return true;
        }
        out.floppy.reset();
        // A floppy format that did not load is an error; anything else may be a hard disk
        const std::vector<std::string> floppies = FloppyFormats::Extensions();
        const std::string ext = Extension(path);
        if (ext != "img" && std::find(floppies.begin(), floppies.end(), ext) != floppies.end())
        {
            error = loaded.message.empty() ? "cannot read " + path : loaded.message;
            return false;
        }
    }
    out.device = std::make_unique<FileBlockDevice>(path, offset);
    if (!out.device->IsOpen())
    {
        error = "cannot read " + path;
        return false;
    }
    out.fs = std::make_unique<DiskFs>(*out.device);
    return true;
}

bool ReadHostFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::string BaseName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

int Fail(const std::string& message)
{
    std::cerr << "zxdisk: " << message << "\n";
    return 1;
}
}  // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    std::string volume;
    std::string output;
    uint64_t offset = 0;
    uint32_t start = 0;
    bool recursive = false;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto value = [&](std::string& to) {
            if (i + 1 >= argc)
                return false;
            to = argv[++i];
            return true;
        };
        std::string number;
        if (a == "-r" || a == "--recursive")
            recursive = true;
        else if (a == "--volume")
        {
            if (!value(volume))
                return Usage();
        }
        else if (a == "-o")
        {
            if (!value(output))
                return Usage();
        }
        else if (a == "--offset" || a == "--start")
        {
            if (!value(number))
                return Usage();
            const unsigned long long n = std::strtoull(number.c_str(), nullptr, 0);
            if (a == "--offset")
                offset = n;
            else
                start = static_cast<uint32_t>(n);
        }
        else if (a.size() > 1 && a[0] == '-')
            return Usage();
        else
            args.push_back(a);
    }
    if (args.size() < 2)
        return Usage();
    const std::string& verb = args[0];
    Opened image;
    std::string error;
    if (!Open(args[1], offset, image, error))
        return Fail(error);
    DiskFs& fs = *image.fs;

    if (verb == "volumes")
    {
        if (fs.Volumes().empty())
            return Fail("no file system found on " + args[1]);
        for (const DiskFsVolume& v : fs.Volumes())
        {
            std::cout << v.id << "  " << v.system << "  \"" << v.label << "\"  " << v.bytes << " bytes";
            if (v.freeBytes >= 0)
                std::cout << ", " << v.freeBytes << " free";
            if (v.offset)
                std::cout << ", at byte " << v.offset;
            std::cout << (v.writable ? "" : ", read only") << "\n";
        }
        for (const std::string& note : fs.Notes())
            std::cout << "note: " << note << "\n";
        return 0;
    }
    if (verb == "ls")
    {
        std::vector<DiskFsEntry> entries;
        if (!fs.List(volume, args.size() > 2 ? args[2] : std::string(), recursive, entries, error))
            return Fail(error);
        for (const DiskFsEntry& e : entries)
        {
            char line[64];
            if (e.directory)
                std::snprintf(line, sizeof(line), "%10s  %-5s  ", "<dir>", "");
            else if (e.start)
                std::snprintf(line, sizeof(line), "%10llu  #%04X  ", static_cast<unsigned long long>(e.size), e.start);
            else
                std::snprintf(line, sizeof(line), "%10llu  %-5s  ", static_cast<unsigned long long>(e.size), "");
            std::cout << line << (e.attributes.empty() ? "" : "[" + e.attributes + "]  ") << e.path << (e.directory ? "/" : "")
                      << (e.error.empty() ? "" : "  error: " + e.error) << "\n";
        }
        for (const std::string& note : fs.Notes())
            std::cout << "note: " << note << "\n";
        return 0;
    }
    if (verb == "cat")
    {
        if (args.size() < 3)
            return Usage();
        std::vector<uint8_t> data;
        if (!fs.Read(volume, args[2], data, error))
            return Fail(error);
        if (output.empty())
        {
            std::cout.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            return std::cout ? 0 : 1;
        }
        std::ofstream out(output, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return out ? 0 : Fail("cannot write " + output);
    }
    if (verb == "put")
    {
        if (args.size() < 3)
            return Usage();
        if (!image.floppy)
            return Fail("writing works on TR-DOS and +3DOS floppies only");
        std::vector<uint8_t> data;
        if (!ReadHostFile(args[2], data))
            return Fail("cannot read " + args[2]);
        DiskFsWriteOptions options;
        options.start = static_cast<uint16_t>(start);
        const std::string name = args.size() > 3 ? args[3] : BaseName(args[2]);
        if (!fs.Write(volume, name, data, options, error))
            return Fail(error);
        const std::string target = output.empty() ? args[1] : output;
        const FloppySaveResult saved = FloppyFormats::Save({}, *image.floppy, target, false);
        if (!saved.saved)
            return Fail("cannot save " + target + (saved.reason.empty() ? std::string() : ": " + saved.reason));
        return 0;
    }
    return Usage();
}
