#include "stdafx.h"

#include "audiofiledecoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

#include "common/filehelper.h"
#include "common/stringhelper.h"

// Third-party decoders: keep the project's warning set away from them
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wcomma"
#endif
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
// The MP3 decoder's implementation is compiled once, with the NeoGS VS10xx model
// (emulator/sound/chips/neogs/vs10xx.cpp): only its declarations here
#include "3rdparty/minimp3/minimp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO  // the file is read into memory by the caller (UTF-8 paths on every host)
#define DR_FLAC_NO_OGG
#include "3rdparty/dr_flac/dr_flac.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace AudioFileDecoder
{
    namespace
    {
        constexpr uint32_t kRedBookRate = 44100;

        bool Fail(std::string* error, const std::string& reason)
        {
            if (error)
                *error = reason;
            return false;
        }

        uint32_t Le32(const uint8_t* p)
        {
            return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }
        uint16_t Le16(const uint8_t* p)
        {
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }

        /// region <MP3>

        /// Where the audio frames are: after any ID3v2 tags, before ID3v1 / APEv2 at the end
        void Mp3Bounds(const std::vector<uint8_t>& bytes, size_t& begin, size_t& end)
        {
            begin = 0;
            end = bytes.size();
            while (end - begin >= 10 && std::memcmp(bytes.data() + begin, "ID3", 3) == 0)
            {
                const uint8_t* h = bytes.data() + begin;
                const size_t size = (static_cast<size_t>(h[6] & 0x7F) << 21) | ((h[7] & 0x7F) << 14) | ((h[8] & 0x7F) << 7) | (h[9] & 0x7F);
                begin = std::min(end, begin + 10 + size + ((h[5] & 0x10) ? 10 : 0));
            }
            if (end - begin >= 128 && std::memcmp(bytes.data() + end - 128, "TAG", 3) == 0)
                end -= 128;
            if (end - begin >= 32 && std::memcmp(bytes.data() + end - 32, "APETAGEX", 8) == 0)
            {
                const uint8_t* footer = bytes.data() + end - 32;
                size_t size = Le32(footer + 12);  // the items and this footer
                if (Le32(footer + 20) & 0x80000000u)
                    size += 32;  // a header too
                end = size <= end - begin ? end - size : begin;
            }
        }

        /// The first frame's Xing / Info / VBRI tag: that frame carries no audio. Its LAME tag
        /// (LAME, or Lavc / Lavf: FFmpeg writes the same layout) gives the encoder delay and
        /// padding; the decoder adds 528 + 1 samples of its own (as minimp3_ex and LAME count)
        struct VbrTag
        {
            bool present = false;
            bool gapless = false;
            int64_t delay = 0;
            int64_t padding = 0;
        };

        VbrTag FindVbrTag(const uint8_t* data, size_t length)
        {
            VbrTag tag;
            // The first frame header (sync, layer III)
            size_t at = 0;
            while (at + 4 <= length && !(data[at] == 0xFF && (data[at + 1] & 0xE0) == 0xE0 && (data[at + 1] & 0x06) == 0x02))
                at++;
            if (at + 4 > length || at > 4096)
                return tag;
            const uint8_t* h = data + at;
            const bool mpeg1 = (h[1] & 0x18) == 0x18;
            const bool mono = (h[3] >> 6) == 3;
            const size_t side = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
            const size_t crc = (h[1] & 0x01) ? 0 : 2;
            auto has = [&](size_t offset, const char* id) { return at + offset + 4 <= length && std::memcmp(h + offset, id, 4) == 0; };
            if (has(4 + 32 + crc, "VBRI") || has(4 + 32, "VBRI"))
            {
                tag.present = true;
                return tag;
            }
            size_t x = 0;
            if (has(4 + side + crc, "Xing") || has(4 + side + crc, "Info"))
                x = 4 + side + crc;
            else if (has(4 + side, "Xing") || has(4 + side, "Info"))
                x = 4 + side;
            else
                return tag;
            tag.present = true;
            if (at + x + 8 > length)
                return tag;
            const uint32_t flags = (static_cast<uint32_t>(h[x + 4]) << 24) | (h[x + 5] << 16) | (h[x + 6] << 8) | h[x + 7];
            size_t p = x + 8;
            p += (flags & 1) ? 4 : 0;
            p += (flags & 2) ? 4 : 0;
            p += (flags & 4) ? 100 : 0;
            p += (flags & 8) ? 4 : 0;
            if (at + p + 24 > length)
                return tag;
            const uint8_t* lame = h + p;
            if (std::memcmp(lame, "LAME", 4) != 0 && std::memcmp(lame, "Lavc", 4) != 0 && std::memcmp(lame, "Lavf", 4) != 0)
                return tag;
            tag.gapless = true;
            tag.delay = ((lame[21] << 4) | (lame[22] >> 4)) + 528 + 1;
            tag.padding = std::max<int64_t>(0, (((lame[22] & 0x0F) << 8) | lame[23]) - (528 + 1));
            return tag;
        }

        bool DecodeMp3(const std::vector<uint8_t>& bytes, Pcm& out, std::string* error)
        {
            size_t begin = 0;
            size_t end = 0;
            Mp3Bounds(bytes, begin, end);
            const VbrTag tag = FindVbrTag(bytes.data() + begin, end - begin);

            mp3dec_t decoder;
            mp3dec_init(&decoder);
            mp3dec_frame_info_t info{};
            mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
            out = Pcm{};
            size_t at = begin;
            bool first = true;
            while (at < end)
            {
                const int remaining = static_cast<int>(std::min<size_t>(end - at, 1u << 20));
                const int samples = mp3dec_decode_frame(&decoder, bytes.data() + at, remaining, pcm, &info);
                if (info.frame_bytes <= 0)
                    break;  // no further frame
                at += static_cast<size_t>(info.frame_bytes);
                if (first)
                {
                    first = false;
                    if (tag.present)
                        continue;  // the Xing / Info / VBRI frame: silence, not audio
                }
                if (samples <= 0)
                    continue;  // skipped data
                if (out.rate == 0)
                {
                    out.rate = static_cast<uint32_t>(info.hz);
                    out.channels = static_cast<uint32_t>(info.channels);
                }
                if (static_cast<uint32_t>(info.hz) != out.rate)
                    return Fail(error, "the sample rate changes inside the MP3 stream (" + std::to_string(out.rate) + " -> " +
                                           std::to_string(info.hz) + " Hz)");
                // A stream that switches between mono and stereo keeps its first layout
                for (int i = 0; i < samples; i++)
                {
                    if (info.channels == static_cast<int>(out.channels))
                    {
                        for (int c = 0; c < info.channels; c++)
                            out.samples.push_back(pcm[i * info.channels + c] / 32768.0f);
                    }
                    else if (out.channels == 2)
                    {
                        out.samples.push_back(pcm[i] / 32768.0f);
                        out.samples.push_back(pcm[i] / 32768.0f);
                    }
                    else
                    {
                        out.samples.push_back((pcm[2 * i] + pcm[2 * i + 1]) / 65536.0f);
                    }
                }
            }
            if (out.rate == 0 || out.samples.empty())
                return Fail(error, "no MP3 audio frames");
            if (tag.gapless)
            {
                // Cut the encoder's and decoder's delay at the start and the padding at the end
                const uint64_t frames = out.Frames();
                const uint64_t head = std::min<uint64_t>(frames, static_cast<uint64_t>(tag.delay));
                const uint64_t tail = std::min<uint64_t>(frames - head, static_cast<uint64_t>(tag.padding));
                out.samples.erase(out.samples.end() - static_cast<std::ptrdiff_t>(tail * out.channels), out.samples.end());
                out.samples.erase(out.samples.begin(), out.samples.begin() + static_cast<std::ptrdiff_t>(head * out.channels));
                if (out.samples.empty())
                    return Fail(error, "no MP3 audio after the encoder delay");
            }
            return true;
        }

        /// endregion </MP3>

        bool DecodeFlac(const std::vector<uint8_t>& bytes, Pcm& out, std::string* error)
        {
            unsigned channels = 0;
            unsigned rate = 0;
            drflac_uint64 frames = 0;
            float* samples = drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels, &rate, &frames, nullptr);
            if (!samples)
                return Fail(error, "not a FLAC stream this decoder reads");
            out = Pcm{};
            out.rate = rate;
            out.channels = channels;
            out.samples.assign(samples, samples + static_cast<size_t>(frames) * channels);
            drflac_free(samples, nullptr);
            if (out.rate == 0 || out.channels == 0 || out.samples.empty())
                return Fail(error, "the FLAC stream holds no samples");
            return true;
        }

        bool DecodeWav(const std::vector<uint8_t>& bytes, Pcm& out, std::string* error)
        {
            const size_t size = bytes.size();
            const uint8_t* b = bytes.data();
            if (size < 12 || std::memcmp(b, "RIFF", 4) != 0 || std::memcmp(b + 8, "WAVE", 4) != 0)
                return Fail(error, "not a RIFF WAVE file");
            uint16_t format = 0;
            uint16_t channels = 0;
            uint32_t rate = 0;
            uint16_t bits = 0;
            bool haveFormat = false;
            size_t at = 12;
            while (at + 8 <= size)
            {
                const uint32_t chunk = Le32(b + at + 4);
                const size_t body = at + 8;
                if (std::memcmp(b + at, "fmt ", 4) == 0)
                {
                    if (chunk < 16 || body + 16 > size)
                        return Fail(error, "a broken fmt chunk");
                    format = Le16(b + body);
                    channels = Le16(b + body + 2);
                    rate = Le32(b + body + 4);
                    bits = Le16(b + body + 14);
                    if (format == 0xFFFE)
                    {
                        if (chunk < 40 || body + 40 > size)
                            return Fail(error, "a broken WAVE_FORMAT_EXTENSIBLE fmt chunk");
                        format = Le16(b + body + 24);  // the sub-format GUID's first two bytes
                    }
                    haveFormat = true;
                }
                else if (std::memcmp(b + at, "data", 4) == 0)
                {
                    if (!haveFormat)
                        return Fail(error, "the data chunk comes before fmt");
                    const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
                    const bool ieee = format == 3 && (bits == 32 || bits == 64);
                    if (!pcm && !ieee)
                        return Fail(error, "WAVE format " + std::to_string(format) + " with " + std::to_string(bits) +
                                               " bits is not supported (PCM 8 / 16 / 24 / 32, float 32 / 64 are)");
                    if (channels == 0 || rate == 0)
                        return Fail(error, "no channels or no sample rate in the fmt chunk");
                    const size_t length = std::min<size_t>(chunk, size - body);
                    const size_t step = bits / 8u;
                    const size_t count = length / step / channels * channels;
                    out = Pcm{};
                    out.rate = rate;
                    out.channels = channels;
                    out.samples.resize(count);
                    const uint8_t* p = b + body;
                    for (size_t i = 0; i < count; i++, p += step)
                    {
                        float v = 0;
                        if (ieee && bits == 32)
                        {
                            const uint32_t raw = Le32(p);
                            std::memcpy(&v, &raw, 4);
                        }
                        else if (ieee)
                        {
                            const uint64_t raw = Le32(p) | (static_cast<uint64_t>(Le32(p + 4)) << 32);
                            double d = 0;
                            std::memcpy(&d, &raw, 8);
                            v = static_cast<float>(d);
                        }
                        else if (bits == 8)
                            v = (static_cast<int>(p[0]) - 128) / 128.0f;
                        else if (bits == 16)
                            v = static_cast<int16_t>(Le16(p)) / 32768.0f;
                        else if (bits == 24)
                            v = static_cast<float>(static_cast<int32_t>((p[0] << 8) | (p[1] << 16) | (static_cast<uint32_t>(p[2]) << 24)) / 2147483648.0);
                        else
                            v = static_cast<float>(static_cast<int32_t>(Le32(p)) / 2147483648.0);
                        out.samples[i] = v;
                    }
                    if (out.samples.empty())
                        return Fail(error, "the WAVE file holds no samples");
                    return true;
                }
                at = body + chunk + (chunk & 1);
            }
            return Fail(error, "no data chunk");
        }

        /// Interleaved samples of any channel count -> stereo pairs (see the header)
        std::vector<float> ToStereo(const Pcm& pcm)
        {
            const uint64_t frames = pcm.Frames();
            const uint32_t channels = pcm.channels;
            std::vector<float> out(static_cast<size_t>(frames) * 2);
            if (channels == 2)
            {
                std::copy(pcm.samples.begin(), pcm.samples.begin() + static_cast<std::ptrdiff_t>(frames * 2), out.begin());
                return out;
            }
            if (channels == 1)
            {
                for (uint64_t i = 0; i < frames; i++)
                    out[2 * i] = out[2 * i + 1] = pcm.samples[i];
                return out;
            }
            // FL, FR, C, LFE, then left / right pairs (WAVE and FLAC channel order)
            constexpr float kSide = 0.70710678f;
            const bool center = channels >= 3;
            const uint32_t pairs = channels > 4 ? (channels - 4) / 2 : 0;
            const float norm = 1.0f + (center ? kSide : 0.0f) + kSide * static_cast<float>(pairs);
            for (uint64_t i = 0; i < frames; i++)
            {
                const float* s = pcm.samples.data() + i * channels;
                float left = s[0];
                float right = s[1];
                if (center)
                {
                    left += kSide * s[2];
                    right += kSide * s[2];
                }
                for (uint32_t p = 0; p < pairs; p++)
                {
                    left += kSide * s[4 + 2 * p];
                    right += kSide * s[5 + 2 * p];
                }
                out[2 * i] = left / norm;
                out[2 * i + 1] = right / norm;
            }
            return out;
        }

        int16_t ToInt16(float value)
        {
            const float scaled = std::nearbyint(value * 32768.0f);
            return static_cast<int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
        }
    }  // namespace

    Kind KindOf(const std::string& path)
    {
        const std::string ext = StringHelper::ToLower(FileHelper::GetFileExtension(path));
        if (ext == "mp3")
            return Kind::Mp3;
        if (ext == "flac")
            return Kind::Flac;
        if (ext == "wav" || ext == "wave")
            return Kind::Wav;
        return Kind::None;
    }

    const char* KindName(Kind kind)
    {
        switch (kind)
        {
            case Kind::Mp3: return "mp3";
            case Kind::Flac: return "flac";
            case Kind::Wav: return "wav";
            case Kind::None: break;
        }
        return "none";
    }

    bool Decode(Kind kind, const std::vector<uint8_t>& bytes, Pcm& out, std::string* error)
    {
        switch (kind)
        {
            case Kind::Mp3: return DecodeMp3(bytes, out, error);
            case Kind::Flac: return DecodeFlac(bytes, out, error);
            case Kind::Wav: return DecodeWav(bytes, out, error);
            case Kind::None: break;
        }
        return Fail(error, "not an MP3, FLAC or WAV file");
    }

    uint64_t RedBookFrames(uint64_t frames, uint32_t rate)
    {
        if (rate == 0)
            return 0;
        return (frames * kRedBookRate + rate - 1) / rate;
    }

    std::vector<int16_t> ToRedBook(const Pcm& pcm)
    {
        const std::vector<float> stereo = ToStereo(pcm);
        const uint64_t frames = stereo.size() / 2;
        if (pcm.rate == kRedBookRate)
        {
            std::vector<int16_t> out(stereo.size());
            for (size_t i = 0; i < stereo.size(); i++)
                out[i] = ToInt16(stereo[i]);
            return out;
        }

        // Polyphase windowed sinc in the exact ratio L / M (output n sits at input n x M / L)
        const uint32_t g = std::gcd(kRedBookRate, pcm.rate);
        const uint64_t up = kRedBookRate / g;    // L
        const uint64_t down = pcm.rate / g;      // M
        const double ratio = std::min(1.0, static_cast<double>(up) / static_cast<double>(down));
        const double cutoff = 0.5 * ratio * 0.95;  // cycles per input sample
        constexpr double kZeroCrossings = 16.0;
        const int half = static_cast<int>(std::ceil(kZeroCrossings / ratio));
        const int taps = 2 * half;
        const double pi = 3.14159265358979323846;
        std::vector<float> table(static_cast<size_t>(up) * taps);
        for (uint64_t phase = 0; phase < up; phase++)
        {
            const double frac = static_cast<double>(phase) / static_cast<double>(up);
            double sum = 0;
            for (int k = 0; k < taps; k++)
            {
                const double x = (k - half + 1) - frac;  // input k - half + 1 relative to the output position
                const double sinc = x == 0.0 ? 1.0 : std::sin(2 * pi * cutoff * x) / (2 * pi * cutoff * x);
                const double w = std::abs(x) >= half ? 0.0 : 0.42 + 0.5 * std::cos(pi * x / half) + 0.08 * std::cos(2 * pi * x / half);
                const double h = 2 * cutoff * sinc * w;
                table[phase * taps + k] = static_cast<float>(h);
                sum += h;
            }
            for (int k = 0; k < taps; k++)
                table[phase * taps + k] = static_cast<float>(table[phase * taps + k] / sum);  // unity gain at DC
        }

        const uint64_t outFrames = RedBookFrames(frames, pcm.rate);
        std::vector<int16_t> out(static_cast<size_t>(outFrames) * 2);
        for (uint64_t n = 0; n < outFrames; n++)
        {
            const uint64_t position = n * down;
            const int64_t base = static_cast<int64_t>(position / up);
            const float* h = table.data() + (position % up) * taps;
            double left = 0;
            double right = 0;
            for (int k = 0; k < taps; k++)
            {
                const int64_t i = base + k - half + 1;
                if (i < 0 || static_cast<uint64_t>(i) >= frames)
                    continue;
                left += h[k] * stereo[static_cast<size_t>(2 * i)];
                right += h[k] * stereo[static_cast<size_t>(2 * i + 1)];
            }
            out[2 * n] = ToInt16(static_cast<float>(left));
            out[2 * n + 1] = ToInt16(static_cast<float>(right));
        }
        return out;
    }
}  // namespace AudioFileDecoder
