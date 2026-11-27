#pragma once

/// @file windowrecorder.h
/// @brief Records the main window's picture as it draws it (CRT effects and all), optionally with the on-screen
/// keyboard under it and the machine's sound, to a full-color file through ffmpeg (ProRes 4444 or H.264 4:4:4: a
/// CRT mask a pixel wide does not survive 4:2:0). The clock is the machine's, not the host's: one video frame per
/// emulated frame at the machine's frame rate (a frame the window did not draw repeats the last picture), and each
/// frame's own sound samples (SoundManager's frame audio tap), so picture and sound stay together whatever the
/// host's load. The window shows the frame the present delay ago (A/V sync, ZX DLSS look-ahead): the sound and the
/// keyboard are delayed as much. A machine run frame by frame (paused, record/frames) shows every frame.

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

#include <condition_variable>
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

/// The request the panel and the full Video Recording dialog both make for a window recording, so the same options
/// give the same file: the format's own codec; the GPU's encoder when View > GPU acceleration is on or the engine is
/// Native (the Mac's own writer for MP4 / MOV; MKV and the FFmpeg engine take the GPU encoder ffmpeg offers, found
/// by probing, software only when there is none); a fixed profile frame draws the CRT at
/// the frame's height (sharp, not scaled up afterwards) and centers it in it
WindowRecordingRequest MakeWindowRecordingRequest(const QString& container, const QString& codec, bool audio,
                                                  bool gpuAcceleration, const QString& profile,
                                                  const QString& path, int engine = 0);

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
    /// The GPU encoder ffmpeg offers is found by trial encodes (ffmpeg started several times: seconds on Windows) and
    /// remembered. A request that wants one: true once its answer is known (start then runs no trial), always true
    /// for one that needs no trial
    static bool HardwareEncoderKnown(const WindowRecordingRequest& request);
    /// Run those trials here, on the caller's thread (never the GUI's: the window would stop for seconds), so that
    /// start finds the answer; nothing when it is known or not needed. Thread-safe
    static void PrepareHardwareEncoder(const WindowRecordingRequest& request);
    /// True while pictures are being taken (false once stopped, even while the file is still being finished)
    bool recording() const;
    /// Never blocks: a stopped recording shows `finalizing` until its file is complete (then `error` if it failed)
    void status(WindowRecordingStatus& out);

signals:
    /// A recording started, or stopped taking pictures (stopped here or elsewhere, or the writer failed): every
    /// recording UI shows the same state whichever one started it
    void stateChanged();

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
        /// The emulated frames since the start, counted here: the machine's frame counter goes back to 0 on a reset
        /// and moves on a snapshot load or a TTD seek (a difference of it wrapped to ~1.8e19 frames, times ffmpeg
        /// could not mux: "Not yet implemented")
        uint64_t frames = 0;
        std::vector<FrameSound> pending;
        /// Video sync: the frames the file has a picture for, or dropped before its start (`frames` counted the
        /// same way). Once the first picture
        /// is in, the machine waits in the tap while it is maxAhead frames ahead of them: the window's pictures or
        /// the writer behind slow the machine down to their pace instead of a picture standing for several frames
        uint64_t pictured = 0;
        uint64_t maxAhead = 4;  ///< the window's present delay and a slack (onRefreshed sets it)
        bool synced = false;
        bool closing = false;  ///< the recording ends: no more waits
        std::condition_variable advanced;
    };

    bool begin(DeviceScreenWrapper* screen, const std::shared_ptr<Emulator>& emulator,
               const WindowRecordingRequest& request, WindowRecordingStatus& out, std::string& error);
    void restoreKeyboard();
    /// The frames the window's picture trails the emulated one by (the effective present delay): where a picture
    /// with no frame number (the software screen) goes
    size_t presentDelay() const;
    void onRefreshed();
    void fill(WindowRecordingStatus& out);
    void removeTap();
    /// Take the finished background writer's result, if it is done
    void reap();
    /// Stop taking pictures and hand the file to the writer to finish (no waiting)
    void closeWriter();

    QPointer<DeviceScreenWrapper> _screen;
    QPointer<OsdKeyboardWindow> _keyboard;
    std::weak_ptr<Emulator> _emulator;
    struct Writer;  ///< the thread that composes frames, feeds ffmpeg, writes the sound and muxes: never the GUI's
    std::shared_ptr<Writer> _writer;
    std::string _lastError;
    QString _ffmpeg;
    std::string _transfer;  ///< what ffmpeg gets: "picture", "nv12", "yuv444", or "native" (the Mac's writer)
    WindowRecordingRequest _request;
    std::string _keyboardId;
    std::string _restoreKeyboard;  ///< the window's own picture, shown again after another machine's was recorded
    QSize _size;            ///< the file's frame
    QRect _screenRect;      ///< where the picture goes in it
    QRect _keyboardRect;    ///< where the keyboard goes (its width is the width it is drawn at)
    bool _audio = false;
    uint64_t _frames = 0;
    double _fps = 0;
    uint32_t _sampleRate = 0;
    uint64_t _audioSamples = 0;
    bool _retryQueued = false;
    std::shared_ptr<Sink> _sink;
    uint64_t _tapId = 0;
    bool _capture = false;  ///< the GPU window captures each new frame it presents (a frame later than a grab)
    int _captureMisses = 0;  ///< refreshes in a row without a captured picture
    QString _videoPath;     ///< where ffmpeg writes the picture (the file itself without sound)
    QString _soundPath;     ///< the raw sound beside it (s16le stereo), muxed in at stop
    /// A picture whose frame is at most this many before the oldest waiting is of a frame in the file already
    static constexpr uint64_t kStalePictureFrames = 8;
    /// Frames waiting for the file's first picture, at most (the older go with their sound)
    static constexpr size_t kMaxUnpictured = 50;
    /// Video sync: the frames the machine may run past the present delay before it waits for the pictures (a
    /// capture is taken a refresh after its present; a refresh may come late)
    static constexpr uint64_t kSyncSlackFrames = 4;
    std::deque<FrameSound> _unpictured;  ///< the frames run and not in the file yet, in order, with their sound
    uint64_t _covered = 0;               ///< the frames run that are in the file or dropped before its start
    std::deque<std::pair<uint64_t, QImage>> _keyboardDelay;  ///< the keyboard by the newest frame run then
};
