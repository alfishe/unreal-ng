// zxdisk - the files of any ZX Spectrum disk image through unreal-disk's DiskFs (unrealdisk/diskfs.h).
//
//   zxdisk volumes <image>                           the file systems on the disk: id, what it reads as, label, size,
//   free zxdisk ls <image> [dir] [-r] [--volume id]       a directory's entries (-r: the whole tree under it) zxdisk
//   cat <image> <file> [-o out] [--volume id] a file's bytes (to stdout without -o) zxdisk put <image> <host-file>
//   [name] [--start n] [-o out-image] [--volume id]
//                                                    a host file onto a floppy (TR-DOS NAME.T, +3DOS NAME.TYP, FAT and
//                                                    iS-DOS dir/name.ext), an older file of that name replaced
//   zxdisk rm <image> <file> [-r] [-o out-image] [--volume id]      delete a file or an empty directory (FAT, iS-DOS;
//                                                    -r: a directory with everything in it)
//   zxdisk mv <image> <file> <new-name> [-o out-image] [--volume id] rename a file (FAT, iS-DOS: move it or a
//   directory) zxdisk mkdir <image> <dir> [-p] [-o out-image] [--volume id]    a new directory (FAT, iS-DOS; -p: the
//   missing ones
//                                                    on the way too)
//   zxdisk attrib <image> <file> [+R-H|RA] [--time t] [-o out-image] [--volume id]
//                                                    set or clear attribute letters (FAT RHSA, iS-DOS RWHD, +3DOS RSA;
//                                                    "+R-H" changes these, "RA" sets exactly these) and the file's time
//                                                    (FAT, iS-DOS; "YYYY-MM-DD HH:MM[:SS]" of the DOS clock, "now")
//   zxdisk label <image> <name> [-o out-image] [--volume id]        the FAT volume label ("" removes it), the TR-DOS
//                                                    disk title, the iS-DOS device name
//   put, rm, mv, mkdir, attrib and label write the image back (or to -o, a floppy's extension picking the format); a
//   hard disk image is changed in place, or copied to -o first and the copy changed
//
// <image>: a floppy in any format unreal-disk reads (TRD, SCL, FDI, UDI, TD0, DSK, MGT, IMG, HFE, SCP, ...) with
// TR-DOS, +3DOS, iS-DOS / TASiS, ATM CP/M, MB-02, MDOS or FAT12 (Sprinter DSS); else a raw hard disk or card image
// (512-byte sectors): MBR or superfloppy FAT partitions (Estex DSS, NedoOS), ATM CP/M partitions (the configuration
// record in sector 0 and the .CDS descriptors), iS-DOS devices (the whole disk or at cylinder boundaries), MDOS3
// virtual disks (an .md3 container, an MBR partition of type #49, slot files on a FAT16 card). ATM CP/M and MB-02 are
// read only.
// `--offset n` skips n bytes of a hard disk image's header (HDF: 22 + the identify block).
//
// Exit code 0 on success, 1 on errors, 2 on bad usage.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
                 "       zxdisk rm <image> <file> [-r] [-o out-image] [--volume id]\n"
                 "       zxdisk mv <image> <file> <new-name> [-o out-image] [--volume id]\n"
                 "       zxdisk mkdir <image> <dir> [-p] [-o out-image] [--volume id]\n"
                 "       zxdisk attrib <image> <file> [+R-H|RA] [--time t] [-o out-image] [--volume id]\n"
                 "       zxdisk label <image> <name> [-o out-image] [--volume id]\n"
                 "       (hard disk images: --offset n skips a header)\n";
    return 2;
}

/// A raw hard disk image: 512-byte sectors from `offset` bytes into the file, written in place when `writable`
class FileBlockDevice : public BlockDevice
{
public:
    FileBlockDevice(const std::string& path, uint64_t offset, bool writable)
        : _in(path, writable ? std::ios::binary | std::ios::in | std::ios::out : std::ios::binary | std::ios::in),
          _offset(offset),
          _path(path),
          _writable(writable)
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
    bool WriteSector(uint64_t lba, const uint8_t* src) override
    {
        if (!_writable || lba >= _count)
            return false;
        _in.clear();
        _in.seekp(static_cast<std::streamoff>(_offset + lba * kSectorSize));
        _in.write(reinterpret_cast<const char*>(src), kSectorSize);
        return static_cast<bool>(_in);
    }
    bool IsWritable() const override { return _writable; }
    bool Flush()
    {
        _in.flush();
        return static_cast<bool>(_in);
    }
    std::string Describe() const override { return _path; }
    uint64_t ContentId() const override { return 0; }

private:
    std::fstream _in;
    uint64_t _offset = 0;
    uint64_t _count = 0;
    std::string _path;
    bool _writable = false;
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

/// `edit`: the hard disk image to change, `path` itself or the -o copy of it (empty: read only)
bool Open(const std::string& path, uint64_t offset, const std::string& edit, Opened& out, std::string& error)
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
    if (!edit.empty() && edit != path)
    {
        std::ifstream in(path, std::ios::binary);
        std::ofstream copy(edit, std::ios::binary | std::ios::trunc);
        if (!in || !copy || !(copy << in.rdbuf()) || !copy.flush())
        {
            error = "cannot copy " + path + " to " + edit;
            return false;
        }
    }
    const std::string& opened = edit.empty() ? path : edit;
    out.device = std::make_unique<FileBlockDevice>(opened, offset, !edit.empty());
    if (!out.device->IsOpen())
    {
        error = "cannot " + std::string(edit.empty() ? "read " : "open for writing ") + opened;
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

/// Seconds since 1970 of a civil date (no time zone)
int64_t CivilSeconds(int year, int month, int day, int hour, int minute, int second)
{
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yoe = year - era * 400;
    const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return ((era * 146097 + doe - 719468) * 24 + hour) * 3600 + minute * 60 + second;
}

/// The local clock as UTC seconds of the DOS wall clock (what a DOS machine would stamp)
int64_t Now()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return CivilSeconds(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
}

/// "YYYY-MM-DD HH:MM[:SS]" (or 'T' between) as UTC seconds of the DOS wall clock, "now" the local clock, else seconds
bool ParseTime(const std::string& text, int64_t& out)
{
    if (text == "now")
    {
        out = Now();
        return true;
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    char separator = 0;
    const int fields = std::sscanf(text.c_str(), "%d-%d-%d%c%d:%d:%d", &year, &month, &day, &separator, &hour, &minute, &second);
    if (fields >= 6 && (separator == ' ' || separator == 'T'))
    {
        if (year < 1980 || year > 2107 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
            second > 59 || hour < 0 || minute < 0 || second < 0)
            return false;
        out = CivilSeconds(year, month, day, hour, minute, second);
        return true;
    }
    char* end = nullptr;
    const long long seconds = std::strtoll(text.c_str(), &end, 10);
    if (text.empty() || *end)
        return false;
    out = seconds;
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
    bool parents = false;
    std::string time;
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
        if (args.size() == 3 && args[0] == "attrib" && a.size() > 1 && a[0] == '-' && a[1] != '-' && a != "-o")
            args.push_back(a);  // attrib's "-R-H"
        else if (a == "-r" || a == "--recursive")
            recursive = true;
        else if (a == "-p" || a == "--parents")
            parents = true;
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
        else if (a == "--time")
        {
            if (!value(time))
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
    const bool editing = verb == "put" || verb == "rm" || verb == "mv" || verb == "mkdir" || verb == "attrib" || verb == "label";
    Opened image;
    std::string error;
    // volumes: a hard disk image opened as zxdisk would change it, to say whether its volumes take writes
    bool opened = Open(args[1], offset, editing ? (output.empty() ? args[1] : output) : verb == "volumes" ? args[1] : std::string(), image, error);
    if (!opened && verb == "volumes")
        opened = Open(args[1], offset, std::string(), image, error);
    if (!opened)
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
    if (editing)
    {
        if (args.size() < (verb == "mv" ? 4u : 3u))
            return Usage();
        if (verb == "attrib")
        {
            if (args.size() < 4 && time.empty())
                return Fail("name the attributes (+R-H, RA) or --time");
            int64_t mtime = 0;
            if (!time.empty() && !ParseTime(time, mtime))
                return Fail("--time takes \"YYYY-MM-DD HH:MM[:SS]\" (1980..2107), \"now\" or seconds, not \"" + time + "\"");
            if (args.size() > 3 && !fs.SetAttributes(volume, args[2], args[3], error))
                return Fail(error);
            if (!time.empty() && !fs.SetTime(volume, args[2], mtime, error))
                return Fail(error);
        }
        else if (verb == "label")
        {
            if (!fs.SetLabel(volume, args[2], Now(), error))
                return Fail(error);
        }
        else if (verb == "put")
        {
            std::vector<uint8_t> data;
            if (!ReadHostFile(args[2], data))
                return Fail("cannot read " + args[2]);
            DiskFsWriteOptions options;
            options.start = static_cast<uint16_t>(start);
            options.mtime = Now();
            const std::string name = args.size() > 3 ? args[3] : BaseName(args[2]);
            if (!fs.Write(volume, name, data, options, error))
                return Fail(error);
        }
        else if (verb == "rm" ? !fs.Delete(volume, args[2], error, recursive)
                 : verb == "mkdir" ? !fs.MakeDirectory(volume, args[2], parents, Now(), error)
                                   : !fs.Rename(volume, args[2], args[3], error))
            return Fail(error);
        const std::string target = output.empty() ? args[1] : output;
        if (!image.floppy)
            return image.device->Flush() ? 0 : Fail("cannot write " + target);
        const FloppySaveResult saved = FloppyFormats::Save({}, *image.floppy, target, false);
        if (!saved.saved)
            return Fail("cannot save " + target + (saved.reason.empty() ? std::string() : ": " + saved.reason));
        return 0;
    }
    return Usage();
}
