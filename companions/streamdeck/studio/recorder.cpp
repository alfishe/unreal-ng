#include "recorder.h"

#include <QFileInfo>
#include <QTimer>
#include <algorithm>

#include "common/encoder_base.h"
#include "common/encoderconfig.h"
#include "emulator/video/screen.h"
#include "encoders/ffmpeg_pipe_encoder.h"
#include "encoders/gif_encoder.h"
#include "platform_encoder.h"

namespace
{
constexpr int kMaxVideoWidth = 2560;  // the deck grabbed on a Retina screen is ~2000 px wide: keep it, cap 5K screens
constexpr int kMaxGifWidth = 960;
constexpr int kMaxGifFps = 25;
constexpr quint64 kMaxCatchUp = 10;  // frames repeated after a stall (a long JPEG decode, a window drag)
}  // namespace

Recorder::Recorder(std::function<QImage()> source, QObject* parent) : QObject(parent), _source(std::move(source))
{
    _timer = new QTimer(this);
    _timer->setTimerType(Qt::PreciseTimer);
    connect(_timer, &QTimer::timeout, this, &Recorder::Tick);
}

Recorder::~Recorder()
{
    _source = nullptr;  // the widget it grabs may be gone already: finalize with what was recorded
    Stop();
}

bool Recorder::Start(const QString& path, int fps, QString* error)
{
    if (Active())
        Stop();
    const QString ext = QFileInfo(path).suffix().toLower();
    const bool gif = ext == QLatin1String("gif");
    _fps = std::clamp(fps > 0 ? fps : 30, 1, gif ? kMaxGifFps : 60);

    const QImage first = _source();
    if (first.isNull())
    {
        if (error)
            *error = QStringLiteral("nothing to record");
        return false;
    }
    QSize s = first.size();
    const int maxW = gif ? kMaxGifWidth : kMaxVideoWidth;
    if (s.width() > maxW)
        s = QSize(maxW, s.height() * maxW / s.width());
    _size = QSize(s.width() & ~1, s.height() & ~1);  // 4:2:0 codecs need even sizes

    EncoderConfig cfg;
    cfg.videoWidth = static_cast<uint32_t>(_size.width());
    cfg.videoHeight = static_cast<uint32_t>(_size.height());
    cfg.frameRate = static_cast<float>(_fps);
    cfg.audioChannels = 0;  // the deck is silent
    cfg.container = ext.isEmpty() ? std::string("mp4") : ext.toStdString();
    cfg.videoCodec = ext == QLatin1String("webm") ? "vp9" : "h264";
    cfg.videoBitrate = gif ? 0 : 16000;  // sharp text and pixel art on a ~2000 px UI
    cfg.qualityPreset = 8;
    cfg.crf = 18;
    cfg.gifDelayMs = static_cast<uint32_t>(1000 / _fps);
    cfg.allowNonRealtime = false;  // never stall the UI: drop frames instead

    std::unique_ptr<EncoderBase> enc;
    if (gif)
        enc = std::make_unique<GIFEncoder>();
    else if (ext == QLatin1String("mp4") || ext == QLatin1String("mov") || ext == QLatin1String("m4v"))
        enc = PlatformEncoderFactory::createNativeEncoder(cfg);  // VideoToolbox / NVENC, no external tools
    if (!enc)
        enc = std::make_unique<FFmpegPipeEncoder>();
    if (!enc->Start(path.toStdString(), cfg))
    {
        if (error)
            *error = QString::fromStdString(enc->GetLastError());
        return false;
    }
    _encoder = std::move(enc);
    _backend = QString::fromStdString(_encoder->GetDisplayName());
    _path = path;
    _frames = _next = 0;
    _clock.start();
    _timer->start(1000 / _fps);
    Tick();
    emit StateChanged();
    return true;
}

QString Recorder::Stop()
{
    if (!_encoder)
        return {};
    _timer->stop();
    Tick();  // the last picture
    _encoder->Stop();
    const QString err = QString::fromStdString(_encoder->GetLastError());
    const double secs = static_cast<double>(_next) / _fps;
    _encoder.reset();
    const QString summary = QStringLiteral("%1: %2x%3, %4 frames, %5 s at %6 fps, %7, %8 KB%9")
                                .arg(_path)
                                .arg(_size.width())
                                .arg(_size.height())
                                .arg(_frames)
                                .arg(secs, 0, 'f', 1)
                                .arg(_fps)
                                .arg(_backend)
                                .arg(QFileInfo(_path).size() / 1024)
                                .arg(err.isEmpty() ? QString() : QStringLiteral(" (") + err + QLatin1Char(')'));
    emit StateChanged();
    return summary;
}

void Recorder::Tick()
{
    if (!_encoder || !_source)
        return;
    // Constant frame rate: frame k shows at k / fps. After a stall the current picture fills the gap.
    const auto due = static_cast<quint64>(_clock.elapsed() * _fps / 1000);
    if (due < _next)
        return;
    QImage img = _source();
    if (img.isNull())
        return;
    if (img.size() != _size)  // up: nearest, no blur
        img = img.scaled(_size, Qt::IgnoreAspectRatio,
                         img.width() < _size.width() ? Qt::FastTransformation : Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_RGBA8888);
    if (due - _next > kMaxCatchUp)
        _next = due - kMaxCatchUp;
    for (; _next <= due; ++_next)
        Feed(img, static_cast<double>(_next) / _fps);
}

void Recorder::Feed(const QImage& rgba, double t)
{
    FramebufferDescriptor fb;
    fb.width = static_cast<uint16_t>(rgba.width());
    fb.height = static_cast<uint16_t>(rgba.height());
    fb.memoryBuffer = const_cast<uint8_t*>(rgba.constBits());
    fb.memoryBufferSize = static_cast<size_t>(rgba.sizeInBytes());
    _encoder->OnVideoFrame(fb, t);
    ++_frames;
}
