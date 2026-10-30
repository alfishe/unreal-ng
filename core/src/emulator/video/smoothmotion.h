#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// Smooth motion on a fixed display (frame presenter design, docs/inprogress/2026-10-26-frame-presenter, sections 4.3-4.5
/// and 6.5): the picture a refresh between two emulated frames shows.
///
/// This is the CPU reference. Integer arithmetic only (the weight in 1/256 steps, the warp in whole pixels), so the
/// output is the same bits on every compiler; the presenters' GPU passes implement the same steps and are tested
/// against it. No thread, no OS calls.
namespace smoothmotion
{

/// `[VIDEO] SmoothMotion`
enum class Mode : uint8_t
{
    Off,          // the frame due (the beat pattern on a fixed display)
    Blend,        // (1 - w) A + w B between the two frames around the refresh's time
    Interpolate,  // each block moved to its position at the refresh's time, the blend where no motion is found
};

/// A game profile's `video.smooth` (mapping-and-profiles.md section 7.7); only with Mode::Interpolate
enum class GameMode : uint8_t
{
    Off,      // the frame due, even with Mode::Interpolate
    Cadence,  // the step to the game's next picture, once that picture is in the lookahead
    Carry,    // the motion of the game's last two pictures carried on through its period
};

const char* ModeName(Mode mode);
/// "off" | "blend" | "interpolate", any case; false for anything else
bool ParseMode(const char* text, Mode& mode);
const char* GameModeName(GameMode mode);
bool ParseGameMode(const char* text, GameMode& mode);

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool Empty() const { return width <= 0 || height <= 0; }
    bool operator==(const Rect& o) const = default;
};

/// The game's picture cadence, from its profile's `video:` section
struct GameVideo
{
    int picturePeriod = 1;  ///< frames per picture, 1..16
    GameMode mode = GameMode::Off;
    Rect window;                ///< the play area in framebuffer pixels; empty = the whole frame
    std::vector<Rect> exclude;  ///< regions inside the window left alone (score, status row)
    bool operator==(const GameVideo& o) const = default;
};

/// An RGBA8 picture, rows `width` pixels apart
struct Picture
{
    int width = 0;
    int height = 0;
    const uint32_t* pixels = nullptr;
    uint32_t At(int x, int y) const;  ///< clamped to the edges
};

/// The weight of one: 0 = A, kOne = B, up to 2 * kOne extrapolates past B along the motion from A to B
constexpr int kOne = 256;

/// Per channel (a (256 - w) + b w + 128) >> 8, w in 0..kOne
uint32_t Mix(uint32_t a, uint32_t b, int w);
/// Where `t` lies between `ta` and `tb`, rounded to 1/256 and clamped to 0..kOne
int WeightAt(int64_t t, int64_t ta, int64_t tb);
/// The whole picture mixed: (1 - w) A + w B
void Blend(const Picture& a, const Picture& b, int w, uint32_t* out);

/// One motion vector per 8x8 block of a pair of pictures A, B, and the in-between pictures from it.
///
/// Estimate, every pass per block on its own (a GPU runs every block at once):
/// 1. the vectors within the range in the order of their length (zero first; the same length row by row), each
///    scored by the pixels of the block where A warped back and B warped forward to the middle disagree; the first
///    with the fewest, at most kAccept, wins, none: unmatched;
/// 2. kRefinePasses times, from the previous pass alone: zero, the kGlobalVectors most common vectors (most first)
///    and the eight neighbours' (row by row); the fewest mismatches within kAccept, the first of equals; none of
///    them: the block's own from pass 1. A flat or periodic area agrees under many vectors, and the nearest one the
///    search finds first is not the motion: the common motion wins there.
///
/// Compose, per pixel at the weight w: of the candidates (the block's vector, zero, the eight neighbour blocks' and the
/// kComposeCommon most common of the field: a sprite moves its edge blocks by no single vector of theirs), those
/// on which the warped A and B agree at the pixel; the one that agrees on most of the 3x3 around it wins, the first
/// of equals. None: the blend, or B past it (w > kOne). Extrapolating, a moving vector only draws pixels whose
/// sources changed from A to B, so a static border is not pulled into a scrolling window.
///
/// Known limit: a background periodic in a vector the search reaches (8x8 tiles, a checkerboard) agrees with itself
/// under that vector; a block of mostly background beside a moving sprite may take it and shift the tiles near the
/// sprite. Preferring zero there cost more on real content (POC 038 sequences: 7th Reality MAE 9.6 -> 22).
class MotionField
{
public:
    static constexpr int kBlock = 8;
    static constexpr int kAccept = 4;       ///< mismatching pixels of 64 still taken as a match
    static constexpr int kRangeFrames = 8;  ///< consecutive frames
    static constexpr int kRangePictures = 16;  ///< a game's pictures, several frames apart
    static constexpr size_t kGlobalVectors = 4;  ///< the most common vectors of a pass every block also tries
    static constexpr int kRefinePasses = 2;     ///< after the search
    static constexpr size_t kComposeCommon = 2;  ///< the most common vectors of the field Compose also tries

    struct Vector
    {
        int x = 0;
        int y = 0;
        bool operator==(const Vector& o) const = default;
    };
    struct Block
    {
        Vector v;
        bool matched = false;
    };
    struct Stats
    {
        int blocks = 0;
        int still = 0;      ///< the zero vector matched
        int moved = 0;      ///< another vector matched
        int unmatched = 0;  ///< nothing within kAccept: the blend there
    };

    void Estimate(const Picture& a, const Picture& b, int range);
    /// `w` 0..2 * kOne. `mask` (width * height, optional): only the pixels whose byte is non-zero are written
    void Compose(const Picture& a, const Picture& b, int w, uint32_t* out, const uint8_t* mask = nullptr) const;

    int BlocksWide() const { return _blocksWide; }
    int BlocksHigh() const { return _blocksHigh; }
    const Block& At(int i, int j) const { return _blocks[static_cast<size_t>(j) * _blocksWide + i]; }
    const Stats& GetStats() const { return _stats; }
    const std::vector<Vector>& Common() const { return _common; }

    /// The vectors within `range` in the order Estimate tries them
    static const std::vector<Vector>& SearchOrder(int range);
    /// The `limit` most common moving vectors of matched blocks, most first, equals in the search order: the votes of
    /// a refinement pass and the field's Common()
    static std::vector<Vector> CommonVectors(const std::vector<Block>& blocks, size_t limit, int range);
    /// A field estimated elsewhere (the GPU passes, the same steps): its blocks row by row, Common() and the stats
    /// from them
    void Assign(int blocksWide, int blocksHigh, std::vector<Block> blocks, int range);

private:
    int _blocksWide = 0;
    int _blocksHigh = 0;
    std::vector<Block> _blocks;
    std::vector<Vector> _common;  ///< most first
    Stats _stats;
};

/// Where the heavy steps run (`[VIDEO] EffectsDevice`): the CPU reference without one; a GPU device implements the
/// same steps to the bit. A step it cannot do now (no context, a failed pass) returns false and runs on the CPU
class Device
{
public:
    virtual ~Device() = default;
    virtual bool Estimate(const Picture& a, const Picture& b, int range, MotionField& field) = 0;
    virtual bool Blend(const Picture& a, const Picture& b, int w, uint32_t* out) = 0;
    /// MotionField::Compose: `out` holds what stays outside `mask`
    virtual bool Compose(const MotionField& field, const Picture& a, const Picture& b, int w, uint32_t* out,
                         const uint8_t* mask) = 0;
};

/// The presenter's side: every timed frame once, in order (Screen::CopyNextFrame), and per refresh the picture for
/// its time. Keeps copies of the recent frames and of the game's last pictures; caches the motion fields per pair.
///
/// Flicker is not motion: pixels that alternate frame by frame (gigascreen, a 25 Hz blink) belong to the temporal
/// effects (ZX DLSS, the blending). An 8x8 block where at least 3/4 of the pixels changing from A to B went the same
/// way two frames before (A B A B) is flicker: Blend and Interpolate show it as the frame due, and a pair that differs
/// in nothing else as the frame; blended or moved, it would beat against the refresh even harder. Per block, over
/// four frames: a thin stroke scrolling by turns a lone pixel back now and then, not a block for two frames.
class SmoothMotion
{
public:
    /// The game mode's lookahead: the step to the next picture starts at most this many frames before it
    static constexpr int kLookaheadFrames = 2;
    static constexpr size_t kFrames = 8;    ///< recent frames kept
    static constexpr size_t kPictures = 3;  ///< the game's pictures kept
    /// A block flickers when this share of its changing pixels alternates
    static constexpr int kFlickerNumerator = 3;
    static constexpr int kFlickerDenominator = 4;

    /// What a refresh shows
    enum class Kind : uint8_t
    {
        None,         // no frame yet
        Frame,        // the frame due, as it is
        Blend,
        Interpolate,
        Cadence,
        Carry,
    };

    struct Result
    {
        Kind kind = Kind::None;
        uint64_t serial = 0;  ///< the frame due at the time
        /// What the picture is made of: the same key, the same pixels (the caller skips the upload)
        uint64_t from = 0;
        uint64_t to = 0;
        int weight = 0;
        bool operator==(const Result& o) const = default;
    };

    struct Stats
    {
        uint64_t frames = 0;    ///< pushed
        uint64_t pictures = 0;  ///< game pictures found
        uint64_t renders = 0;
        uint64_t blended = 0;
        uint64_t interpolated = 0;  ///< Interpolate, Cadence or Carry
        uint64_t estimates = 0;     ///< motion fields estimated
        uint64_t onDevice = 0;      ///< steps the Device ran (the rest on the CPU)
        uint64_t heldFlicker = 0;   ///< Blend / Interpolate renders that kept flickering pixels as the frame due
    };

    void SetMode(Mode mode);
    Mode GetMode() const { return _mode; }
    /// nullptr: the CPU. Not owned; the caller keeps its context current while it renders
    void SetDevice(Device* device) { _device = device; }
    Device* GetDevice() const { return _device; }
    /// nullptr: no game profile
    void SetGame(const GameVideo* game);
    bool HasGame() const { return _hasGame; }

    /// The frames in order of their serials, each once. A size change starts over
    void Push(uint64_t serial, int64_t targetNs, const uint32_t* pixels, int width, int height);
    /// Forget every frame (a detach, a seek)
    void Reset();
    /// The serial of the newest frame pushed (0: none)
    uint64_t LastSerial() const { return _frames.empty() ? 0 : _frames.back().serial; }

    /// What a refresh reaching the screen at `tNs` shows; the same result, the same picture (no need to draw it again)
    Result Plan(int64_t tNs) const;
    /// A plan's picture into `out` (width * height pixels of the frames). false: none (Kind::None, or its frames gone)
    bool Draw(const Result& result, uint32_t* out);
    /// Plan and Draw
    Result Render(int64_t tNs, uint32_t* out);

    int Width() const { return _width; }
    int Height() const { return _height; }
    const Stats& GetStats() const { return _stats; }

private:
    struct Stored
    {
        uint64_t serial = 0;
        int64_t targetNs = 0;
        std::vector<uint32_t> pixels;
        size_t changed = 0;  ///< pixels that differ from the frame before it
        /// The pixels changing to the next frame inside flickering blocks (known once the next frame is in); `smooth`
        /// (per pixel, non-zero: not in such a block) is empty while none flickers
        size_t flicker = 0;
        std::vector<uint8_t> smooth;
        Picture View(int width, int height) const { return {width, height, pixels.data()}; }
    };
    struct CachedField
    {
        uint64_t from = 0;
        uint64_t to = 0;
        int range = 0;
        MotionField field;
    };

    const MotionField& FieldFor(const Stored& a, const Stored& b, int range);
    void Compose(const MotionField& field, const Picture& a, const Picture& b, int w, uint32_t* out,
                 const uint8_t* mask);
    bool ChangedInWindow(const uint32_t* a, const uint32_t* b) const;
    /// The flickering blocks of `a` on its way to `b` (the frames y, z, a before it, in order)
    void FindFlicker(const Stored& y, const Stored& z, Stored& a, const uint32_t* b);
    void BuildMask();
    int64_t FramePeriodNs() const;

    Mode _mode = Mode::Off;
    Device* _device = nullptr;
    bool _hasGame = false;
    GameVideo _game;
    int _width = 0;
    int _height = 0;
    std::vector<Stored> _frames;    ///< oldest first
    std::vector<Stored> _pictures;  ///< oldest first
    std::vector<uint8_t> _mask;     ///< the game's window minus its exclusions (empty: no game)
    std::vector<int64_t> _periods;  ///< the last target steps
    CachedField _fields[2];
    size_t _nextField = 0;
    Stats _stats;
};

}  // namespace smoothmotion
