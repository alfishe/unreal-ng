#include "stdafx.h"

#include "tapeaudio/tapeaudiorenderer.h"

#include "common/filehelper.h"
#include "emulator/io/tape/tapepulsegen.h"
#include "encoders/ffmpeg_pipe_encoder.h"
#include "ffmpeg_probe.h"
#include "loaders/tape/loader_tape.h"
#include "tapeaudio/tapeaudioconfig.h"
#include "tapeaudio/tapeaudiowavencoder.h"

#include <algorithm>
#include <memory>

/// region <Documentation>

/// Render pipeline (design §5): load → select encoder by extension → per
/// block materialize pulses → fixed-point square-wave synthesis → EncoderBase.
/// The sample grid is absolute (32.32 T-states per step), so edge rounding
/// never accumulates — every pulse lands within ±1 sample of its rational
/// position for the whole file (design §5.2).

/// endregion </Documentation>

namespace
{
    /// Per-render synthesis state — one sample grid over the whole file.
    /// The chunk-flush callback fires whenever the PCM buffer reaches the
    /// streaming granularity, so a single long block (a data-block pilot is
    /// ~100 s of audio) never buffers whole (design §9).
    class SquareWaveWriter
    {
    public:
        using FlushCallback = std::function<void()>;

        SquareWaveWriter(uint32_t sampleRate, double amplitude, std::vector<int16_t>& chunk, FlushCallback onChunkFull)
            : _step((uint64_t(TapeAudio::TSTATE_HZ) << 32) / sampleRate),
              _amplitude(static_cast<int16_t>(amplitude * 32767.0 + 0.5)),
              _chunk(chunk),
              _onChunkFull(std::move(onChunkFull))
        {
        }

        /// Samples advance the absolute cursor; the level holds until the
        /// next edge crosses it. `halfPeriods` are T-state half-periods.
        void WritePulses(const std::vector<uint32_t>& halfPeriods, bool& level)
        {
            for (uint32_t pulse : halfPeriods)
            {
                _nextEdge += uint64_t(pulse) << 32;

                while (_cursor < _nextEdge)
                {
                    _chunk.push_back(level ? _amplitude : -_amplitude);
                    _cursor += _step;
                    CheckChunk();
                }

                level = !level;
            }
        }

        /// Pause = digital silence (design §4.2): a held ±A level for a
        /// second would DC-bias downstream gear, and real cassette gaps are
        /// silence anyway.
        void WriteSilenceMs(uint32_t ms)
        {
            _nextEdge += (uint64_t(ms) * TapeAudio::TSTATES_PER_MS) << 32;

            while (_cursor < _nextEdge)
            {
                _chunk.push_back(0);
                _cursor += _step;
                CheckChunk();
            }
        }

    private:
        void CheckChunk()
        {
            if (_chunk.size() >= TapeAudio::PCM_CHUNK_SAMPLES && _onChunkFull)
            {
                _onChunkFull();
            }
        }

        uint64_t _step;        // T-states per sample, 32.32 fixed point
        uint64_t _cursor = 0;  // absolute position, 32.32
        uint64_t _nextEdge = 0;
        int16_t _amplitude;
        std::vector<int16_t>& _chunk;
        FlushCallback _onChunkFull;
    };

    std::string LowerExtension(const std::string& path)
    {
        size_t dot = path.rfind('.');
        if (dot == std::string::npos)
        {
            return {};
        }
        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }

    bool LoadTapeImage(const std::string& path, TapeImage& image, std::string& errorText)
    {
        // FileHelper::GetFileSize answers SIZE_MAX for anything unreadable —
        // check existence first so a bad path never becomes vector(SIZE_MAX)
        if (!FileHelper::FileExists(path))
        {
            errorText = "source not readable: '" + path + "'";
            return false;
        }

        size_t size = FileHelper::GetFileSize(path);
        std::vector<uint8_t> buffer(size);
        if (size > 0 && FileHelper::ReadFileToBuffer(path, buffer.data(), size) != size)
        {
            errorText = "source read failed: '" + path + "'";
            return false;
        }

        LoaderTapeBase* loader = TapeLoaderRegistry::Instance().Select(buffer, path);
        if (loader == nullptr)
        {
            errorText = "no tape loader claims '" + path + "' (supported: " + []()
            {
                std::string joined;
                for (const std::string& ext : TapeLoaderRegistry::Instance().SupportedExtensions())
                {
                    joined += (joined.empty() ? "" : ", ") + ext;
                }
                return joined;
            }() + ")";
            return false;
        }

        image = loader->Load(buffer, path);
        if (!image.IsUsable())
        {
            errorText = "tape image not usable: " + (image.errorText.empty() ? "no playable blocks" : image.errorText);
            return false;
        }

        return true;
    }
}

TapeRenderResult RenderTapeToAudio(const TapeRenderRequest& request)
{
    TapeRenderResult result;

    // --- Range + parameter validation (before any file is touched)
    size_t last = request.lastBlock;
    if (last == SIZE_MAX)
    {
        last = request.firstBlock;  // resolved against the image below
    }

    if (request.sampleRate < 8000 || request.sampleRate > 192000)
    {
        result.errorText = "sample rate out of range (8000..192000): " + std::to_string(request.sampleRate);
        return result;
    }
    if (request.amplitude <= 0.0 || request.amplitude > 1.0)
    {
        result.errorText = "amplitude out of range (0..1]: " + std::to_string(request.amplitude);
        return result;
    }

    // --- Load
    TapeImage image;
    if (!LoadTapeImage(request.sourcePath, image, result.errorText))
    {
        return result;
    }

    size_t blockCount = image.blocks.size();
    if (request.firstBlock >= blockCount)
    {
        result.errorText = "first block " + std::to_string(request.firstBlock) + " out of range (image has " +
                           std::to_string(blockCount) + " blocks)";
        return result;
    }
    if (request.lastBlock != SIZE_MAX && request.lastBlock >= blockCount)
    {
        result.warnings.push_back("last block clamped to " + std::to_string(blockCount - 1));
    }
    if (request.lastBlock == SIZE_MAX || request.lastBlock >= blockCount)
    {
        last = blockCount - 1;
    }
    if (last < request.firstBlock)
    {
        result.errorText = "empty block range";
        return result;
    }

    // --- Encoder by extension (design §5.3)
    const std::string ext = LowerExtension(request.outputPath);
    EncoderConfig config;
    config.audioSampleRate = request.sampleRate;
    config.audioOutputSampleRate = request.sampleRate;  // native passthrough — no resampling
    config.audioChannels = 1;
    config.audioCodec = "flac";
    config.videoCodec.clear();  // audio-only: no video pipe, no video input args
    config.videoWidth = 0;
    config.videoHeight = 0;
    config.ffmpegPath = FFmpegProbe::findFFmpeg();

    std::unique_ptr<EncoderBase> encoder;
    if (ext == "wav")
    {
        encoder = std::make_unique<TapeAudioWavEncoder>();
        result.encoderUsed = "tinywav";
    }
    else if (ext == "flac")
    {
        if (!FFmpegProbe::isAvailable(config.ffmpegPath))
        {
            result.errorText = "ffmpeg not found — FLAC render unavailable; export WAV instead";
            return result;
        }
        auto flacEncoder = std::make_unique<FFmpegPipeEncoder>();
        flacEncoder->setBlocking(true);  // offline render may outrun realtime
        encoder = std::move(flacEncoder);
        result.encoderUsed = "ffmpeg(flac)";
    }
    else
    {
        result.errorText = "unsupported output extension '" + ext + "' (expected .wav or .flac)";
        return result;
    }

    if (!encoder->Start(request.outputPath, config))
    {
        result.errorText = "encoder start failed: " + encoder->GetLastError();
        return result;
    }

    // --- Synthesis
    std::vector<int16_t> chunk;
    chunk.reserve(TapeAudio::PCM_CHUNK_SAMPLES);

    auto flush = [&chunk, &encoder, &result, &request]()
    {
        if (chunk.empty())
        {
            return;
        }
        encoder->OnAudioSamples(chunk.data(), chunk.size(),
                                static_cast<double>(result.samplesWritten) / request.sampleRate);
        result.samplesWritten += chunk.size();
        chunk.clear();
    };

    SquareWaveWriter writer(request.sampleRate, request.amplitude, chunk, [&flush]() { flush(); });

    const size_t blocksTotal = last - request.firstBlock + 1;
    size_t blocksDone = 0;
    bool cancelled = false;

    for (size_t i = request.firstBlock; i <= last; i++)
    {
        if (request.cancelRequested && request.cancelRequested())
        {
            cancelled = true;
            break;
        }

        const TapeBlock& block = image.blocks[i];
        const TapeBlockDescriptor* descriptor = i < image.descriptors.size() ? &image.descriptors[i] : nullptr;

        TapePulseGen::MaterializedPulses pulses = TapePulseGen::MaterializePulses(block, descriptor);

        // Level convention (design §4.2): entries alternate starting LOW;
        // TZX $2B polarity and the request override flip the start only.
        bool level = request.invertLevel ||
                     (descriptor != nullptr && descriptor->timing.invertedLevel);

        if (pulses.halfPeriods.empty() && pulses.pauseAfterMs == 0)
        {
            result.warnings.push_back("block " + std::to_string(i) + ": no signal (control-only), skipped");
        }
        else
        {
            if (pulses.halfPeriods.empty())
            {
                result.warnings.push_back("block " + std::to_string(i) + ": silence only");
            }
            writer.WritePulses(pulses.halfPeriods, level);
            writer.WriteSilenceMs(pulses.pauseAfterMs);
            result.blocksRendered++;
        }

        flush();

        blocksDone++;
        if (request.onProgress)
        {
            request.onProgress(blocksDone, blocksTotal);
        }
    }

    encoder->Stop();

    if (cancelled)
    {
        // No orphan half-written files (design §5.3): Stop() finalized the
        // partial output, so remove it explicitly.
        std::remove(request.outputPath.c_str());
        result.errorText = "cancelled";
        result.ok = false;
        result.durationSec = 0.0;
        result.samplesWritten = 0;
        result.blocksRendered = 0;
        return result;
    }

    if (result.blocksRendered == 0)
    {
        result.errorText = "nothing renderable in block range";
        return result;
    }

    result.durationSec = static_cast<double>(result.samplesWritten) / request.sampleRate;
    result.ok = true;
    return result;
}

bool IsFlacRenderAvailable()
{
    // Same probe the FLAC path uses — one filesystem lookup, no subprocess
    return FFmpegProbe::isAvailable(FFmpegProbe::findFFmpeg());
}
