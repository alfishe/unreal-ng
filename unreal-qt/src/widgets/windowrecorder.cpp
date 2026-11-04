#include "windowrecorder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <thread>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "devicescreenwrapper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "osdkeyboard/osdkeyboardwindow.h"
#ifdef __APPLE__
#include "common/encoderconfig.h"
#include "platform/macos/videotoolbox_encoder.h"
#endif

namespace
{
const QColor kBackground(24, 24, 24);
constexpr int kMaxQueuedFrames = 10;  // ffmpeg behind by this many pictures: the next ones repeat (running) or wait (paused)

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
        QImage picture;   ///< the window's picture (null = a sound-only job)
        QImage keyboard;  ///< the keyboard under it (null = none)
        uint32_t repeat = 0;
        uint64_t frameStart = 0;  ///< the picture's first frame in the file (its time: frameStart / fps)
        QByteArray sound;
        double soundTime = 0;     ///< the sound's emulated time in seconds
    };

    // The file's layout
    QString ffmpeg;
    QStringList args;
    QSize size;
    QRect screenRect, keyboardRect;
    int scale = 1;
    MuxJob mux;
    bool native = false;   ///< the macOS hardware encoder writes the file itself (no ffmpeg, no muxing)
    double fps = 50;
    int nativeQuality = 8;
    std::string nativeCodec = "h264";
    uint32_t audioRate = 44100;
    bool audio = false;
    QString fifoPath;  ///< the sound goes into the same ffmpeg through this FIFO (no muxing afterwards)
    std::atomic<uint64_t> framesDone{0};

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    int queuedPictures = 0;
    bool closing = false;
    bool wantRetry = false;                 ///< a paused machine's picture was refused: tell the GUI when there is room
    std::function<void()> onRoom;           ///< called from the writer thread
    std::atomic<bool> failed{false};
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
    /// The writer is behind: the last queued picture stays on screen for `frames` more
    void repeatLast(uint32_t frames)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = jobs.rbegin(); it != jobs.rend(); ++it)
            if (!it->picture.isNull())
            {
                it->repeat += frames;
                return;
            }
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
        if (scale != 1 || frame.format() != QImage::Format_ARGB32 || (!swap && !same))
            return false;
        if (picture.width() > frame.width() || picture.height() > frame.height() ||
            std::abs(picture.width() - screenRect.width()) > 4 || std::abs(picture.height() - screenRect.height()) > 4)
            return false;
        const int x0 = screenRect.x() + (screenRect.width() - picture.width()) / 2;
        const int y0 = screenRect.y() + (screenRect.height() - picture.height()) / 2;
        if (x0 < 0 || y0 < 0 || x0 + picture.width() > frame.width() || y0 + picture.height() > frame.height())
            return false;
        // Rows are independent: a few threads write them at once (the encoder's pixel buffer is shared memory, one
        // thread does not fill its bandwidth)
        const auto swapRows = [&](int first, int last)
        {
            for (int y = first; y < last; ++y)
            {
                const uint32_t* from = reinterpret_cast<const uint32_t*>(picture.constScanLine(y));
                uint32_t* to = reinterpret_cast<uint32_t*>(frame.scanLine(y0 + y)) + x0;
                for (int x = 0; x < picture.width(); ++x)
                {
                    const uint32_t p = from[x];
                    // RGBA bytes = A,B,G,R as a little-endian word: red and blue swap. BGRA-ordered formats are the
                    // frame's own order. The window is opaque: the alpha is set either way
                    to[x] = (swap ? ((p & 0x0000FF00u) | ((p & 0xFFu) << 16) | ((p >> 16) & 0xFFu)) : (p & 0x00FFFFFFu)) |
                            0xFF000000u;
                }
            }
        };
        constexpr int kParts = 4;
        const int rows = picture.height();
        std::thread helpers[kParts - 1];
        for (int part = 1; part < kParts; ++part)
            helpers[part - 1] = std::thread(swapRows, rows * part / kParts, rows * (part + 1) / kParts);
        swapRows(0, rows / kParts);
        for (std::thread& helper : helpers)
            helper.join();
        return true;
    }

    void composeInto(QImage& frame, const Job& job) const
    {
        frame.fill(kBackground);
        const bool fast = composeFast(frame, job);
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
                if (!job.picture.isNull() && --queuedPictures < kMaxQueuedFrames && wantRetry)
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
            if (job.picture.isNull())
                continue;
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
        }
        encoder.Stop();
        error = failure;
        done = true;
    }
#endif

    void run()
    {
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
        int fifoFd = -1;
        std::deque<QByteArray> backlog;  // sound waiting for ffmpeg to open the FIFO (it does so after the first frames)
        // Opens the FIFO once ffmpeg reads it (never blocks), then hands over what the pipe takes. It never waits for
        // the pipe: ffmpeg reads the sound only as fast as the picture lets it, and a writer stuck on the sound could
        // not feed the picture (a deadlock). False: ffmpeg is gone
        size_t backlogOffset = 0;
        auto pumpSound = [&]() -> bool {
#ifdef Q_OS_UNIX
            if (fifoPath.isEmpty())
                return true;
            if (fifoFd < 0)
            {
                fifoFd = ::open(fifoPath.toLocal8Bit().constData(), O_WRONLY | O_NONBLOCK);
                if (fifoFd < 0)
                    return true;  // no reader yet
            }
            while (!backlog.empty())
            {
                const QByteArray& chunk = backlog.front();
                const ssize_t n = ::write(fifoFd, chunk.constData() + backlogOffset,
                                          static_cast<size_t>(chunk.size()) - backlogOffset);
                if (n < 0)
                    return errno == EAGAIN || errno == EWOULDBLOCK;  // the pipe is full: the rest waits
                backlogOffset += static_cast<size_t>(n);
                if (backlogOffset == static_cast<size_t>(chunk.size()))
                {
                    backlog.pop_front();
                    backlogOffset = 0;
                }
            }
#endif
            return true;
        };
        auto fail = [&](const std::string& why) {
            if (failure.empty())
                failure = why;
            failed = true;
        };
        if (!failure.empty())
            failed = true;

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
                if (!job.picture.isNull() && --queuedPictures < kMaxQueuedFrames && wantRetry)
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
                if (!fifoPath.isEmpty())
                {
                    backlog.push_back(std::move(job.sound));
                    if (!pumpSound())
                        fail("ffmpeg stopped taking the sound");
                }
                else if (sound.write(job.sound) != job.sound.size())
                    fail("Cannot write the sound: " + sound.errorString().toStdString());
            }
            if (job.picture.isNull() || !failure.empty())
                continue;
            const QImage frame = compose(job);
            for (uint32_t i = 0; i < job.repeat && failure.empty(); i++)
            {
                process.write(reinterpret_cast<const char*>(frame.constBits()), frame.sizeInBytes());
                // No event loop here: the bytes go to ffmpeg only while we wait, and ffmpeg's pace is the pace
                if (!process.waitForBytesWritten(60000) || process.state() != QProcess::Running)
                    fail("ffmpeg stopped taking frames: " + process.errorString().toStdString());
                framesDone++;
                if (!pumpSound())
                    fail("ffmpeg stopped taking the sound");
            }
        }

        sound.close();
        std::string result = failure;
        if (failure.empty())
        {
            process.closeWriteChannel();  // the end of the picture first: ffmpeg interleaves the sound with it
#ifdef Q_OS_UNIX
            if (!fifoPath.isEmpty())
            {
                // The rest of the sound goes in as ffmpeg takes it (it opens the FIFO only after it has the frames),
                // then the FIFO closes: that is the end of the sound. A slow encoder is waited for, never killed
                while ((fifoFd < 0 || !backlog.empty()) && process.state() != QProcess::NotRunning)
                {
                    if (!pumpSound())
                    {
                        failure = result = "ffmpeg stopped taking the sound";
                        break;
                    }
                    process.waitForFinished(5);
                }
                if (fifoFd >= 0)
                    ::close(fifoFd);
                fifoFd = -1;
            }
#endif
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
#ifdef Q_OS_UNIX
        if (fifoFd >= 0)
            ::close(fifoFd);
        if (!fifoPath.isEmpty())
            ::unlink(fifoPath.toLocal8Bit().constData());
#endif
        error = result;
        done = true;
    }
};

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
    return ok;
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
    // The Mac's own encoder (VideoToolbox: H.264 / H.265 on the GPU, ProRes 4444) needs no ffmpeg
    bool native = false;
#ifdef __APPLE__
    native = request.codec == "prores" || request.hardware;
#endif
    QString ffmpeg;
    if (!native)
    {
        // A GUI started from the Finder has no Homebrew PATH
        ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty())
            ffmpeg = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
        if (ffmpeg.isEmpty())
        {
            error = "The window recording needs ffmpeg on the PATH";
            return false;
        }
    }
    _ffmpeg = ffmpeg;

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
    const int keyboardAspectHeight = _keyboardId.empty() ? 0 : _keyboard->renderImage(1000).height();
    if (request.frameWidth > 0 && request.frameHeight > 0)
    {
        _size = QSize(request.frameWidth & ~1, request.frameHeight & ~1);
        if (request.fit)  // the picture scaled to the frame, aspect kept, centred (a CRT drawn at its height needs none)
        {
            const double factor = std::min(static_cast<double>(_size.width()) / picture.width(),
                                           static_cast<double>(_size.height()) / picture.height());
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
            const int height = keyboardAspectHeight * _size.width() / 1000;
            const int top = (_size.height() - picture.height() - height) / 2;
            _screenRect = QRect(QPoint((_size.width() - picture.width()) / 2, std::max(0, top)), picture);
            _keyboardRect = QRect(0, _screenRect.bottom() + 1, _size.width(), height);
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

    // The machine's frame rate, exact (3500000/71680 on a Pentagon): the sound's clock is the same frames
    const unsigned tstatesPerFrame = context->config.frame;
    QString rate = QString::number(request.fps);
    _fps = request.fps;
    if (request.fps == 0)
    {
        if (tstatesPerFrame == 0)
        {
            error = "The machine reports no frame length";
            return false;
        }
        rate = QString("%1/%2").arg(static_cast<qulonglong>(CPU_CLOCK_RATE)).arg(tstatesPerFrame);
        _fps = static_cast<double>(CPU_CLOCK_RATE) / tstatesPerFrame;
    }
    _sampleRate = request.audio ? static_cast<uint32_t>(context->pSoundManager->getCoreRate()) : 0;
    _audioSamples = 0;

    // With sound the picture goes to a file beside the target and the sound to a raw file, muxed at stop
    const QString path = QString::fromStdString(request.path);
    _videoPath = path;
    _soundPath.clear();
    QString fifo;
    _audio = request.audio;
    if (request.audio && !native)
    {
        const QFileInfo target(path);
#ifdef Q_OS_UNIX
        // The sound reaches the same ffmpeg through a FIFO: one file from the start, nothing to mux (and copy) at
        // the end, however large the picture is
        fifo = target.dir().filePath("." + target.completeBaseName() + ".sound.fifo");
        ::unlink(fifo.toLocal8Bit().constData());
        if (::mkfifo(fifo.toLocal8Bit().constData(), 0600) != 0)
            fifo.clear();
        signal(SIGPIPE, SIG_IGN);  // a dead ffmpeg is an error return, not a signal
#endif
        if (fifo.isEmpty())  // no FIFO (Windows): the picture and the sound in two files, muxed at the end
        {
            _videoPath = target.dir().filePath("." + target.completeBaseName() + ".video." + target.suffix());
            _soundPath = target.dir().filePath("." + target.completeBaseName() + ".sound.s16le");
        }
    }

    QStringList args{"-v",      "error", "-y",  "-f",  "rawvideo", "-pix_fmt", "rgba", "-s",
                     QString("%1x%2").arg(_size.width()).arg(_size.height()), "-r", rate,
                     "-i",      "-"};
    if (!fifo.isEmpty())
        args << "-thread_queue_size" << "512" << "-f" << "s16le" << "-ar" << QString::number(_sampleRate) << "-ac"
             << QString::number(AUDIO_CHANNELS) << "-i" << fifo << "-map" << "0:v:0" << "-map" << "1:a:0";
    if (request.codec == "prores")
        args << "-c:v" << "prores_videotoolbox" << "-profile:v" << "4444";
    else if (request.hardware)  // the GPU's encoder: 4:2:0, real time at any size
    {
#ifdef Q_OS_MAC
        // Measured on an M1 Ultra: hevc_videotoolbox takes 4:2:2 10-bit at best (rext, even when 4:4:4 is asked), h264
        // 4:2:0 only. Full 4:4:4 in hardware is ProRes 4444 alone
        if (request.codec == "h265")
            args << "-c:v" << "hevc_videotoolbox" << "-profile:v" << "rext" << "-q:v" << "85" << "-pix_fmt"
                 << "yuv422p10le" << "-tag:v" << "hvc1";
        else
            args << "-c:v" << "h264_videotoolbox" << "-q:v" << "85" << "-pix_fmt" << "yuv420p";
#else
        args << "-c:v" << (request.codec == "h265" ? "hevc_nvenc" : "h264_nvenc") << "-cq" << "18" << "-pix_fmt"
             << "yuv420p";
#endif
    }
    else if (request.codec == "h265")
        args << "-c:v" << "libx265" << "-preset" << "veryfast" << "-crf" << "14" << "-pix_fmt" << "yuv444p";
    else
        args << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "12" << "-pix_fmt" << "yuv444p";
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
    writer->size = _size;
    writer->screenRect = _screenRect;
    writer->keyboardRect = _keyboardRect;
    writer->scale = std::clamp(request.scale, 1, 4);
    writer->mux.ffmpeg = ffmpeg;
    writer->mux.path = path;
    writer->mux.videoPath = _videoPath;
    writer->mux.soundPath = _soundPath;
    writer->mux.sampleRate = _sampleRate;
    writer->fifoPath = fifo;
    writer->native = native;
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
    _soundDelay.clear();
    _keyboardDelay.clear();

    // Every emulated frame from now on: its number (the picture follows it) and its sound
    _emulator = emulator;
    _sink = std::make_shared<Sink>();
    _sink->keepSamples = request.audio;
    _tapId = context->pSoundManager->addFrameAudioTap(
        [sink = _sink](const int16_t* samples, size_t sampleFrames, uint64_t frame) {
            std::lock_guard<std::mutex> lock(sink->mutex);
            if (!sink->started)
            {
                sink->started = true;
                sink->firstFrame = frame;
            }
            sink->lastFrame = frame;
            FrameSound sound;
            sound.frame = frame;
            if (sink->keepSamples)
                sound.samples.assign(samples, samples + sampleFrames * AUDIO_CHANNELS);
            sink->pending.push_back(std::move(sound));
        });
    connect(_screen, &DeviceScreenWrapper::refreshed, this, &WindowRecorder::onRefreshed);
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
        disconnect(_screen, &DeviceScreenWrapper::refreshed, this, &WindowRecorder::onRefreshed);
    // The frames after the last picture are dropped with their sound: both end on the same frame
    removeTap();
    restoreKeyboard();
    _writer->close(_frames);  // the writer finishes the file in the background
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
    // The emulated frames since the start: the file holds one picture for each, and each one's sound
    std::vector<FrameSound> sounds;
    uint64_t target = 0;
    {
        std::lock_guard<std::mutex> lock(_sink->mutex);
        if (!_sink->started)
            return;  // a redraw before the first frame ran
        target = _sink->lastFrame - _sink->firstFrame + 1;
        sounds.swap(_sink->pending);
    }
    // The window shows the frame latched the present delay ago (2 frames for the A/V sync, more while ZX DLSS looks
    // ahead): the sound and the keyboard wait as long, so all three in the file are of the same emulated frame
    const size_t delay = presentDelay();
    for (FrameSound& sound : sounds)
    {
        if (sound.samples.empty())
            continue;
        const size_t length = sound.samples.size();
        _soundDelay.push_back(std::move(sound.samples));
        while (_soundDelay.size() > delay + 1)
            _soundDelay.pop_front();  // the delay shrank: drop the oldest
        std::vector<int16_t> out;
        if (_soundDelay.size() > delay)
        {
            out = std::move(_soundDelay.front());
            _soundDelay.pop_front();
        }
        else
            out.assign(length, 0);  // silence while the line fills
        _writer->pushSound(QByteArray(reinterpret_cast<const char*>(out.data()),
                                      static_cast<qsizetype>(out.size() * sizeof(int16_t))),
                           _sampleRate > 0 ? static_cast<double>(_audioSamples) / _sampleRate : 0.0);
        _audioSamples += out.size() / AUDIO_CHANNELS;
    }
    if (target <= _frames)
        return;  // a redraw of a frame already in the file
    // The writer is behind (ffmpeg, the disk): the GUI never waits for it. A running machine's picture stays on
    // screen for the frames it owes; a paused one (every frame wanted) takes the picture when there is room again
    if (!_writer->hasRoom())
    {
        const std::shared_ptr<Emulator> emulator = _emulator.lock();
        if (emulator && emulator->IsPaused())
        {
            _writer->requestRetry();
            return;
        }
        _writer->repeatLast(static_cast<uint32_t>(target - _frames));
        _frames = target;
        return;
    }
    // The keyboard as it was when the frame on screen ran: the newest picture of it at most the delay old
    if (!_keyboardId.empty() && _keyboard)
    {
        _keyboard->refresh();
        _keyboardDelay.emplace_back(target, _keyboard->renderImage(_keyboardRect.width()));
        while (_keyboardDelay.size() > 1 && _keyboardDelay[1].first + delay <= target)
            _keyboardDelay.pop_front();
    }
    QImage picture;
    std::string error;
    if (!_screen || !_screen->grabWindowPicture(picture, error, _request.pictureHeight))
        return;
    // The frames the window did not draw (a busy GUI, turbo) repeat this picture: the time stays the machine's
    Writer::Job job;
    job.picture = std::move(picture);
    if (!_keyboardDelay.empty())
        job.keyboard = _keyboardDelay.front().second;
    job.repeat = static_cast<uint32_t>(target - _frames);
    job.frameStart = _frames;
    if (_writer->pushPicture(std::move(job)))
        _frames = target;
}

size_t WindowRecorder::presentDelay() const
{
    const std::shared_ptr<Emulator> emulator = _emulator.lock();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;  // lifetime-ok: locked
    return context && context->pScreen ? context->pScreen->GetEffectivePresentDelayFrames() : 0;
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
    out.framesPending = _writer ? (_frames > _writer->framesDone ? _frames - _writer->framesDone : 0) : 0;
    out.sampleRate = _sampleRate;
    out.audioSamples = _audioSamples;
}
