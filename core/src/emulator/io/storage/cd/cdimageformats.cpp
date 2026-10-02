#include "stdafx.h"

#include "cdimageformats.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/io/storage/chd/chdfile.h"

using namespace cd;

namespace
{
    uint64_t Fnv(uint64_t h, const void* data, size_t length)
    {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < length; i++)
        {
            h ^= p[i];
            h *= 0x100000001b3ULL;
        }
        return h;
    }

    uint64_t IdentityOf(const std::string& description, const std::vector<StoredTrack>& tracks, uint64_t extra)
    {
        uint64_t h = 0xcbf29ce484222325ULL;
        h = Fnv(h, description.data(), description.size());
        h = Fnv(h, &extra, sizeof(extra));
        for (const StoredTrack& t : tracks)
        {
            const uint32_t values[] = {t.track.number, static_cast<uint32_t>(t.track.mode), t.track.pregapLba, t.track.startLba,
                                       t.track.endLba, static_cast<uint32_t>(t.format), t.stride};
            h = Fnv(h, values, sizeof(values));
        }
        return h;
    }

    bool Fail(std::string* error, const std::string& reason)
    {
        if (error)
            *error = reason;
        return false;
    }

    /// region <Sources>

    /// A file (or a part of it: a WAVE file's data chunk): reads past the end give zeros
    class FileSource : public IFrameSource
    {
    public:
        static std::unique_ptr<FileSource> Open(const std::string& path, uint64_t base, uint64_t length, std::string* error)
        {
            auto source = std::unique_ptr<FileSource>(new FileSource());
            source->_path = path;
            source->_file.open(FileHelper::ToFsPath(path), std::ios::binary);
            if (!source->_file)
            {
                Fail(error, "cannot open '" + path + "'");
                return nullptr;
            }
            source->_base = base;
            source->_length = length;
            return source;
        }

        bool Read(uint64_t offset, uint8_t* dst, uint32_t length) override
        {
            std::memset(dst, 0, length);
            if (offset >= _length)
                return true;
            const uint64_t available = std::min<uint64_t>(length, _length - offset);
            _file.clear();
            _file.seekg(static_cast<std::streamoff>(_base + offset));
            _file.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(available));
            return static_cast<uint64_t>(_file.gcount()) == available;
        }
        std::string Describe() const override { return _path; }

    private:
        FileSource() = default;
        std::string _path;
        std::ifstream _file;
        uint64_t _base = 0;
        uint64_t _length = 0;
    };

    /// A CD CHD's logical bytes
    class ChdSource : public IFrameSource
    {
    public:
        explicit ChdSource(std::unique_ptr<chd::ChdFile> file) : _file(std::move(file)) {}
        bool Read(uint64_t offset, uint8_t* dst, uint32_t length) override
        {
            if (offset >= _file->LogicalBytes())
            {
                std::memset(dst, 0, length);
                return true;
            }
            const uint64_t available = std::min<uint64_t>(length, _file->LogicalBytes() - offset);
            if (available < length)
                std::memset(dst + available, 0, length - available);
            return _file->ReadBytes(offset, dst, static_cast<uint32_t>(available));
        }
        std::string Describe() const override { return _file->Path(); }
        chd::ChdFile& File() { return *_file; }

    private:
        std::unique_ptr<chd::ChdFile> _file;
    };

    /// endregion </Sources>

    uint64_t FileBytes(const std::string& path)
    {
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(FileHelper::ToFsPath(path), ec);
        return ec ? 0 : size;
    }

    bool ReadAt(const std::string& path, uint64_t offset, uint8_t* dst, size_t length)
    {
        std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
        if (!in)
            return false;
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(length));
        return static_cast<size_t>(in.gcount()) == length;
    }

    uint32_t Le32(const uint8_t* p)
    {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    uint16_t Le16(const uint8_t* p)
    {
        return static_cast<uint16_t>(p[0] | (p[1] << 8));
    }

    /// A WAVE file's PCM data: 44.1 kHz, 16-bit, stereo only (the Red Book format)
    bool WaveData(const std::string& path, uint64_t& offset, uint64_t& length, std::string* error)
    {
        const uint64_t size = FileBytes(path);
        uint8_t header[12];
        if (size < 12 || !ReadAt(path, 0, header, 12) || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
            return Fail(error, "'" + path + "' is no WAVE file");
        uint64_t at = 12;
        bool format = false;
        while (at + 8 <= size)
        {
            uint8_t chunk[8];
            if (!ReadAt(path, at, chunk, 8))
                break;
            const uint32_t chunkSize = Le32(chunk + 4);
            if (std::memcmp(chunk, "fmt ", 4) == 0)
            {
                uint8_t fmt[16];
                if (chunkSize < 16 || !ReadAt(path, at + 8, fmt, 16))
                    return Fail(error, "'" + path + "': a broken fmt chunk");
                if (Le16(fmt) != 1 || Le16(fmt + 2) != 2 || Le32(fmt + 4) != kSampleRate || Le16(fmt + 14) != 16)
                    return Fail(error, "'" + path + "': CD audio needs PCM, 2 channels, 44100 Hz, 16 bits");
                format = true;
            }
            else if (std::memcmp(chunk, "data", 4) == 0)
            {
                if (!format)
                    return Fail(error, "'" + path + "': the data chunk comes before fmt");
                offset = at + 8;
                length = std::min<uint64_t>(chunkSize, size - offset);
                return true;
            }
            at += 8 + chunkSize + (chunkSize & 1);
        }
        return Fail(error, "'" + path + "': no PCM data chunk");
    }

    /// region <CUE sheets>

    struct CueTrack
    {
        uint8_t number = 0;
        TrackMode mode = TrackMode::Mode1;
        StoredFormat format = StoredFormat::Raw2352;
        uint32_t stride = kFrameBytes;
        int file = -1;
        int64_t index0 = -1;  ///< frames into the file
        int64_t index1 = -1;
        uint32_t pregap = 0;  ///< PREGAP: silence not in the file
        uint32_t postgap = 0;
    };

    struct CueFile
    {
        std::string path;
        enum class Type : uint8_t { Binary, Motorola, Wave } type = Type::Binary;
        uint64_t base = 0;
        uint64_t length = 0;
    };

    /// Split a CUE line into words; quoted strings are one word
    std::vector<std::string> Words(const std::string& line)
    {
        std::vector<std::string> words;
        size_t i = 0;
        while (i < line.size())
        {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
                i++;
            if (i >= line.size())
                break;
            std::string word;
            if (line[i] == '"')
            {
                i++;
                while (i < line.size() && line[i] != '"')
                    word.push_back(line[i++]);
                i++;
            }
            else
            {
                while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
                    word.push_back(line[i++]);
            }
            words.push_back(word);
        }
        return words;
    }

    bool ParseTime(const std::string& text, uint32_t& frames)
    {
        unsigned m = 0, s = 0, f = 0;
        char tail = 0;
        if (std::sscanf(text.c_str(), "%u:%u:%u%c", &m, &s, &f, &tail) != 3 || s >= 60 || f >= kFramesPerSecond)
            return false;
        frames = MsfToFrames(static_cast<uint8_t>(std::min(m, 255u)), static_cast<uint8_t>(s), static_cast<uint8_t>(f));
        return true;
    }

    /// A FILE name relative to the sheet's folder; a sheet written on another
    /// system may differ in case or carry a path: the folder is searched for the
    /// file name ignoring case
    std::string ResolveFile(const std::string& folder, const std::string& name)
    {
        std::string normalized = name;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        const std::string direct = FileHelper::IsAbsolutePath(normalized) ? normalized : FileHelper::PathCombine(folder, normalized);
        if (FileHelper::IsFile(direct))
            return direct;
        const size_t slash = normalized.find_last_of('/');
        const std::string base = slash == std::string::npos ? normalized : normalized.substr(slash + 1);
        const std::string lower = StringHelper::ToLower(base);
        std::error_code ec;
        for (std::filesystem::directory_iterator it(FileHelper::ToFsPath(folder.empty() ? "." : folder), ec), end; !ec && it != end;
             it.increment(ec))
        {
            const auto u8 = it->path().filename().u8string();
            const std::string candidate(u8.begin(), u8.end());
            if (StringHelper::ToLower(candidate) == lower)
                return FileHelper::PathCombine(folder, candidate);
        }
        return direct;
    }

    bool TrackType(const std::string& type, CueTrack& track)
    {
        const std::string t = StringHelper::ToLower(type);
        if (t == "audio")
            track.mode = TrackMode::Audio, track.format = StoredFormat::Raw2352, track.stride = 2352;
        else if (t == "cdg")
            track.mode = TrackMode::Audio, track.format = StoredFormat::Raw2352, track.stride = 2448;
        else if (t == "mode1/2048")
            track.mode = TrackMode::Mode1, track.format = StoredFormat::Cooked2048, track.stride = 2048;
        else if (t == "mode1/2352")
            track.mode = TrackMode::Mode1, track.format = StoredFormat::Raw2352, track.stride = 2352;
        else if (t == "mode2/2048")
            track.mode = TrackMode::Mode2, track.format = StoredFormat::Cooked2048, track.stride = 2048;
        else if (t == "mode2/2336" || t == "cdi/2336")
            track.mode = TrackMode::Mode2, track.format = StoredFormat::Mode2_2336, track.stride = 2336;
        else if (t == "mode2/2352" || t == "cdi/2352")
            track.mode = TrackMode::Mode2, track.format = StoredFormat::Raw2352, track.stride = 2352;
        else
            return false;
        return true;
    }

    /// endregion </CUE sheets>

    /// region <CHD track metadata>

    struct ChdTrackInfo
    {
        uint32_t number = 0;
        std::string type;
        std::string subtype;
        uint32_t frames = 0;
        uint32_t pregap = 0;
        std::string pgtype;
        uint32_t postgap = 0;
    };

    /// "TRACK:1 TYPE:MODE1_RAW SUBTYPE:NONE FRAMES:1000 PREGAP:0 PGTYPE:MODE1 PGSUB:RW POSTGAP:0"
    ChdTrackInfo ParseChdTrack(const std::string& text)
    {
        ChdTrackInfo info;
        std::istringstream in(text);
        std::string word;
        while (in >> word)
        {
            const size_t colon = word.find(':');
            if (colon == std::string::npos)
                continue;
            const std::string key = word.substr(0, colon);
            const std::string value = word.substr(colon + 1);
            auto number = [&value]() { return static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10)); };
            if (key == "TRACK")
                info.number = number();
            else if (key == "TYPE")
                info.type = value;
            else if (key == "SUBTYPE")
                info.subtype = value;
            else if (key == "FRAMES")
                info.frames = number();
            else if (key == "PREGAP")
                info.pregap = number();
            else if (key == "PGTYPE")
                info.pgtype = value;
            else if (key == "POSTGAP")
                info.postgap = number();
        }
        return info;
    }

    /// MAME's track types (cdrom.cpp): the mode and how many bytes of each 2448-byte frame are the sector
    bool ChdTrackType(const std::string& type, TrackMode& mode, StoredFormat& format)
    {
        if (type == "AUDIO")
            mode = TrackMode::Audio, format = StoredFormat::Raw2352;
        else if (type == "MODE1")
            mode = TrackMode::Mode1, format = StoredFormat::Cooked2048;
        else if (type == "MODE1_RAW")
            mode = TrackMode::Mode1, format = StoredFormat::Raw2352;
        else if (type == "MODE2" || type == "MODE2_FORM_MIX")
            mode = TrackMode::Mode2, format = StoredFormat::Mode2_2336;
        else if (type == "MODE2_FORM1")
            mode = TrackMode::Mode2, format = StoredFormat::Cooked2048;
        else if (type == "MODE2_RAW")
            mode = TrackMode::Mode2, format = StoredFormat::Raw2352;
        else
            return false;
        return true;
    }

    /// endregion </CHD track metadata>
}  // namespace

namespace CdImageFormats
{
    std::string Probe(const std::string& path, std::string* error)
    {
        const std::string extension = StringHelper::ToLower(FileHelper::GetFileExtension(path));
        if (!FileHelper::IsFile(path))
        {
            Fail(error, "no such file: " + path);
            return "";
        }
        const uint64_t size = FileBytes(path);
        uint8_t head[16] = {};
        const bool headRead = size >= sizeof(head) && ReadAt(path, 0, head, sizeof(head));
        if (headRead && std::memcmp(head, "MComprHD", 8) == 0)
            return "chd";
        if (extension == "cue")
            return "cue";
        uint8_t volume[6] = {};
        if (size >= 0x8006 && ReadAt(path, 0x8000, volume, 6) && std::memcmp(volume + 1, "CD001", 5) == 0)
            return "iso";
        if (headRead && std::memcmp(head, kSync, sizeof(kSync)) == 0 && size % kFrameBytes == 0)
            return "bin";
        if (extension == "iso" && size % kUserBytes == 0 && size > 0)
            return "iso";  // an ISO without an ISO 9660 volume (another file system): still 2048-byte blocks
        Fail(error, "'" + path + "' is no CD image (an ISO 9660 image, a CUE sheet, a raw BIN of 2352-byte frames or a CD CHD)");
        return "";
    }

    std::unique_ptr<CdImage> Open(const std::string& path, std::string* error)
    {
        const std::string format = Probe(path, error);
        if (format == "iso")
            return OpenIso(path, error);
        if (format == "bin")
            return OpenRawBin(path, error);
        if (format == "cue")
            return OpenCue(path, error);
        if (format == "chd")
            return OpenChd(path, error);
        return nullptr;
    }

    std::unique_ptr<CdImage> OpenIso(const std::string& path, std::string* error)
    {
        const uint64_t size = FileBytes(path);
        if (size < kUserBytes)
        {
            Fail(error, "'" + path + "' is too short for a CD image");
            return nullptr;
        }
        auto source = FileSource::Open(path, 0, size, error);
        if (!source)
            return nullptr;
        StoredTrack track;
        track.track.number = 1;
        track.track.mode = TrackMode::Mode1;
        track.track.pregapLba = track.track.startLba = 0;
        track.track.endLba = static_cast<uint32_t>((size + kUserBytes - 1) / kUserBytes);
        track.source = 0;
        track.format = StoredFormat::Cooked2048;
        track.stride = kUserBytes;
        track.storedFirstLba = 0;
        track.storedEndLba = track.track.endLba;
        std::vector<std::unique_ptr<IFrameSource>> sources;
        sources.push_back(std::move(source));
        std::vector<StoredTrack> tracks{track};
        const uint64_t id = IdentityOf(FileHelper::AbsolutePath(path), tracks, size);
        return std::make_unique<CdImage>(std::move(sources), std::move(tracks), "iso", path, id);
    }

    std::unique_ptr<CdImage> OpenRawBin(const std::string& path, std::string* error)
    {
        const uint64_t size = FileBytes(path);
        uint8_t head[16] = {};
        if (size < kFrameBytes || !ReadAt(path, 0, head, sizeof(head)) || std::memcmp(head, kSync, sizeof(kSync)) != 0)
        {
            Fail(error, "'" + path + "' is no raw CD image (no sync pattern): give its CUE sheet");
            return nullptr;
        }
        auto source = FileSource::Open(path, 0, size, error);
        if (!source)
            return nullptr;
        StoredTrack track;
        track.track.number = 1;
        track.track.mode = head[15] == 2 ? TrackMode::Mode2 : TrackMode::Mode1;
        track.track.endLba = static_cast<uint32_t>(size / kFrameBytes);
        track.source = 0;
        track.format = StoredFormat::Raw2352;
        track.stride = kFrameBytes;
        track.storedEndLba = track.track.endLba;
        std::vector<std::unique_ptr<IFrameSource>> sources;
        sources.push_back(std::move(source));
        std::vector<StoredTrack> tracks{track};
        const uint64_t id = IdentityOf(FileHelper::AbsolutePath(path), tracks, size);
        return std::make_unique<CdImage>(std::move(sources), std::move(tracks), "bin", path, id);
    }

    std::unique_ptr<CdImage> OpenCue(const std::string& path, std::string* error)
    {
        const uint64_t size = FileBytes(path);
        if (size == 0 || size > 1024 * 1024)
        {
            Fail(error, "'" + path + "' is no CUE sheet");
            return nullptr;
        }
        std::string text(static_cast<size_t>(size), '\0');
        if (!ReadAt(path, 0, reinterpret_cast<uint8_t*>(text.data()), text.size()))
        {
            Fail(error, "cannot read '" + path + "'");
            return nullptr;
        }
        const std::filesystem::path fs = FileHelper::ToFsPath(path);
        const auto folder8 = fs.has_parent_path() ? fs.parent_path().u8string() : std::filesystem::path(".").u8string();
        return ParseCue(text, std::string(folder8.begin(), folder8.end()), path, error);
    }

    std::unique_ptr<CdImage> ParseCue(const std::string& text, const std::string& folder, const std::string& sheetPath, std::string* error)
    {
        std::vector<CueFile> files;
        std::vector<CueTrack> tracks;
        auto fail = [error, &sheetPath](int line, const std::string& reason) -> std::unique_ptr<CdImage> {
            Fail(error, sheetPath + (line > 0 ? ":" + std::to_string(line) : std::string()) + ": " + reason);
            return nullptr;
        };

        std::istringstream in(text);
        std::string line;
        int lineNumber = 0;
        while (std::getline(in, line))
        {
            lineNumber++;
            if (lineNumber == 1 && line.size() >= 3 && static_cast<uint8_t>(line[0]) == 0xEF && static_cast<uint8_t>(line[1]) == 0xBB &&
                static_cast<uint8_t>(line[2]) == 0xBF)
                line.erase(0, 3);  // UTF-8 byte order mark
            const std::vector<std::string> w = Words(line);
            if (w.empty())
                continue;
            const std::string key = StringHelper::ToUpper(w[0]);
            if (key == "FILE")
            {
                if (w.size() < 3)
                    return fail(lineNumber, "FILE needs a name and a type");
                CueFile file;
                file.path = ResolveFile(folder, w[1]);
                const std::string type = StringHelper::ToUpper(w[2]);
                if (type == "BINARY")
                    file.type = CueFile::Type::Binary;
                else if (type == "MOTOROLA")
                    file.type = CueFile::Type::Motorola;
                else if (type == "WAVE")
                    file.type = CueFile::Type::Wave;
                else
                    return fail(lineNumber, "FILE type " + w[2] + " is not supported (BINARY, MOTOROLA, WAVE are)");
                if (!FileHelper::IsFile(file.path))
                    return fail(lineNumber, "the file '" + w[1] + "' is not there (looked for '" + file.path + "')");
                if (file.type == CueFile::Type::Wave)
                {
                    std::string reason;
                    if (!WaveData(file.path, file.base, file.length, &reason))
                        return fail(lineNumber, reason);
                }
                else
                {
                    file.length = FileBytes(file.path);
                }
                files.push_back(file);
            }
            else if (key == "TRACK")
            {
                if (files.empty())
                    return fail(lineNumber, "TRACK before any FILE");
                if (w.size() < 3)
                    return fail(lineNumber, "TRACK needs a number and a type");
                CueTrack track;
                const int number = std::atoi(w[1].c_str());
                if (number < 1 || number > 99)
                    return fail(lineNumber, "track number " + w[1] + " is out of range (1-99)");
                if (!tracks.empty() && number <= tracks.back().number)
                    return fail(lineNumber, "track numbers must ascend");
                track.number = static_cast<uint8_t>(number);
                if (!TrackType(w[2], track))
                    return fail(lineNumber, "track type " + w[2] + " is not supported");
                track.file = static_cast<int>(files.size()) - 1;
                if (track.mode != TrackMode::Audio && files.back().type == CueFile::Type::Wave)
                    return fail(lineNumber, "a data track cannot be in a WAVE file");
                tracks.push_back(track);
            }
            else if (key == "INDEX")
            {
                if (tracks.empty() || w.size() < 3)
                    return fail(lineNumber, "INDEX outside a track");
                uint32_t frames = 0;
                if (!ParseTime(w[2], frames))
                    return fail(lineNumber, "bad time " + w[2]);
                const int index = std::atoi(w[1].c_str());
                if (index == 0)
                    tracks.back().index0 = frames;
                else if (index == 1)
                    tracks.back().index1 = frames;
                // INDEX 02+: inside the track, nothing to lay out
            }
            else if (key == "PREGAP" || key == "POSTGAP")
            {
                if (tracks.empty() || w.size() < 2)
                    return fail(lineNumber, key + " outside a track");
                uint32_t frames = 0;
                if (!ParseTime(w[1], frames))
                    return fail(lineNumber, "bad time " + w[1]);
                (key == "PREGAP" ? tracks.back().pregap : tracks.back().postgap) = frames;
            }
            // CATALOG, CDTEXTFILE, FLAGS, ISRC, PERFORMER, REM, SONGWRITER, TITLE: nothing to lay out
        }

        if (tracks.empty())
            return fail(0, "no TRACK");
        for (const CueTrack& t : tracks)
        {
            if (t.index1 < 0)
                return fail(0, "track " + std::to_string(t.number) + " has no INDEX 01");
            if (t.index0 > t.index1)
                return fail(0, "track " + std::to_string(t.number) + ": INDEX 00 after INDEX 01");
        }

        // Lay the tracks out on the disc: positions in frames, LBA 0 = track 1's INDEX 01
        std::vector<StoredTrack> stored;
        int64_t disc = 0;
        uint64_t fileByte = 0;  // the byte in the current file where the previous track's first index was
        int64_t fileFrame = 0;  // ... and its frame (in the previous track's stride)
        uint32_t previousStride = 0;
        int currentFile = -1;
        std::vector<int64_t> pregapAt(tracks.size());
        std::vector<int64_t> storedAt(tracks.size());
        std::vector<int64_t> startAt(tracks.size());
        std::vector<int64_t> endAt(tracks.size());
        for (size_t i = 0; i < tracks.size(); i++)
        {
            const CueTrack& t = tracks[i];
            const CueFile& file = files[t.file];
            const int64_t first = t.index0 >= 0 ? t.index0 : t.index1;
            if (t.file != currentFile)
            {
                currentFile = t.file;
                fileByte = 0;
                fileFrame = 0;
                previousStride = t.stride;
            }
            if (first < fileFrame)
                return fail(0, "track " + std::to_string(t.number) + " starts before the previous track in its file");
            // Bytes into the file of this track's first stored frame (the previous track's frames have its stride)
            const uint64_t byte = fileByte + static_cast<uint64_t>(first - fileFrame) * previousStride;
            fileByte = byte;
            fileFrame = first;
            previousStride = t.stride;

            // Frames stored for this track: up to the next track in the same file, or the file's end
            int64_t storedFrames = 0;
            if (i + 1 < tracks.size() && tracks[i + 1].file == t.file)
            {
                const CueTrack& next = tracks[i + 1];
                storedFrames = (next.index0 >= 0 ? next.index0 : next.index1) - first;
            }
            else
            {
                const uint64_t bytes = file.length > byte ? file.length - byte : 0;
                storedFrames = static_cast<int64_t>((bytes + t.stride - 1) / t.stride);
            }

            pregapAt[i] = disc;
            storedAt[i] = disc + t.pregap;
            startAt[i] = storedAt[i] + (t.index1 - first);
            endAt[i] = storedAt[i] + storedFrames + t.postgap;
            if (endAt[i] < startAt[i])
                return fail(0, "track " + std::to_string(t.number) + " has no frames after INDEX 01");
            disc = endAt[i];

            StoredTrack s;
            s.track.number = t.number;
            s.track.mode = t.mode;
            s.source = t.file;
            s.format = t.format;
            s.stride = t.stride;
            s.offset = byte;
            s.audioBigEndian = file.type == CueFile::Type::Motorola;
            s.storedFirstLba = 0;  // set below, after the shift
            s.storedEndLba = static_cast<uint32_t>(storedFrames);
            stored.push_back(s);
        }

        // Track 1's INDEX 01 is LBA 0: anything before it (its pregap) is the lead-in, not addressable
        const int64_t shift = startAt[0];
        for (size_t i = 0; i < stored.size(); i++)
        {
            StoredTrack& s = stored[i];
            int64_t storedFirst = storedAt[i] - shift;
            const int64_t storedEnd = storedFirst + s.storedEndLba;
            if (storedFirst < 0)
            {
                s.offset += static_cast<uint64_t>(-storedFirst) * s.stride;
                storedFirst = 0;
            }
            s.storedFirstLba = static_cast<uint32_t>(storedFirst);
            s.storedEndLba = static_cast<uint32_t>(std::max<int64_t>(storedEnd, storedFirst));
            s.track.pregapLba = static_cast<uint32_t>(std::max<int64_t>(pregapAt[i] - shift, 0));
            s.track.startLba = static_cast<uint32_t>(startAt[i] - shift);
            s.track.endLba = static_cast<uint32_t>(endAt[i] - shift);
        }

        std::vector<std::unique_ptr<IFrameSource>> sources;
        uint64_t sizes = 0;
        for (const CueFile& file : files)
        {
            auto source = FileSource::Open(file.path, file.base, file.length, error);
            if (!source)
                return nullptr;
            sources.push_back(std::move(source));
            sizes = sizes * 31 + file.length;
        }
        const uint64_t id = IdentityOf(FileHelper::AbsolutePath(sheetPath), stored, sizes);
        return std::make_unique<CdImage>(std::move(sources), std::move(stored), "cue", sheetPath, id);
    }

    bool IsCdChd(const std::string& path)
    {
        std::string error;
        return chd::ChdFile::OpenCd(path, &error) != nullptr;
    }

    std::unique_ptr<CdImage> OpenChd(const std::string& path, std::string* error)
    {
        std::unique_ptr<chd::ChdFile> file = chd::ChdFile::OpenCd(path, error);
        if (!file)
            return nullptr;

        std::vector<ChdTrackInfo> infos;
        for (const chd::MetadataEntry& entry : file->Metadata())
        {
            if (entry.tag != chd::MakeTag('C', 'H', 'T', '2') && entry.tag != chd::MakeTag('C', 'H', 'T', 'R'))
                continue;
            std::string text(entry.data.begin(), entry.data.end());
            while (!text.empty() && text.back() == '\0')
                text.pop_back();
            infos.push_back(ParseChdTrack(text));
        }
        if (infos.empty())
        {
            Fail(error, path + ": no CD track metadata (CHT2 / CHTR)");
            return nullptr;
        }
        std::sort(infos.begin(), infos.end(), [](const ChdTrackInfo& a, const ChdTrackInfo& b) { return a.number < b.number; });

        // MAME's layout (cdrom.cpp): tracks padded to 4 frames in the CHD; a
        // pregap stored in the CHD (PGTYPE V...) is inside FRAMES
        std::vector<StoredTrack> tracks;
        uint64_t chdFrame = 0;
        uint32_t lba = 0;
        for (const ChdTrackInfo& info : infos)
        {
            StoredTrack s;
            if (info.number < 1 || info.number > 99 || !ChdTrackType(info.type, s.track.mode, s.format))
            {
                Fail(error, path + ": track " + std::to_string(info.number) + " type '" + info.type + "' is not supported");
                return nullptr;
            }
            const bool pregapStored = !info.pgtype.empty() && info.pgtype[0] == 'V';
            s.track.number = static_cast<uint8_t>(info.number);
            s.source = 0;
            s.stride = chd::kCdFrameBytes;
            s.offset = chdFrame * chd::kCdFrameBytes;
            s.audioBigEndian = true;
            s.track.pregapLba = lba;
            s.track.startLba = lba + info.pregap;
            s.storedFirstLba = pregapStored ? lba : lba + info.pregap;
            s.storedEndLba = s.storedFirstLba + info.frames;
            s.track.endLba = s.storedEndLba + info.postgap;
            if (pregapStored && info.frames < info.pregap)
            {
                Fail(error, path + ": track " + std::to_string(info.number) + " is shorter than its pregap");
                return nullptr;
            }
            lba = s.track.endLba;
            chdFrame += (info.frames + 3) / 4 * 4;
            tracks.push_back(s);
        }
        if (static_cast<uint64_t>(chdFrame) * chd::kCdFrameBytes > file->LogicalBytes() + 4ull * chd::kCdFrameBytes)
        {
            Fail(error, path + ": the track metadata covers more frames than the CHD holds");
            return nullptr;
        }

        const chd::Sha1 sha1 = file->OverallSha1();
        uint64_t id = 0;
        std::memcpy(&id, sha1.data(), sizeof(id));
        id = IdentityOf(FileHelper::AbsolutePath(path), tracks, id);
        std::vector<std::unique_ptr<IFrameSource>> sources;
        sources.push_back(std::make_unique<ChdSource>(std::move(file)));
        return std::make_unique<CdImage>(std::move(sources), std::move(tracks), "chd", path, id);
    }
}  // namespace CdImageFormats
