#pragma once

#include <QImage>
#include <QOpenGLExtraFunctions>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>
#include <vector>

class QJsonObject;
class QOpenGLShaderProgram;

/// @brief Runs a libretro slang CRT preset (the passes tools/crt-shaders/import.py made into GLSL 330, in
/// data/shaders/crt/<key>/) on an OpenGL 3.3 core context.
///
/// Every pass renders into a framebuffer of its scale; the last one into the output size, which the caller places
/// in its framebuffer (`drawTo`). The semantics of the slang spec: `Original`, `Source`, `OriginalHistory#`,
/// `PassOutput#`, `PassFeedback#`, the passes' aliases and `<alias>Feedback`, the LUTs; the uniforms `MVP`,
/// `OutputSize`, `SourceSize`, `OriginalSize`, `FinalViewportSize`, `<texture>Size`, `FrameCount`, `FrameDirection`,
/// `FrameTimeDelta`, `OriginalFPS` and the parameters.
///
/// The textures keep the slang (Vulkan) orientation: the picture's top row is row 0 and texture coordinate 0, so
/// `gl_FragCoord` and the texture coordinates mean what the shaders expect; drawTo() flips into a GL framebuffer.
/// Every call needs the context current.
class SlangPresetRunner : protected QOpenGLExtraFunctions
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

    /// The preset in `directory` (preset.json, pass<N>.vert / .frag, the LUT images; a resource path works). @return
    /// false with error() when a file is missing or a pass does not compile
    bool load(const QString& directory);
    /// The GL objects go; load() again for another preset
    void release();
    bool isLoaded() const { return !_passes.empty(); }
    const QString& error() const { return _error; }
    const QString& directory() const { return _directory; }

    const std::vector<Parameter>& parameters() const { return _parameters; }
    bool setParameter(const QString& name, float value);

    /// One frame: `source` is a texture (rows top first) whose `sourceRect` is the picture; the preset renders it at
    /// `outputSize`. Then drawTo() places the result
    void render(GLuint source, QRect sourceRect, QSize outputSize);
    /// The last render() into the bound draw framebuffer at `viewport` (GL coordinates, rows from the bottom), 1:1
    void drawTo(GLuint framebuffer, QRect viewport);
    /// The last render() as an image, rows top first
    QImage outputImage();

    uint32_t frameCount() const { return _frameCount; }

private:
    static std::vector<Parameter> parametersOf(const QJsonObject& manifest);

    struct Texture
    {
        GLuint id = 0;
        GLuint framebuffer = 0;
        QSize size;
        GLenum format = 0;
    };
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
        GLint sampler = -1;  ///< the sampler uniform
        GLint size = -1;     ///< its <name>Size uniform
    };
    struct Scale
    {
        QString type;  ///< Input, Viewport, Absolute
        float factor = 1.0f;
    };
    struct Pass
    {
        std::unique_ptr<QOpenGLShaderProgram> program;
        QString alias;
        bool linear = false;
        GLenum wrap = 0;
        bool mipmapInput = false;
        int frameCountMod = 0;
        GLenum format = 0;
        bool srgb = false;
        bool scaleValid = false;
        Scale scaleX;
        Scale scaleY;
        std::vector<Binding> bindings;
        GLint mvp = -1;
        GLint outputSize = -1;
        GLint finalViewportSize = -1;
        GLint frameCount = -1;
        GLint frameDirection = -1;
        GLint frameTimeDelta = -1;
        GLint originalFps = -1;
        std::vector<std::pair<GLint, int>> parameterUniforms;  ///< location, index into _parameters
        Texture output;
        Texture feedback;  ///< the previous frame's output, when a pass reads it
        bool hasFeedback = false;
    };
    struct Lut
    {
        QString name;
        Texture texture;
        bool linear = false;
        GLenum wrap = 0;
        bool mipmap = false;
    };

    bool resolve(int passIndex, const QString& name, Binding& binding);
    void allocate(Texture& texture, QSize size, GLenum format);
    void destroy(Texture& texture);
    GLuint sampler(bool linear, GLenum wrap, bool mipmap);
    void bindInput(int unit, const Texture& texture, bool linear, GLenum wrap, bool mipmap);
    QSize passSize(const Pass& pass, QSize input, QSize viewport) const;

    QString _directory;
    QString _error;
    std::vector<Pass> _passes;
    std::vector<Lut> _luts;
    std::vector<Parameter> _parameters;
    Texture _original;
    std::vector<Texture> _history;  ///< the previous originals, [0] = OriginalHistory1
    std::map<uint32_t, GLuint> _samplers;
    GLuint _vao = 0;
    GLuint _vbo = 0;
    uint32_t _frameCount = 0;
    QSize _outputSize;
    bool _initialized = false;
};
