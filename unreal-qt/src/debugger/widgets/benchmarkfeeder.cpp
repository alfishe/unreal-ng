#include "benchmarkfeeder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ── Deterministic PRNG (no <random> needed, reproducible across runs) ──

namespace
{
/// Simple xorshift32 — deterministic so benchmark runs are reproducible
uint32_t s_rngState = 0x12345678;

inline uint32_t rngNext()
{
    uint32_t x = s_rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rngState = x;
    return x;
}

inline float rngRange(float lo, float hi)
{
    return lo + (hi - lo) * (rngNext() / 4294967296.0f);
}

inline void rngSeed(uint32_t seed)
{
    s_rngState = seed ? seed : 0x12345678;
}
}  // namespace

// ── Palette ────────────────────────────────────────────────────────────

const uint32_t BenchmarkFeeder::ZX_PALETTE[16] = {
    0xFF000000, 0xFFC00000, 0xFF0000C0, 0xFFC000C0,
    0xFF00C000, 0xFF00C0C0, 0xFFC0C000, 0xFFC0C0C0,
    0xFF000000, 0xFF0000FF, 0xFFFF0000, 0xFFFF00FF,
    0xFF00FF00, 0xFF00FFFF, 0xFFFFFF00, 0xFFFFFFFF,
};

// ── Constructor ────────────────────────────────────────────────────────

BenchmarkFeeder::BenchmarkFeeder(int width, int height, float fps)
    : _width(width), _height(height), _fps(fps)
{
    // Initialize ball in the center, moving diagonally
    _ballX = width * 0.5f;
    _ballY = height * 0.5f;
    float ballSpeed = std::min(width, height) * 0.8f;  // pixels/sec
    _ballVX = ballSpeed * 0.7f;
    _ballVY = ballSpeed * 0.5f;
    _ballPrevX = _ballX;
    _ballPrevY = _ballY;

    _particles.reserve(256);
    rngSeed(42);  // Reproducible particle spawns
}

// ── Frame entry point ──────────────────────────────────────────────────

void BenchmarkFeeder::renderFrame(uint8_t* buffer, uint64_t frameIndex)
{
    double time = static_cast<double>(frameIndex) / _fps;
    double dt = 1.0 / _fps;

    // Update simulation state before drawing
    updateParticles(dt);

    // Composite layers back-to-front
    drawCheckerboard(buffer, time);
    drawPongElements(buffer, time);
    drawParticles(buffer, time);
    drawGradientOverlay(buffer, time);
}

// ── Layer 1: Sliding checkerboard ──────────────────────────────────────

void BenchmarkFeeder::drawCheckerboard(uint8_t* buffer, double time)
{
    // Grid scrolls diagonally — every frame the offset changes, so
    // temporal prediction cannot reuse the previous frame at all.
    const int cellSize = 8;  // Match ZX attribute grid (8×8 pixels)
    float scrollX = std::fmod(time * 40.0f, static_cast<float>(cellSize * 2));
    float scrollY = std::fmod(time * 30.0f, static_cast<float>(cellSize * 2));

    for (int y = 0; y < _height; y++)
    {
        for (int x = 0; x < _width; x++)
        {
            int gx = static_cast<int>((x + scrollX) / cellSize);
            int gy = static_cast<int>((y + scrollY) / cellSize);
            int cellIdx = ((gx + gy) & 0x0F);

            uint32_t color = ZX_PALETTE[cellIdx];
            memcpy(&buffer[(y * _width + x) * 4], &color, 4);
        }
    }
}

// ── Layer 2: Pong paddles + bouncing ball ──────────────────────────────

void BenchmarkFeeder::drawPongElements(uint8_t* buffer, double time)
{
    const int paddleW = 6;
    const int paddleH = _height / 5;

    // Left paddle: oscillates sinusoidally (red/cyan)
    int leftY = static_cast<int>((_height - paddleH) * 0.5f *
                                  (1.0f + std::sin(time * 1.7f)));
    leftY = std::max(0, std::min(leftY, _height - paddleH));
    fillRect(buffer, 12, leftY, paddleW, paddleH, 0xFF, 0x00, 0x00);

    // Right paddle: different frequency, offset phase (green/magenta)
    int rightY = static_cast<int>((_height - paddleH) * 0.5f *
                                   (1.0f + std::sin(time * 2.3f + 1.0f)));
    rightY = std::max(0, std::min(rightY, _height - paddleH));
    fillRect(buffer, _width - 12 - paddleW, rightY, paddleW, paddleH, 0x00, 0xFF, 0x00);

    // Update ball physics
    _ballPrevX = _ballX;
    _ballPrevY = _ballY;

    float dt = 1.0f / _fps;
    _ballX += _ballVX * dt;
    _ballY += _ballVY * dt;

    // Bounce off top/bottom walls
    if (_ballY < 4.0f)
    {
        _ballY = 4.0f;
        _ballVY = std::abs(_ballVY);
        spawnParticles(_ballX, _ballY, 8, time);
    }
    if (_ballY > _height - 4.0f)
    {
        _ballY = _height - 4.0f;
        _ballVY = -std::abs(_ballVY);
        spawnParticles(_ballX, _ballY, 8, time);
    }

    // Bounce off paddles
    float paddleLeftEdge = 12 + paddleW;
    float paddleRightEdge = _width - 12 - paddleW;

    if (_ballX < paddleLeftEdge + 4 && _ballVX < 0)
    {
        // Check paddle Y range
        if (_ballY >= leftY && _ballY <= leftY + paddleH)
        {
            _ballVX = std::abs(_ballVX);
            spawnParticles(_ballX, _ballY, 12, time);
        }
    }
    if (_ballX > paddleRightEdge - 4 && _ballVX > 0)
    {
        if (_ballY >= rightY && _ballY <= rightY + paddleH)
        {
            _ballVX = -std::abs(_ballVX);
            spawnParticles(_ballX, _ballY, 12, time);
        }
    }

    // Reset if ball escapes
    if (_ballX < 0 || _ballX > _width)
    {
        _ballX = _width * 0.5f;
        _ballY = _height * 0.5f;
        _ballVX = (_ballVX > 0 ? -1.0f : 1.0f) * std::min(_width, _height) * 0.7f;
        _ballVY = (_ballVY > 0 ? -1.0f : 1.0f) * std::min(_width, _height) * 0.5f;
    }

    // Ball trail (3 ghost circles fading behind)
    for (int i = 1; i <= 3; i++)
    {
        float t = i / 3.0f;
        float tx = _ballX * (1 - t) + _ballPrevX * t;
        float ty = _ballY * (1 - t) + _ballPrevY * t;
        uint8_t a = static_cast<uint8_t>(120 * (1 - t));
        fillCircle(buffer, tx, ty, 3.0f, 0xFF, 0xFF, 0x00);
        // Overlay trail with alpha (simple approach: just darken)
    }

    // Ball — bright yellow, changes color based on speed
    float speed = std::sqrt(_ballVX * _ballVX + _ballVY * _ballVY);
    uint8_t ballR = 0xFF;
    uint8_t ballG = static_cast<uint8_t>(std::min(255.0f, 255.0f - speed * 0.3f));
    uint8_t ballB = 0x00;
    fillCircle(buffer, _ballX, _ballY, 4.0f, ballR, ballG, ballB);
}

// ── Layer 3: Particles ─────────────────────────────────────────────────

void BenchmarkFeeder::spawnParticles(float x, float y, int count, double /*time*/)
{
    for (int i = 0; i < count; i++)
    {
        if (_particles.size() >= 500)
            break;

        Particle p;
        p.x = x;
        p.y = y;
        float angle = rngRange(0.0f, 6.283185f);
        float speed = rngRange(20.0f, 80.0f);
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.life = rngRange(0.4f, 1.0f);

        // Random saturated color
        int hueIdx = rngNext() % 6;
        static const uint8_t hues[6][3] = {
            {0xFF, 0x00, 0x00},  // Red
            {0x00, 0xFF, 0x00},  // Green
            {0x00, 0x00, 0xFF},  // Blue
            {0xFF, 0xFF, 0x00},  // Yellow
            {0xFF, 0x00, 0xFF},  // Magenta
            {0x00, 0xFF, 0xFF},  // Cyan
        };
        p.r = hues[hueIdx][0];
        p.g = hues[hueIdx][1];
        p.b = hues[hueIdx][2];

        _particles.push_back(p);
    }
}

void BenchmarkFeeder::updateParticles(double dt)
{
    for (auto& p : _particles)
    {
        p.x += p.vx * static_cast<float>(dt);
        p.y += p.vy * static_cast<float>(dt);
        p.vy += 40.0f * static_cast<float>(dt);  // Gravity
        p.life -= static_cast<float>(dt) * 1.2f;
    }

    // Remove dead particles
    _particles.erase(
        std::remove_if(_particles.begin(), _particles.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        _particles.end());
}

void BenchmarkFeeder::drawParticles(uint8_t* buffer, double /*time*/)
{
    for (const auto& p : _particles)
    {
        int px = static_cast<int>(p.x);
        int py = static_cast<int>(p.y);
        if (px < 0 || px >= _width || py < 0 || py >= _height)
            continue;

        uint8_t intensity = static_cast<uint8_t>(255.0f * p.life);

        // 2×2 sparkle with additive blending
        for (int dy = -1; dy <= 1; dy++)
        {
            for (int dx = -1; dx <= 1; dx++)
            {
                int intensityScaled;
                if (dx == 0 && dy == 0)
                    intensityScaled = intensity;
                else
                    intensityScaled = intensity / 3;
                addLight(buffer, px + dx, py + dy, p.r, p.g, p.b, intensityScaled);
            }
        }
    }
}

// ── Layer 4: Moving radial gradient overlay ────────────────────────────

void BenchmarkFeeder::drawGradientOverlay(uint8_t* buffer, double time)
{
    // Light source traces a lissajous curve across the screen
    float lx = _width * (0.5f + 0.35f * std::sin(time * 0.6f));
    float ly = _height * (0.5f + 0.35f * std::sin(time * 0.9f + 1.5f));

    int radius = std::min(_width, _height) / 2;

    // Sparse sampling — one pixel every 2 in each direction, bilinear-ish.
    // Full per-pixel is overkill for a benchmark overlay and wastes cycles
    // that should be spent in the encoder.
    for (int y = 0; y < _height; y += 2)
    {
        for (int x = 0; x < _width; x += 2)
        {
            float dx = x - lx;
            float dy = y - ly;
            float dist = std::sqrt(dx * dx + dy * dy);
            float falloff = std::max(0.0f, 1.0f - dist / radius);
            falloff = falloff * falloff;  // Quadratic falloff

            int brightness = static_cast<int>(falloff * 60);
            if (brightness > 0)
            {
                addLight(buffer, x, y, 0xFF, 0xFF, 0xE0, brightness);
                addLight(buffer, x + 1, y, 0xFF, 0xFF, 0xE0, brightness);
                addLight(buffer, x, y + 1, 0xFF, 0xFF, 0xE0, brightness);
                addLight(buffer, x + 1, y + 1, 0xFF, 0xFF, 0xE0, brightness);
            }
        }
    }
}

// ── Drawing primitives ─────────────────────────────────────────────────

void BenchmarkFeeder::fillRect(uint8_t* buffer, int x, int y, int w, int h,
                                uint8_t r, uint8_t g, uint8_t b)
{
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(_width, x + w);
    int y1 = std::min(_height, y + h);

    for (int py = y0; py < y1; py++)
    {
        uint8_t* row = buffer + (py * _width + x0) * 4;
        for (int px = x0; px < x1; px++)
        {
            row[0] = r;
            row[1] = g;
            row[2] = b;
            row[3] = 0xFF;
            row += 4;
        }
    }
}

void BenchmarkFeeder::fillCircle(uint8_t* buffer, float cx, float cy, float radius,
                                  uint8_t r, uint8_t g, uint8_t b)
{
    int x0 = static_cast<int>(cx - radius - 1);
    int y0 = static_cast<int>(cy - radius - 1);
    int x1 = static_cast<int>(cx + radius + 1);
    int y1 = static_cast<int>(cy + radius + 1);

    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(_width, x1);
    y1 = std::min(_height, y1);

    float r2 = radius * radius;

    for (int py = y0; py < y1; py++)
    {
        for (int px = x0; px < x1; px++)
        {
            float dx = px - cx;
            float dy = py - cy;
            if (dx * dx + dy * dy <= r2)
            {
                uint8_t* p = buffer + (py * _width + px) * 4;
                p[0] = r;
                p[1] = g;
                p[2] = b;
                p[3] = 0xFF;
            }
        }
    }
}

void BenchmarkFeeder::blendPixel(uint8_t* buffer, int x, int y,
                                  uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || x >= _width || y < 0 || y >= _height)
        return;

    uint8_t* p = buffer + (y * _width + x) * 4;
    float alpha = a / 255.0f;
    float inv = 1.0f - alpha;
    p[0] = static_cast<uint8_t>(r * alpha + p[0] * inv);
    p[1] = static_cast<uint8_t>(g * alpha + p[1] * inv);
    p[2] = static_cast<uint8_t>(b * alpha + p[2] * inv);
    p[3] = 0xFF;
}

void BenchmarkFeeder::addLight(uint8_t* buffer, int x, int y,
                                uint8_t r, uint8_t g, uint8_t b, int intensity)
{
    if (x < 0 || x >= _width || y < 0 || y >= _height)
        return;

    uint8_t* p = buffer + (y * _width + x) * 4;
    p[0] = static_cast<uint8_t>(std::min(255, p[0] + intensity));
    p[1] = static_cast<uint8_t>(std::min(255, p[1] + (intensity * g) / 255));
    p[2] = static_cast<uint8_t>(std::min(255, p[2] + (intensity * b) / 255));
    p[3] = 0xFF;
}
