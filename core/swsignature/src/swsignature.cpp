#include "swsignature.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <sstream>

#include "3rdparty/digestpp/digestpp.hpp"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tapetypes.h"
#include "loaders/snapshot/loader_sna.h"
#include "loaders/snapshot/loader_z80.h"
#include "loaders/snapshot/snapshotimage.h"
#include "loaders/snapshot/szx/loaderszx.h"
#include "loaders/snapshot/szx/szxreader.h"
#include "loaders/tape/loader_tape.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/floppyformats.h"
#include "unrealdisk/options.h"
#include "unrealdisk/trdoscatalog.h"
#include "unrealdisk/trdosfs.h"

namespace unrealng::swsignature
{

namespace
{
constexpr size_t kPageSize = 256;
constexpr size_t kBankSize = 16384;
constexpr size_t kPagesPerBank = kBankSize / kPageSize;
/// The screen #4000-#5AFF: the first #1B pages of bank 5
constexpr size_t kScreenPages = 0x1B;

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Extension(const std::string& name)
{
    const size_t slash = name.find_last_of("/\\");
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    return Lower(name.substr(dot + 1));
}

std::string Sha256Hex(const uint8_t* data, size_t size)
{
    return digestpp::sha256().absorb(data, size).hexdigest();
}

std::string Sha256Hex(const std::string& text)
{
    return digestpp::sha256().absorb(text).hexdigest();
}

/// The first 64 bits of a page's SHA-256, big-endian
uint64_t PageHash(const uint8_t* page)
{
    unsigned char digest[32];
    digestpp::sha256().absorb(page, kPageSize).digest(digest, sizeof digest);
    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++)
        value = (value << 8) | digest[i];
    return value;
}

std::string Hex64(uint64_t value)
{
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(value));
    return text;
}

/// A tape: the data payloads of its byte blocks in play order
bool FromTape(const uint8_t* data, size_t size, const std::string& name, Signatures& out)
{
    const std::span<const uint8_t> bytes(data, size);
    LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(bytes, name);
    if (loader == nullptr)
        return false;
    const TapeImage image = loader->Load(bytes, name);
    if (image.blocks.empty())
        return false;
    out.kind = MediaKind::Tape;
    out.format = image.formatId;
    if (!image.IsUsable())
        out.warnings.push_back("the tape loads with errors: " + image.errorText);
    digestpp::sha256 content;
    size_t payloadBytes = 0;
    for (size_t i = 0; i < image.blocks.size(); i++)
    {
        const TapeBlock& block = image.blocks[i];
        if (block.data.empty())
            continue;  // a pulse block: no bytes to hash
        const bool flagByte = i >= image.descriptors.size() || image.descriptors[i].flagBytePresent;
        const uint8_t* payload = block.data.data();
        size_t length = block.data.size();
        if (flagByte)
        {
            if (length < 2)
                continue;  // a flag with no payload
            payload += 1;
            length -= 2;   // the flag in front, the checksum at the end
        }
        TapeBlockHash hash;
        hash.index = i;
        hash.flag = flagByte ? block.data.front() : 0;
        hash.flagByte = flagByte;
        hash.length = length;
        hash.sha256 = Sha256Hex(payload, length);
        out.blocks.push_back(std::move(hash));
        content.absorb(payload, length);
        payloadBytes += length;
    }
    if (payloadBytes > 0)
        out.content = std::string(kTapePrefix) + content.hexdigest();
    else
        out.warnings.push_back("the tape has no byte blocks: exact signature only");
    return true;
}

/// One TR-DOS file: `bytes` are its whole sectors, as TrdosFileSystem::Read gives them (data + tail)
void AddDiskFile(const unrealdisk::TrdosFile& file, const uint8_t* bytes, size_t size, Signatures& out)
{
    // BASIC: program and variables (the start field); others: the length field; 0 (XAS) or beyond: the sectors
    size_t hashed = file.type == 'B' ? file.start : file.length;
    if (hashed == 0 || hashed > size)
        hashed = size;
    DiskFile entry;
    entry.name = file.TrimmedName();
    entry.type = file.type;
    entry.start = file.start;
    entry.length = file.length;
    entry.sectors = file.sectors;
    entry.code = file.type == 'B' || file.type == 'C';
    entry.dataLength = hashed;
    entry.dataSha256 = Sha256Hex(bytes, hashed);
    std::string record = file.name;  // 8 characters as stored
    record.resize(8, ' ');
    record.push_back(file.type);
    record.push_back(static_cast<char>(file.start & 0xFF));
    record.push_back(static_cast<char>(file.start >> 8));
    record.push_back(static_cast<char>(file.length & 0xFF));
    record.push_back(static_cast<char>(file.length >> 8));
    entry.catalogSha256 = Sha256Hex(record);
    record += entry.dataSha256;
    entry.entrySha256 = Sha256Hex(record);
    out.files.push_back(std::move(entry));
}

/// The content signature over the files' entry hashes (sorted: the catalog order does not count)
void FinishDisk(Signatures& out)
{
    if (out.files.empty())
    {
        out.warnings.push_back("an empty TR-DOS disk: exact signature only");
        return;
    }
    std::vector<std::string> hashes;
    for (const DiskFile& file : out.files)
        hashes.push_back(file.entrySha256);
    std::sort(hashes.begin(), hashes.end());
    std::string joined;
    for (const std::string& hash : hashes)
        joined += hash;
    out.content = std::string(kTrdosPrefix) + Sha256Hex(joined);
}

/// A catalog entry as TrdosCatalog::Parse reads it
unrealdisk::TrdosFile CatalogEntry(const uint8_t* entry, size_t base)
{
    unrealdisk::TrdosFile file;
    file.name.assign(reinterpret_cast<const char*>(entry), 8);
    file.type = static_cast<char>(entry[8]);
    file.start = static_cast<uint16_t>(entry[9] | (entry[10] << 8));
    file.length = static_cast<uint16_t>(entry[11] | (entry[12] << 8));
    file.sectors = entry[13];
    file.firstSector = static_cast<uint8_t>(base == 16 ? entry[14] : 0);
    file.firstTrack = static_cast<uint8_t>(base == 16 ? entry[15] : 0);
    return file;
}

/// The sector-linear images (TRD, SCL) read straight from their bytes: the same files and bytes as the DiskImage path
/// (the core loaders build every MFM track first, ~13 ms a disk). False when the image is anything but plain (a
/// catalog TR-DOS refuses, a file past the image end, a bad SCL sum): the DiskImage path then decides
bool FromSectorImage(const uint8_t* data, size_t size, const std::string& format, Signatures& out)
{
    constexpr size_t kSector = unrealdisk::TrdosCatalog::SECTOR_SIZE;
    constexpr size_t kMaxFiles = unrealdisk::TrdosCatalog::MAX_FILES;
    Signatures result = out;
    if (format == "trd")
    {
        // TrdosCatalog::IsTrdos on the raw sectors: the id #10 in sector 9, the first free sector and every entry's
        // first sector inside a track
        if (size < 9 * kSector)
            return false;
        const uint8_t* info = data + 8 * kSector;
        if (info[unrealdisk::TrdosCatalog::INFO_TRDOS_ID] != 0x10 ||
            info[unrealdisk::TrdosCatalog::INFO_FIRST_FREE_SECTOR] >= 16)
            return false;
        for (size_t slot = 0; slot < kMaxFiles; slot++)
        {
            const uint8_t* entry = data + slot * 16;
            if (entry[0] == 0x00)
                break;
            if (entry[14] >= 16)
                return false;
            if (entry[0] == 0x01)
                continue;  // deleted
            const unrealdisk::TrdosFile file = CatalogEntry(entry, 16);
            const size_t first = (static_cast<size_t>(file.firstTrack) * 16 + file.firstSector) * kSector;
            const size_t length = static_cast<size_t>(file.sectors) * kSector;
            if (first + length > size)
                return false;  // the loader pads a short image: leave it to the DiskImage path
            AddDiskFile(file, data + first, length, result);
        }
    }
    else if (format == "scl")
    {
        // LoaderSCL::loadSCLData: the byte sum, the sizes, the files onto an empty disk in archive order (the
        // first 128 only; a name starting #00 ends the catalog, #01 is deleted)
        if (size < 9 + 4 || std::memcmp(data, "SINCLAIR", 8) != 0)
            return false;
        uint32_t sum = 0;
        for (size_t i = 0; i + 4 < size; i++)
            sum += data[i];
        const uint8_t* tail = data + size - 4;
        if (sum != (tail[0] | (tail[1] << 8) | (tail[2] << 16) | (static_cast<uint32_t>(tail[3]) << 24)))
            return false;
        const size_t count = data[8];
        const size_t dataStart = 9 + count * 14;
        size_t sectors = 0;
        for (size_t i = 0; i < count && 9 + i * 14 + 14 <= size; i++)
            sectors += data[9 + i * 14 + 13];
        if (dataStart + sectors * kSector > size || sectors > 2544)
            return false;
        size_t offset = dataStart;
        for (size_t i = 0; i < count && i < kMaxFiles; i++)
        {
            const uint8_t* entry = data + 9 + i * 14;
            const unrealdisk::TrdosFile file = CatalogEntry(entry, 14);
            const size_t length = static_cast<size_t>(file.sectors) * kSector;
            if (entry[0] == 0x00)
                break;
            if (entry[0] != 0x01)
                AddDiskFile(file, data + offset, length, result);
            offset += length;
        }
    }
    else
        return false;
    out = std::move(result);
    out.kind = MediaKind::Disk;
    out.format = format;
    FinishDisk(out);
    return true;
}

/// A disk: its TR-DOS file set
bool FromDisk(const uint8_t* data, size_t size, const std::string& name, Signatures& out, bool sectorImages)
{
    const std::string probed = unrealdisk::FloppyFormats::ProbeData(data, size, name);
    if (probed.empty())
        return false;
    if (sectorImages && FromSectorImage(data, size, probed, out))
        return true;
    const unrealdisk::LoaderOptions options;
    std::unique_ptr<unrealdisk::DiskImage> disk;
    std::string format;
    const unrealdisk::FormatResult result = unrealdisk::FloppyFormats::LoadData(options, data, size, name, disk, format);
    if (!result.Ok() || !disk)
        return false;
    out.kind = MediaKind::Disk;
    out.format = format;
    unrealdisk::TrdosFileSystem fs(*disk);
    std::vector<unrealdisk::TrdosFile> files;
    std::string error;
    if (!fs.IsTrdos() || !fs.List(files, error))
    {
        out.warnings.push_back("no TR-DOS file system: exact signature only");
        return true;
    }
    for (const unrealdisk::TrdosFile& file : files)
    {
        std::vector<uint8_t> body;
        std::vector<uint8_t> tail;
        if (!fs.Read(file, body, &tail, error))
        {
            out.warnings.push_back(error);
            continue;
        }
        body.insert(body.end(), tail.begin(), tail.end());
        AddDiskFile(file, body.data(), body.size(), out);
    }
    FinishDisk(out);
    return true;
}

/// A snapshot's RAM through the core's readers
bool ReadSnapshot(const uint8_t* data, size_t size, const std::string& name, const std::string& ext,
                  snapshot::Image& image, std::string& error)
{
    if (ext == "szx")
    {
        szx::Stage stage;
        if (!SzxReader::Parse(data, size, stage, error))
            return false;
        image = LoaderSZX::BuildImage(stage, name);
        return true;
    }
    // The SNA / Z80 readers stage through a context of their own: nothing of a machine is touched
    EmulatorContext context(LoggerLevel::LogNone);
    std::vector<uint8_t> bytes(data, data + size);
    if (ext == "sna")
    {
        LoaderSNA loader(&context, std::move(bytes), name);
        if (!loader.Stage())
        {
            error = "not a loadable SNA file";
            return false;
        }
        image = loader.GetSnapshotImage();
        return true;
    }
    LoaderZ80 loader(&context, std::move(bytes), name);
    if (!loader.Stage())
    {
        error = "not a loadable Z80 file";
        return false;
    }
    image = loader.GetSnapshotImage();
    return true;
}

/// The logical bank #C000-#FFFF shows in the image's paging
uint16_t TopBank(const snapshot::Image& image)
{
    if (image.memoryModel == snapshot::MemoryModel::Mem48k || !image.paging.p7FFD.has_value())
        return 0;
    return static_cast<uint16_t>(*image.paging.p7FFD & 7);
}

bool FromSnapshot(const uint8_t* data, size_t size, const std::string& name, const std::string& ext, Signatures& out)
{
    snapshot::Image image;
    std::string error;
    if (!ReadSnapshot(data, size, name, ext, image, error))
    {
        out.warnings.push_back(error + ": exact signature only");
        return false;
    }
    out.kind = MediaKind::Snapshot;
    out.format = ext;
    out.machineHint = image.machineHint;
    if (image.memoryModel == snapshot::MemoryModel::Physical || image.banks.empty())
    {
        out.warnings.push_back("no logical RAM banks: exact signature only");
        return true;
    }
    // The stack page: SP's 256 bytes, in the bank the address shows
    int stackBank = -1;
    size_t stackPage = 0;
    const uint16_t sp = image.cpu.sp;
    if (sp >= 0x4000)
    {
        stackBank = sp < 0x8000 ? 5 : (sp < 0xC000 ? 2 : TopBank(image));
        stackPage = (sp & 0x3FFF) / kPageSize;
    }
    digestpp::sha256 content;
    for (const auto& [bank, bytes] : image.banks)
    {
        if (bytes.size() < kBankSize)
            continue;
        for (size_t page = 0; page < kPagesPerBank; page++)
        {
            if (bank == 5 && page < kScreenPages)
                continue;
            if (static_cast<int>(bank) == stackBank && page == stackPage)
                continue;
            const uint8_t* at = bytes.data() + page * kPageSize;
            if (std::all_of(at + 1, at + kPageSize, [at](uint8_t value) { return value == *at; }))
                continue;  // a uniform page (cleared RAM): no trace of the program
            MemoryPage entry;
            entry.bank = bank;
            entry.page = static_cast<uint8_t>(page);
            entry.hash = PageHash(bytes.data() + page * kPageSize);
            const uint8_t record[3] = {static_cast<uint8_t>(bank & 0xFF), static_cast<uint8_t>(bank >> 8),
                                       entry.page};
            content.absorb(record, sizeof record);
            const std::string hex = Hex64(entry.hash);
            content.absorb(hex);
            out.pages.push_back(entry);
        }
    }
    if (!out.pages.empty())
        out.content = std::string(kMemoryPrefix) + content.hexdigest();
    return true;
}

bool IsSnapshotExtension(const std::string& ext)
{
    return ext == "sna" || ext == "z80" || ext == "szx";
}
}  // namespace

const char* ToText(MediaKind kind)
{
    switch (kind)
    {
        case MediaKind::Tape:
            return "tape";
        case MediaKind::Disk:
            return "disk";
        case MediaKind::Snapshot:
            return "snapshot";
        default:
            return "unknown";
    }
}

const char* ToText(MatchLevel level)
{
    switch (level)
    {
        case MatchLevel::Exact:
            return "exact";
        case MatchLevel::Content:
            return "content";
        case MatchLevel::Fuzzy:
            return "fuzzy";
        case MatchLevel::Memory:
            return "memory";
        default:
            return "none";
    }
}

std::string DiskFile::Token() const
{
    return std::string(code ? "c:" : "d:") + entrySha256.substr(0, 16);
}

std::string DiskFile::CatalogToken() const
{
    return "n:" + catalogSha256.substr(0, 16);
}

std::vector<std::string> Signatures::Keys() const
{
    std::vector<std::string> keys;
    if (!sha256.empty())
        keys.push_back(std::string(kFilePrefix) + sha256);
    if (!content.empty())
        keys.push_back(content);
    return keys;
}

std::vector<std::string> Signatures::FileSet() const
{
    std::vector<std::string> tokens;
    for (const DiskFile& file : files)
    {
        tokens.push_back(file.Token());
        tokens.push_back(file.CatalogToken());
    }
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

bool Compute(const uint8_t* data, size_t size, const std::string& name, Signatures& out, std::string& error,
             const Options& options)
{
    out = Signatures{};
    out.name = name;
    out.size = size;
    if (data == nullptr || size == 0)
    {
        error = "an empty file";
        return false;
    }
    out.sha256 = Sha256Hex(data, size);
    if (options.externalHashes)
    {
        out.sha1 = digestpp::sha1().absorb(data, size).hexdigest();
        out.md5 = digestpp::md5().absorb(data, size).hexdigest();
    }
    const std::string ext = Extension(name);
    // The extension names a snapshot (their content has no common mark); a disk has marks or sizes; a tape a probe
    if (IsSnapshotExtension(ext))
    {
        if (!FromSnapshot(data, size, name, ext, out))
            out.format = ext;
        return true;
    }
    if (ext != "tap" && ext != "tzx" && FromDisk(data, size, name, out, options.sectorImages))
        return true;
    if (FromTape(data, size, name, out))
        return true;
    out.format = ext;
    return true;
}

bool ComputeFile(const std::string& path, Signatures& out, std::string& error, const Options& options)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        out = Signatures{};
        error = "cannot read '" + path + "'";
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    const std::string bytes = text.str();
    const size_t slash = path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return Compute(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), name, out, error, options);
}

double MemoryShare(const std::vector<MemoryPage>& a, const std::vector<MemoryPage>& b)
{
    std::map<uint32_t, uint64_t> pages;
    for (const MemoryPage& page : a)
        pages[(static_cast<uint32_t>(page.bank) << 8) | page.page] = page.hash;
    size_t common = 0;
    size_t equal = 0;
    for (const MemoryPage& page : b)
    {
        const auto it = pages.find((static_cast<uint32_t>(page.bank) << 8) | page.page);
        if (it == pages.end())
            continue;
        ++common;
        if (it->second == page.hash)
            ++equal;
    }
    return common < kMemoryMinPages ? 0.0 : static_cast<double>(equal) / static_cast<double>(common);
}

MatchResult Compare(const Signatures& reference, const Signatures& candidate)
{
    MatchResult result;
    if (!reference.sha256.empty() && reference.sha256 == candidate.sha256)
    {
        result.level = MatchLevel::Exact;
        result.score = 1.0;
        result.reason = "the same file";
        return result;
    }
    if (!reference.content.empty() && reference.content == candidate.content)
    {
        result.level = MatchLevel::Content;
        result.score = 1.0;
        result.reason = reference.content.substr(0, reference.content.find(':')) + " content equal";
        return result;
    }
    char text[160];
    if (!reference.files.empty() && !candidate.files.empty())
    {
        const FileSetScore score = CompareFileSets(reference.FileSet(), candidate.FileSet());
        // The catalog rule: the share of the reference's files with their data unchanged
        result.score = score.catalogRule ? static_cast<double>(score.common) /
                                               static_cast<double>(score.common + score.changed)
                                         : score.jaccard;
        result.reason = "file set: " + score.Reason();
        if (score.matches)
            result.level = MatchLevel::Fuzzy;
        return result;
    }
    if (!reference.pages.empty() && !candidate.pages.empty())
    {
        const double share = MemoryShare(reference.pages, candidate.pages);
        std::snprintf(text, sizeof text, "memory: %.1f %% of the RAM pages equal", share * 100.0);
        result.score = share;
        result.reason = text;
        if (share >= kMemoryShare)
            result.level = MatchLevel::Memory;
        return result;
    }
    result.reason = "nothing in common";
    return result;
}

StateNode ToStateNode(const Signatures& signatures, bool details)
{
    StateNode node = StateNode::Object();
    node["name"] = signatures.name;
    node["kind"] = ToText(signatures.kind);
    node["format"] = signatures.format;
    node["size"] = static_cast<uint64_t>(signatures.size);
    node["sha256"] = signatures.sha256;
    if (!signatures.sha1.empty())
        node["sha1"] = signatures.sha1;
    if (!signatures.md5.empty())
        node["md5"] = signatures.md5;
    node["content"] = signatures.content;
    StateNode keys = StateNode::Array();
    for (const std::string& key : signatures.Keys())
        keys.push(key);
    node["keys"] = keys;
    StateNode fileSet = StateNode::Array();
    for (const std::string& token : signatures.FileSet())
        fileSet.push(token);
    node["file_set"] = fileSet;
    if (!signatures.machineHint.empty())
        node["machine_hint"] = signatures.machineHint;
    node["memory_pages"] = static_cast<uint64_t>(signatures.pages.size());
    if (details)
    {
        StateNode blocks = StateNode::Array();
        for (const TapeBlockHash& block : signatures.blocks)
        {
            StateNode item = StateNode::Object();
            item["index"] = static_cast<uint64_t>(block.index);
            item["flag"] = static_cast<unsigned>(block.flag);
            item["flag_byte"] = block.flagByte;
            item["length"] = static_cast<uint64_t>(block.length);
            item["sha256"] = block.sha256;
            blocks.push(item);
        }
        node["blocks"] = blocks;
        StateNode files = StateNode::Array();
        for (const DiskFile& file : signatures.files)
        {
            StateNode item = StateNode::Object();
            item["name"] = file.name;
            item["type"] = std::string(1, file.type);
            item["start"] = static_cast<unsigned>(file.start);
            item["length"] = static_cast<unsigned>(file.length);
            item["sectors"] = static_cast<unsigned>(file.sectors);
            item["code"] = file.code;
            item["data_sha256"] = file.dataSha256;
            item["token"] = file.Token();
            item["catalog_token"] = file.CatalogToken();
            files.push(item);
        }
        node["files"] = files;
    }
    StateNode warnings = StateNode::Array();
    for (const std::string& warning : signatures.warnings)
        warnings.push(warning);
    node["warnings"] = warnings;
    return node;
}

StateNode ToStateNode(const MatchResult& result)
{
    StateNode node = StateNode::Object();
    node["level"] = ToText(result.level);
    node["matches"] = result.level != MatchLevel::None;
    node["score"] = result.score;
    node["reason"] = result.reason;
    return node;
}

}  // namespace unrealng::swsignature
