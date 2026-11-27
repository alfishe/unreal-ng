#include "windowrecorder.h"

#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "devicescreenwrapper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "osdkeyboard/osdkeyboardwindow.h"
#include "common/framescaler.h"
#include "common/threadpool.h"
#include "common/named_pipe.h"
#include "ffmpeg_probe.h"
#ifdef __APPLE__
#include "common/encoderconfig.h"
#include "platform/macos/videotoolbox_encoder.h"
#include "platform/macos/recordingsurfaces.h"
#endif

namespace
{
const QColor kBackground(24, 24, 24);

/// The switches an ffmpeg build takes for a variable frame rate output: -fps_mode since ffmpeg 5.1, -vsync before
/// it (Linux distributions still ship 4.x); -enc_time_base where the build has it (else the times are rounded to
/// the frame rate ffmpeg guesses). Cached per ffmpeg
QStringList FfmpegVfrArgs(const QString& ffmpeg)
{
    static std::mutex cacheMutex;
    static std::map<QString, QStringList> cache;
    std::lock_guard<std::mutex> lock(cacheMutex);
    const auto found = cache.find(ffmpeg);
    if (found != cache.end())
        return found->second;
    QProcess help;
    help.start(ffmpeg, {"-hide_banner", "-h", "long"});
    const QByteArray text = help.waitForFinished(15000) ? help.readAllStandardOutput() : QByteArray();
    if (help.state() != QProcess::NotRunning)
        help.kill();
    QStringList args{text.contains("-fps_mode") ? QStringLiteral("-fps_mode") : QStringLiteral("-vsync"), "vfr"};
    if (text.contains("-enc_time_base"))
        args << "-enc_time_base" << "1:1000000";  // the stream's microseconds, exact
    return cache[ffmpeg] = args;
}

// The picture goes to ffmpeg as Matroska: uncompressed B,G,R,A frames, each in its own cluster with its time in
// microseconds. A picture held for several frames (a still screen, a writer that falls behind) is sent once, its
// next picture's time says how long it lasts: nothing of the repeats crosses the pipe

/// EBML's variable-length size: the shortest one that fits (all ones is "unknown", never used for a known size)
QByteArray EbmlSize(uint64_t size)
{
    int length = 1;
    while (length < 8 && size >= (uint64_t{1} << (7 * length)) - 1)
        length++;
    QByteArray out(length, '\0');
    const uint64_t coded = size | (uint64_t{1} << (7 * length));
    for (int i = 0; i < length; i++)
        out[length - 1 - i] = static_cast<char>((coded >> (8 * i)) & 0xFF);
    return out;
}

QByteArray EbmlElement(uint32_t id, const QByteArray& payload)
{
    QByteArray out;
    for (int shift = 24; shift >= 0; shift -= 8)
        if ((id >> shift) != 0)
            out.append(static_cast<char>((id >> shift) & 0xFF));
    return out + EbmlSize(static_cast<uint64_t>(payload.size())) + payload;
}

QByteArray EbmlUint(uint32_t id, uint64_t value)
{
    QByteArray payload;
    do
    {
        payload.prepend(static_cast<char>(value & 0xFF));
        value >>= 8;
    } while (value != 0);
    return EbmlElement(id, payload);
}

/// The file's start: one video track of width x height frames in `colorSpace` (the FourCC of uncompressed video:
/// "BGRA", "NV12": the Y plane, then U,V interleaved at half resolution, or "444P": the Y, U and V planes), times in microseconds, the segment's size
/// unknown (it is a stream)
QByteArray MatroskaStreamHeader(int width, int height, const char* colorSpace)
{
    const QByteArray ebml = EbmlElement(0x1A45DFA3, EbmlUint(0x4286, 1) + EbmlUint(0x42F7, 1) + EbmlUint(0x42F2, 4) +
                                                        EbmlUint(0x42F3, 8) + EbmlElement(0x4282, "matroska") +
                                                        EbmlUint(0x4287, 4) + EbmlUint(0x4285, 2));
    const QByteArray info =
        EbmlElement(0x1549A966, EbmlUint(0x2AD7B1, 1000) + EbmlElement(0x4D80, "unreal-ng") + EbmlElement(0x5741, "unreal-ng"));
    const QByteArray video = EbmlElement(0xE0, EbmlUint(0xB0, static_cast<uint64_t>(width)) +
                                                   EbmlUint(0xBA, static_cast<uint64_t>(height)) +
                                                   EbmlElement(0x2EB524, colorSpace));
    const QByteArray track = EbmlElement(0xAE, EbmlUint(0xD7, 1) + EbmlUint(0x73C5, 1) + EbmlUint(0x83, 1) +
                                                   EbmlElement(0x86, "V_UNCOMPRESSED") + video);
    const QByteArray segment = QByteArray::fromHex("18538067") + QByteArray::fromHex("01FFFFFFFFFFFFFF");
    return ebml + segment + info + EbmlElement(0x1654AE6B, track);
}

/// One frame's cluster up to its pixels (the `bytes` of the picture follow it): the time, a key frame of track 1
QByteArray MatroskaFrameHeader(uint64_t timeUs, qsizetype bytes)
{
    const QByteArray block = QByteArray::fromHex("81000080");  // track 1, relative time 0, key frame
    const QByteArray simpleBlock = QByteArray::fromHex("A3") + EbmlSize(static_cast<uint64_t>(block.size() + bytes)) + block;
    const QByteArray body = EbmlUint(0xE7, timeUs) + simpleBlock;
    return QByteArray::fromHex("1F43B675") + EbmlSize(static_cast<uint64_t>(body.size() + bytes)) + body;
}

/// The GPU encoder ffmpeg offers for the codec, found rather than assumed: the build lists it AND a real trial
/// encode works (NVENC is listed by every build, whatever the card; VA-API needs the card's driver). Empty name:
/// none, the caller encodes in software. Cached per ffmpeg and codec: the trials run once
struct HardwareEncoder
{
    QString name;            ///< h264_nvenc, hevc_vaapi, ...; empty = none
    QString device;          ///< VA-API: the render node the trial opened (/dev/dri/renderD128)
    bool full = false;       ///< takes 4:4:4: the CRT's color mask stays sharp (4:2:0 halves its color resolution)
    bool lowPower = false;   ///< VA-API: the fixed-function encoder (Intel VDENC): the fast one, and HEVC 4:4:4 only there
};

std::mutex hardwareCacheMutex;
std::map<QString, HardwareEncoder> hardwareCache;

QString HardwareCacheKey(const QString& ffmpeg, bool h265, bool only420)
{
    return ffmpeg + (h265 ? "|h265" : "|h264") + (only420 ? "|420" : "");
}

/// `only420`: the frames come as NV12 (transfer nv12): the 4:4:4 ways are not tried. The trials start ffmpeg several
/// times (about a second each on Windows): WindowRecorder::PrepareHardwareEncoder runs them off the GUI thread
HardwareEncoder FfmpegHardwareEncoder(const QString& ffmpeg, bool h265, bool only420)
{
    const QString key = HardwareCacheKey(ffmpeg, h265, only420);
    {
        std::lock_guard<std::mutex> lock(hardwareCacheMutex);
        const auto found = hardwareCache.find(key);
        if (found != hardwareCache.end())
            return found->second;
    }
    const std::string path = ffmpeg.toStdString();
    const FFmpegProbe::HWAccelInfo info = FFmpegProbe::detectHWAccel(path);
    const QString codec = h265 ? "hevc_" : "h264_";
    // Each way to run an encoder, the best first: a trial of a few frames into the null muxer opens the device or
    // it does not (the card, its driver, the profile)
    std::vector<HardwareEncoder> ways;
#ifdef __APPLE__
    if (info.videoToolbox)
        ways.push_back({codec + "videotoolbox", {}, false, false});
#else
    if (info.nvenc)
    {
        ways.push_back({codec + "nvenc", {}, true, false});  // Turing and later: High 4:4:4 / HEVC Rext
        ways.push_back({codec + "nvenc", {}, false, false});
    }
#ifdef __linux__
    if (info.vaapi)
    {
        // Every render node: a laptop has the Intel and the NVIDIA one, VA-API runs on the one with its driver
        const QStringList nodes = QDir("/dev/dri").entryList({"renderD*"}, QDir::System, QDir::Name);
        for (const QString& node : nodes)
        {
            const QString device = "/dev/dri/" + node;
            if (h265)
            {
                ways.push_back({codec + "vaapi", device, true, true});  // Intel Ice Lake and later: HEVC 4:4:4 low power
                ways.push_back({codec + "vaapi", device, false, true});
            }
            ways.push_back({codec + "vaapi", device, false, false});
        }
    }
#else
    // QSV off Linux only. On Linux VA-API drives the same Intel encoder, and h264_qsv / hevc_qsv refused the
    // recorder's stream there ("Current frame rate is unsupported": the rate comes from the VFR Matroska stream,
    // which has none) while the trial's fixed-rate lavfi source passed (ffmpeg 8.0, oneVPL 26.1)
    if (info.qsv)
        ways.push_back({codec + "qsv", {}, false, false});
#endif
    if (info.amf)
        ways.push_back({codec + "amf", {}, false, false});
#endif
    HardwareEncoder chosen;
    for (const HardwareEncoder& way : ways)
    {
        if ((only420 && way.full) || !FFmpegProbe::isEncoderAvailable(way.name.toStdString(), path))
            continue;
        QStringList args{"-v", "error", "-f", "lavfi", "-i", "color=size=256x256:rate=10", "-frames:v", "3"};
        if (!way.device.isEmpty())
            args << "-vaapi_device" << way.device << "-vf" << (way.full ? "format=vuyx,hwupload" : "format=nv12,hwupload");
        args << "-c:v" << way.name;
        if (way.lowPower)
            args << "-low_power" << "1";
        if (way.device.isEmpty())
            args << "-pix_fmt" << (way.full ? "yuv444p" : "yuv420p");
        args << "-f" << "null" << "-";
        QProcess trial;
        trial.start(ffmpeg, args);
        if (trial.waitForFinished(15000) && trial.exitStatus() == QProcess::NormalExit && trial.exitCode() == 0)
        {
            chosen = way;
            break;
        }
        if (trial.state() != QProcess::NotRunning)
            trial.kill();
    }
    std::lock_guard<std::mutex> lock(hardwareCacheMutex);
    hardwareCache[key] = chosen;
    return chosen;
}
/// ffmpeg on the PATH; a GUI started from the Finder has no Homebrew PATH
QString FindFfmpeg()
{
    QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty())
        ffmpeg = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
    return ffmpeg;
}

/// The Mac's own encoder (VideoToolbox: H.264 / H.265 on the GPU, ProRes 4444) needs no ffmpeg; ffmpeg only (an MKV,
/// or asked): ffmpeg's ProRes / GPU encoder, the Mac's writer makes no MKV
bool NativeWriter(const WindowRecordingRequest& request)
{
#ifdef __APPLE__
    return !request.ffmpegOnly && (request.codec == "prores" || request.hardware);
#else
    (void)request;
    return false;
#endif
}

/// The arguments of FfmpegHardwareEncoder for @p request; false when it asks for no probe (software, ProRes, the
/// Mac's own writer, no ffmpeg)
bool HardwareProbeOf(const WindowRecordingRequest& request, QString& ffmpeg, bool& h265, bool& nv12)
{
    if (!request.hardware || request.codec == "prores" || NativeWriter(request))
        return false;
    ffmpeg = FindFfmpeg();
    h265 = request.codec == "h265";
    nv12 = request.transfer == "nv12";
    return !ffmpeg.isEmpty();
}

constexpr int kMaxQueuedFrames = 10;  // the encoder behind by this many pictures: the next one waits for room (video sync)

QImage ToImage(const WindowViewGrab& grab)
{
    QImage image(grab.rgba.data(), grab.width, grab.height, static_cast<qsizetype>(grab.width) * 4,
                 QImage::Format_RGBA8888);
    return image.copy();  // own the pixels: the grab goes away
}

/// What the writer needs to finish the file
struct MuxJob
{
    QString ffmpeg, path, videoPath, soundPath;
    uint32_t sampleRate = 0;
    uint64_t frames = 0;
};

/// The video file and the sound into the requested file (the video copied, the sound encoded for the container)
bool Mux(const MuxJob& job, std::string& error)
{
    const QString& path = job.path;
    if (job.frames == 0)  // nothing recorded: the empty picture file is the result
    {
        QFile::remove(path);
        QFile::remove(job.soundPath);
        return QFile::rename(job.videoPath, path);
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    QStringList args{"-v", "error", "-y", "-i", job.videoPath, "-f", "s16le", "-ar", QString::number(job.sampleRate),
                     "-ac", QString::number(AUDIO_CHANNELS), "-i", job.soundPath, "-map", "0:v:0", "-map", "1:a:0",
                     "-c:v", "copy"};
    if (suffix == "mp4" || suffix == "m4v")  // MP4 takes no raw PCM
        args << "-c:a" << "aac" << "-b:a" << "320k";
    else
        args << "-c:a" << "pcm_s16le";
    args << path;
    QProcess muxer;
    muxer.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    muxer.start(job.ffmpeg, args);
    while (!muxer.waitForFinished(5000) && muxer.state() != QProcess::NotRunning)
    {
    }
    if (muxer.exitStatus() != QProcess::NormalExit || muxer.exitCode() != 0)
    {
        error = "ffmpeg could not add the sound (the picture is in " + job.videoPath.toStdString() +
                ", the sound in " + job.soundPath.toStdString() + ")";
        return false;
    }
    QFile::remove(job.videoPath);
    QFile::remove(job.soundPath);
    return true;
}
}  // namespace

struct WindowRecorder::Writer
{
    struct Job
    {
        QImage picture;   ///< the window's picture (null and no `frame`: a sound-only job)
        /// The whole frame the GPU composed in the hardware encoder's own buffer (macOS), instead of `picture`
        std::shared_ptr<void> frame;
        QImage keyboard;  ///< the keyboard under it (null = none)
        uint32_t repeat = 0;
        uint64_t frameStart = 0;  ///< the picture's first frame in the file (its time: frameStart / fps)
        QByteArray sound;
        double soundTime = 0;     ///< the sound's emulated time in seconds

        bool hasPicture() const { return !picture.isNull() || frame; }
    };

    // The file's layout
    QString ffmpeg;
    QStringList args;
    QSize size;
    QRect screenRect, keyboardRect;
    int scale = 1;
    // transfer nv12: the frame built in NV12 itself (composeNv12), the writer thread's alone
    FrameScaler::PlaneScaler lumaScaler, chromaScaler;
    std::vector<uint8_t> pictureYuv;  ///< the picture's Y, U, V planes at its own size (scaled from)
    MuxJob mux;
    bool native = false;   ///< the macOS hardware encoder writes the file itself (no ffmpeg, no muxing)
    bool nv12 = false;     ///< transfer nv12: the composed frame goes to ffmpeg as NV12 (else B,G,R,A)
    bool yuv444 = false;   ///< transfer yuv444: the composed frame goes to ffmpeg as planar Y, U, V (yuv444p)
    double fps = 50;
    int nativeQuality = 8;
    std::string nativeCodec = "h264";
    uint32_t audioRate = 44100;
    bool audio = false;
    /// The sound goes into the same ffmpeg through this pipe (no muxing afterwards): a FIFO in soundDir (Unix), a
    /// named pipe (Windows). Null: the sound goes to a file, muxed at the end
    std::shared_ptr<NamedPipe> soundPipe;
    std::shared_ptr<QTemporaryDir> soundDir;  ///< the FIFO's private directory (this user's alone), removed with it
    std::atomic<uint64_t> framesDone{0};
    /// The writer thread's helpers for a frame's rows (made once: three threads started for every frame cost more
    /// than the rows at 1080p)
    std::unique_ptr<ThreadPool> rowPool;

    /// `band(part)` for each of kParts parts at once, on the writer thread and its helpers
    static constexpr int kParts = 4;
    void inParts(const std::function<void(int)>& band) const
    {
        if (!rowPool)
        {
            for (int part = 0; part < kParts; ++part)
                band(part);
            return;
        }
        rowPool->ParallelFor(kParts, kParts, [&](int first, int last) {
            for (int part = first; part < last; ++part)
                band(part);
        });
    }

    // Timings in microseconds since the writer was made (0 = not reached): the status shows where a late start went
    const std::chrono::steady_clock::time_point created = std::chrono::steady_clock::now();
    std::atomic<int64_t> writerStartUs{0}, encoderReadyUs{0}, firstFrameUs{0}, slowestFrameUs{0};
    int64_t sinceCreatedUs() const
    {
        return std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - created).count());
    }
    /// UNREAL_WINDOW_RECORDER_TIMING: every picture's time on the writer and how it went, on stderr
    /// (tools/verification/recording); read once per recording
    bool timing = false;

    /// One picture handled: the first one's time, the slowest one's duration. `how`: `native` (the Mac's writer),
    /// `nv12` / `yuv444` (built in its planes), `composed` (composed B,G,R,A, sent as it is or as NV12), `same` (not sent: as before)
    void pictureHandled(int64_t startedUs, const char* how)
    {
        const int64_t now = sinceCreatedUs();
        if (timing)
            std::fprintf(stderr, "window-recorder frame %s %.3f ms\n", how, static_cast<double>(now - startedUs) / 1000.0);
        int64_t zero = 0;
        firstFrameUs.compare_exchange_strong(zero, now);
        const int64_t took = now - startedUs;
        int64_t slowest = slowestFrameUs.load();
        while (took > slowest && !slowestFrameUs.compare_exchange_weak(slowest, took))
        {
        }
    }

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    int queuedPictures = 0;
    bool closing = false;
    bool wantRetry = false;                 ///< a paused machine's picture was refused: tell the GUI when there is room
    std::function<void()> onRoom;           ///< called from the writer thread
    std::atomic<bool> failed{false};
    /// The encoder is up (the hardware encoder's first start, or ffmpeg's, can take seconds on a loaded machine):
    /// the GUI side starts the file's clock only then, so no picture or sound of the wait is lost or piles up
    std::atomic<bool> ready{false};
    std::atomic<bool> done{false};
    std::string error;                      ///< valid once done
    std::thread thread;

    bool hasRoom()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queuedPictures < kMaxQueuedFrames;
    }
    /// GUI thread, never waits. A picture is refused (false) when the writer is behind
    bool pushPicture(Job&& job)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queuedPictures >= kMaxQueuedFrames)
            return false;
        queuedPictures++;
        jobs.push_back(std::move(job));
        wake.notify_one();
        return true;
    }
    void pushSound(QByteArray&& sound, double time)
    {
        std::lock_guard<std::mutex> lock(mutex);
        Job job;
        job.sound = std::move(sound);
        job.soundTime = time;
        jobs.push_back(std::move(job));
        wake.notify_one();
    }
    /// Writer thread: the sound queued behind the picture being written, taken out of the queue in its order. A
    /// picture repeated for the frames the writer owes is written before the sound of those frames, which is already
    /// queued after it: ffmpeg interleaves the two inputs and takes no more picture until it has that sound, so the
    /// writer must hand the sound over while it waits for the picture (else both wait for good)
    void takeQueuedSound(std::deque<QByteArray>& into)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = jobs.begin(); it != jobs.end();)
            if (!it->hasPicture() && !it->sound.isEmpty())
            {
                into.push_back(std::move(it->sound));
                it = jobs.erase(it);
            }
            else
                ++it;
    }
    void requestRetry()
    {
        std::lock_guard<std::mutex> lock(mutex);
        wantRetry = true;
    }
    void close(uint64_t frames)
    {
        std::lock_guard<std::mutex> lock(mutex);
        mux.frames = frames;
        closing = true;
        wake.notify_one();
    }

    QImage compose(const Job& job) const
    {
        QImage frame(size, QImage::Format_RGBA8888);
        composeInto(frame, job);
        return frame;
    }

    /// The common case at full size: the window's picture (RGBA) is at most a few pixels off the frame's picture
    /// area, so it is placed in the middle with no scaling and its red / blue swapped row by row into the frame
    /// (ARGB32 = B,G,R,A bytes). Qt's smooth scaling and format conversion of a 4K picture took 25-50 ms, more than
    /// a frame, and the frames owed were held (the interlace then jumps instead of flickering)
    bool composeFast(QImage& frame, const Job& job) const
    {
        const QImage& picture = job.picture;
        const bool swap = picture.format() == QImage::Format_RGBA8888;
        const bool same = picture.format() == QImage::Format_ARGB32_Premultiplied ||
                          picture.format() == QImage::Format_ARGB32 || picture.format() == QImage::Format_RGB32;
        const bool bgra = frame.format() == QImage::Format_ARGB32 || frame.format() == QImage::Format_ARGB32_Premultiplied;
        if (scale != 1 || !bgra || (!swap && !same))
            return false;
        if (std::abs(picture.width() - screenRect.width()) > 4 || std::abs(picture.height() - screenRect.height()) > 4)
            return false;
        // Centered; a picture a few pixels larger than the frame (the pipe's frame is made even: a 659 wide window
        // picture in 658) loses its edge pixels instead of going through Qt's smooth scaling
        const int left = screenRect.x() + (screenRect.width() - picture.width()) / 2;
        const int top = screenRect.y() + (screenRect.height() - picture.height()) / 2;
        const int fromX = std::max(0, -left);
        const int fromY = std::max(0, -top);
        const int x0 = std::max(0, left);
        const int y0 = std::max(0, top);
        const int width = std::min(picture.width() - fromX, frame.width() - x0);
        const int rows = std::min(picture.height() - fromY, frame.height() - y0);
        if (width <= 0 || rows <= 0 || picture.width() - width > 4 || picture.height() - rows > 4)
            return false;
        // Rows are independent: a few threads write them at once (the encoder's pixel buffer is shared memory, one
        // thread does not fill its bandwidth). Only the background around the picture is written (the whole frame
        // filled first wrote a 1080p frame twice)
        // The width in a local: read through the QImage in the loop condition it is reloaded after every store (a
        // uint32_t store may alias the image's fields), which kept the loop scalar. A 4K frame on 4 threads / on one:
        // 5.2 / 16+ ms -> 1.2 / 2 (clang, M1), 12.7 / 29 -> 4.9 / 7.1 (gcc 15), 4.4 / 16.8 -> 2.4 / 3.1 (MSVC)
        const int frameWidth = frame.width();
        const int frameHeight = frame.height();
        const uint32_t background = kBackground.rgb();
        const auto band = [&](int part)
        {
            const int first = frameHeight * part / kParts;
            const int last = frameHeight * (part + 1) / kParts;
            for (int row = first; row < last; ++row)
            {
                uint32_t* line = reinterpret_cast<uint32_t*>(frame.scanLine(row));
                const int y = row - y0;
                if (y < 0 || y >= rows)
                {
                    std::fill_n(line, frameWidth, background);
                    continue;
                }
                std::fill_n(line, x0, background);
                std::fill_n(line + x0 + width, frameWidth - x0 - width, background);
                const uint32_t* from = reinterpret_cast<const uint32_t*>(picture.constScanLine(fromY + y)) + fromX;
                uint32_t* to = line + x0;
                for (int x = 0; x < width; ++x)
                {
                    const uint32_t p = from[x];
                    // RGBA bytes = A,B,G,R as a little-endian word: red and blue swap. BGRA-ordered formats are the
                    // frame's own order. The window is opaque: the alpha is set either way
                    to[x] = (swap ? ((p & 0x0000FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu)) : (p & 0x00FFFFFFu)) |
                            0xFF000000u;
                }
            }
        };
        inParts(band);
        return true;
    }

    /// The frame the GPU composed (background and picture): only the keyboard drawn over it here
    void drawKeyboardInto(void* pixelBuffer, const QImage& keyboard) const
    {
#ifdef __APPLE__
        uint8_t* data = nullptr;
        size_t stride = 0;
        if (keyboard.isNull() || !RecordingSurfaces::LockPixels(pixelBuffer, data, stride))
            return;
        {
            QImage frame(data, size.width(), size.height(), static_cast<qsizetype>(stride),
                         QImage::Format_ARGB32_Premultiplied);  // B,G,R,A; opaque, so the same bytes
            QPainter painter(&frame);
            painter.drawImage(keyboardRect.topLeft(), keyboard);
        }
        RecordingSurfaces::UnlockPixels(pixelBuffer);
#else
        (void)pixelBuffer;
        (void)keyboard;
#endif
    }

    void composeInto(QImage& frame, const Job& job) const
    {
        const bool fast = composeFast(frame, job);  // the background around the picture too
        if (!fast)
            frame.fill(kBackground);
        if (fast)
        {
            if (!job.keyboard.isNull())
            {
                QPainter painter(&frame);
                painter.drawImage(keyboardRect.topLeft(), job.keyboard);
            }
            return;
        }
        QPainter painter(&frame);
        QImage picture = job.picture;
        if (scale > 1)  // blocks: the CRT mask stays sharp
            picture = picture.scaled(picture.width() * scale, picture.height() * scale, Qt::IgnoreAspectRatio,
                                     Qt::FastTransformation);
        if (picture.size() != screenRect.size())
            picture = picture.scaled(screenRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(screenRect.topLeft(), picture);
        if (!job.keyboard.isNull())
            painter.drawImage(keyboardRect.topLeft(), job.keyboard);
    }

    /// The composed B,G,R,A frame as NV12 into `out` (width x height Y bytes, then the U,V rows): FrameScaler's
    /// conversion (BT.709 limited), in bands of rows on a few threads as
    /// composeFast does. The frame's sides are even (the layout makes them so)
    void ToNv12(const QImage& frame, uint8_t* out) const
    {
        const int width = frame.width();
        const int height = frame.height();
        const size_t stride = static_cast<size_t>(frame.bytesPerLine());
        uint8_t* yPlane = out;
        uint8_t* uvPlane = out + static_cast<size_t>(width) * height;
        const auto band = [&](int part)
        {
            const int first = height / 2 * part / kParts * 2;
            const int last = height / 2 * (part + 1) / kParts * 2;
            FrameScaler::PackedToNv12(frame.constBits() + static_cast<size_t>(first) * stride, stride,
                                      static_cast<uint32_t>(width), static_cast<uint32_t>(last - first),
                                      yPlane + static_cast<size_t>(first) * width, static_cast<size_t>(width),
                                      uvPlane + static_cast<size_t>(first / 2) * width, static_cast<size_t>(width), true);
        };
        inParts(band);
    }

    /// The composed B,G,R,A frame as planar Y, U, V into `out` (three width x height planes): FrameScaler::PackedToYuv
    /// in bands of rows, as ToNv12
    void ToYuv444(const QImage& frame, uint8_t* out) const
    {
        const int width = frame.width();
        const int height = frame.height();
        const size_t stride = static_cast<size_t>(frame.bytesPerLine());
        const size_t plane = static_cast<size_t>(width) * height;
        const auto band = [&](int part)
        {
            const int first = height * part / kParts;
            const int last = height * (part + 1) / kParts;
            const size_t at = static_cast<size_t>(first) * width;
            FrameScaler::PackedToYuv(frame.constBits() + static_cast<size_t>(first) * stride, stride,
                                     static_cast<uint32_t>(width), static_cast<uint32_t>(last - first), true,
                                     out + at, out + plane + at, out + plane * 2 + at, static_cast<size_t>(width));
        };
        inParts(band);
    }

    /// One NV12 frame of transfer nv12 built in place: the background is written once per buffer, then only the
    /// picture's rectangle every frame and the keyboard's when it changes
    struct Nv12Frame
    {
        std::vector<uint8_t> bytes;  ///< Y plane (width x height), then the U,V pairs (width x height / 2); for
                                     ///< transfer yuv444 the Y, U and V planes (width x height each)
        bool background = false;     ///< the background is in
        QImage keyboard;             ///< the keyboard drawn into it (a shared copy: compared with the next)
    };

    static bool Bgra(QImage::Format format)
    {
        return format == QImage::Format_RGB32 || format == QImage::Format_ARGB32 ||
               format == QImage::Format_ARGB32_Premultiplied;
    }

    static bool Even(const QRect& rect)
    {
        return ((rect.x() | rect.y() | rect.width() | rect.height()) & 1) == 0;
    }

    /// Same pixels: the same image, or equal bytes
    static bool SameImage(const QImage& a, const QImage& b)
    {
        if (a.isNull() || b.isNull())
            return a.isNull() == b.isNull();
        return a.size() == b.size() && a.format() == b.format() && a.bytesPerLine() == b.bytesPerLine() &&
               (a.cacheKey() == b.cacheKey() ||
                std::memcmp(a.constBits(), b.constBits(), static_cast<size_t>(a.sizeInBytes())) == 0);
    }

    /// Whether composeNv12 takes this job: even rectangles (NV12's 2x2 chroma blocks), a 32-bit picture whose blocks
    /// are at most twice its place (BlocksFit)
    bool canComposeNv12(const Job& job) const
    {
        if (yuv444)
            return canComposeYuv444(job);
        const QImage& picture = job.picture;
        const QImage::Format format = picture.format();
        const bool keyboardFits =
            job.keyboard.isNull() || (Even(keyboardRect) && QRect(QPoint(0, 0), size).contains(keyboardRect));
        // NV12 from the GPU (RecordingFrameLayout::planes): the place's size, any scale (the window made the blocks)
        if (format == QImage::Format_Grayscale8)
            return picture.width() == screenRect.width() && picture.height() == screenRect.height() * 3 / 2 &&
                   Even(screenRect) && QRect(QPoint(0, 0), size).contains(screenRect) && keyboardFits;
        if (!(Bgra(format) || format == QImage::Format_RGBA8888) || picture.width() < 4 || picture.height() < 4 ||
            !BlocksFit(picture.size(), screenRect.size()) || !Even(screenRect) ||
            !QRect(QPoint(0, 0), size).contains(screenRect))
            return false;
        return keyboardFits;
    }

    /// canComposeNv12 for transfer yuv444: any rectangles (no chroma blocks), the GPU's planes (three times the place's
    /// rows) or a 32-bit picture whose blocks are at most twice its place (BlocksFit)
    bool canComposeYuv444(const Job& job) const
    {
        const QImage& picture = job.picture;
        const QImage::Format format = picture.format();
        const QRect frame(QPoint(0, 0), size);
        if (!frame.contains(screenRect) || screenRect.isEmpty() ||
            (!job.keyboard.isNull() && !frame.contains(keyboardRect)))
            return false;
        if (format == QImage::Format_Grayscale8)
            return picture.width() == screenRect.width() && picture.height() == screenRect.height() * 3;
        return (Bgra(format) || format == QImage::Format_RGBA8888) && picture.width() >= 4 && picture.height() >= 4 &&
               BlocksFit(picture.size(), screenRect.size());
    }

    /// The picture in its blocks scaled to its place by PlaneScaler: up, or down to half at most (its four taps;
    /// Qt's area averaging keeps a picture of more than twice its place)
    bool BlocksFit(const QSize& picture, const QSize& place) const
    {
        return picture.width() * scale <= place.width() * 2 && picture.height() * scale <= place.height() * 2;
    }

    /// The picture's Y, U, V planes at its own size (w x h each), PlaneScaler's source: it scales them as drawn in
    /// scale x scale blocks first (as composeInto does before its smooth scaling) without a copy in blocks
    const uint8_t* PictureToPlanes(const QImage& picture)
    {
        const uint32_t w = static_cast<uint32_t>(picture.width());
        const uint32_t h = static_cast<uint32_t>(picture.height());
        const size_t own = static_cast<size_t>(w) * h;
        pictureYuv.resize(own * 3);
        FrameScaler::PackedToYuv(picture.constBits(), static_cast<size_t>(picture.bytesPerLine()), w, h,
                                 Bgra(picture.format()), pictureYuv.data(), pictureYuv.data() + own,
                                 pictureYuv.data() + own * 2, w);
        return pictureYuv.data();
    }

    /// The frame of transfer yuv444 in its planes, as composeNv12 builds NV12: the background once per buffer, the
    /// picture's rectangle every frame (the GPU's rows copied, or converted here and scaled in each plane), the
    /// keyboard when it changes
    void composeYuv444(Nv12Frame& out, const Job& job)
    {
        const int width = size.width();
        const int height = size.height();
        const size_t plane = static_cast<size_t>(width) * height;
        const size_t stride = static_cast<size_t>(width);
        if (out.bytes.size() != plane * 3 || (job.keyboard.isNull() && !out.keyboard.isNull()))
            out.background = false;
        if (!out.background)
        {
            out.bytes.resize(plane * 3);
            const uint32_t argb = kBackground.rgb();
            const uint8_t pixel[4] = {static_cast<uint8_t>(argb), static_cast<uint8_t>(argb >> 8),
                                      static_cast<uint8_t>(argb >> 16), 255};
            uint8_t y, u, v;
            FrameScaler::PackedToYuv(pixel, 4, 1, 1, true, &y, &u, &v, 1);
            std::memset(out.bytes.data(), y, plane);
            std::memset(out.bytes.data() + plane, u, plane);
            std::memset(out.bytes.data() + plane * 2, v, plane);
            out.background = true;
            out.keyboard = QImage();
        }
        uint8_t* planes[3] = {out.bytes.data(), out.bytes.data() + plane, out.bytes.data() + plane * 2};
        const size_t offset = static_cast<size_t>(screenRect.y()) * stride + screenRect.x();

        const QImage& picture = job.picture;
        const uint32_t pw = static_cast<uint32_t>(picture.width());
        const uint32_t ph = static_cast<uint32_t>(picture.height());
        const uint32_t rw = static_cast<uint32_t>(screenRect.width());
        const uint32_t rh = static_cast<uint32_t>(screenRect.height());
        const size_t srcStride = static_cast<size_t>(picture.bytesPerLine());
        const bool bgra = Bgra(picture.format());
        std::function<void(int)> band;
        if (picture.format() == QImage::Format_Grayscale8)
        {
            // Converted by the GPU: its Y, U and V rows into the place
            band = [&](int part)
            {
                for (uint32_t row = rh * part / kParts; row < rh * (part + 1) / kParts; ++row)
                    for (uint32_t p = 0; p < 3; ++p)
                        std::memcpy(planes[p] + offset + row * stride,
                                    picture.constScanLine(static_cast<int>(p * rh + row)), rw);
            };
        }
        else if (pw == rw && ph == rh && scale == 1)
        {
            band = [&](int part)
            {
                const uint32_t first = rh * part / kParts;
                const uint32_t last = rh * (part + 1) / kParts;
                const size_t at = offset + first * stride;
                FrameScaler::PackedToYuv(picture.constBits() + first * srcStride, srcStride, rw, last - first, bgra,
                                         planes[0] + at, planes[1] + at, planes[2] + at, stride);
            };
        }
        else
        {
            // Converted at its own size, then each plane scaled to the place (as in scale x scale blocks)
            const uint8_t* source = PictureToPlanes(picture);
            const size_t planeSize = static_cast<size_t>(pw) * ph;
            lumaScaler.Prepare(pw, ph, rw, rh, static_cast<uint32_t>(scale));
            // By value: the band runs after this block
            band = [this, &planes, source, planeSize, pw, rh, offset, stride](int part)
            {
                for (uint32_t p = 0; p < 3; ++p)
                    lumaScaler.Rows(source + planeSize * p, pw, planes[p] + offset, stride, rh * part / kParts,
                                    rh * (part + 1) / kParts);
            };
        }
        inParts(band);

        if (!job.keyboard.isNull() && !SameImage(job.keyboard, out.keyboard))
        {
            QImage drawn(keyboardRect.size(), QImage::Format_ARGB32_Premultiplied);
            drawn.fill(kBackground);
            {
                QPainter painter(&drawn);
                painter.drawImage(QPoint(0, 0), job.keyboard);
            }
            const size_t at = static_cast<size_t>(keyboardRect.y()) * stride + keyboardRect.x();
            FrameScaler::PackedToYuv(drawn.constBits(), static_cast<size_t>(drawn.bytesPerLine()),
                                     static_cast<uint32_t>(drawn.width()), static_cast<uint32_t>(drawn.height()), true,
                                     planes[0] + at, planes[1] + at, planes[2] + at, stride);
            out.keyboard = job.keyboard;
        }
    }

    /// The frame of transfer nv12 straight in NV12, no B,G,R,A frame: the picture converted at its own size and
    /// scaled in its planes (Y at the place's size, U,V at half: a third of the samples of a 4K B,G,R,A frame), a
    /// picture of its place's size converted into it. The layout's rectangles are even (canComposeNv12)
    void composeNv12(Nv12Frame& out, const Job& job)
    {
        if (yuv444)
        {
            composeYuv444(out, job);
            return;
        }
        const int width = size.width();
        const int height = size.height();
        const size_t ySize = static_cast<size_t>(width) * height;
        uint8_t* yPlane;
        uint8_t* uvPlane;
        if (out.bytes.size() != ySize * 3 / 2 || (job.keyboard.isNull() && !out.keyboard.isNull()))
            out.background = false;
        if (!out.background)
        {
            out.bytes.resize(ySize * 3 / 2);
            // The background's Y and U,V: PackedToNv12 of a 2x2 block of it
            const uint32_t argb = kBackground.rgb();
            const uint8_t pixel[4] = {static_cast<uint8_t>(argb), static_cast<uint8_t>(argb >> 8),
                                      static_cast<uint8_t>(argb >> 16), 255};
            uint8_t block[16], y[4], uv[2];
            for (int i = 0; i < 4; ++i)
                std::memcpy(block + i * 4, pixel, 4);
            FrameScaler::PackedToNv12(block, 8, 2, 2, y, 2, uv, 2, true);
            std::memset(out.bytes.data(), y[0], ySize);
            for (size_t at = ySize; at < out.bytes.size(); at += 2)
            {
                out.bytes[at] = uv[0];
                out.bytes[at + 1] = uv[1];
            }
            out.background = true;
            out.keyboard = QImage();
        }
        yPlane = out.bytes.data();
        uvPlane = yPlane + ySize;
        const size_t stride = static_cast<size_t>(width);

        const QImage& picture = job.picture;
        const bool bgra = Bgra(picture.format());
        const uint32_t pw = static_cast<uint32_t>(picture.width());
        const uint32_t ph = static_cast<uint32_t>(picture.height());
        const uint32_t rw = static_cast<uint32_t>(screenRect.width());
        const uint32_t rh = static_cast<uint32_t>(screenRect.height());
        uint8_t* yAt = yPlane + static_cast<size_t>(screenRect.y()) * stride + screenRect.x();
        uint8_t* uvAt = uvPlane + static_cast<size_t>(screenRect.y() / 2) * stride + screenRect.x();
        const size_t srcStride = static_cast<size_t>(picture.bytesPerLine());
        std::function<void(int)> band;
        if (picture.format() == QImage::Format_Grayscale8)
        {
            // Converted by the GPU: its Y rows, then its U,V rows, into the place
            band = [&](int part)
            {
                const uint32_t first = rh / 2 * part / kParts * 2;
                const uint32_t last = rh / 2 * (part + 1) / kParts * 2;
                for (uint32_t row = first; row < last; ++row)
                    std::memcpy(yAt + row * stride, picture.constScanLine(static_cast<int>(row)), rw);
                for (uint32_t row = first / 2; row < last / 2; ++row)
                    std::memcpy(uvAt + row * stride, picture.constScanLine(static_cast<int>(rh + row)), rw);
            };
        }
        else if (pw == rw && ph == rh && scale == 1)
        {
            band = [&](int part)
            {
                const uint32_t first = rh / 2 * part / kParts * 2;
                const uint32_t last = rh / 2 * (part + 1) / kParts * 2;
                FrameScaler::PackedToNv12(picture.constBits() + first * srcStride, srcStride, rw, last - first,
                                          yAt + first * stride, stride, uvAt + first / 2 * stride, stride, bgra);
            };
        }
        else
        {
            // Converted at its own size (a few hundred thousand pixels), then each plane scaled: Y to the place,
            // U and V to half of it, written as NV12's pairs (as in scale x scale blocks: PlaneScaler's `blocks`)
            const uint8_t* planes = PictureToPlanes(picture);
            const size_t planeSize = static_cast<size_t>(pw) * ph;
            lumaScaler.Prepare(pw, ph, rw, rh, static_cast<uint32_t>(scale));
            chromaScaler.Prepare(pw, ph, rw / 2, rh / 2, static_cast<uint32_t>(scale));
            // By value: the band runs after this block (planeSize is its own)
            band = [this, planes, planeSize, pw, rh, yAt, uvAt, stride](int part)
            {
                lumaScaler.Rows(planes, pw, yAt, stride, rh * part / kParts, rh * (part + 1) / kParts);
                chromaScaler.RowsInterleaved(planes + planeSize, planes + planeSize * 2, pw, uvAt, stride,
                                             rh / 2 * part / kParts, rh / 2 * (part + 1) / kParts);
            };
        }
        inParts(band);

        // The keyboard over the background, as QPainter draws it (its corners are transparent), when it changed
        if (!job.keyboard.isNull() && !SameImage(job.keyboard, out.keyboard))
        {
            QImage drawn(keyboardRect.size(), QImage::Format_ARGB32_Premultiplied);
            drawn.fill(kBackground);
            {
                QPainter painter(&drawn);
                painter.drawImage(QPoint(0, 0), job.keyboard);
            }
            FrameScaler::PackedToNv12(drawn.constBits(), static_cast<size_t>(drawn.bytesPerLine()),
                                      static_cast<uint32_t>(drawn.width()), static_cast<uint32_t>(drawn.height()),
                                      yPlane + static_cast<size_t>(keyboardRect.y()) * stride + keyboardRect.x(), stride,
                                      uvPlane + static_cast<size_t>(keyboardRect.y() / 2) * stride + keyboardRect.x(),
                                      stride, true);
            out.keyboard = job.keyboard;
        }
    }

#ifdef __APPLE__
    /// The macOS hardware encoder (AVAssetWriter / VideoToolbox) writes picture and sound into one file itself. The
    /// picture goes straight into the encoder's own pixel buffer; a picture is held until the next one's time, so
    /// the frames owed while the encoder lags cost nothing
    void runNative()
    {
        VideoToolboxEncoder encoder;
        EncoderConfig config;
        config.videoWidth = static_cast<uint32_t>(size.width());
        config.videoHeight = static_cast<uint32_t>(size.height());
        config.scaleFactor = 1;
        config.frameRate = static_cast<float>(fps);
        config.videoCodec = nativeCodec;
        config.qualityPreset = nativeQuality;
        // The encoder's own default is sized for a 352x288 frame and grows with the area: about 550 Mbit/s at 4K, which
        // the hardware encoder cannot deliver in real time on a flickering (interlaced) picture. 0.25 bit per pixel and
        // frame: ~100 Mbit/s at 4K, ~25 at 1080p
        config.videoBitrate = std::max<uint32_t>(
            8000, static_cast<uint32_t>(static_cast<double>(size.width()) * size.height() * fps * 0.25 / 1000.0));
        config.audioSampleRate = audioRate;
        config.audioChannels = audio ? AUDIO_CHANNELS : 0;
        std::string failure;
        if (!encoder.Start(mux.path.toStdString(), config))
            failure = "The hardware encoder did not start: " + encoder.GetLastError();
        if (!failure.empty())
            failed = true;
        ready = true;  // a failed start is told by `failed`
        encoderReadyUs = std::max<int64_t>(1, sinceCreatedUs());  // after `ready`: > 0 tells a recorded step it may run
        for (;;)
        {
            Job job;
            bool signalRoom = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return !jobs.empty() || closing; });
                if (jobs.empty())
                    break;
                job = std::move(jobs.front());
                jobs.pop_front();
                if (job.hasPicture() && --queuedPictures < kMaxQueuedFrames && wantRetry)
                {
                    wantRetry = false;
                    signalRoom = true;
                }
            }
            if (signalRoom && onRoom)
                onRoom();
            if (!failure.empty())
                continue;
            if (!job.sound.isEmpty())
                encoder.OnAudioSamples(reinterpret_cast<const int16_t*>(job.sound.constData()),
                                       static_cast<size_t>(job.sound.size()) / sizeof(int16_t), job.soundTime);
            if (!job.hasPicture())
                continue;
            const int64_t pictureStartUs = sinceCreatedUs();
            if (job.frame)
            {
                // Composed by the GPU in a buffer of the encoder's kind: the keyboard over it, then in as it is
                drawKeyboardInto(job.frame.get(), job.keyboard);
                encoder.AppendPixelBuffer(job.frame.get(), static_cast<double>(job.frameStart) / fps);
                job.frame.reset();  // back to the pool once the encoder is done with it
                framesDone = job.frameStart + job.repeat;
                pictureHandled(pictureStartUs, "gpu");
                continue;
            }
            FrameTarget target;
            if (encoder.AcquireFrameTarget(static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()),
                                           target) == FrameTargetResult::Ready)
            {
                // The pixel buffer is B,G,R,A: Format_ARGB32 is that byte order on a little-endian host
                QImage frame(target.data, size.width(), size.height(), static_cast<qsizetype>(target.stride),
                             QImage::Format_ARGB32);
                composeInto(frame, job);
                encoder.SubmitFrameTarget(target, static_cast<double>(job.frameStart) / fps);
            }
            // else: the encoder is stuck for now: this picture stays out, the previous one holds
            framesDone = job.frameStart + job.repeat;
            pictureHandled(pictureStartUs, "native");
        }
        encoder.Stop();
        error = failure;
        done = true;
    }
#endif

    void run()
    {
        writerStartUs = sinceCreatedUs();
        rowPool = std::make_unique<ThreadPool>(kParts - 1, "window-recorder-rows");
#ifdef __APPLE__
        if (native)
        {
            runNative();
            return;
        }
#endif
        QProcess process;
        process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        process.start(ffmpeg, args);
        QFile sound;
        std::string failure;
        std::deque<QByteArray> backlog;  // sound waiting for ffmpeg to open the pipe (it does so after the first frames)
        // Takes the pipe once ffmpeg has opened it (never waits), then hands over what the pipe takes. It never waits
        // for the pipe: ffmpeg reads the sound only as fast as the picture lets it, and a writer stuck on the sound
        // could not feed the picture (a deadlock). False: ffmpeg is gone, or another process opened the pipe
        size_t backlogOffset = 0;
        auto pumpSound = [&]() -> bool {
            if (!soundPipe)
                return true;
            if (!soundPipe->tryOpenForWrite())
                return !soundPipe->failed();  // no reader yet
            while (!backlog.empty())
            {
                const QByteArray& chunk = backlog.front();
                const int64_t n = soundPipe->tryWrite(chunk.constData() + backlogOffset,
                                                      static_cast<size_t>(chunk.size()) - backlogOffset);
                if (n < 0)
                    return false;
                if (n == 0)
                    return true;  // the pipe is full: the rest waits
                backlogOffset += static_cast<size_t>(n);
                if (backlogOffset == static_cast<size_t>(chunk.size()))
                {
                    backlog.pop_front();
                    backlogOffset = 0;
                }
            }
            return true;
        };
        auto soundFailure = [&]() {
            return "ffmpeg stopped taking the sound" +
                   (soundPipe && !soundPipe->getLastError().empty() ? ": " + soundPipe->getLastError() : std::string());
        };
        if (!process.waitForStarted(5000))
            failure = "ffmpeg did not start: " + process.errorString().toStdString();
        else if (soundPipe)
            soundPipe->setReaderProcess(static_cast<uint64_t>(process.processId()));  // only this ffmpeg may open it
        else if (!mux.soundPath.isEmpty())  // no sound pipe: the sound into its own file, muxed at the end
        {
            sound.setFileName(mux.soundPath);
            if (!sound.open(QIODevice::WriteOnly | QIODevice::Truncate))
                failure = "Cannot write " + mux.soundPath.toStdString() + ": " + sound.errorString().toStdString();
        }
        auto fail = [&](const std::string& why) {
            if (failure.empty())
                failure = why;
            failed = true;
        };
        if (!failure.empty())
            failed = true;
        else
            process.write(MatroskaStreamHeader(size.width(), size.height(), nv12 ? "NV12" : yuv444 ? "444P" : "BGRA"));
        ready = true;  // a failed start is told by `failed`
        encoderReadyUs = std::max<int64_t>(1, sinceCreatedUs());  // after `ready`: > 0 tells a recorded step it may run

        // The last picture sent and its frame: a picture equal to it is not sent (a still screen; it only lengthens
        // the one before), unless a quarter second went by (seeking wants a frame that often)
        // Two frame buffers, reused: one is composed into, the other holds the last picture sent (filling a fresh 4K
        // QImage took 4.1 ms on Windows, a reused one 2.0; the same on Linux and macOS). lastSent points at one of them
        QImage frames[2];
        std::vector<uint8_t> converted[2];  // transfer nv12: the frame of the same index as NV12
        Nv12Frame nv12Frames[2];            // transfer nv12 built in NV12 itself (composeNv12)
        bool lastDirect = false;            // the last frame sent was built by composeNv12 (lastSent is a stand-in)
        QImage lastPicture, lastKeyboard;   // its picture and keyboard (compared with the next)
        int composing = 0;
        const QImage* lastSent = nullptr;   // the composed frame last sent (compared with the next one)
        QByteArrayView lastSentBytes;       // its bytes as ffmpeg got them (the frame's, or its NV12)
        uint64_t lastSentFrame = 0;
        uint64_t lastSentUs = 0;            // its time in the stream
        uint64_t picturesSent = 0;
        const uint64_t resendFrames = std::max<uint64_t>(1, static_cast<uint64_t>(fps / 4));
        // The sound queued behind this picture goes to ffmpeg ahead of it, and again while it waits (see
        // takeQueuedSound): the picture is never ahead of its sound
        auto feedSound = [&]() -> bool {
            std::deque<QByteArray> queued;
            takeQueuedSound(queued);
            for (QByteArray& chunk : queued)
            {
                if (soundPipe)
                    backlog.push_back(std::move(chunk));
                else if (sound.write(chunk) != chunk.size())
                {
                    fail("Cannot write the sound: " + sound.errorString().toStdString());
                    return false;
                }
            }
            if (pumpSound())
                return true;
            fail(soundFailure());
            return false;
        };
        // One picture into ffmpeg at its frame's time. No event loop here: the bytes go to ffmpeg only while we wait,
        // and ffmpeg's pace is the pace. Wait in short slices, the sound flowing meanwhile, until the whole frame is
        // in the pipe. A slow ffmpeg keeps taking bytes; one that took nothing for 15 s is stuck (the sound it waits
        // for is fed here, so that is not a lack of it): the recording fails instead of hanging
        auto sendFrameAt = [&](const QImage* frame, QByteArrayView bytes, uint64_t frameIndex, uint64_t timeUs) {
            process.write(MatroskaFrameHeader(timeUs, bytes.size()));
            process.write(bytes.data(), bytes.size());
            int stalledMs = 0;
            bool taken = false;
            while (process.state() == QProcess::Running && stalledMs < 15000)
            {
                const bool progress = process.waitForBytesWritten(100);
                if (progress && process.bytesToWrite() == 0)  // the whole frame is in the pipe
                {
                    taken = true;
                    break;
                }
                stalledMs = progress ? 0 : stalledMs + 100;
                if (!feedSound())
                    break;
            }
            if (!taken || process.state() != QProcess::Running)
                fail("ffmpeg stopped taking frames: " + process.errorString().toStdString());
            lastSent = frame;
            lastSentBytes = bytes;
            lastSentFrame = frameIndex;
            lastSentUs = timeUs;
            picturesSent++;
        };
        auto sendFrame = [&](const QImage* frame, QByteArrayView bytes, uint64_t frameIndex) {
            sendFrameAt(frame, bytes, frameIndex,
                        static_cast<uint64_t>(std::llround(static_cast<double>(frameIndex) * 1e6 / fps)));
        };

        for (;;)
        {
            Job job;
            bool signalRoom = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return !jobs.empty() || closing; });
                if (jobs.empty())
                    break;  // closing and drained
                job = std::move(jobs.front());
                jobs.pop_front();
                if (job.hasPicture() && --queuedPictures < kMaxQueuedFrames && wantRetry)
                {
                    wantRetry = false;
                    signalRoom = true;
                }
            }
            if (signalRoom && onRoom)
                onRoom();
            if (!failure.empty())
                continue;  // drain without writing
            if (!job.sound.isEmpty())
            {
                if (soundPipe)
                {
                    backlog.push_back(std::move(job.sound));
                    if (!pumpSound())
                        fail(soundFailure());
                }
                else if (sound.write(job.sound) != job.sound.size())
                    fail("Cannot write the sound: " + sound.errorString().toStdString());
            }
            if (job.picture.isNull() || !failure.empty())
                continue;
            const int64_t pictureStartUs = sinceCreatedUs();
            // B,G,R,A bytes on a little-endian host: the stream's order. Premultiplied, QPainter's own format (the
            // keyboard drawn into plain ARGB32 took most of a frame's time at 1080p); the frame is opaque, so the bytes
            // are the same
            if ((nv12 || yuv444) && canComposeNv12(job))
            {
                // Compared by the picture and the keyboard (a few hundred KB, not the 4K frame), built only when sent
                const bool same = lastSent && lastDirect && job.frameStart < lastSentFrame + resendFrames &&
                                  SameImage(job.picture, lastPicture) && SameImage(job.keyboard, lastKeyboard);
                const bool send = feedSound() && !same;
                if (send)
                {
                    Nv12Frame& out = nv12Frames[composing];
                    composeNv12(out, job);
                    lastPicture = job.picture;
                    lastKeyboard = job.keyboard;
                    sendFrame(&frames[composing],
                              QByteArrayView(reinterpret_cast<const char*>(out.bytes.data()),
                                             static_cast<qsizetype>(out.bytes.size())),
                              job.frameStart);
                    lastDirect = true;
                    composing ^= 1;
                }
                framesDone = job.frameStart + job.repeat;
                if (!pumpSound())
                    fail(soundFailure());
                pictureHandled(pictureStartUs, send ? (yuv444 ? "yuv444" : "nv12") : "same");
                continue;
            }
            QImage& frame = frames[composing];
            if (frame.size() != size)
                frame = QImage(size, QImage::Format_ARGB32_Premultiplied);
            composeInto(frame, job);
            const bool same = lastSent && !lastDirect && job.frameStart < lastSentFrame + resendFrames &&
                              std::memcmp(frame.constBits(), lastSent->constBits(),
                                          static_cast<size_t>(frame.sizeInBytes())) == 0;
            const bool send = feedSound() && !same;
            if (send)
            {
                QByteArrayView bytes(reinterpret_cast<const char*>(frame.constBits()), frame.sizeInBytes());
                if (nv12 || yuv444)
                {
                    std::vector<uint8_t>& out = converted[composing];
                    const size_t pixels = static_cast<size_t>(size.width()) * size.height();
                    out.resize(yuv444 ? pixels * 3 : pixels * 3 / 2);
                    if (yuv444)
                        ToYuv444(frame, out.data());
                    else
                        ToNv12(frame, out.data());
                    bytes = QByteArrayView(reinterpret_cast<const char*>(out.data()), static_cast<qsizetype>(out.size()));
                }
                sendFrame(&frame, bytes, job.frameStart);
                lastDirect = false;
                composing ^= 1;  // the frame sent is kept; the next one goes into the other buffer
            }
            framesDone = job.frameStart + job.repeat;
            if (!pumpSound())
                fail(soundFailure());
            pictureHandled(pictureStartUs, send ? "composed" : "same");
        }
        // x265 (4.3) sets its B-frame delay time (Encoder::m_bframeDelayTime) only at its third picture and leaves it
        // uninitialized before: with fewer pictures (a still screen: the first one and the last again) the packets'
        // dts are pts minus garbage, and the MP4 muxer refused them about one time in three ("pts/dts pair
        // unsupported": "Not yet implemented in FFmpeg"). The last picture is sent on the frames before the last one
        // until x265 has three; a recording of one or two frames gets them a microsecond apart
        const bool x265 = args.contains("libx265");
        // The last picture once more at the last frame's time: the picture lasts to the end of the sound
        if (failure.empty() && lastSent && mux.frames > lastSentFrame + 1)
        {
            for (uint64_t frame = std::max(lastSentFrame + 1, mux.frames - std::min<uint64_t>(mux.frames, 2));
                 x265 && failure.empty() && picturesSent + 1 < 3 && frame < mux.frames - 1; frame++)
                sendFrame(lastSent, lastSentBytes, frame);
            sendFrame(lastSent, lastSentBytes, mux.frames - 1);
        }
        while (x265 && failure.empty() && lastSent && picturesSent < 3)
            sendFrameAt(lastSent, lastSentBytes, lastSentFrame, lastSentUs + 1);

        sound.close();
        std::string result = failure;
        if (failure.empty())
        {
            process.closeWriteChannel();  // the end of the picture first: ffmpeg interleaves the sound with it
            if (soundPipe)
            {
                // The rest of the sound goes in as ffmpeg takes it (it opens the pipe only after it has the frames),
                // then the pipe closes: that is the end of the sound. Closed only once the last bytes are in the pipe
                // (a write still in flight would be dropped). A slow encoder is waited for, never killed
                while ((!soundPipe->isOpen() || !backlog.empty() || !soundPipe->flushed()) &&
                       process.state() != QProcess::NotRunning)
                {
                    if (!pumpSound())
                    {
                        failure = result = soundFailure();
                        break;
                    }
                    process.waitForFinished(5);
                }
                soundPipe->close();
            }
            // A slow encoder is not a failed one: wait as long as ffmpeg runs (killing it loses the whole file)
            while (!process.waitForFinished(5000) && process.state() != QProcess::NotRunning)
            {
            }
            if (!failure.empty())
            {
            }
            else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
                result = "ffmpeg failed with code " + std::to_string(process.exitCode());
            else if (!mux.soundPath.isEmpty() && !Mux(mux, result))
            {
            }
        }
        else
        {
            process.kill();
            process.waitForFinished(5000);
        }
        if (soundPipe)
        {
            soundPipe->close();
            NamedPipe::remove(soundPipe->getPath());
        }
        error = result;
        done = true;
    }
};

WindowRecordingRequest MakeWindowRecordingRequest(const QString& container, const QString& codec, bool audio,
                                                  bool gpuAcceleration, const QString& profile,
                                                  const QString& path, int engine)
{
    WindowRecordingRequest request;
    request.codec = codec.toStdString();
    // The engine chosen in the recording dialog decides (1 = Native: the hardware encoder, 2 = FFmpeg: ffmpeg);
    // Auto follows View > GPU acceleration. MKV never goes to the hardware encoder (it writes MP4 / MOV)
    // Native = the Mac's own writer (MP4 / MOV); FFmpeg, and MKV, take the GPU's encoder through ffmpeg
    request.hardware = engine == 1 || gpuAcceleration;
    request.ffmpegOnly = engine == 2 || container.compare(QLatin1String("MKV"), Qt::CaseInsensitive) == 0;
    request.audio = audio;
    const struct { const char* id; int w, h; } profiles[] = {{"1080p", 1920, 1080}, {"1440p", 2560, 1440}, {"4k", 3840, 2160}};
    for (const auto& fixed : profiles)
        if (profile == QLatin1String(fixed.id))
        {
            request.frameWidth = static_cast<uint16_t>(fixed.w);
            request.frameHeight = static_cast<uint16_t>(fixed.h);
            request.pictureHeight = fixed.h;
            request.fit = true;  // without a CRT shader the window's picture is its own size: fitted to the frame
        }
    request.path = path.toStdString();
    return request;
}

WindowRecorder::WindowRecorder(OsdKeyboardWindow* keyboard, QObject* parent) : QObject(parent), _keyboard(keyboard)
{
}

WindowRecorder::~WindowRecorder()
{
    WindowRecordingStatus status;
    std::string error;
    if (_writer && !_writer->closing)
        stop(status, error);
    if (_writer)  // the application is closing: the file is finished first
    {
        if (_writer->thread.joinable())
            _writer->thread.join();
        _writer.reset();
    }
}

bool WindowRecorder::recording() const
{
    return _writer && !_writer->closing;
}

void WindowRecorder::status(WindowRecordingStatus& out)
{
    fill(out);
}

void WindowRecorder::reap()
{
    if (!_writer || !_writer->done)
        return;
    if (_writer->thread.joinable())
        _writer->thread.join();
    _lastError = _writer->error;
    _writer.reset();
}

bool WindowRecorder::start(DeviceScreenWrapper* screen, const std::shared_ptr<Emulator>& emulator,
                           const WindowRecordingRequest& request, WindowRecordingStatus& out, std::string& error)
{
    const bool ok = begin(screen, emulator, request, out, error);
    if (!ok)
        restoreKeyboard();
    else
        emit stateChanged();
    return ok;
}

bool WindowRecorder::HardwareEncoderKnown(const WindowRecordingRequest& request)
{
    QString ffmpeg;
    bool h265 = false;
    bool nv12 = false;
    if (!HardwareProbeOf(request, ffmpeg, h265, nv12))
        return true;
    std::lock_guard<std::mutex> lock(hardwareCacheMutex);
    return hardwareCache.count(HardwareCacheKey(ffmpeg, h265, nv12)) != 0;
}

void WindowRecorder::PrepareHardwareEncoder(const WindowRecordingRequest& request)
{
    QString ffmpeg;
    bool h265 = false;
    bool nv12 = false;
    if (HardwareProbeOf(request, ffmpeg, h265, nv12))
        FfmpegHardwareEncoder(ffmpeg, h265, nv12);
}

bool WindowRecorder::begin(DeviceScreenWrapper* screen, const std::shared_ptr<Emulator>& emulator,
                           const WindowRecordingRequest& request, WindowRecordingStatus& out, std::string& error)
{
    reap();
    if (_writer)
    {
        error = _writer->closing ? "The previous recording is still being finished: " + _request.path
                                 : "The window is already being recorded to " + _request.path;
        return false;
    }
    _lastError.clear();
    _screen = screen;
    if (!_screen)
    {
        error = "The window has no screen";
        return false;
    }
    if (request.path.empty())
    {
        error = "A recording needs a path";
        return false;
    }
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;  // lifetime-ok: the caller's shared_ptr
    if (!context || !context->pSoundManager)
    {
        error = "The window shows no emulator";
        return false;
    }
    if (request.codec != "prores" && request.codec != "h264" && request.codec != "h265")
    {
        error = "Unknown codec '" + request.codec + "': use prores, h264 or h265";
        return false;
    }
    const bool native = NativeWriter(request);
    QString ffmpeg;
    if (!native)
    {
        ffmpeg = FindFfmpeg();
        if (ffmpeg.isEmpty())
        {
            error = "The window recording needs ffmpeg on the PATH";
            return false;
        }
    }
    _ffmpeg = ffmpeg;
    // transfer auto: NV12 where the file is 4:2:0 anyway (a hardware encoder without 4:4:4: its trial is the one the
    // picture way runs, so no second one); x264 / x265, ProRes and 4:4:4 encoders get YUV444 (decided below, by the
    // frame's size)
    const bool h265 = request.codec == "h265";
    const bool hardwareWay = !native && request.codec != "prores" && request.hardware;
    HardwareEncoder hardware = hardwareWay && request.transfer != "nv12" ? FfmpegHardwareEncoder(ffmpeg, h265, false)
                                                                          : HardwareEncoder{};
    const bool nv12 = !native && (request.transfer == "nv12" ||
                                  (request.transfer == "auto" && !hardware.name.isEmpty() && !hardware.full));
    if (hardwareWay && request.transfer == "nv12")
        hardware = FfmpegHardwareEncoder(ffmpeg, h265, true);

    _request = request;
    _keyboardId.clear();
    if (!request.keyboard.empty())
    {
        if (!_keyboard)
        {
            error = "This window has no on-screen keyboard";
            return false;
        }
        _keyboard->refresh();
        if (request.keyboard != "default")
        {
            // Another machine's picture with the same matrix (the ZX Next's on a Pentagon) is shown for the
            // recording only, the window's own choice comes back at stop
            const auto& choices = _keyboard->choices();
            _restoreKeyboard = _keyboard->picture().id;
            std::string otherError;
            if (std::find(choices.begin(), choices.end(), request.keyboard) == choices.end() &&
                !_keyboard->chooseOther(request.keyboard, otherError))
            {
                std::string offered;
                for (const std::string& id : choices)
                    offered += (offered.empty() ? "" : ", ") + id;
                error = "Keyboard '" + request.keyboard + "': " + otherError + " (this machine's: " + offered +
                        ", or another with its matrix)";
                _restoreKeyboard.clear();
                return false;
            }
            if (std::find(choices.begin(), choices.end(), request.keyboard) != choices.end())
            {
                _keyboard->choose(request.keyboard);
                _restoreKeyboard.clear();
            }
        }
        _keyboardId = _keyboard->picture().id;
        if (_keyboardId.empty())
        {
            error = "No keyboard picture for this machine";
            return false;
        }
    }

    // The frame's layout from the first grab: the picture in scale x scale blocks, the keyboard under it (even sides)
    // or right of it in a given frame
    WindowViewGrab grab;
    if (!_screen->grabWindowView(grab, error, false, request.pictureHeight))
        return false;
    const int scale = std::clamp(request.scale, 1, 4);
    QSize picture(grab.width * scale, grab.height * scale);
    const QSize nativePicture = picture;
    const int keyboardAspectHeight = _keyboardId.empty() ? 0 : _keyboard->renderImage(1000).height();
    if (request.frameWidth > 0 && request.frameHeight > 0)
    {
        _size = QSize(request.frameWidth & ~1, request.frameHeight & ~1);
        if (request.fit)  // the picture scaled to the frame, aspect kept, centered (a CRT drawn at its height needs none)
        {
            // A keyboard below is as wide as the picture: the two, one above the other, fit the frame together. A
            // keyboard beside gets half the width at least (else it is a strip a few keys high)
            const bool below = request.layout != "side" && keyboardAspectHeight > 0;
            const double blockHeight = picture.height() + (below ? picture.width() * keyboardAspectHeight / 1000.0 : 0);
            const int width = keyboardAspectHeight > 0 && !below ? _size.width() / 2 : _size.width();
            const double factor = std::min(static_cast<double>(width) / picture.width(),
                                           static_cast<double>(_size.height()) / blockHeight);
            picture = QSize(std::max(2, static_cast<int>(picture.width() * factor) & ~1),
                            std::max(2, static_cast<int>(picture.height() * factor) & ~1));
        }
        if (picture.width() > _size.width() || picture.height() > _size.height())
        {
            error = "The picture (" + std::to_string(picture.width()) + "x" + std::to_string(picture.height()) +
                    ") does not fit the frame";
            return false;
        }
        if (request.layout == "side" && keyboardAspectHeight > 0)
        {
            _screenRect = QRect(QPoint(0, (_size.height() - picture.height()) / 2), picture);
            const int margin = _size.height() / 30;
            int width = _size.width() - picture.width() - 2 * margin;
            if (width < 64)  // nothing to draw a keyboard in
            {
                error = "The picture leaves no room for the keyboard beside it in a " + std::to_string(_size.width()) +
                        "x" + std::to_string(_size.height()) + " frame (use fit, or a wider frame)";
                return false;
            }
            int height = width * keyboardAspectHeight / 1000;
            if (height > _size.height() - 2 * margin)
            {
                height = _size.height() - 2 * margin;
                width = height * 1000 / keyboardAspectHeight;
            }
            const int left = picture.width() + (_size.width() - picture.width() - width) / 2;
            _keyboardRect = QRect(left, (_size.height() - height) / 2, width, height);
        }
        else
        {
            // The keyboard under the picture: the frame's width, or the picture's when fitted (see above)
            const int width = request.fit && keyboardAspectHeight > 0 ? picture.width() : _size.width();
            const int height = keyboardAspectHeight * width / 1000;
            const int top = (_size.height() - picture.height() - height) / 2;
            _screenRect = QRect(QPoint((_size.width() - picture.width()) / 2, std::max(0, top)), picture);
            _keyboardRect = QRect((_size.width() - width) / 2, _screenRect.bottom() + 1, width, height);
        }
    }
    else
    {
        const int width = picture.width() & ~1;
        const int height = keyboardAspectHeight * width / 1000;
        _size = QSize(width, (picture.height() + height) & ~1);
        _screenRect = QRect(QPoint(0, 0), picture);
        _keyboardRect = QRect(0, picture.height(), width, height);
    }
    if (nv12)
    {
        // NV12 colors 2x2 blocks: the frame is built in place only on even rectangles (a pixel off the center)
        const auto even = [](const QRect& rect)
        { return rect.isEmpty() ? rect : QRect(rect.x() & ~1, rect.y() & ~1, rect.width() & ~1, rect.height() & ~1); };
        _screenRect = even(_screenRect);
        _keyboardRect = even(_keyboardRect);
    }

    // The machine's frame rate, exact (3500000/71680 on a Pentagon): the sound's clock is the same frames
    const unsigned tstatesPerFrame = context->config.frame;
    _fps = request.fps;
    if (request.fps == 0)
    {
        if (tstatesPerFrame == 0)
        {
            error = "The machine reports no frame length";
            return false;
        }
        _fps = static_cast<double>(CPU_CLOCK_RATE) / tstatesPerFrame;
    }
    _sampleRate = request.audio ? static_cast<uint32_t>(context->pSoundManager->getCoreRate()) : 0;
    _audioSamples = 0;

    // With sound the sound goes to ffmpeg through a pipe; without one, the picture to a file beside the target and the
    // sound to a raw file, muxed at stop
    const QString path = QString::fromStdString(request.path);
    _videoPath = path;
    _soundPath.clear();
    QString fifo;
    std::shared_ptr<NamedPipe> soundPipe;
    std::shared_ptr<QTemporaryDir> soundDir;
    _audio = request.audio;
    if (request.audio && !native)
    {
        const QFileInfo target(path);
        // The sound reaches the same ffmpeg through a pipe: one file from the start, nothing to mux (and copy) at the
        // end, however large the picture is. Unix: a FIFO in a private directory of its own (this user's alone; any
        // file system of the target, another emulator's FIFO never in the way); Windows: a named pipe of a name no
        // other has, open to this user and, once ffmpeg runs, to this ffmpeg alone
        soundPipe = std::make_shared<NamedPipe>();
#ifdef Q_OS_WIN
        const std::string pipeName = NamedPipe::uniqueName("sound");
#else
        soundDir = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/unreal-rec-XXXXXX");
        const std::string pipeName = soundDir->isValid() ? soundDir->filePath("sound.fifo").toStdString() : std::string();
#endif
        if (!pipeName.empty() && soundPipe->create(pipeName))
            fifo = QString::fromStdString(NamedPipe::readerPath(pipeName));
        else
        {
            soundPipe.reset();
            soundDir.reset();
        }
        if (fifo.isEmpty())  // no pipe: the picture and the sound in two files, muxed at the end
        {
            _videoPath = target.dir().filePath("." + target.completeBaseName() + ".video." + target.suffix());
            _soundPath = target.dir().filePath("." + target.completeBaseName() + ".sound.s16le");
        }
    }

    // What ffmpeg gets: the frame's content (picture and keyboard, without the empty border) at the picture's own
    // size, scaled to the frame and bordered by ffmpeg. A 1080p frame is 8 MB, a pipe takes about a hundred MB a
    // second on a loaded machine: the frames themselves did not fit through, the native picture does (four to
    // twelve times less). The keyboard is drawn at its size there, so it is scaled with the picture
    // With transfer nv12 the frame is composed here at the file's size and goes as NV12: ffmpeg only encodes (a GPU
    // encoder takes NV12 as it is; 4K BGRA was too much for ffmpeg in front of Intel's encoder, POC 054)
    // With transfer yuv444 the same in planar 4:4:4 (twice NV12's bytes): the 4:4:4 encoders (x264 / x265, NVENC and
    // VA-API 4:4:4, ProRes through prores_ks) without ffmpeg's scaling and conversion, which took a core and more of
    // it. auto takes it up to 1080p: a 6 MB frame went through a pipe in 9 ms (Linux, 1080p50, POC data in the
    // window recording's notes); a 4K one would not keep up, there the native picture goes
    bool appleProres = false;
#ifdef __APPLE__
    appleProres = request.codec == "prores";  // prores_videotoolbox converts B,G,R,A itself
#endif
    // Software and ProRes encoders take 4:4:4, a GPU's only where its trial found it (VideoToolbox never)
    const bool takes444 = !appleProres && (hardware.name.isEmpty() || hardware.full) &&
                          !hardware.name.contains("videotoolbox");
    const bool yuv444 =
        !native && !nv12 &&
        (request.transfer == "yuv444" ||
         (request.transfer == "auto" && takes444 && static_cast<qint64>(_size.width()) * _size.height() <= 1920 * 1088));
    _transfer = native ? "native" : (nv12 ? "nv12" : yuv444 ? "yuv444" : "picture");
    QSize pipeSize = _size;
    QRect pipeScreen = _screenRect;
    QRect pipeKeyboard = _keyboardRect;
    QString pipeFilter;
    if (!native && !nv12 && !yuv444)
    {
        QRect content = _keyboardRect.isEmpty() ? _screenRect : _screenRect.united(_keyboardRect);
        content = content.intersected(QRect(QPoint(0, 0), _size));
        content.setLeft(content.left() & ~1);  // even edges and sides: any encoder's chroma
        content.setTop(content.top() & ~1);
        content.setWidth(content.width() & ~1);
        content.setHeight(content.height() & ~1);
        const double factor = static_cast<double>(_screenRect.width()) / nativePicture.width();
        const double shrink = factor > 1.01 ? factor : 1.0;
        pipeSize = QSize(std::max(2, static_cast<int>(std::lround(content.width() / shrink)) & ~1),
                         std::max(2, static_cast<int>(std::lround(content.height() / shrink)) & ~1));
        const auto toPipe = [&](const QRect& rect) {
            if (rect.isEmpty())
                return QRect(0, pipeSize.height(), pipeSize.width(), 0);
            const int x = static_cast<int>(std::lround((rect.x() - content.x()) / shrink));
            const int y = static_cast<int>(std::lround((rect.y() - content.y()) / shrink));
            const int w = static_cast<int>(std::lround(rect.width() / shrink));
            const int h = static_cast<int>(std::lround(rect.height() / shrink));
            return QRect(x, y, std::min(w, pipeSize.width() - x), std::min(h, pipeSize.height() - y));
        };
        pipeScreen = shrink > 1.0 ? QRect(toPipe(_screenRect).topLeft(), nativePicture) : toPipe(_screenRect);
        pipeScreen = pipeScreen.intersected(QRect(QPoint(0, 0), pipeSize));
        pipeKeyboard = toPipe(_keyboardRect);
        QStringList filters;
        if (pipeSize != content.size())
            filters << QString("scale=%1:%2:flags=bicubic").arg(content.width()).arg(content.height());
        if (content.size() != _size)
            filters << QString("pad=%1:%2:%3:%4:color=0x%5")
                           .arg(_size.width())
                           .arg(_size.height())
                           .arg(content.x())
                           .arg(content.y())
                           .arg(kBackground.rgb() & 0xFFFFFF, 6, 16, QChar('0'));
        pipeFilter = filters.join(',');
    }

    // The picture in Matroska with each frame's time (see MatroskaStreamHeader), the output's frame rate variable:
    // those times, microsecond-exact, are the file's
    QStringList args{"-v", "error", "-y", "-f", "matroska", "-i", "-"};
    // The raw sound's format is all given: no analysis. With ffmpeg's default (5 s of sound read before it goes on)
    // it stopped reading the picture meanwhile, and a paused machine stepped by record/frames makes sound only as the
    // picture is taken: both waited for good ("recorded nothing within 10 s"); running, it only started 5 s late
    if (!fifo.isEmpty())
        // No -thread_queue_size: ffmpeg 9 takes it for outputs only and refuses to start with it before an input.
        // The writer never waits for the sound pipe (it keeps what the pipe does not take), so the default queue does
        args << "-probesize" << "32" << "-analyzeduration" << "0" << "-f" << "s16le"
             << "-ar" << QString::number(_sampleRate) << "-ac" << QString::number(AUDIO_CHANNELS) << "-i" << fifo
             << "-map" << "0:v:0" << "-map" << "1:a:0";
    // The color: B,G,R,A to Y'CbCr by the BT.709 matrix (swscale's default is BT.601, which HD players decode as
    // BT.709: shifted colors) in the last scaler, which makes the encoder's pixel format in the same pass; NV12
    // frames are BT.709 already (FrameScaler). Then the tags (FrameScaler::FfmpegColorTags, the sRGB transfer).
    // ProRes through VideoToolbox takes the B,G,R,A frames themselves and converts them: they get the matrix, primaries
    // and transfer tags but no range (a YUV range tag on B,G,R,A stops the encoder; it writes limited range itself).
    // Elsewhere ProRes is ffmpeg's own prores_ks (no VideoToolbox): 4:4:4 10-bit made by the scaler like the others
#ifdef __APPLE__
    if (request.codec == "prores" && !yuv444)
        pipeFilter += QString(pipeFilter.isEmpty() ? "" : ",") + FrameScaler::FfmpegColorTagsNoRange;
    else
#endif
    {
        QStringList color;
        if (!nv12 && !yuv444)
            color << (request.codec == "prores" ? "scale=out_color_matrix=bt709:out_range=tv,format=yuv444p10le"
                                                : "scale=out_color_matrix=bt709:out_range=tv");
        color << FrameScaler::FfmpegColorTags;
        pipeFilter += QString(pipeFilter.isEmpty() ? "" : ",") + color.join(',');
    }
    if (!hardware.device.isEmpty())
    {
        // VA-API encodes from the card's memory: the picture goes up after the scaling, in the encoder's layout
        // (NV12 frames go up as they come)
        args << "-vaapi_device" << hardware.device;
        pipeFilter += QString(pipeFilter.isEmpty() ? "" : ",") +
                      (nv12 ? "hwupload" : hardware.full ? "format=vuyx,hwupload" : "format=nv12,hwupload");
    }
    // The pixel format the encoder gets: NV12 frames stay 4:2:0 (NVENC and QSV take NV12 itself, x264 / x265 their
    // planar 4:2:0); else 4:4:4 where the encoder has it
    const char* const pixFmt420 = nv12 ? "nv12" : "yuv420p";
    if (!pipeFilter.isEmpty())
        args << "-vf" << pipeFilter;
    args << FfmpegVfrArgs(ffmpeg);
    if (request.codec == "prores")
#ifdef __APPLE__
        args << "-c:v" << "prores_videotoolbox" << "-profile:v" << "4444";
#else
        args << "-c:v" << "prores_ks" << "-profile:v" << "4444" << "-pix_fmt" << "yuv444p10le";
#endif
    else if (!hardware.name.isEmpty())
    {
        // The GPU's encoder (found by probing): real time at any size; 4:4:4 where the card takes it
        args << "-c:v" << hardware.name;
        if (hardware.name.contains("videotoolbox"))
        {
            // Measured on an M1 Ultra: hevc_videotoolbox takes 4:2:2 10-bit at best (rext, even when 4:4:4 is asked),
            // h264 4:2:0 only. Full 4:4:4 in hardware is ProRes 4444 alone. NV12 frames go in as they are
            if (h265 && !nv12)
                args << "-profile:v" << "rext" << "-q:v" << "85" << "-pix_fmt" << "yuv422p10le" << "-tag:v" << "hvc1";
            else if (h265)
                args << "-q:v" << "85" << "-pix_fmt" << "nv12" << "-tag:v" << "hvc1";
            else
                args << "-q:v" << "85" << "-pix_fmt" << pixFmt420;
        }
        else if (hardware.name.contains("nvenc"))
        {
            // Constant QP, as VA-API: hevc_nvenc keeps no -cq in its VBR (measured on a T1200, driver 595, ffmpeg
            // 8.0: 0.2 MB a minute, 30.6 dB on CRT frames at cq 14 and 18 alike; constqp 18 46.1 dB, h264 45.2)
            args << "-rc" << "constqp" << "-qp" << "18" << "-pix_fmt" << (hardware.full ? "yuv444p" : pixFmt420);
        }
        else if (hardware.name.contains("vaapi"))
        {
            if (hardware.lowPower)
                args << "-low_power" << "1";
            args << "-qp" << "18";
        }
        else if (hardware.name.contains("qsv"))
            args << "-global_quality" << "18" << "-pix_fmt" << "nv12";
        else  // amf
            args << "-quality" << "quality" << "-rc" << "cqp" << "-qp_i" << "18" << "-qp_p" << "18" << "-pix_fmt"
                 << pixFmt420;
    }
    else
    {
        // Software: x264 / x265 4:4:4. Presets measured at 1080p (M1 Ultra, loaded): x264 veryfast 33 fps, ultrafast 42; x265 veryfast 3.4,
        // ultrafast 7.6. Above the native picture the fastest preset; x265 always (far from real time either way)
        const bool large = static_cast<qint64>(_size.width()) * _size.height() > 1000000;
        // NV12 frames carry 4:2:0 color: encoded as 4:2:0 (4:4:4 would only repeat the chroma)
        const char* const pixFmt = nv12 ? "yuv420p" : "yuv444p";
        if (request.codec == "h265")
            args << "-c:v" << "libx265" << "-preset" << "ultrafast" << "-crf" << "14" << "-pix_fmt" << pixFmt;
        else
            args << "-c:v" << "libx264" << "-preset" << (large ? "ultrafast" : "veryfast") << "-crf" << "12"
                 << "-pix_fmt" << pixFmt;
    }
    if (!fifo.isEmpty())
    {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == "mp4" || suffix == "m4v")  // MP4 takes no raw PCM
            args << "-c:a" << "aac" << "-b:a" << "320k";
        else
            args << "-c:a" << "pcm_s16le";
    }
    args << _videoPath;

    // The GUI thread only grabs the window; composing, ffmpeg, the sound file and the muxing are the writer's
    auto writer = std::make_shared<Writer>();
    writer->ffmpeg = ffmpeg;
    writer->args = args;
    writer->size = native ? _size : pipeSize;
    writer->screenRect = native ? _screenRect : pipeScreen;
    writer->keyboardRect = native ? _keyboardRect : pipeKeyboard;
    writer->scale = std::clamp(request.scale, 1, 4);
    writer->mux.ffmpeg = ffmpeg;
    writer->mux.path = path;
    writer->mux.videoPath = _videoPath;
    writer->mux.soundPath = _soundPath;
    writer->mux.sampleRate = _sampleRate;
    writer->soundPipe = soundPipe;
    writer->soundDir = soundDir;
    writer->native = native;
    writer->nv12 = nv12;
    writer->yuv444 = yuv444;
    const char* timing = std::getenv("UNREAL_WINDOW_RECORDER_TIMING");
    writer->timing = timing && *timing && std::strcmp(timing, "0") != 0;
    writer->fps = _fps;
    writer->nativeCodec = request.codec;
    writer->audioRate = _sampleRate;
    writer->audio = request.audio;
    writer->onRoom = [self = QPointer<WindowRecorder>(this)] {
        if (self)
            QMetaObject::invokeMethod(self, [self] { if (self) self->onRefreshed(); }, Qt::QueuedConnection);
    };
    _writer = writer;
    writer->thread = std::thread([writer] { writer->run(); });
    _frames = 0;
    _captureMisses = 0;
    _unpictured.clear();
    _covered = 0;
    _keyboardDelay.clear();

    // Every emulated frame from now on: its number (the picture follows it) and its sound
    _emulator = emulator;
    _sink = std::make_shared<Sink>();
    _sink->keepSamples = request.audio;
    _tapId = context->pSoundManager->addFrameAudioTap(
        [sink = _sink](const int16_t* samples, size_t sampleFrames, uint64_t frame) {
            std::unique_lock<std::mutex> lock(sink->mutex);
            // Video sync: the recording is behind (the window drew no picture, the encoder is busy): the machine
            // waits for it. Bounded: a GUI thread busy elsewhere (a modal dialog, waiting for the machine to pause) must
            // not hold the machine for good
            sink->advanced.wait_for(lock, std::chrono::milliseconds(250), [&] {
                return sink->closing || !sink->synced || sink->frames < sink->pictured + sink->maxAhead;
            });
            sink->started = true;
            sink->frames++;
            FrameSound sound;
            sound.frame = frame;
            if (sink->keepSamples)
                sound.samples.assign(samples, samples + sampleFrames * AUDIO_CHANNELS);
            sink->pending.push_back(std::move(sound));
        });
    connect(_screen, &DeviceScreenWrapper::refreshed, this, &WindowRecorder::onRefreshed);
#ifdef __APPLE__
    // The Mac's own encoder takes frames the GPU window composes in its buffers (no copy through the CPU); the CPU
    // way stays for the software screen and Vulkan
    if (_writer->native)
        _screen->setRecordingFrame({_writer->size, _writer->screenRect, kBackground, _writer->scale});
#endif
    // Transfer nv12 / yuv444: the GPU window converts the picture in its place, the writer only puts the bytes in the
    // frame
    if (_writer->nv12 || _writer->yuv444)
        _screen->setRecordingFrame({_writer->size, _writer->screenRect, kBackground, _writer->scale,
                                    _writer->nv12 ? RecordingFrameLayout::Planes::Nv12
                                                  : RecordingFrameLayout::Planes::Yuv444});
    _capture = _screen->setRecordingCapture(true, _request.pictureHeight);
    fill(out);
    return true;
}

bool WindowRecorder::stop(WindowRecordingStatus& out, std::string& error)
{
    reap();
    if (!_writer || _writer->closing)
    {
        error = "The window is not being recorded";
        return false;
    }
    closeWriter();
    fill(out);
    return true;
}

void WindowRecorder::closeWriter()
{
    if (_screen)
    {
        disconnect(_screen, &DeviceScreenWrapper::refreshed, this, &WindowRecorder::onRefreshed);
        if (_capture)
            _screen->setRecordingCapture(false, 0);
    }
    _capture = false;
    if (_sink)
    {
        // A tap waiting for the recording goes on: removeTap waits for the call in progress
        std::lock_guard<std::mutex> lock(_sink->mutex);
        _sink->closing = true;
        _sink->advanced.notify_all();
    }
    // The frames after the last picture are dropped with their sound: both end on the same frame
    removeTap();
    restoreKeyboard();
    _writer->close(_frames);  // the writer finishes the file in the background
    emit stateChanged();
}

void WindowRecorder::restoreKeyboard()
{
    if (!_restoreKeyboard.empty() && _keyboard)
        _keyboard->choose(_restoreKeyboard);
    _restoreKeyboard.clear();
}

void WindowRecorder::removeTap()
{
    if (_tapId == 0)
        return;
    // Returns after a tap call in progress: nothing reaches the sink afterwards
    if (const std::shared_ptr<Emulator> emulator = _emulator.lock())
        if (EmulatorContext* context = emulator->GetContext(); context && context->pSoundManager)  // lifetime-ok: locked
            context->pSoundManager->removeFrameAudioTap(_tapId);
    _tapId = 0;
}

void WindowRecorder::onRefreshed()
{
    if (!_writer || _writer->closing || !_sink)
        return;
    if (_writer->failed)  // ffmpeg or the files failed: finish what there is, the error comes with the status
    {
        closeWriter();
        return;
    }
    if (!_writer->ready)
    {
        // The encoder is still starting: the file's first frame is the first one after it is up, so what ran
        // meanwhile is dropped, not queued behind the start (a lost picture, a sound far ahead of the video). The
        // count goes too: kept, the first picture stood for the frames of the start with no sound under them, and
        // the whole file's sound came that much (300 ms) before its picture
        std::lock_guard<std::mutex> lock(_sink->mutex);
        _sink->pending.clear();
        _sink->frames = 0;
        _sink->started = false;
        return;
    }
    // The emulated frames since the start: the file holds one picture for each, and each one's sound
    std::vector<FrameSound> sounds;
    {
        std::lock_guard<std::mutex> lock(_sink->mutex);
        if (!_sink->started)
            return;  // a redraw before the first frame ran
        sounds.swap(_sink->pending);
    }
    // The frames in the order they ran, each with its sound, until a picture of it is in the file: the picture and
    // the sound of one emulated frame go in together, matched by the machine's frame number the window's picture
    // carries (the window shows a frame some latches after it ran: more while ZX DLSS looks ahead, under load, on
    // a slow presenter)
    for (FrameSound& sound : sounds)
        _unpictured.push_back(std::move(sound));
    if (_unpictured.empty())
        return;  // a redraw of a frame already in the file
    // The writer is behind (the encoder, the disk): the GUI never waits for it. The picture is taken when there is
    // room again; meanwhile the machine waits in the tap (video sync), no picture stands for frames it did not show
    if (!_writer->hasRoom())
    {
        _writer->requestRetry();
        return;
    }
    // The keyboard as it is now, by the newest frame run: a picture takes the newest one not after its frame
    if (!_keyboardId.empty() && _keyboard)
    {
        _keyboard->refresh();
        _keyboardDelay.emplace_back(_unpictured.back().frame, _keyboard->renderImage(_writer->keyboardRect.width()));
        if (_keyboardDelay.size() > 64)
            _keyboardDelay.pop_front();
    }
    // The pictures the GPU window captured after its presents (read back without waiting), one per frame it showed,
    // oldest first, each with its frame's number (0: not known)
    std::vector<RecordingPicture> pictures;
    if (_capture && _screen)
        _screen->takeRecordingPictures(pictures);
    // None twice in a row: the window presents nothing (hidden, minimized; or no GPU window: the software screen),
    // so the picture is drawn here, the frame the window would show now
    _captureMisses = pictures.empty() ? _captureMisses + 1 : 0;
    if (pictures.empty() && (!_capture || _captureMisses > 1))
    {
        RecordingPicture picture;
        std::string error;
        if (!_screen || !_screen->grabRecordingPicture(picture, error, _request.pictureHeight))
            return;
        pictures.push_back(std::move(picture));
    }
    for (size_t k = 0; k < pictures.size() && !_unpictured.empty(); ++k)
    {
        // The picture's frame among those waiting. Not known (the software screen): the frame the window shows,
        // the present delay behind the newest. Not among them: a frame already in the file (a picture late), or the
        // counter jumped (a reset, a snapshot, a TTD seek): then it stands for the oldest waiting
        size_t at = 0;
        bool matched = false;
        const uint64_t shown = pictures[k].number;
        if (shown == 0)
        {
            const size_t behind = std::min(presentDelay(), _unpictured.size() - 1);
            at = _unpictured.size() - 1 - behind;
            matched = true;
        }
        else
            for (size_t i = 0; i < _unpictured.size(); ++i)
                if (_unpictured[i].frame == shown)
                {
                    at = i;
                    matched = true;
                    break;
                }
        if (!matched)
        {
            const uint64_t oldest = _unpictured.front().frame;
            if (shown < oldest && oldest - shown <= kStalePictureFrames)
                continue;  // its frame is in the file already
        }
        if (_frames == 0)
        {
            // The file starts with this picture: the frames run before it go with their sound
            _covered += at;
            _unpictured.erase(_unpictured.begin(), _unpictured.begin() + static_cast<std::ptrdiff_t>(at));
            at = 0;
        }
        // Frames before it with no picture of their own (the window skipped them): it stands for them too
        const uint64_t number = _unpictured[at].frame;
        while (_keyboardDelay.size() > 1 && _keyboardDelay[1].first <= number)
            _keyboardDelay.pop_front();
        Writer::Job job;
        job.picture = std::move(pictures[k].image);
        job.frame = std::move(pictures[k].frame);
        if (!_keyboardDelay.empty())
            job.keyboard = _keyboardDelay.front().second;
        job.repeat = static_cast<uint32_t>(at + 1);
        job.frameStart = _frames;
        if (!_writer->pushPicture(std::move(job)))
        {
            _writer->requestRetry();  // full: the rest when there is room (the machine waits in the tap meanwhile)
            break;
        }
        for (size_t i = 0; i <= at; ++i)
        {
            std::vector<int16_t>& samples = _unpictured.front().samples;
            if (!samples.empty())
            {
                _writer->pushSound(QByteArray(reinterpret_cast<const char*>(samples.data()),
                                              static_cast<qsizetype>(samples.size() * sizeof(int16_t))),
                                   _sampleRate > 0 ? static_cast<double>(_audioSamples) / _sampleRate : 0.0);
                _audioSamples += samples.size() / AUDIO_CHANNELS;
            }
            _unpictured.pop_front();
        }
        _frames += at + 1;
        _covered += at + 1;
    }
    // Before the first picture: only the newest frames wait (the file starts with a picture)
    if (_frames == 0 && _unpictured.size() > kMaxUnpictured)
    {
        const size_t extra = _unpictured.size() - kMaxUnpictured;
        _unpictured.erase(_unpictured.begin(), _unpictured.begin() + static_cast<std::ptrdiff_t>(extra));
        _covered += extra;
    }
    // The window's pictures come the present delay after their frame ran, and a refresh later than their present:
    // the machine waits only past that (with less, it waited on every frame: 47.5 instead of 49.5 fps)
    const uint64_t maxAhead = presentDelay() + kSyncSlackFrames;
    std::lock_guard<std::mutex> lock(_sink->mutex);
    _sink->maxAhead = maxAhead;
    _sink->pictured = _covered;
    _sink->synced = _sink->synced || _frames > 0;
    _sink->advanced.notify_all();
}

size_t WindowRecorder::presentDelay() const
{
    const std::shared_ptr<Emulator> emulator = _emulator.lock();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;  // lifetime-ok: locked
    return context && context->pScreen ? context->pScreen->GetShownDelayFrames() : 0;
}

void WindowRecorder::fill(WindowRecordingStatus& out)
{
    reap();
    out.recording = _writer && !_writer->closing;
    out.finalizing = _writer && _writer->closing;
    out.error = _lastError;
    out.path = _request.path;
    out.keyboard = _keyboardId;
    out.frames = _frames;
    out.width = static_cast<uint16_t>(std::max(0, _size.width()));  // 0 before the first recording, not -1
    out.height = static_cast<uint16_t>(std::max(0, _size.height()));
    out.fps = _fps;
    out.audio = _audio;
    out.transfer = _transfer;
    out.framesPending = _writer ? (_frames > _writer->framesDone ? _frames - _writer->framesDone : 0) : 0;
    out.sampleRate = _sampleRate;
    out.audioSamples = _audioSamples;
    if (_writer)
    {
        out.writerStartMs = static_cast<double>(_writer->writerStartUs) / 1000.0;
        out.encoderReadyMs = static_cast<double>(_writer->encoderReadyUs) / 1000.0;
        out.firstFrameMs = static_cast<double>(_writer->firstFrameUs) / 1000.0;
        out.slowestFrameMs = static_cast<double>(_writer->slowestFrameUs) / 1000.0;
    }
}

#include "moc_windowrecorder.cpp"
