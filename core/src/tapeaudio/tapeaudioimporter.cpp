#include "stdafx.h"

#include "tapeaudio/tapeaudioimporter.h"

#include "common/filehelper.h"
#include "3rdparty/tinywav/tinywav.h"
#include "emulator/io/tape/tapecatalog.h"
#include "ffmpeg_probe.h"
#include "loaders/tape/writer_tap.h"
#include "loaders/tape/writer_tzx.h"
#include "common/subprocess.h"
#include "tapeaudio/tapeaudioconfig.h"
#include "tapeaudio/tapepulseextractor.h"
#include "tapeaudio/taperecognizer.h"

#include <algorithm>
#include <memory>

/// region <Documentation>

/// See tapeaudioimporter.h. Decode details:
///   WAV  — tinywav_read_f (any bit depth, normalized to float), first
///          channel of multi-channel input, native sample rate.
///   FLAC/MP3 — `ffmpeg -i src -f s16le -ac 1 -ar 44100 -` through the
///          existing Subprocess plumbing, bytes → int16 LE → float/32768.
///          Fixed 44100 keeps the pipeline deterministic without an ffprobe
///          metadata parser; the recognizer tolerances absorb resampling.

/// endregion </Documentation>

namespace
{
    std::string LowerExtension(const std::string& path)
    {
        const size_t dot = path.rfind('.');
        if (dot == std::string::npos)
        {
            return {};
        }
        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }

    std::string BaseName(const std::string& path)
    {
        const size_t slash = path.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos)
        {
            name = name.substr(0, dot);
        }
        return name;
    }

    bool DecodeWav(const std::string& path, std::vector<float>& samples, uint32_t& sampleRate, std::string& errorText)
    {
        TinyWav tw;
        if (tinywav_open_read(&tw, path.c_str(), TW_INTERLEAVED) != 0)
        {
            errorText = "tinywav cannot open '" + path + "' (corrupt or unsupported WAV)";
            return false;
        }

        sampleRate = static_cast<uint32_t>(tw.h.SampleRate);
        const int channels = tw.h.NumChannels;

        std::vector<float> chunk(4096 * channels);
        int read = 0;
        while ((read = tinywav_read_f(&tw, chunk.data(), static_cast<int>(chunk.size() / channels))) > 0)
        {
            for (int i = 0; i < read * channels; i += channels)
            {
                samples.push_back(chunk[i]);  // first channel — antiphase stereo must not cancel
            }
        }
        tinywav_close_read(&tw);

        if (samples.empty())
        {
            errorText = "WAV contains no audio frames";
            return false;
        }
        return true;
    }

    bool DecodeViaFfmpeg(const std::string& path, const std::string& formatLabel,
                         std::vector<float>& samples, uint32_t& sampleRate, std::string& errorText)
    {
        const std::string ffmpeg = FFmpegProbe::findFFmpeg();
        if (!FFmpegProbe::isAvailable(ffmpeg))
        {
            errorText = "ffmpeg not found — " + formatLabel + " import unavailable (WAV import needs no ffmpeg)";
            return false;
        }

        Subprocess ffmpegProc;
        if (!ffmpegProc.spawn(ffmpeg, { "-hide_banner", "-loglevel", "error", "-i", path,
                                        "-map", "a:0", "-f", "s16le", "-acodec", "pcm_s16le",
                                        "-ac", "1", "-ar", std::to_string(TapeAudio::DEFAULT_SAMPLE_RATE), "-" }))
        {
            errorText = "cannot spawn ffmpeg: " + ffmpegProc.getLastError();
            return false;
        }

        const std::string raw = ffmpegProc.readAllStdout();
        ffmpegProc.closeStdin();
        const int exitCode = ffmpegProc.waitForFinished(30000);

        if (exitCode != 0 || raw.empty())
        {
            const std::string stderrTail = ffmpegProc.readAllStderr();
            errorText = "ffmpeg decode failed for '" + path + "'" + (stderrTail.empty() ? "" : ": " + stderrTail.substr(0, 300));
            return false;
        }

        sampleRate = TapeAudio::DEFAULT_SAMPLE_RATE;
        samples.resize(raw.size() / 2);
        for (size_t i = 0; i + 1 < raw.size(); i += 2)
        {
            const int16_t value = static_cast<int16_t>(static_cast<uint8_t>(raw[i]) | (static_cast<uint8_t>(raw[i + 1]) << 8));
            samples[i / 2] = static_cast<float>(value) / 32768.0f;
        }
        return true;
    }

    /// RecognizedBlock → TapeBlock + descriptor, mirroring LoaderTZX's emit
    /// conventions one block type at a time (the loaders are the contract).
    void AppendRecognized(const RecognizedBlock& recognized, TapeImage& image)
    {
        TapeBlock block;
        block.blockIndex = image.blocks.size();
        block.type = recognized.data.empty() ? TAP_BLOCK_FLAG_DATA
                                             : static_cast<TapeBlockFlagEnum>(recognized.data[0]);

        TapeBlockDescriptor descriptor;

        switch (recognized.kind)
        {
            case RecognizedBlockKind::StandardBlock:
                block.data = recognized.data;  // representation 1 — ROM encoding implied
                descriptor.timing.pauseMs = static_cast<uint16_t>(recognized.pauseMs);
                descriptor.playable = true;
                break;

            case RecognizedBlockKind::TurboBlock:
                block.data = recognized.data;
                block.timing = recognized.timing;  // representation 2
                descriptor.timing = recognized.timing;
                descriptor.playable = true;
                break;

            case RecognizedBlockKind::PureTone:
                block.edgePulseTimings.assign(recognized.tonePulses, recognized.tonePeriod);
                block.totalBitstreamLength = uint64_t(recognized.tonePulses) * recognized.tonePeriod;
                descriptor.kind = TapeBlockKindEnum::Tone;
                descriptor.timing.profile = TapeSpeedProfileEnum::PulseStream;
                descriptor.playable = true;
                break;

            case RecognizedBlockKind::PulseSequence:
                block.edgePulseTimings = recognized.pulses;
                for (uint32_t pulse : block.edgePulseTimings)
                {
                    block.totalBitstreamLength += pulse;
                }
                descriptor.kind = TapeBlockKindEnum::PulseStream;
                descriptor.timing.profile = TapeSpeedProfileEnum::PulseStream;
                descriptor.playable = true;
                break;

            case RecognizedBlockKind::PauseGap:
            {
                const uint32_t holdT = recognized.pauseMs * TapeAudio::TSTATES_PER_MS;
                block.edgePulseTimings.push_back(holdT);
                block.totalBitstreamLength = holdT;
                descriptor.kind = TapeBlockKindEnum::Control;
                descriptor.timing.pauseMs = static_cast<uint16_t>(std::min<uint32_t>(recognized.pauseMs, 0xFFFF));
                descriptor.playable = true;
                break;
            }
        }

        image.blocks.push_back(std::move(block));
        image.descriptors.push_back(std::move(descriptor));
    }
}

TapeImportResult ImportAudioToTape(const TapeImportRequest& request)
{
    TapeImportResult result;

    auto stage = [&request](const char* name)
    {
        if (request.onStage)
        {
            request.onStage(name);
        }
    };

    if (!FileHelper::FileExists(request.sourcePath))
    {
        result.errorText = "source not readable: '" + request.sourcePath + "'";
        return result;
    }

    // --- Decode
    stage("decode");
    const std::string ext = LowerExtension(request.sourcePath);
    std::vector<float> samples;
    if (ext == "wav")
    {
        result.decoderUsed = "tinywav(wav)";
        if (!DecodeWav(request.sourcePath, samples, result.sampleRate, result.errorText))
        {
            return result;
        }
    }
    else if (ext == "flac" || ext == "mp3")
    {
        result.decoderUsed = "ffmpeg(" + ext + ")";
        if (!DecodeViaFfmpeg(request.sourcePath, ext, samples, result.sampleRate, result.errorText))
        {
            return result;
        }
    }
    else
    {
        result.errorText = "unsupported input extension '" + ext + "' (expected .wav, .flac or .mp3)";
        return result;
    }
    result.samplesDecoded = samples.size();

    if (request.cancelRequested && request.cancelRequested())
    {
        result.errorText = "cancelled";
        return result;
    }

    // --- Extract
    stage("extract");
    TapePulseExtractorOptions extractorOptions;
    extractorOptions.hysteresis = request.hysteresis;
    const TapePulseTrack track = TapePulseExtractor::Extract(samples, result.sampleRate, extractorOptions);
    result.signalEdges = track.signalEdges;
    if (track.entries.empty())
    {
        result.errorText = "no tape signal found — silence or noise below the detection threshold";
        return result;
    }

    if (request.cancelRequested && request.cancelRequested())
    {
        result.errorText = "cancelled";
        return result;
    }

    // --- Recognize
    stage("recognize");
    const std::vector<RecognizedBlock> recognized = TapeRecognizer::Recognize(track.entries);

    result.image.formatId = "tzx";  // canonical exchange format until saved (design §6.4)
    result.image.title = BaseName(request.sourcePath);
    for (const RecognizedBlock& block : recognized)
    {
        AppendRecognized(block, result.image);
    }

    // Full catalog derivation — the same pass Tape::EnsureImageLoaded runs
    // for files, so headers, pairing and fast-load analysis read correctly.
    result.image.descriptors = TapeCatalogParser::Build(result.image);

    for (const RecognizedBlock& block : recognized)
    {
        for (const std::string& note : block.notes)
        {
            result.warnings.push_back(note);
        }
    }

    result.blocksRecognized = recognized.size();
    result.ok = !result.image.blocks.empty();
    if (!result.ok)
    {
        result.errorText = "recognition produced no blocks";
    }
    return result;
}

TapeSaveResult SaveTapeImage(const TapeImage& image, const std::string& outputPath)
{
    TapeSaveResult result;

    const std::string ext = LowerExtension(outputPath);
    std::string errorText;

    if (ext == "tzx")
    {
        if (TzxArchiveWriter::Save(image, outputPath, errorText))
        {
            result.ok = true;
            result.blocksWritten = image.blocks.size();
        }
        else
        {
            result.errorText = errorText;
        }
    }
    else if (ext == "tap")
    {
        if (TapArchiveWriter::Save(image, outputPath, errorText))
        {
            result.ok = true;
            result.blocksWritten = image.blocks.size();
        }
        else
        {
            result.errorText = errorText;
        }
    }
    else
    {
        result.errorText = "unsupported output extension '" + ext + "' (expected .tzx or .tap)";
    }

    return result;
}
