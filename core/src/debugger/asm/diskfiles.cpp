#include "diskfiles.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "3rdparty/unreal-asm/tools/common/diskfiles.h"
#include "loaders/disk/diskloaderoptions.h"
#include "unrealdisk/diskfs.h"
#include "unrealdisk/floppyformats.h"

namespace
{

/// Runs `work` where nothing else touches the disk: paused, stopped or between frames
bool Coherent(EmulatorContext* context, const std::function<void()>& work, std::string& error)
{
    if (context && context->pEmulator)
    {
        if (context->pEmulator->RunAtCoherentMoment(work, 2000) == Emulator::CoherentMoment::Busy)
        {
            error = "no coherent moment to reach the disk within 2 s (another client is stepping the machine)";
            return false;
        }
        return true;
    }
    work();
    return true;
}

bool Drive(EmulatorContext* context, uint8_t drive, FDD*& fdd, DiskImage*& image, std::string& error)
{
    const char letter = static_cast<char>('A' + drive);
    if (!context || drive > 3 || !context->coreState.diskDrives[drive])
    {
        error = std::string("no drive ") + letter;
        return false;
    }
    fdd = context->coreState.diskDrives[drive];
    image = fdd->getDiskImage();
    if (!image)
    {
        error = std::string("no disk in drive ") + letter;
        return false;
    }
    if (unrealdisk::DiskFs(*image).Volumes().empty())
    {
        error = std::string("the disk in drive ") + letter + " has no file system (TR-DOS, +3DOS, iS-DOS, FAT)";
        return false;
    }
    return true;
}
}  // namespace

bool ParseDiskFileRef(const std::string& text, DiskFileRef& out)
{
    if (text.size() < 8 || text.compare(0, 5, "disk:") != 0 || text[6] != '/')
        return false;
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(text[5])));
    if (letter < 'A' || letter > 'D')
        return false;
    std::string file = text.substr(7);
    const size_t dot = file.find_last_of('.');
    DiskFileRef ref;
    ref.drive = static_cast<uint8_t>(letter - 'A');
    if (dot != std::string::npos && dot + 2 == file.size())
    {
        ref.type = file[dot + 1];
        file.resize(dot);
    }
    while (!file.empty() && file.back() == ' ')
        file.pop_back();
    if (file.empty() || file.size() > 8)
        return false;
    ref.name = file;
    out = ref;
    return true;
}

bool IsDiskFileRef(const std::string& text)
{
    DiskFileRef ref;
    return ParseDiskFileRef(text, ref);
}

bool ReadDiskFiles(EmulatorContext* context, uint8_t drive, std::vector<unrealasm::containers::TrdosFile>& out, std::string& error)
{
    out.clear();
    bool ok = false;
    if (!Coherent(context, [&]() {
            FDD* fdd = nullptr;
            DiskImage* image = nullptr;
            if (!Drive(context, drive, fdd, image, error))
                return;
            ok = unrealasm::tools::ReadDiskFiles(*image, out, error);
        }, error) || !ok)
        return false;
    return true;
}

bool ReadDiskFile(EmulatorContext* context, const DiskFileRef& ref, unrealasm::containers::TrdosFile& out, std::string& error)
{
    std::vector<unrealasm::containers::TrdosFile> files;
    if (!ReadDiskFiles(context, ref.drive, files, error))
        return false;
    for (auto it = files.rbegin(); it != files.rend(); ++it)
        if (it->TrimmedName() == ref.name && (ref.type == 0 || it->type == ref.type))
        {
            out = *it;
            return true;
        }
    error = "no file " + ref.name + (ref.type ? std::string(".") + ref.type : std::string()) + " on the disk in drive " +
            static_cast<char>('A' + ref.drive);
    return false;
}

namespace
{
/// The write itself, at a coherent moment
bool WriteNow(EmulatorContext* context, uint8_t drive, const unrealasm::containers::TrdosFile& file, std::string& error)
{
    FDD* fdd = nullptr;
    DiskImage* image = nullptr;
    if (!Drive(context, drive, fdd, image, error))
        return false;
    if (fdd->isWriteProtect())
    {
        error = std::string("the disk in drive ") + static_cast<char>('A' + drive) + " is write-protected";
        return false;
    }
    return unrealasm::tools::PutDiskFile(*image, file, error);
}
}  // namespace

bool WriteDiskFile(EmulatorContext* context, const DiskFileRef& ref, uint16_t start, const std::vector<uint8_t>& bytes, std::string& error)
{
    if (ref.type == 0)
    {
        error = "a file written to a disk needs its type letter (disk:A/NAME.T)";
        return false;
    }
    if (bytes.size() > 0xFFFF)
    {
        error = "a TR-DOS file holds at most 65535 bytes";
        return false;
    }
    bool ok = false;
    unrealasm::containers::TrdosFile file;
    file.name = ref.name;
    file.type = ref.type;
    file.start = start;
    file.length = static_cast<uint16_t>(bytes.size());
    file.data = bytes;
    const bool reached = Coherent(context, [&]() { ok = WriteNow(context, ref.drive, file, error); }, error);
    return reached && ok;
}

bool WriteDiskFileNow(EmulatorContext* context, uint8_t drive, const unrealasm::containers::TrdosFile& file, std::string& error)
{
    if (file.data.size() > 0xFFFF)
    {
        error = "a TR-DOS file holds at most 65535 bytes";
        return false;
    }
    return WriteNow(context, drive, file, error);
}

bool IsImageExtension(const std::string& extension)
{
    if (extension == "tap" || extension == "tzx")
        return true;
    if (extension.size() == 2 && extension[0] == '$')
        return false;   // a hobeta file is one file
    const std::vector<std::string> disks = unrealdisk::FloppyFormats::Extensions();
    return std::find(disks.begin(), disks.end(), extension) != disks.end();
}

bool ReadImageFiles(EmulatorContext* context, const std::string& name, const std::vector<uint8_t>& bytes,
                    std::vector<unrealasm::containers::TrdosFile>& out, std::string& error)
{
    out.clear();
    const size_t dot = name.find_last_of('.');
    std::string extension = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == "tap" || extension == "tzx")
        return unrealasm::containers::ReadTape(bytes, out, error);
    std::unique_ptr<DiskImage> disk;
    std::string format;
    const unrealdisk::FormatResult loaded = unrealdisk::FloppyFormats::LoadData(DiskLoaderOptions(context), bytes.data(), bytes.size(), name, disk, format);
    if (!loaded.Ok())
    {
        error = loaded.message;
        return false;
    }
    return unrealasm::tools::ReadDiskFiles(*disk, out, error);
}
