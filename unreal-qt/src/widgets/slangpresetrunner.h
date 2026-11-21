#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>
#include <vector>

#include "gpu/gpudevice.h"

class QJsonObject;

/// @brief Runs a libretro slang CRT preset (the passes tools/crt-shaders/import.py made into GLSL 330, in
/// data/shaders/crt/<key>/) on the screen's GPU layer (gpu::Device: GL or Vulkan).
///
/// Every pass renders into a texture of its scale; the last one into the output size, which the caller places in
/// its target (`drawTo`). The semantics of the slang spec: `Original`, `Source`, `OriginalHistory#`,
/// `PassOutput#`, `PassFeedback#`, the passes' aliases and `<alias>Feedback`, the LUTs; the uniforms `MVP`,
/// `OutputSize`, `SourceSize`, `OriginalSize`, `FinalViewportSize`, `<texture>Size`, `FrameCount`, `FrameDirection`,
/// `FrameTimeDelta`, `OriginalFPS` and the parameters.
///
/// The textures keep the slang (Vulkan) orientation: the picture's top row is row 0 and texture coordinate 0, so
/// `gl_FragCoord` and the texture coordinates mean what the shaders expect; drawTo() turns it over into a target of
/// GL rows. Every call needs the device's thread (GL: its context current).
class SlangPresetRunner
{
public:
    struct Parameter
    {
        QString name;
        QString description;
        float initial = 0.0f;
        float minimum = 0.0f;
        float maximum = 0.0f;
        float step = 0.0f;
        float value = 0.0f;
    };

    /// The presets built into the application (`:/crt/<key>`, data/shaders/crt), by key
    static QStringList availablePresets();
    static bool isPreset(const QString& key);
    static QString presetDirectory(const QString& key) { return QStringLiteral(":/crt/") + key; }
    /// The parameters a preset declares (its preset.json), at their initial values, without loading it (no context)
    static std::vector<Parameter> readParameters(const QString& directory);

    SlangPresetRunner();
    ~SlangPresetRunner();
    SlangPresetRunner(const SlangPresetRunner&) = delete;
    SlangPresetRunner& operator=(const SlangPresetRunner&) = delete;

    /// The preset in `directory` (preset.json, pass<N>.vert / .frag, the LUT images; a resource path works) on
    /// `device`, which outlives the runner or its release(). @return false with error() when a file is missing or a
    /// pass does not compile
    bool load(const QString& directory, gpu::Device* device);
    /// The GPU objects go; load() again for another preset
    void release();
    bool isLoaded() const { return !_passes.empty(); }
    const QString& error() const { return _error; }
    const QString& directory() const { return _directory; }

    const std::vector<Parameter>& parameters() const { return _parameters; }
    bool setParameter(const QString& name, float value);

    /// One frame: `source` (rows top first) whose `sourceRect` is the picture; the preset renders it at
    /// `outputSize`. Then drawTo() places the result
    void render(gpu::Texture& source, QRect sourceRect, QSize outputSize);
    /// The last render() into `target` at `viewport` (GL coordinates, rows from the bottom), 1:1 and opaque
    void drawTo(gpu::Texture& target, QRect viewport);
    /// The last render() as an image, rows top first
    QImage outputImage();
    /// The last render()'s picture (rows top first), null before one
    gpu::Texture* output() const;

    uint32_t frameCount() const { return _frameCount; }
    /// The shaders' FrameCount: interlace presets alternate their lines by its parity. renderWindowImage sets its own
    /// count for the picture it renders, then puts the window's back
    void setFrameCount(uint32_t count) { _frameCount = count; }

private:
    static std::vector<Parameter> parametersOf(const QJsonObject& manifest);

    /// What a texture semantic of a pass reads
    enum class Source
    {
        Original,
        Source,
        History,   ///< OriginalHistory<index>
        Output,    ///< PassOutput<index> or an alias
        Feedback,  ///< PassFeedback<index> or <alias>Feedback
        Lut,
    };
    struct Binding
    {
        Source source = Source::Source;
        int index = 0;
        std::string sampler;  ///< the sampler uniform, empty when the pass reads only its size
        std::string size;     ///< its <name>Size uniform, empty when unused
    };
    struct Scale
    {
        QString type;  ///< Input, Viewport, Absolute
        float factor = 1.0f;
    };
    using TexturePtr = std::unique_ptr<gpu::Texture>;
    struct Pass
    {
        std::unique_ptr<gpu::Program> program;
        QString alias;
        bool linear = false;
        gpu::Wrap wrap = gpu::Wrap::ClampToBorder;
        bool mipmapInput = false;
        int frameCountMod = 0;
        gpu::Format format = gpu::Format::RGBA8;
        bool scaleValid = false;
        Scale scaleX;
        Scale scaleY;
        std::vector<Binding> bindings;
        bool mvp = false;
        bool outputSize = false;
        bool finalViewportSize = false;
        bool frameCount = false;
        bool frameDirection = false;
        bool frameTimeDelta = false;
        bool originalFps = false;
        std::vector<std::pair<std::string, int>> parameterUniforms;  ///< uniform, index into _parameters
        TexturePtr output;
        TexturePtr feedback;  ///< the previous frame's output, when a pass reads it
        bool hasFeedback = false;
    };
    struct Lut
    {
        QString name;
        TexturePtr texture;
        bool linear = false;
        gpu::Wrap wrap = gpu::Wrap::ClampToBorder;
        bool mipmap = false;
    };

    bool resolve(int passIndex, const QString& name, Binding& binding);
    /// `texture` at `size` / `format` (a new one, black, when it differs), every mip level the size allows
    void allocate(TexturePtr& texture, QSize size, gpu::Format format);
    QSize passSize(const Pass& pass, QSize input, QSize viewport) const;

    gpu::Device* _device = nullptr;
    QString _directory;
    QString _error;
    std::vector<Pass> _passes;
    std::vector<Lut> _luts;
    std::vector<Parameter> _parameters;
    TexturePtr _original;
    std::vector<TexturePtr> _history;  ///< the previous originals, [0] = OriginalHistory1
    std::unique_ptr<gpu::Program> _copy;  ///< drawTo's: the output turned over, opaque
    uint32_t _frameCount = 0;
    QSize _outputSize;
};
