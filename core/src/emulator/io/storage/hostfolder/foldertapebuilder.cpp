#include "stdafx.h"

#include "foldertapebuilder.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

#include "emulator/io/storage/hostfolder/folderdiskbuilder.h"
#include "emulator/io/storage/hostfolder/foldermanifest.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/tape/tapecatalog.h"
#include "loaders/disk/loader_hobeta.h"
#include "loaders/tape/loader_tape.h"
#include "loaders/tape/loader_tzx.h"

namespace
{
    constexpr size_t kScreenBytes = 6912;
    constexpr uint16_t kCodeStart = 32768;
    constexpr uint16_t kScreenStart = 16384;
    constexpr uint16_t kNoAutorun = 32768;
    constexpr size_t kTzxHeaderSize = 10;  ///< "ZXTape!" 0x1A major minor

    // ROM save timings (T-states at 3.5 MHz)
    constexpr uint64_t kPilotPulse = 2168;
    constexpr uint64_t kHeaderPilotPulses = 8063;
    constexpr uint64_t kDataPilotPulses = 3223;
    constexpr uint64_t kSyncPulses = 667 + 735;
    constexpr uint64_t kZeroBit = 2 * 855;
    constexpr uint64_t kOneBit = 2 * 1710;
    constexpr uint64_t kTStatesPerMs = 3500;

    enum HeaderType : uint8_t
    {
        Program = 0,
        NumberArray = 1,
        Bytes = 3
    };

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::pair<std::string, std::string> SplitName(const std::string& name)
    {
        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot == 0)
            return {name, ""};
        return {name.substr(0, dot), name.substr(dot + 1)};
    }

    bool ReadHostFile(const std::filesystem::path& path, std::vector<uint8_t>& data)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return !in.bad();
    }

    void PutU16(std::vector<uint8_t>& out, uint32_t value)
    {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    /// A block as the ROM saves it, with the duration of its signal
    struct Chunk
    {
        std::vector<uint8_t> tzx;  ///< TZX blocks, no file header
        uint64_t ms = 0;
    };

    /// One #10 block (flag + payload + checksum as saved), then `pauseMs` of
    /// silence. Its duration at ROM timings: pilot, sync, two pulses per bit
    void AddRawBlock(Chunk& chunk, const std::vector<uint8_t>& block, uint16_t pauseMs)
    {
        chunk.tzx.push_back(0x10);
        PutU16(chunk.tzx, pauseMs);
        PutU16(chunk.tzx, static_cast<uint32_t>(block.size()));
        chunk.tzx.insert(chunk.tzx.end(), block.begin(), block.end());

        uint64_t tstates = kPilotPulse * (block[0] < 0x80 ? kHeaderPilotPulses : kDataPilotPulses) + kSyncPulses;
        for (uint8_t byte : block)
        {
            for (int bit = 0; bit < 8; bit++)
                tstates += ((byte >> bit) & 1) ? kOneBit : kZeroBit;
        }
        chunk.ms += (tstates + kTStatesPerMs - 1) / kTStatesPerMs + pauseMs;
    }

    void AddStandardBlock(Chunk& chunk, uint8_t flag, const uint8_t* payload, size_t size, uint16_t pauseMs)
    {
        std::vector<uint8_t> block;
        block.reserve(size + 2);
        block.push_back(flag);
        block.insert(block.end(), payload, payload + size);
        uint8_t checksum = 0;
        for (uint8_t byte : block)
            checksum ^= byte;
        block.push_back(checksum);
        AddRawBlock(chunk, block, pauseMs);
    }

    /// A ROM header (17 bytes) and its data block
    void AddFile(Chunk& chunk, uint8_t type, const std::string& name10, const std::vector<uint8_t>& data,
                 uint16_t param1, uint16_t param2, uint16_t pauseMs)
    {
        uint8_t header[17];
        header[0] = type;
        std::memcpy(header + 1, name10.data(), 10);
        header[11] = static_cast<uint8_t>(data.size() & 0xFF);
        header[12] = static_cast<uint8_t>(data.size() >> 8);
        header[13] = static_cast<uint8_t>(param1 & 0xFF);
        header[14] = static_cast<uint8_t>(param1 >> 8);
        header[15] = static_cast<uint8_t>(param2 & 0xFF);
        header[16] = static_cast<uint8_t>(param2 >> 8);
        AddStandardBlock(chunk, 0x00, header, sizeof(header), pauseMs);
        AddStandardBlock(chunk, 0xFF, data.data(), data.size(), pauseMs);
    }

    bool IsTapeExtension(const std::string& ext)
    {
        const std::vector<std::string> known = TapeLoaderRegistry::Instance().SupportedExtensions();
        return std::find(known.begin(), known.end(), Lower(ext)) != known.end();
    }

    /// A ready tape in the folder: its blocks go on unchanged
    bool PrepareTape(const std::string& name, const std::vector<uint8_t>& data, uint16_t pauseMs, Chunk& out,
                     std::string& why)
    {
        LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(data, name);
        if (!loader)
        {
            why = "not a tape this build reads";
            return false;
        }
        TapeImage image = loader->Load(data, name);
        if (!image.IsUsable())
        {
            why = image.errorText.empty() ? "not a usable tape" : image.errorText;
            return false;
        }

        if (loader->Format().id == "tzx")
        {
            // Appended raw: every block type and its timing survive
            out.tzx.assign(data.begin() + kTzxHeaderSize, data.end());
            double seconds = 0.0;
            for (const TapeBlockDescriptor& descriptor : TapeCatalogParser::Build(image))
                seconds += descriptor.estimatedSeconds;
            out.ms = static_cast<uint64_t>(seconds * 1000.0 + 0.5);
            return true;
        }

        // The TAP family: byte blocks at ROM speed, each a #10 block
        for (const TapeBlock& block : image.blocks)
        {
            const bool standard = !block.timing || block.timing->profile == TapeSpeedProfileEnum::StandardRom;
            if (block.data.size() < 2 || !standard || block.data.size() > 0xFFFF)
            {
                why = "has a block that is not a standard-speed byte block";
                return false;
            }
        }
        for (const TapeBlock& block : image.blocks)
            AddRawBlock(out, block.data, pauseMs);  // the block's own checksum, right or wrong
        return true;
    }

    /// One host file as tape blocks; false with `why` when it cannot go on a tape
    bool Prepare(const FolderEntry& file, const FolderManifest& manifest, uint16_t pauseMs, Chunk& out, std::string& why)
    {
        std::vector<uint8_t> data;
        if (!ReadHostFile(file.hostPath, data))
        {
            why = "cannot be read";
            return false;
        }

        const auto [stem, ext] = SplitName(file.name);
        if (IsTapeExtension(ext))
            return PrepareTape(file.name, data, pauseMs, out, why);

        std::string name = FolderDiskBuilder::CompatibleName(stem, 10);
        char type = 'C';
        uint16_t start = kCodeStart;
        uint16_t programLength = 0;
        uint16_t line = kNoAutorun;

        LoaderHobeta::Header header;
        std::vector<std::string> ignored;
        if (LoaderHobeta::detect(data.data(), data.size()) &&
            LoaderHobeta::parseHeader(data.data(), data.size(), header, ignored))
        {
            // TR-DOS keeps a program's full length (program + variables) and
            // the program alone in start / length; the autorun line after the data
            name = std::string(header.name, 8) + "  ";
            type = static_cast<char>(header.type);
            std::vector<uint8_t> body(data.begin() + LoaderHobeta::HEADER_SIZE, data.end());
            size_t size = header.length;
            if (type == 'B')
            {
                size = std::max(header.start, header.length);
                programLength = std::min(header.start, header.length);
                if (body.size() >= size + 4 && body[size] == 0x80 && body[size + 1] == 0xAA)
                    line = static_cast<uint16_t>(body[size + 2] | (body[size + 3] << 8));
            }
            else
                start = header.start;
            if (body.size() < size)
            {
                why = "its Hobeta header claims more data than the file holds";
                return false;
            }
            body.resize(size);
            data = std::move(body);
        }
        else
        {
            type = DiskTypeMap::TypeFor(ext);
            if (Lower(ext) == "scr" && data.size() == kScreenBytes)
                start = kScreenStart;
            programLength = static_cast<uint16_t>(std::min<size_t>(data.size(), 0xFFFF));
        }

        // The manifest overrides what the file says
        auto override = manifest.files.find(file.name);
        if (override != manifest.files.end())
        {
            const ManifestFileOverride& o = override->second;
            if (o.name)
                name = FolderDiskBuilder::CompatibleName(*o.name, 10);
            if (o.type)
                type = *o.type;
            if (o.start)
                start = *o.start;
            if (o.line)
                line = *o.line;
            if (type == 'B' && programLength == 0)
                programLength = static_cast<uint16_t>(std::min<size_t>(data.size(), 0xFFFF));
        }

        if (data.size() > FolderTapeBuilder::kMaxFileBytes)
        {
            why = "larger than one tape block holds (65 533 bytes)";
            return false;
        }

        switch (type)
        {
            case 'B':
                AddFile(out, Program, name, data, line, programLength, pauseMs);
                break;
            case 'D':
                // TR-DOS does not say which array; the variable name is a()
                AddFile(out, NumberArray, name, data, 0x8100, kNoAutorun, pauseMs);
                break;
            default:
                AddFile(out, Bytes, name, data, start, kNoAutorun, pauseMs);
                break;
        }
        return true;
    }
}  // namespace

MediaResult FolderTapeBuilder::BuildTzx(const std::filesystem::path& folder, std::vector<uint8_t>& tzx,
                                        uint32_t capacityMs)
{
    tzx.clear();
    MediaResult result = MediaResult::Success();

    const FolderManifest manifest = FolderManifest::Load(folder);
    result.report.insert(result.report.end(), manifest.report.begin(), manifest.report.end());
    if (manifest.tapeFormat && Lower(*manifest.tapeFormat) != "tzx")
        result.report.push_back("tape.format '" + *manifest.tapeFormat + "': a folder is always built as tzx");
    const uint16_t pauseMs = static_cast<uint16_t>(std::min<uint32_t>(manifest.tapePauseMs.value_or(kDefaultPauseMs), 0xFFFF));

    FolderScanOptions scan;
    scan.recursive = false;
    scan.excludePatterns = manifest.exclude;
    FolderSnapshot snapshot;
    std::string error;
    if (!FolderSnapshot::Scan(folder, scan, snapshot, &error))
        return MediaResult::Fail(MediaError::UnreadableSource, error);
    for (const SkippedEntry& skipped : snapshot.Skipped())
        result.report.push_back(skipped.path + ": skipped, " + skipped.reason);

    static const uint8_t kHeader[kTzxHeaderSize] = {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20};
    tzx.assign(kHeader, kHeader + kTzxHeaderSize);

    uint64_t usedMs = 0;
    for (const FolderEntry* file : FolderDiskBuilder::OrderFiles(snapshot.Root(), manifest, result.report))
    {
        Chunk chunk;
        std::string why;
        if (!Prepare(*file, manifest, pauseMs, chunk, why))
        {
            result.report.push_back(file->name + ": skipped, " + why);
            continue;
        }
        if (usedMs + chunk.ms > capacityMs)
        {
            result.report.push_back(file->name + ": skipped, does not fit on one side of a C90 (" +
                                    std::to_string((chunk.ms + 999) / 1000) + " s, " +
                                    std::to_string((capacityMs - usedMs) / 1000) + " s left)");
            continue;
        }
        usedMs += chunk.ms;
        tzx.insert(tzx.end(), chunk.tzx.begin(), chunk.tzx.end());
    }

    if (tzx.size() == kTzxHeaderSize)
        return MediaResult{MediaError::DoesNotFit, "nothing in the folder goes on a tape", result.report};
    return result;
}

MediaResult FolderTapeBuilder::Build(const std::filesystem::path& folder, std::unique_ptr<TapeImage>& image,
                                     uint32_t capacityMs)
{
    image.reset();
    std::vector<uint8_t> tzx;
    MediaResult result = BuildTzx(folder, tzx, capacityMs);
    if (!result.Ok())
        return result;

    LoaderTZX loader;
    auto built = std::make_unique<TapeImage>(loader.Load(tzx, "folder.tzx"));
    if (!built->IsUsable())
        return MediaResult{MediaError::IoError, "the tape built from the folder does not decode: " + built->errorText,
                           result.report};
    image = std::move(built);
    return result;
}
