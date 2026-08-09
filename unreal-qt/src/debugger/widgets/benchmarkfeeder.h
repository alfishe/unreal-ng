#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

/// @brief Autonomous synthetic video generator for encoder benchmarking.
///
/// Produces deliberately complex RGBA frames that stress every part of a
/// video encoder pipeline:
/// - **Sliding checkerboard**: high-frequency spatial detail that scrolls.
///   The grid offset shifts every frame so temporal prediction gets zero
///   reuse from the previous frame.
/// - **Pong elements**: two saturated-color paddles oscillating vertically
///   at different rates, plus a bouncing ball. Large moving solid regions
///   exercise motion estimation and macroblock coding.
/// - **Particle field**: sparkles spawning from the ball position with
///   random velocities, lifetimes and colors. Scattered, unpredictable
///   detail that defeats run-length and inter-frame compression.
/// - **Moving gradient overlay**: a soft radial light that sweeps across
///   the frame, testing gradient/smooth-area handling (DCT ringing).
///
/// The class is fully self-contained — no emulator, no Qt, no external
/// dependencies beyond the C++ standard library. This makes it trivial to
/// extract into a standalone benchmark tool later.
///
/// Usage:
///   BenchmarkFeeder feeder(352, 288, 50.0f);
///   feeder.renderFrame(buffer, frameIndex);
class BenchmarkFeeder
{
public:
    /// @param width  Framebuffer width in pixels
    /// @param height Framebuffer height in pixels
    /// @param fps    Target frame rate (used for deriving element speeds)
    BenchmarkFeeder(int width, int height, float fps);

    /// Render the frame at the given index into the RGBA buffer.
    /// @param buffer     Destination buffer (width * height * 4 bytes, RGBA8888)
    /// @param frameIndex Zero-based frame counter
    void renderFrame(uint8_t* buffer, uint64_t frameIndex);

    int width() const { return _width; }
    int height() const { return _height; }

private:
    // ── Layer renderers (composited in order) ──────────────────────────

    void drawCheckerboard(uint8_t* buffer, double time);
    void drawPongElements(uint8_t* buffer, double time);
    void drawParticles(uint8_t* buffer, double time);
    void drawGradientOverlay(uint8_t* buffer, double time);

    // ── Drawing primitives ─────────────────────────────────────────────

    /// Solid filled rectangle (clipped to framebuffer bounds)
    void fillRect(uint8_t* buffer, int x, int y, int w, int h,
                  uint8_t r, uint8_t g, uint8_t b);

    /// Filled circle (clipped)
    void fillCircle(uint8_t* buffer, float cx, float cy, float radius,
                    uint8_t r, uint8_t g, uint8_t b);

    /// Alpha-blend a pixel (0 = transparent, 255 = opaque)
    void blendPixel(uint8_t* buffer, int x, int y,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    /// Additive light — clamps to 255
    void addLight(uint8_t* buffer, int x, int y,
                  uint8_t r, uint8_t g, uint8_t b, int intensity);

    // ── Particle system ────────────────────────────────────────────────

    struct Particle
    {
        float x, y;
        float vx, vy;
        uint8_t r, g, b;
        float life;       // 0..1, decremented each frame
    };

    void spawnParticles(float x, float y, int count, double time);
    void updateParticles(double dt);

    // ── State ──────────────────────────────────────────────────────────

    int _width;
    int _height;
    float _fps;

    // Pong ball position/velocity (pixels, pixels/sec)
    float _ballX, _ballY;
    float _ballVX, _ballVY;
    float _ballPrevX, _ballPrevY;

    // Particle pool (reused across frames, no per-frame allocation)
    std::vector<Particle> _particles;

    // ZX Spectrum bright palette for checkerboard cells
    static const uint32_t ZX_PALETTE[16];
};
