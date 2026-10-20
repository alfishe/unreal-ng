// Records what the deck widget shows to a video with unreal-ng's encoders (core/recording):
// .mp4/.mov -> VideoToolbox (macOS) / NVENC (Windows) or ffmpeg, .mkv/.webm/.avi -> ffmpeg, .gif -> GIF.
#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <functional>
#include <memory>

class EncoderBase;
class QTimer;

class Recorder : public QObject
{
    Q_OBJECT
public:
    // `source` renders the current picture (any size; it is scaled to the size of the first frame).
    explicit Recorder(std::function<QImage()> source, QObject* parent = nullptr);
    ~Recorder() override;

    bool Start(const QString& path, int fps, QString* error);
    QString Stop();  // finalizes the file; returns a summary ("file: N frames, S s, backend")
    [[nodiscard]] bool Active() const
    {
        return _encoder != nullptr;
    }
    [[nodiscard]] QString Path() const
    {
        return _path;
    }
    [[nodiscard]] double Seconds() const
    {
        return Active() ? static_cast<double>(_clock.elapsed()) / 1000.0 : 0.0;
    }
    [[nodiscard]] quint64 Frames() const
    {
        return _frames;
    }

signals:
    void StateChanged();

private:
    void Tick();
    void Feed(const QImage& rgba, double t);

    std::function<QImage()> _source;
    std::unique_ptr<EncoderBase> _encoder;
    QTimer* _timer;
    QElapsedTimer _clock;
    QString _path, _backend;
    QSize _size;
    int _fps = 30;
    quint64 _frames = 0, _next = 0;
};
