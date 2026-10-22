#pragma once

/// @file windowrecorder.h
/// @brief Records the main window's picture as it draws it (CRT effects and all), optionally with the on-screen
/// keyboard under it and the machine's sound, to a full-colour file through ffmpeg (ProRes 4444 or H.264 4:4:4: a
/// CRT mask a pixel wide does not survive 4:2:0). The clock is the machine's, not the host's: one video frame per
/// emulated frame at the machine's frame rate (a frame the window did not draw repeats the last picture), and each
/// frame's own sound samples (SoundManager's frame audio tap), so picture and sound stay together whatever the
/// host's load. The window shows the frame the present delay ago (A/V sync, ZX DLSS look-ahead): the sound and the
/// keyboard are delayed as much. A machine run frame by frame (paused, record/frames) shows every frame.

#include <QFile>
#include <QImage>
#include <QObject>
#include <QPointer>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "emulator/video/windowview.h"

class DeviceScreenWrapper;
class Emulator;
class OsdKeyboardWindow;
class QProcess;

class WindowRecorder : public QObject
{
    Q_OBJECT

public:
    explicit WindowRecorder(OsdKeyboardWindow* keyboard, QObject* parent = nullptr);
    ~WindowRecorder() override;

    /// Record @p screen (the window's current one: switching the GPU re-creates it) showing @p emulator
    bool start(DeviceScreenWrapper* screen, const std::shared_ptr<Emulator>& emulator,
               const WindowRecordingRequest& request, WindowRecordingStatus& out, std::string& error);
    bool stop(WindowRecordingStatus& out, std::string& error);
    bool recording() const { return _process != nullptr; }
    void status(WindowRecordingStatus& out) const { fill(out); }

private:
    /// What the emulation thread hands over at each frame end: the frame's number and its samples
    struct FrameSound
    {
        uint64_t frame = 0;
        std::vector<int16_t> samples;  ///< stereo pairs; empty without a sound track
    };
    struct Sink
    {
        std::mutex mutex;
        bool keepSamples = false;
        bool started = false;
        uint64_t firstFrame = 0;
        uint64_t lastFrame = 0;
        std::vector<FrameSound> pending;
    };

    bool begin(DeviceScreenWrapper* screen, const std::shared_ptr<Emulator>& emulator,
               const WindowRecordingRequest& request, WindowRecordingStatus& out, std::string& error);
    void restoreKeyboard();
    /// The frames the window's picture trails the emulated one by (the effective present delay)
    size_t presentDelay() const;
    void onRefreshed();
    /// The window's picture and the keyboard under it, in the file's size
    bool compose(QImage& frame, std::string& error);
    void fill(WindowRecordingStatus& out) const;
    void removeTap();
    /// The video file and the sound into the requested file (the video copied, the sound encoded for the container)
    bool mux(std::string& error);

    QPointer<DeviceScreenWrapper> _screen;
    QPointer<OsdKeyboardWindow> _keyboard;
    std::weak_ptr<Emulator> _emulator;
    QProcess* _process = nullptr;
    QString _ffmpeg;
    WindowRecordingRequest _request;
    std::string _keyboardId;
    std::string _restoreKeyboard;  ///< the window's own picture, shown again after another machine's was recorded
    QSize _size;            ///< the file's frame
    QRect _screenRect;      ///< where the picture goes in it
    QRect _keyboardRect;    ///< where the keyboard goes (its width is the width it is drawn at)
    uint64_t _frames = 0;
    double _fps = 0;
    uint32_t _sampleRate = 0;
    uint64_t _audioSamples = 0;
    std::shared_ptr<Sink> _sink;
    uint64_t _tapId = 0;
    QString _videoPath;     ///< where ffmpeg writes the picture (the file itself without sound)
    QString _soundPath;     ///< the raw sound beside it (s16le stereo), muxed in at stop
    QFile _sound;
    std::deque<std::vector<int16_t>> _soundDelay;           ///< the frames' sound waiting for their picture
    std::deque<std::pair<uint64_t, QImage>> _keyboardDelay;  ///< the keyboard by the file's frame count, likewise
};
