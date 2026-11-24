#pragma once

#include "displayrate.h"
#include "screen.h"
#include "smoothmotion.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace smoothmotion
{

/// Smooth motion for one stream of frames shown among many (a video wall's tile): the frames pulled in order with
/// their times, the picture for a refresh, the step down when the pictures take too long. No threads of its own:
/// the wall spreads its tiles over a pool, each tile touched by one thread at a time.
///
/// Made only while smooth motion is on: a wall with it off holds no Tile and pays nothing.
class Tile
{
public:
    /// Screen::CopyNextFrame of the tile's machine: the frame after `afterSerial` into `dst` (RGBA, `bytes` long).
    /// false: none yet
    using NextFrame = std::function<bool(uint64_t afterSerial, uint8_t* dst, size_t bytes, PresentFrameInfo* info)>;

    enum class Step : uint8_t
    {
        Nothing,    ///< no frame yet, or the size is not a picture
        Unchanged,  ///< the picture shown is still the one
        Updated,    ///< Picture() is another one: show it
    };

    /// Blend, Interpolate, InterpolateLite (Auto runs as Interpolate). Off is not a Tile: the wall drops it
    void SetMode(Mode mode);
    Mode GetMode() const { return _engine.GetMode(); }

    /// The frames that came since the last call, then the picture for a refresh reaching the screen at `tNs`
    /// (steady clock), a display refresh `refreshNs` long. `width` x `height`: the machine's frame now
    Step Advance(const NextFrame& next, int width, int height, int64_t tNs, int64_t refreshNs);

    /// One level down, whatever its own pictures took (the wall's refresh does not fit its tiles): the full
    /// interpolation to the cheap one, that to the blend, the blend to the frames as they are (no engine work: what
    /// off draws). false: at the frames already
    bool StepDown();

    const uint32_t* Picture() const { return _picture.data(); }
    int Width() const { return _width; }
    int Height() const { return _height; }
    /// Of the frame the picture is of
    const PictureGeometry& Geometry() const { return _geometry; }
    /// The picture is between two frames, or frames came at the last call: the wall should draw the next refresh too
    bool Moving() const { return _moving; }

    struct Stats
    {
        uint64_t frames = 0;    ///< pulled
        uint64_t pictures = 0;  ///< drawn (Updated)
        uint64_t between = 0;   ///< of them between two frames (blended or interpolated)
        uint64_t behind = 0;    ///< refreshes the plan was behind the frame due
        bool steppedDown = false;  ///< the interpolation took too long: the cheap one instead
        bool tooSlow = false;      ///< ... and that one too: blended
        bool framesOnly = false;   ///< stepped down past the blend: every frame as it is
        int64_t lastNs = 0;        ///< what the last Advance took
    };
    const Stats& GetStats() const { return _stats; }
    const SmoothMotion& Engine() const { return _engine; }

private:
    Step AdvanceTimed(const NextFrame& next, int width, int height, int64_t tNs, int64_t refreshNs);

    SmoothMotion _engine;
    SmoothGuard _guard;
    std::vector<uint32_t> _next, _picture;
    int _width = 0, _height = 0;
    SmoothMotion::Result _shown;
    PictureGeometry _geometry;
    /// The geometry of the frames the engine may still plan with, by serial
    struct Pulled
    {
        uint64_t serial = 0;
        PictureGeometry geometry;
    };
    Pulled _pulled[SmoothMotion::kFrames];
    size_t _nextPulled = 0;
    bool _atOnce = false;  ///< low latency: frames go to the screen at once, no picture between them
    bool _moving = false;
    uint64_t _lastSerial = 0;  ///< the newest frame pulled
    Stats _stats;
};

}  // namespace smoothmotion
