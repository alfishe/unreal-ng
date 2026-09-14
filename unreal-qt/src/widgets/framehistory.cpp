#include "framehistory.h"

#include <algorithm>
#include <cstring>
#include <numeric>

// ============================================================================
// FrameHistory (CPU path)
// ============================================================================

FrameHistory::FrameHistory()
{
    setEqualWeights();
}

FrameHistory::~FrameHistory()
{
    release();
}

void FrameHistory::init(int width, int height, int historySize)
{
    release();

    _width = width;
    _height = height;
    _frameSize = static_cast<size_t>(width) * height * 4;  // RGBA8888
    _historySize = std::clamp(historySize, kMinFrames, kMaxFrames);
    _frameCount = 0;
    _headIndex = 0;

    // Pre-allocate frame buffers
    for (int i = 0; i < _historySize; ++i)
    {
        _frames[i].resize(_frameSize);
        std::fill(_frames[i].begin(), _frames[i].end(), 0);
    }

    updateWeights();
}

void FrameHistory::release()
{
    for (auto& frame : _frames)
    {
        frame.clear();
        frame.shrink_to_fit();
    }
    _width = 0;
    _height = 0;
    _frameSize = 0;
    _frameCount = 0;
}

void FrameHistory::setHistorySize(int size)
{
    int newSize = std::clamp(size, kMinFrames, kMaxFrames);
    if (newSize == _historySize)
        return;

    // Re-init if dimensions are set
    if (_width > 0 && _height > 0)
    {
        init(_width, _height, newSize);
    }
    else
    {
        _historySize = newSize;
        updateWeights();
    }
}

int FrameHistory::ringIndex(int logicalIndex) const
{
    // Convert logical index (0 = newest) to ring buffer index
    int idx = _headIndex - logicalIndex;
    if (idx < 0)
        idx += _historySize;
    return idx;
}

void FrameHistory::pushFrame(const uint8_t* pixels, size_t size)
{
    if (_frameSize == 0 || size < _frameSize)
        return;

    // Advance head to next slot
    _headIndex = (_headIndex + 1) % _historySize;

    // Copy frame data (single memcpy)
    std::memcpy(_frames[_headIndex].data(), pixels, _frameSize);

    if (_frameCount < _historySize)
        ++_frameCount;
}

bool FrameHistory::getBlendedFrame(uint8_t* dst, size_t size) const
{
    if (!_blendingEnabled || _frameCount < 2 || size < _frameSize)
    {
        // Just return newest frame
        return getFrame(0, dst, size);
    }

    // Blend frames using weights
    int framesToBlend = std::min(_frameCount, _historySize);

    // Use 32-bit accumulators for precision
    std::vector<uint32_t> accum(_frameSize, 0);

    float totalWeight = 0.0f;
    for (int i = 0; i < framesToBlend; ++i)
    {
        float weight = (i < static_cast<int>(_weights.size())) ? _weights[i] : 1.0f;
        totalWeight += weight;

        const uint8_t* src = _frames[ringIndex(i)].data();
        for (size_t j = 0; j < _frameSize; ++j)
        {
            accum[j] += static_cast<uint32_t>(src[j] * weight);
        }
    }

    // Normalize and output
    if (totalWeight > 0.0f)
    {
        float invWeight = 1.0f / totalWeight;
        for (size_t j = 0; j < _frameSize; ++j)
        {
            dst[j] = static_cast<uint8_t>(std::min(255u, static_cast<uint32_t>(accum[j] * invWeight)));
        }
    }

    return true;
}

bool FrameHistory::getFrame(int index, uint8_t* dst, size_t size) const
{
    if (index < 0 || index >= _frameCount || size < _frameSize)
        return false;

    std::memcpy(dst, _frames[ringIndex(index)].data(), _frameSize);
    return true;
}

const uint8_t* FrameHistory::frameData(int index) const
{
    if (index < 0 || index >= _frameCount)
        return nullptr;

    return _frames[ringIndex(index)].data();
}

void FrameHistory::setBlendWeights(const std::vector<float>& weights)
{
    _weights = weights;
    _weights.resize(_historySize, 1.0f);
}

void FrameHistory::setEqualWeights()
{
    _weights.resize(_historySize);
    std::fill(_weights.begin(), _weights.end(), 1.0f);
}

void FrameHistory::setExponentialWeights(float decay)
{
    _weights.resize(_historySize);
    float weight = 1.0f;
    for (int i = 0; i < _historySize; ++i)
    {
        _weights[i] = weight;
        weight *= decay;
    }
}

void FrameHistory::updateWeights()
{
    if (static_cast<int>(_weights.size()) != _historySize)
    {
        setEqualWeights();
    }
}

// ============================================================================
// FrameHistoryGL (GPU path)
// ============================================================================

FrameHistoryGL::FrameHistoryGL()
{
    std::fill(_textures.begin(), _textures.end(), 0);
    setEqualWeights();
}

FrameHistoryGL::~FrameHistoryGL()
{
    // Note: releaseGL() must be called with GL context current before destruction
}

void FrameHistoryGL::initGL(int width, int height, int historySize)
{
    if (_initialized)
        releaseGL();

    initializeOpenGLFunctions();

    _width = width;
    _height = height;
    _historySize = std::clamp(historySize, kMinFrames, kMaxFrames);
    _frameCount = 0;
    _headIndex = 0;

    // Create textures
    glGenTextures(_historySize, _textures.data());

    for (int i = 0; i < _historySize; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, _textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        // Nearest-neighbor for pixel-perfect scaling
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    updateWeights();
    _initialized = true;
}

void FrameHistoryGL::releaseGL()
{
    if (!_initialized)
        return;

    glDeleteTextures(_historySize, _textures.data());
    std::fill(_textures.begin(), _textures.end(), 0);

    _width = 0;
    _height = 0;
    _frameCount = 0;
    _initialized = false;
}

void FrameHistoryGL::setHistorySize(int size)
{
    int newSize = std::clamp(size, kMinFrames, kMaxFrames);
    if (newSize == _historySize)
        return;

    if (_initialized)
    {
        initGL(_width, _height, newSize);
    }
    else
    {
        _historySize = newSize;
        updateWeights();
    }
}

int FrameHistoryGL::ringIndex(int logicalIndex) const
{
    int idx = _headIndex - logicalIndex;
    if (idx < 0)
        idx += _historySize;
    return idx;
}

void FrameHistoryGL::pushFrame(const uint8_t* pixels, int width, int height)
{
    if (!_initialized || width != _width || height != _height)
        return;

    // Advance to next slot
    _headIndex = (_headIndex + 1) % _historySize;

    // Upload to texture (single glTexSubImage2D)
    glBindTexture(GL_TEXTURE_2D, _textures[_headIndex]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (_frameCount < _historySize)
        ++_frameCount;
}

GLuint FrameHistoryGL::nextFrameTexture()
{
    if (!_initialized)
        return 0;

    // Advance to next slot and return texture for rendering into
    _headIndex = (_headIndex + 1) % _historySize;

    if (_frameCount < _historySize)
        ++_frameCount;

    return _textures[_headIndex];
}

void FrameHistoryGL::bindHistoryTextures(GLenum startUnit, int count)
{
    if (!_initialized)
        return;

    int toBind = std::min(count, _frameCount);
    for (int i = 0; i < toBind; ++i)
    {
        glActiveTexture(startUnit + i);
        glBindTexture(GL_TEXTURE_2D, _textures[ringIndex(i)]);
    }

    // Reset to texture unit 0
    glActiveTexture(GL_TEXTURE0);
}

void FrameHistoryGL::unbindHistoryTextures(GLenum startUnit, int count)
{
    int toUnbind = std::min(count, _historySize);
    for (int i = 0; i < toUnbind; ++i)
    {
        glActiveTexture(startUnit + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

GLuint FrameHistoryGL::textureId(int index) const
{
    if (!_initialized || index < 0 || index >= _frameCount)
        return 0;

    return _textures[ringIndex(index)];
}

void FrameHistoryGL::setEqualWeights()
{
    _weights.resize(_historySize);
    std::fill(_weights.begin(), _weights.end(), 1.0f / _historySize);
}

void FrameHistoryGL::setExponentialWeights(float decay)
{
    _weights.resize(_historySize);
    float weight = 1.0f;
    float total = 0.0f;

    for (int i = 0; i < _historySize; ++i)
    {
        _weights[i] = weight;
        total += weight;
        weight *= decay;
    }

    // Normalize
    if (total > 0.0f)
    {
        for (float& w : _weights)
            w /= total;
    }
}

void FrameHistoryGL::updateWeights()
{
    if (static_cast<int>(_weights.size()) != _historySize)
    {
        setEqualWeights();
    }
}
