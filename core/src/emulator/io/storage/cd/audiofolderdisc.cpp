#include "stdafx.h"

#include "audiofolderdisc.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "common/filehelper.h"
#include "emulator/io/storage/cd/audiofiledecoder.h"

using namespace cd;

namespace AudioFolderDisc
{
    namespace
    {
        bool Fail(std::string* error, const std::string& reason)
        {
            if (error)
                *error = reason;
            return false;
        }

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

        /// The decoded disc in memory: the tracks' frames back to back, little-endian samples
        class PcmSource : public IFrameSource
        {
        public:
            PcmSource(std::vector<int16_t> pcm, std::string description) : _pcm(std::move(pcm)), _description(std::move(description)) {}

            bool Read(uint64_t offset, uint8_t* dst, uint32_t length) override
            {
                const uint64_t bytes = static_cast<uint64_t>(_pcm.size()) * 2;
                std::memset(dst, 0, length);
                if (offset >= bytes)
                    return true;
                const uint64_t available = std::min<uint64_t>(length, bytes - offset);
                // Host order -> little-endian (the CD image layer's audio frames)
                const uint8_t* src = reinterpret_cast<const uint8_t*>(_pcm.data()) + offset;
                if (IsLittleEndian())
                {
                    std::memcpy(dst, src, static_cast<size_t>(available));
                }
                else
                {
                    for (uint64_t i = 0; i < available; i++)
                    {
                        const uint64_t at = offset + i;
                        const uint16_t sample = static_cast<uint16_t>(_pcm[static_cast<size_t>(at / 2)]);
                        dst[i] = static_cast<uint8_t>((at & 1) ? sample >> 8 : sample);
                    }
                }
                return true;
            }
            std::string Describe() const override { return _description; }

        private:
            static bool IsLittleEndian()
            {
                const uint16_t probe = 1;
                uint8_t first = 0;
                std::memcpy(&first, &probe, 1);
                return first == 1;
            }

            std::vector<int16_t> _pcm;
            std::string _description;
        };

        std::string Utf8(const std::filesystem::path& path)
        {
            const auto u8 = path.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::string Duration(uint64_t frames)
        {
            char text[32];
            const uint64_t seconds = frames / kFramesPerSecond;
            std::snprintf(text, sizeof(text), "%u:%02u", static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
            return text;
        }

        bool ReadAll(const std::string& path, std::vector<uint8_t>& bytes)
        {
            std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
            if (!in)
                return false;
            in.seekg(0, std::ios::end);
            const std::streamoff size = in.tellg();
            if (size < 0)
                return false;
            bytes.resize(static_cast<size_t>(size));
            in.seekg(0);
            in.read(reinterpret_cast<char*>(bytes.data()), size);
            return static_cast<std::streamoff>(in.gcount()) == size;
        }

        struct Candidate
        {
            std::string name;
            std::string path;
            AudioFileDecoder::Kind kind = AudioFileDecoder::Kind::None;
        };

        /// The folder's audio files in natural order; `ignored` counts everything else
        bool List(const std::string& folder, std::vector<Candidate>& files, uint32_t& ignored, std::string* error)
        {
            std::error_code ec;
            const std::filesystem::path root = FileHelper::ToFsPath(folder);
            if (!std::filesystem::is_directory(root, ec))
                return Fail(error, "'" + folder + "' is no folder");
            for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            {
                const std::string name = Utf8(it->path().filename());
                const AudioFileDecoder::Kind kind = AudioFileDecoder::KindOf(name);
                std::error_code typeError;
                if (name.empty() || name[0] == '.' || kind == AudioFileDecoder::Kind::None || !it->is_regular_file(typeError))
                {
                    ignored++;
                    continue;
                }
                files.push_back({name, Utf8(it->path()), kind});
            }
            if (ec)
                return Fail(error, "cannot list '" + folder + "': " + ec.message());
            std::sort(files.begin(), files.end(), [](const Candidate& a, const Candidate& b) { return NaturalCompare(a.name, b.name) < 0; });
            return true;
        }
    }  // namespace

    int NaturalCompare(const std::string& a, const std::string& b)
    {
        size_t i = 0;
        size_t j = 0;
        auto lower = [](unsigned char c) { return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c; };
        auto digit = [](char c) { return c >= '0' && c <= '9'; };
        while (i < a.size() && j < b.size())
        {
            if (digit(a[i]) && digit(b[j]))
            {
                // Two numbers: by value (leading zeros dropped: by length, then digit by digit)
                size_t ei = i;
                size_t ej = j;
                while (ei < a.size() && digit(a[ei]))
                    ei++;
                while (ej < b.size() && digit(b[ej]))
                    ej++;
                size_t si = i;
                size_t sj = j;
                while (si + 1 < ei && a[si] == '0')
                    si++;
                while (sj + 1 < ej && b[sj] == '0')
                    sj++;
                if (ei - si != ej - sj)
                    return ei - si < ej - sj ? -1 : 1;
                for (; si < ei; si++, sj++)
                {
                    if (a[si] != b[sj])
                        return a[si] < b[sj] ? -1 : 1;
                }
                i = ei;
                j = ej;
                continue;
            }
            const unsigned char ca = lower(static_cast<unsigned char>(a[i]));
            const unsigned char cb = lower(static_cast<unsigned char>(b[j]));
            if (ca != cb)
                return ca < cb ? -1 : 1;
            i++;
            j++;
        }
        if (i < a.size() || j < b.size())
            return i < a.size() ? 1 : -1;
        return a == b ? 0 : (a < b ? -1 : 1);  // equal but for case or leading zeros: by the bytes
    }

    bool HasAudioFiles(const std::string& folder)
    {
        std::vector<Candidate> files;
        uint32_t ignored = 0;
        return List(folder, files, ignored, nullptr) && !files.empty();
    }

    std::vector<std::string> Result::Lines() const
    {
        std::vector<std::string> lines;
        char line[64];
        for (const FileEntry& f : files)
        {
            switch (f.status)
            {
                case FileEntry::Status::Taken:
                    std::snprintf(line, sizeof(line), "track %02u: ", static_cast<unsigned>(f.track));
                    lines.push_back(line + f.name + " (" + f.kind + ", " + Duration(f.frames) + ")");
                    break;
                case FileEntry::Status::Skipped:
                    lines.push_back("skipped: " + f.name + ": " + f.reason);
                    break;
                case FileEntry::Status::NotTaken:
                    lines.push_back("not taken: " + f.name + ": " + f.reason);
                    break;
            }
        }
        if (ignored)
            lines.push_back("ignored: " + std::to_string(ignored) + " folder entries that are no MP3 / FLAC / WAV files");
        std::snprintf(line, sizeof(line), "audio CD: lead-out at %s of 80:00, built in %.2f s", Duration(leadOutLba + kLeadInFrames).c_str(),
                      seconds);
        lines.push_back(line);
        return lines;
    }

    std::unique_ptr<CdImage> Build(const std::string& folder, Result& result, std::string* error, const Options& options)
    {
        const auto started = std::chrono::steady_clock::now();
        result = Result{};
        std::vector<Candidate> candidates;
        if (!List(folder, candidates, result.ignored, error))
            return nullptr;
        if (candidates.empty())
        {
            Fail(error, "'" + folder + "' holds no MP3, FLAC or WAV files (an audio CD is built from those)");
            return nullptr;
        }

        std::vector<int16_t> pcm;
        std::vector<StoredTrack> tracks;
        std::vector<std::string> titles;
        uint64_t identity = 0xcbf29ce484222325ULL;
        uint32_t lba = 0;  // the next track's pregap (track 1: its INDEX 01)
        bool full = false;
        std::string fullReason;
        uint64_t bytesRead = 0;
        for (const Candidate& candidate : candidates)
        {
            FileEntry entry;
            entry.name = candidate.name;
            entry.kind = AudioFileDecoder::KindName(candidate.kind);
            if (full)
            {
                entry.reason = fullReason;
                result.files.push_back(entry);
                continue;
            }
            if (options.cancelRequested && options.cancelRequested())
            {
                Fail(error, "cancelled");
                return nullptr;
            }
            if (tracks.size() >= kMaxTracks)
            {
                full = true;
                fullReason = "the disc already has 99 tracks (the Red Book limit)";
                entry.reason = fullReason;
                result.files.push_back(entry);
                continue;
            }
            std::vector<uint8_t> bytes;
            AudioFileDecoder::Pcm decoded;
            std::string reason;
            const bool read = ReadAll(candidate.path, bytes);
            bytesRead += bytes.size();
            if (options.onProgress)
                options.onProgress(result.files.size() + 1, bytesRead);
            if (!read)
                reason = "cannot read the file";
            else if (!AudioFileDecoder::Decode(candidate.kind, bytes, decoded, &reason))
                reason = "does not decode: " + reason;
            if (!reason.empty())
            {
                entry.status = FileEntry::Status::Skipped;
                entry.reason = reason;
                result.files.push_back(entry);
                continue;
            }
            std::vector<int16_t> red = AudioFileDecoder::ToRedBook(decoded);
            const uint64_t samples = red.size() / 2;
            const uint32_t frames = static_cast<uint32_t>(std::max<uint64_t>(kMinTrackFrames, (samples + kSamplesPerFrame - 1) / kSamplesPerFrame));
            const uint32_t pregap = tracks.empty() ? 0 : kPregapFrames;
            const uint64_t endLba = static_cast<uint64_t>(lba) + pregap + frames;
            if (endLba + kLeadInFrames > options.capacityFrames)
            {
                full = true;
                const uint64_t left = options.capacityFrames - kLeadInFrames - lba;
                entry.reason = "does not fit: needs " + Duration(pregap + frames) + " (with its pregap), " + Duration(left) +
                               " left on the 80-minute disc";
                fullReason = "the disc ends at the first file that did not fit (" + candidate.name + "); no packing";
                result.files.push_back(entry);
                continue;
            }

            StoredTrack track;
            track.track.number = static_cast<uint8_t>(tracks.size() + 1);
            track.track.session = 1;
            track.track.mode = TrackMode::Audio;
            track.track.pregapLba = lba;
            track.track.startLba = lba + pregap;
            track.track.endLba = static_cast<uint32_t>(endLba);
            track.source = 0;
            track.format = StoredFormat::Raw2352;
            track.stride = kFrameBytes;
            track.offset = static_cast<uint64_t>(pcm.size()) * 2;
            track.storedFirstLba = track.track.startLba;
            track.storedEndLba = track.track.endLba;
            red.resize(static_cast<size_t>(frames) * kSamplesPerFrame * 2, 0);  // silence to the frame boundary / 4 s
            pcm.insert(pcm.end(), red.begin(), red.end());
            tracks.push_back(track);
            titles.push_back(candidate.name);
            lba = track.track.endLba;

            identity = Fnv(identity, candidate.name.data(), candidate.name.size());
            identity = Fnv(identity, bytes.data(), bytes.size());
            const uint32_t layout[] = {track.track.number, track.track.pregapLba, track.track.startLba, track.track.endLba};
            identity = Fnv(identity, layout, sizeof(layout));

            entry.status = FileEntry::Status::Taken;
            entry.track = track.track.number;
            entry.frames = frames;
            entry.samples = samples;
            result.files.push_back(entry);
        }
        result.leadOutLba = lba;
        result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        if (tracks.empty())
        {
            Fail(error, "no file of '" + folder + "' could be taken: " + result.files.front().name + ": " + result.files.front().reason);
            return nullptr;
        }

        pcm.shrink_to_fit();  // the buffer grew track by track: keep only the disc
        std::vector<std::unique_ptr<IFrameSource>> sources;
        sources.push_back(std::make_unique<PcmSource>(std::move(pcm), folder));
        auto disc = std::make_unique<CdImage>(std::move(sources), std::move(tracks), "audio-cd", folder, identity);
        disc->SetTrackTitles(std::move(titles));
        return disc;
    }
}  // namespace AudioFolderDisc
