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
    const int period = cellSize * 2;
    int offsetX = static_cast<int>(time * 40.0f) % period;
    int offsetY = static_cast<int>(time * 30.0f) % period;

    // Precompute one full horizontal period (2 cells = 16 pixels = 64 bytes)
    // For each phase, we only need one period row pattern that we repeat.
    // This avoids per-pixel division and palette lookup.
    uint32_t rowPattern[period];  // 2 cells worth of pixels
    {
        int cellY = offsetY / cellSize;
        for (int x = 0; x < period; x++)
        {
            int cellX = (x + offsetX) / cellSize;
            int cellIdx = ((cellX + cellY) & 0x0F);
            rowPattern[x] = ZX_PALETTE[cellIdx];
        }
    }

    // Fill each row by tiling the period pattern
    // For rows within the same vertical cell, the horizontal pattern is
    // identical — only the cellY changes when we cross a cell boundary.
    int rowOffsetY = offsetY;
    for (int y = 0; y < _height; y++)
    {
        // Check if we crossed into a new vertical cell
        int newCellY = (y + offsetY) / cellSize;
        if (newCellY != rowOffsetY / cellSize)
        {
            rowOffsetY = y + offsetY;
            int cellY = rowOffsetY / cellSize;
            for (int x = 0; x < period; x++)
            {
                int cellX = (x + offsetX) / cellSize;
                int cellIdx = ((cellX + cellY) & 0x0F);
                rowPattern[x] = ZX_PALETTE[cellIdx];
            }
        }

        // Tile the pattern across the row
        uint8_t* row = buffer + y * _width * 4;
        int x = 0;
        // Fast path: full periods
        while (x + period <= _width)
        {
            memcpy(row + x * 4, rowPattern, period * 4);
            x += period;
        }
        // Remaining pixels
        if (x < _width)
        {
            memcpy(row + x * 4, rowPattern, (_width - x) * 4);
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
        fillCircle(buffer, tx, ty, 3.0f, 0xFF, 0xFF, 0x00);
    }

    // Ball — bright yellow
    fillCircle(buffer, _ballX, _ballY, 4.0f, 0xFF, 0xFF, 0x00);
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

        int hueIdx = rngNext() % 6;
        static const uint8_t hues[6][3] = {
            {0xFF, 0x00, 0x00}, {0x00, 0xFF, 0x00}, {0x00, 0x00, 0xFF},
            {0xFF, 0xFF, 0x00}, {0xFF, 0x00, 0xFF}, {0x00, 0xFF, 0xFF},
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
        p.vy += 40.0f * static_cast<float>(dt);
        p.life -= static_cast<float>(dt) * 1.2f;
    }

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
        if (px < 1 || px >= _width - 1 || py < 1 || py >= _height - 1)
            continue;

        uint8_t intensity = static_cast<uint8_t>(255.0f * p.life);

        // 3×3 sparkle with additive blending
        addLight(buffer, px, py, p.r, p.g, p.b, intensity);
        uint8_t dim = intensity >> 2;
        addLight(buffer, px - 1, py, p.r, p.g, p.b, dim);
        addLight(buffer, px + 1, py, p.r, p.g, p.b, dim);
        addLight(buffer, px, py - 1, p.r, p.g, p.b, dim);
        addLight(buffer, px, py + 1, p.r, p.g, p.b, dim);
    }
}

// ── Layer 4: Moving radial gradient overlay ────────────────────────────

void BenchmarkFeeder::drawGradientOverlay(uint8_t* buffer, double time)
{
    // Light source traces a lissajous curve across the screen
    float lx = _width * (0.5f + 0.35f * std::sin(time * 0.6f));
    float ly = _height * (0.5f + 0.35f * std::sin(time * 0.9f + 1.5f));

    int radius = std::min(_width, _height) / 2;
    int radiusSq = radius * radius;
    int iLx = static_cast<int>(lx);
    int iLy = static_cast<int>(ly);

    // Bounding box of the light — skip everything outside
    int x0 = std::max(0, iLx - radius);
    int y0 = std::max(0, iLy - radius);
    int x1 = std::min(_width, iLx + radius);
    int y1 = std::min(_height, iLy + radius);

    // Process 2×2 blocks within the bounding box only.
    // No sqrt: compare squared distance against radiusSq and use
    // an integer approximation for the quadratic falloff.
    // falloff = (1 - dist/radius)^2, but we avoid division by
    // precomputing the inverse radius and using integer math.
    const int FALLOFF_SCALE = 256;  // Fixed-point scale
    int invRadius = FALLOFF_SCALE / radius;

    for (int y = y0; y < y1; y += 2)
    {
        int dy = y - iLy;
        int dySq = dy * dy;
        for (int x = x0; x < x1; x += 2)
        {
            int dx = x - iLx;
            int distSq = dx * dx + dySq;

            if (distSq >= radiusSq)
                continue;

            // Integer quadratic falloff: (1 - dist/radius)^2
            // dist = sqrt(distSq) — but we approximate via
            // falloff ≈ ((radiusSq - distSq) * invRadius) / (radius * FALLOFF_SCALE)
            // Simpler: use the squared-distance ratio directly.
            // (radiusSq - distSq) / radiusSq gives (1 - d²/r²)² ≈ (1-d/r)^4,
            // which is slightly sharper but visually equivalent.
            int ratio = FALLOFF_SCALE - (distSq * FALLOFF_SCALE) / radiusSq;
            int brightness = (ratio * ratio * 60) / (FALLOFF_SCALE * FALLOFF_SCALE);

            if (brightness > 0)
            {
                // Inline addLight for 4 pixels — avoid 4 function calls
                uint8_t* p0 = buffer + (y * _width + x) * 4;
                p0[0] = std::min(255, p0[0] + brightness);
                p0[1] = std::min(255, p0[1] + (brightness >> 1) + (brightness >> 2));
                p0[2] = std::min(255, p0[2] + (brightness >> 3));
                p0[3] = 0xFF;

                if (x + 1 < _width)
                {
                    uint8_t* p1 = p0 + 4;
                    p1[0] = std::min(255, p1[0] + brightness);
                    p1[1] = std::min(255, p1[1] + (brightness >> 1) + (brightness >> 2));
                    p1[2] = std::min(255, p1[2] + (brightness >> 3));
                    p1[3] = 0xFF;
                }
                if (y + 1 < _height)
                {
                    uint8_t* p2 = p0 + _width * 4;
                    p2[0] = std::min(255, p2[0] + brightness);
                    p2[1] = std::min(255, p2[1] + (brightness >> 1) + (brightness >> 2));
                    p2[2] = std::min(255, p2[2] + (brightness >> 3));
                    p2[3] = 0xFF;

                    if (x + 1 < _width)
                    {
                        uint8_t* p3 = p2 + 4;
                        p3[0] = std::min(255, p3[0] + brightness);
                        p3[1] = std::min(255, p3[1] + (brightness >> 1) + (brightness >> 2));
                        p3[2] = std::min(255, p3[2] + (brightness >> 3));
                        p3[3] = 0xFF;
                    }
                }
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

    // Precompute one row of the rectangle for memcpy
    int rowBytes = (x1 - x0) * 4;
    if (rowBytes <= 0)
        return;

    // Build a single-row template
    // (Small enough that per-pixel fill is fine)
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
    int x0 = std::max(0, static_cast<int>(cx - radius - 1));
    int y0 = std::max(0, static_cast<int>(cy - radius - 1));
    int x1 = std::min(_width, static_cast<int>(cx + radius + 1));
    int y1 = std::min(_height, static_cast<int>(cy + radius + 1));

    int r2 = static_cast<int>(radius * radius);
    int icx = static_cast<int>(cx);
    int icy = static_cast<int>(cy);

    for (int py = y0; py < y1; py++)
    {
        int dy = py - icy;
        int dySq = dy * dy;
        uint8_t* row = buffer + (py * _width + x0) * 4;
        for (int px = x0; px < x1; px++)
        {
            int dx = px - icx;
            if (dx * dx + dySq <= r2)
            {
                row[0] = r;
                row[1] = g;
                row[2] = b;
                row[3] = 0xFF;
            }
            row += 4;
        }
    }
}

void BenchmarkFeeder::blendPixel(uint8_t* buffer, int x, int y,
                                  uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (x < 0 || x >= _width || y < 0 || y >= _height)
        return;

    uint8_t* p = buffer + (y * _width + x) * 4;
    int alpha = a;
    int inv = 255 - alpha;
    p[0] = static_cast<uint8_t>((r * alpha + p[0] * inv) >> 8);
    p[1] = static_cast<uint8_t>((g * alpha + p[1] * inv) >> 8);
    p[2] = static_cast<uint8_t>((b * alpha + p[2] * inv) >> 8);
    p[3] = 0xFF;
}

void BenchmarkFeeder::addLight(uint8_t* buffer, int x, int y,
                                uint8_t r, uint8_t g, uint8_t b, int intensity)
{
    if (x < 0 || x >= _width || y < 0 || y >= _height)
        return;

    uint8_t* p = buffer + (y * _width + x) * 4;
    p[0] = static_cast<uint8_t>(std::min(255, p[0] + intensity));
    p[1] = static_cast<uint8_t>(std::min(255, p[1] + ((intensity * g) >> 8)));
    p[2] = static_cast<uint8_t>(std::min(255, p[2] + ((intensity * b) >> 8)));
    p[3] = 0xFF;
}
