#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

/// @brief Frame history ring buffer for temporal effects (gigascreen smoothing, motion blur)
/// Supports both CPU-side pixel buffers and GPU-side textures with minimal copies.
class FrameHistory
{
public:
    static constexpr int kMinFrames = 2;
    static constexpr int kMaxFrames = 5;
    static constexpr int kDefaultFrames = 2;

    FrameHistory();
    ~FrameHistory();

    /// @brief Initialize with frame dimensions
    /// @param width Frame width in pixels
    /// @param height Frame height in pixels
    /// @param historySize Number of frames to keep (2-5)
    void init(int width, int height, int historySize = kDefaultFrames);

    /// @brief Release all resources
    void release();

    /// @brief Clear history without releasing buffers (use when viewport/geometry changes)
    void clear() { _frameCount = 0; _headIndex = 0; }

    /// @brief Set history depth (2-5 frames)
    void setHistorySize(int size);
    int historySize() const { return _historySize; }

    /// @brief Push a new frame (CPU path)
    /// @param pixels RGBA8888 pixel data
    /// @param size Size in bytes
    void pushFrame(const uint8_t* pixels, size_t size);

    /// @brief Get blended frame (CPU path) - averages all frames in history
    /// @param dst Destination buffer
    /// @param size Size in bytes
    /// @return true if blended frame was produced
    bool getBlendedFrame(uint8_t* dst, size_t size) const;

    /// @brief Get specific frame from history (0 = most recent)
    /// @param index Frame index (0 = newest, historySize-1 = oldest)
    /// @param dst Destination buffer
    /// @param size Size in bytes
    /// @return true if frame was copied
    bool getFrame(int index, uint8_t* dst, size_t size) const;

    /// @brief Get raw pointer to frame buffer (zero-copy access)
    /// @param index Frame index (0 = newest)
    /// @return Pointer to frame data, or nullptr if invalid
    const uint8_t* frameData(int index) const;

    /// @brief Check if history has minimum frames for blending
    bool hasMinimumHistory() const { return _frameCount >= 2; }

    /// @brief Get number of frames currently in history
    int frameCount() const { return _frameCount; }

    /// @brief Frame dimensions
    int width() const { return _width; }
    int height() const { return _height; }
    size_t frameSize() const { return _frameSize; }

    /// @brief Enable/disable temporal blending
    void setBlendingEnabled(bool enabled) { _blendingEnabled = enabled; }
    bool blendingEnabled() const { return _blendingEnabled; }

    /// @brief Set blend weights (custom weighting, newest frame first)
    /// @param weights Array of weights (will be normalized)
    void setBlendWeights(const std::vector<float>& weights);

    /// @brief Use equal weighting for all frames
    void setEqualWeights();

    /// @brief Use exponential decay weighting (newest frame has most weight)
    /// @param decay Decay factor (0.5 = each older frame has half the weight)
    void setExponentialWeights(float decay = 0.5f);

private:
    int ringIndex(int logicalIndex) const;
    void updateWeights();

    int _width = 0;
    int _height = 0;
    size_t _frameSize = 0;
    int _historySize = kDefaultFrames;
    int _frameCount = 0;
    int _headIndex = 0;  // Points to newest frame slot

    bool _blendingEnabled = true;
    std::vector<float> _weights;

    // Ring buffer of frame data (CPU path)
    std::array<std::vector<uint8_t>, kMaxFrames> _frames;
};

/// @brief GPU-side frame history using OpenGL textures
/// Designed for minimal CPU-GPU transfers - frames stay on GPU
class FrameHistoryGL : protected QOpenGLFunctions
{
public:
    static constexpr int kMinFrames = 2;
    static constexpr int kMaxFrames = 5;
    static constexpr int kDefaultFrames = 2;

    FrameHistoryGL();
    ~FrameHistoryGL();

    /// @brief Initialize OpenGL resources (must be called with GL context current)
    void initGL(int width, int height, int historySize = kDefaultFrames);

    /// @brief Release OpenGL resources (must be called with GL context current)
    void releaseGL();

    /// @brief Set history depth (requires re-init if changed)
    void setHistorySize(int size);
    int historySize() const { return _historySize; }

    /// @brief Push new frame from CPU data
    /// @param pixels RGBA8888 pixel data
    void pushFrame(const uint8_t* pixels, int width, int height);

    /// @brief Advance to next texture slot (for GPU-to-GPU copy)
    /// @return Texture ID to render into
    GLuint nextFrameTexture();

    /// @brief Bind textures for temporal blending shader
    /// @param startUnit First texture unit to use (e.g., GL_TEXTURE1)
    /// @param count Number of textures to bind (up to historySize)
    void bindHistoryTextures(GLenum startUnit, int count);

    /// @brief Unbind history textures
    void unbindHistoryTextures(GLenum startUnit, int count);

    /// @brief Get texture ID for specific frame
    /// @param index Frame index (0 = newest)
    GLuint textureId(int index) const;

    /// @brief Check if initialized
    bool isInitialized() const { return _initialized; }

    /// @brief Check if history has minimum frames
    bool hasMinimumHistory() const { return _frameCount >= 2; }

    int frameCount() const { return _frameCount; }
    int width() const { return _width; }
    int height() const { return _height; }

    /// @brief Get blend weights for shader
    const std::vector<float>& weights() const { return _weights; }

    void setBlendingEnabled(bool enabled) { _blendingEnabled = enabled; }
    bool blendingEnabled() const { return _blendingEnabled; }

    void setEqualWeights();
    void setExponentialWeights(float decay = 0.5f);

private:
    int ringIndex(int logicalIndex) const;
    void updateWeights();

    bool _initialized = false;
    int _width = 0;
    int _height = 0;
    int _historySize = kDefaultFrames;
    int _frameCount = 0;
    int _headIndex = 0;

    bool _blendingEnabled = true;
    std::vector<float> _weights;

    // Ring buffer of GL textures
    std::array<GLuint, kMaxFrames> _textures{};
};
