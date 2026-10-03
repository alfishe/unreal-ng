#pragma once

/// @file audiofiledecoder.h
/// @brief Host audio files (MP3, FLAC, WAV) decoded to Red Book PCM: 44100 Hz,
/// 16-bit, stereo. The audio-CD-from-a-folder builder (audiofolderdisc.h) turns
/// each file into one track with it.
///
/// | Format | Decoder | Takes |
/// |---|---|---|
/// | MP3 | minimp3 (3rdparty/minimp3, CC0) frame by frame | MPEG-1 / 2 / 2.5 layer III (and I / II), any rate, mono / stereo; an ID3v2 tag at the start and ID3v1 / APE at the end are skipped; the Xing / Info frame is no audio and its LAME tag's encoder delay and padding are cut (gapless, as the encoder wrote it) |
/// | FLAC | dr_flac (3rdparty/dr_flac, public domain / MIT-0) | native FLAC, 4 to 32 bits, 1 to 8 channels, any rate |
/// | WAV | here | RIFF WAVE: PCM 8 / 16 / 24 / 32-bit, IEEE float 32 / 64-bit, WAVE_FORMAT_EXTENSIBLE of those |
///
/// Conversion (one way, the same every time: a disc built twice is the same disc):
/// - **Channels.** Mono plays on both sides. Two channels stay. More channels (in
///   the WAVE / FLAC order: front left, front right, center, LFE, then left / right
///   pairs) mix down: L = FL + 0.7071 C + 0.7071 (every left surround), R alike,
///   LFE left out, divided by the sum of the left side's weights so nothing clips
///   (ITU-R BS.775 coefficients).
/// - **Rate.** 44100 Hz stays sample for sample. Any other rate goes through a
///   windowed-sinc resampler (Blackman window, 16 zero crossings each side, cut-off
///   at 0.95 x the lower Nyquist frequency) in its exact rational ratio, a polyphase
///   table per ratio: 48000 -> 44100 is 147 / 160.
/// - **Bits.** Floating point to 16 bits by rounding to nearest and clamping, no
///   dither.
///
/// Worked example: a 48 kHz mono FLAC of 4800 samples (0.1 s) becomes 4410
/// stereo sample pairs, left = right.

#include <cstdint>
#include <string>
#include <vector>

namespace AudioFileDecoder
{
    enum class Kind : uint8_t
    {
        None,
        Mp3,
        Flac,
        Wav,
    };

    /// By extension (case-insensitive): .mp3, .flac, .wav / .wave
    Kind KindOf(const std::string& path);
    const char* KindName(Kind kind);

    /// A decoded file before conversion: interleaved samples in [-1, 1)
    struct Pcm
    {
        uint32_t rate = 0;
        uint32_t channels = 0;
        std::vector<float> samples;
        uint64_t Frames() const { return channels ? samples.size() / channels : 0; }
    };

    /// Decode `bytes` (the file's content) of `kind`. False with a reason
    bool Decode(Kind kind, const std::vector<uint8_t>& bytes, Pcm& out, std::string* error = nullptr);

    /// To Red Book: interleaved 16-bit stereo at 44100 Hz (see the file comment)
    std::vector<int16_t> ToRedBook(const Pcm& pcm);

    /// Stereo sample pairs ToRedBook makes of `frames` at `rate` (the resampler's length rule:
    /// ceil(frames x 44100 / rate))
    uint64_t RedBookFrames(uint64_t frames, uint32_t rate);
}  // namespace AudioFileDecoder
