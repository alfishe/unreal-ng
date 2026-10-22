#include "windowrecorder.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

#include "devicescreenwrapper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/video/screen.h"
#include "osdkeyboard/osdkeyboardwindow.h"

namespace
{
const QColor kBackground(24, 24, 24);
constexpr qint64 kMaxQueuedBytes = 256LL * 1024 * 1024;  // ffmpeg behind by this much: wait for it

QImage ToImage(const WindowViewGrab& grab)
{
    QImage image(grab.rgba.data(), grab.width, grab.height, static_cast<qsizetype>(grab.width) * 4,
                 QImage::Format_RGBA8888);
    return image.copy();  // own the pixels: the grab goes away
}
}  // namespace

WindowRecorder::WindowRecorder(OsdKeyboardWindow* keyboard, QObject* parent) : QObject(parent), _keyboard(keyboard)
{
}

WindowRecorder::~WindowRecorder()
{
    WindowRecordingStatus status;
    std::string error;
    if (_process)
        stop(status, error);
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
    if (_process)
    {
        error = "The window is already being recorded to " + _request.path;
        return false;
    }
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
    if (request.codec != "prores" && request.codec != "h264")
    {
        error = "Unknown codec '" + request.codec + "': use prores or h264";
        return false;
    }
    // A GUI started from the Finder has no Homebrew PATH
    QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty())
        ffmpeg = QStandardPaths::findExecutable("ffmpeg", {"/opt/homebrew/bin", "/usr/local/bin"});
    if (ffmpeg.isEmpty())
    {
        error = "The window recording needs ffmpeg on the PATH";
        return false;
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
    const QSize picture(grab.width * scale, grab.height * scale);
    const int keyboardAspectHeight = _keyboardId.empty() ? 0 : _keyboard->renderImage(1000).height();
    if (request.frameWidth > 0 && request.frameHeight > 0)
    {
        _size = QSize(request.frameWidth & ~1, request.frameHeight & ~1);
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
    if (request.audio)
    {
        const QFileInfo target(path);
        _videoPath = target.dir().filePath("." + target.completeBaseName() + ".video." + target.suffix());
        _soundPath = target.dir().filePath("." + target.completeBaseName() + ".sound.s16le");
        _sound.setFileName(_soundPath);
        if (!_sound.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            error = "Cannot write " + _soundPath.toStdString() + ": " + _sound.errorString().toStdString();
            return false;
        }
    }

    QStringList args{"-v",      "error", "-y",  "-f",  "rawvideo", "-pix_fmt", "rgba", "-s",
                     QString("%1x%2").arg(_size.width()).arg(_size.height()), "-r", rate,
                     "-i",      "-"};
    if (request.codec == "prores")
        args << "-c:v" << "prores_videotoolbox" << "-profile:v" << "4444";
    else
        args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "12" << "-pix_fmt" << "yuv444p";
    args << _videoPath;

    _process = new QProcess(this);
    _process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    _process->start(ffmpeg, args);
    if (!_process->waitForStarted(5000))
    {
        error = "ffmpeg did not start: " + _process->errorString().toStdString();
        delete _process;
        _process = nullptr;
        _sound.close();
        if (!_soundPath.isEmpty())
            QFile::remove(_soundPath);
        return false;
    }
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
    if (!_process)
    {
        error = "The window is not being recorded";
        return false;
    }
    if (_screen)
        disconnect(_screen, &DeviceScreenWrapper::refreshed, this, &WindowRecorder::onRefreshed);
    // The frames after the last picture are dropped with their sound: both end on the same frame
    removeTap();
    restoreKeyboard();
    _process->closeWriteChannel();
    const bool finished = _process->waitForFinished(120000);
    bool ok = finished && _process->exitStatus() == QProcess::NormalExit && _process->exitCode() == 0;
    if (!ok)
        error = finished ? "ffmpeg failed with code " + std::to_string(_process->exitCode())
                         : "ffmpeg did not finish within 2 minutes";
    delete _process;
    _process = nullptr;
    if (_sound.isOpen())
        _sound.close();
    if (ok && !_soundPath.isEmpty())
        ok = mux(error);
    fill(out);
    out.recording = false;
    return ok;
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

bool WindowRecorder::mux(std::string& error)
{
    const QString path = QString::fromStdString(_request.path);
    if (_frames == 0)  // nothing recorded: the empty picture file is the result
    {
        QFile::remove(path);
        QFile::remove(_soundPath);
        return QFile::rename(_videoPath, path);
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    QStringList args{"-v", "error", "-y", "-i", _videoPath, "-f", "s16le", "-ar", QString::number(_sampleRate),
                     "-ac", QString::number(AUDIO_CHANNELS), "-i", _soundPath, "-map", "0:v:0", "-map", "1:a:0",
                     "-c:v", "copy"};
    if (suffix == "mp4" || suffix == "m4v")  // MP4 takes no raw PCM
        args << "-c:a" << "aac" << "-b:a" << "320k";
    else
        args << "-c:a" << "pcm_s16le";
    args << path;
    QProcess muxer;
    muxer.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    muxer.start(_ffmpeg, args);
    if (!muxer.waitForFinished(120000) || muxer.exitStatus() != QProcess::NormalExit || muxer.exitCode() != 0)
    {
        error = "ffmpeg could not add the sound (the picture is in " + _videoPath.toStdString() + ", the sound in " +
                _soundPath.toStdString() + ")";
        return false;
    }
    QFile::remove(_videoPath);
    QFile::remove(_soundPath);
    return true;
}

void WindowRecorder::onRefreshed()
{
    if (!_process || !_sink)
        return;
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
        _sound.write(reinterpret_cast<const char*>(out.data()), static_cast<qint64>(out.size() * sizeof(int16_t)));
        _audioSamples += out.size() / AUDIO_CHANNELS;
    }
    if (target <= _frames)
        return;  // a redraw of a frame already in the file
    // The keyboard as it was when the frame on screen ran: the newest picture of it at most the delay old
    if (!_keyboardId.empty() && _keyboard)
    {
        _keyboard->refresh();
        _keyboardDelay.emplace_back(target, _keyboard->renderImage(_keyboardRect.width()));
        while (_keyboardDelay.size() > 1 && _keyboardDelay[1].first + delay <= target)
            _keyboardDelay.pop_front();
    }
    QImage frame;
    std::string error;
    if (!compose(frame, error))
        return;
    // The frames the window did not draw (a busy GUI, turbo) repeat this picture: the time stays the machine's
    for (uint64_t i = _frames; i < target; i++)
    {
        while (_process->bytesToWrite() > kMaxQueuedBytes && _process->waitForBytesWritten(1000))
        {
        }
        _process->write(reinterpret_cast<const char*>(frame.constBits()), frame.sizeInBytes());
    }
    _frames = target;
}

size_t WindowRecorder::presentDelay() const
{
    const std::shared_ptr<Emulator> emulator = _emulator.lock();
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;  // lifetime-ok: locked
    return context && context->pScreen ? context->pScreen->GetEffectivePresentDelayFrames() : 0;
}

bool WindowRecorder::compose(QImage& frame, std::string& error)
{
    WindowViewGrab grab;
    if (!_screen || !_screen->grabWindowView(grab, error, false, _request.pictureHeight))
        return false;
    frame = QImage(_size, QImage::Format_RGBA8888);
    frame.fill(kBackground);
    QPainter painter(&frame);
    QImage picture = ToImage(grab);
    const int scale = std::clamp(_request.scale, 1, 4);
    if (scale > 1)  // blocks: the CRT mask stays sharp
        picture = picture.scaled(picture.width() * scale, picture.height() * scale, Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation);
    if (picture.size() != _screenRect.size())
        picture = picture.scaled(_screenRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    painter.drawImage(_screenRect.topLeft(), picture);
    if (!_keyboardDelay.empty())
        painter.drawImage(_keyboardRect.topLeft(), _keyboardDelay.front().second);
    return true;
}

void WindowRecorder::fill(WindowRecordingStatus& out) const
{
    out.recording = _process != nullptr;
    out.path = _request.path;
    out.keyboard = _keyboardId;
    out.frames = _frames;
    out.width = static_cast<uint16_t>(std::max(0, _size.width()));  // 0 before the first recording, not -1
    out.height = static_cast<uint16_t>(std::max(0, _size.height()));
    out.fps = _fps;
    out.audio = !_soundPath.isEmpty();
    out.sampleRate = _sampleRate;
    out.audioSamples = _audioSamples;
}
