#include "slangpresetrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLShaderProgram>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// Desktop GL enums the ES-based Qt headers may not declare
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

namespace
{
/// The slang `#pragma format` names into GL internal formats (unknown ones are RGBA8, as in RetroArch)
GLenum InternalFormat(const QString& format)
{
    static const std::map<QString, GLenum> kFormats = {
        {QStringLiteral("R8G8B8A8Unorm"), GL_RGBA8},
        {QStringLiteral("R8G8B8A8Srgb"), GL_SRGB8_ALPHA8},
        {QStringLiteral("A2B10G10R10UnormPack32"), GL_RGB10_A2},
        {QStringLiteral("R16G16B16A16Sfloat"), GL_RGBA16F},
        {QStringLiteral("R32G32B32A32Sfloat"), GL_RGBA32F},
        {QStringLiteral("R16G16Sfloat"), GL_RG16F},
        {QStringLiteral("R32G32Sfloat"), GL_RG32F},
        {QStringLiteral("R16Sfloat"), GL_R16F},
        {QStringLiteral("R32Sfloat"), GL_R32F},
        {QStringLiteral("R8Unorm"), GL_R8},
        {QStringLiteral("R8G8Unorm"), GL_RG8},
    };
    const auto it = kFormats.find(format);
    return it == kFormats.end() ? GL_RGBA8 : it->second;
}

GLenum Wrap(const QString& wrap)
{
    if (wrap == QLatin1String("ClampToEdge"))
        return GL_CLAMP_TO_EDGE;
    if (wrap == QLatin1String("Repeat"))
        return GL_REPEAT;
    if (wrap == QLatin1String("MirroredRepeat"))
        return GL_MIRRORED_REPEAT;
    return GL_CLAMP_TO_BORDER;
}

/// The quad's corners as Position (x, y, 0, 1) and TexCoord (u, v): a strip over 0..1
constexpr float kQuad[] = {
    0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,  //
    1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,  //
    0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,  //
    1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
};

/// 0..1 into clip space, column major
constexpr float kMvp[] = {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f};

QByteArray ReadFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
}  // namespace

QStringList SlangPresetRunner::availablePresets()
{
    return QDir(QStringLiteral(":/crt")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

std::vector<SlangPresetRunner::Parameter> SlangPresetRunner::parametersOf(const QJsonObject& manifest)
{
    std::vector<Parameter> list;
    const QJsonObject parameters = manifest.value(QStringLiteral("parameters")).toObject();
    for (auto it = parameters.begin(); it != parameters.end(); ++it)
    {
        const QJsonObject p = it.value().toObject();
        Parameter parameter;
        parameter.name = it.key();
        parameter.description = p.value(QStringLiteral("description")).toString();
        parameter.initial = static_cast<float>(p.value(QStringLiteral("initial")).toDouble());
        parameter.minimum = static_cast<float>(p.value(QStringLiteral("minimum")).toDouble());
        parameter.maximum = static_cast<float>(p.value(QStringLiteral("maximum")).toDouble());
        parameter.step = static_cast<float>(p.value(QStringLiteral("step")).toDouble());
        parameter.value = parameter.initial;
        list.push_back(parameter);
    }
    return list;
}

std::vector<SlangPresetRunner::Parameter> SlangPresetRunner::readParameters(const QString& directory)
{
    const QJsonDocument manifest = QJsonDocument::fromJson(ReadFile(directory + QStringLiteral("/preset.json")));
    return manifest.isObject() ? parametersOf(manifest.object()) : std::vector<Parameter>();
}

bool SlangPresetRunner::isPreset(const QString& key)
{
    return !key.isEmpty() && QFileInfo::exists(presetDirectory(key) + QStringLiteral("/preset.json"));
}

SlangPresetRunner::SlangPresetRunner() = default;

SlangPresetRunner::~SlangPresetRunner()
{
    // The owner calls release() with its context current; without a context the GL names are gone with it
}

bool SlangPresetRunner::load(const QString& directory)
{
    release();
    if (!_initialized)
    {
        initializeOpenGLFunctions();
        _initialized = true;
    }
    _directory = directory;
    _error.clear();

    const QJsonDocument manifest = QJsonDocument::fromJson(ReadFile(directory + QStringLiteral("/preset.json")));
    if (!manifest.isObject())
    {
        _error = QStringLiteral("no preset.json in ") + directory;
        return false;
    }
    const QJsonObject root = manifest.object();

    std::map<QString, int> parameterIndex;
    for (const Parameter& parameter : parametersOf(root))
    {
        parameterIndex[parameter.name] = static_cast<int>(_parameters.size());
        _parameters.push_back(parameter);
    }

    for (const QJsonValue& value : root.value(QStringLiteral("textures")).toArray())
    {
        const QJsonObject t = value.toObject();
        QImage image(directory + QLatin1Char('/') + t.value(QStringLiteral("file")).toString());
        if (image.isNull())
        {
            _error = QStringLiteral("LUT image missing: ") + t.value(QStringLiteral("file")).toString();
            release();
            return false;
        }
        image = image.convertToFormat(QImage::Format_RGBA8888);
        Lut lut;
        lut.name = t.value(QStringLiteral("name")).toString();
        lut.linear = t.value(QStringLiteral("filter")).toString() == QLatin1String("Linear");
        lut.wrap = Wrap(t.value(QStringLiteral("wrap")).toString());
        lut.mipmap = t.value(QStringLiteral("mipmap")).toBool();
        allocate(lut.texture, image.size(), GL_RGBA8);
        glBindTexture(GL_TEXTURE_2D, lut.texture.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width(), image.height(), GL_RGBA, GL_UNSIGNED_BYTE,
                        image.constBits());
        if (lut.mipmap)
            glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        _luts.push_back(std::move(lut));
    }

    const QJsonArray passes = root.value(QStringLiteral("passes")).toArray();
    _passes.resize(static_cast<size_t>(passes.size()));
    for (int i = 0; i < passes.size(); ++i)
    {
        const QJsonObject p = passes[i].toObject();
        Pass& pass = _passes[static_cast<size_t>(i)];
        pass.alias = p.value(QStringLiteral("alias")).toString();
        pass.linear = p.value(QStringLiteral("filter")).toString() == QLatin1String("Linear");
        pass.wrap = Wrap(p.value(QStringLiteral("wrap")).toString());
        pass.mipmapInput = p.value(QStringLiteral("mipmap_input")).toBool();
        pass.frameCountMod = p.value(QStringLiteral("frame_count_mod")).toInt();
        // The preset's flags win over the shader's #pragma format, sRGB last (librashader's order)
        pass.format = InternalFormat(p.value(QStringLiteral("format")).toString());
        if (p.value(QStringLiteral("float_framebuffer")).toBool())
            pass.format = GL_RGBA16F;
        if (p.value(QStringLiteral("srgb_framebuffer")).toBool())
            pass.format = GL_SRGB8_ALPHA8;
        // The last pass draws into the output, whose format the preset does not choose (librashader ignores its flags
        // too): an sRGB one would encode the picture twice (crt-hyllian)
        if (i == passes.size() - 1)
            pass.format = GL_RGBA8;
        pass.srgb = pass.format == GL_SRGB8_ALPHA8;
        const QJsonObject scale = p.value(QStringLiteral("scale")).toObject();
        pass.scaleValid = scale.value(QStringLiteral("valid")).toBool();
        for (auto [axis, target] : {std::pair{QStringLiteral("x"), &pass.scaleX}, std::pair{QStringLiteral("y"), &pass.scaleY}})
        {
            const QJsonObject s = scale.value(axis).toObject();
            target->type = s.value(QStringLiteral("type")).toString();
            target->factor = static_cast<float>(s.value(QStringLiteral("factor")).toDouble(1.0));
        }

        const QString base = directory + QStringLiteral("/pass") + QString::number(i);
        const QByteArray vertex = ReadFile(base + QStringLiteral(".vert"));
        const QByteArray fragment = ReadFile(base + QStringLiteral(".frag"));
        pass.program = std::make_unique<QOpenGLShaderProgram>();
        if (vertex.isEmpty() || fragment.isEmpty() ||
            !pass.program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex) ||
            !pass.program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !pass.program->link())
        {
            _error = QStringLiteral("pass %1: %2").arg(i).arg(pass.program->log());
            release();
            return false;
        }
    }

    // The uniforms each pass uses: built-ins, parameters, textures and their sizes
    static const QRegularExpression numbered(QStringLiteral("^(OriginalHistory|PassOutput|PassFeedback)Size(\\d+)$"));
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        const GLuint program = pass.program->programId();
        GLint count = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
        std::map<QString, Binding> textures;
        for (GLint u = 0; u < count; ++u)
        {
            char raw[256];
            GLsizei length = 0;
            GLint size = 0;
            GLenum type = 0;
            glGetActiveUniform(program, static_cast<GLuint>(u), sizeof(raw), &length, &size, &type, raw);
            QString name = QString::fromLatin1(raw, length);
            if (name.endsWith(QLatin1String("[0]")))
                name.chop(3);
            const GLint location = glGetUniformLocation(program, raw);
            if (name.startsWith(QLatin1String("LIBRA_TEXTURE_")))
            {
                const QString texture = name.mid(14);
                Binding& binding = textures[texture];
                if (!resolve(static_cast<int>(i), texture, binding))
                    return false;
                binding.sampler = location;
                continue;
            }
            if (!name.startsWith(QLatin1String("LIBRA_UNIFORM_")))
                continue;
            const QString member = name.mid(14);
            if (member == QLatin1String("MVP"))
                pass.mvp = location;
            else if (member == QLatin1String("OutputSize"))
                pass.outputSize = location;
            else if (member == QLatin1String("FinalViewportSize"))
                pass.finalViewportSize = location;
            else if (member == QLatin1String("FrameCount"))
                pass.frameCount = location;
            else if (member == QLatin1String("FrameDirection"))
                pass.frameDirection = location;
            else if (member == QLatin1String("FrameTimeDelta"))
                pass.frameTimeDelta = location;
            else if (member == QLatin1String("OriginalFPS"))
                pass.originalFps = location;
            else if (const auto it = parameterIndex.find(member); it != parameterIndex.end())
                pass.parameterUniforms.emplace_back(location, it->second);
            else if (member.endsWith(QLatin1String("Size")))
            {
                // <texture>Size, OriginalHistorySize#, PassOutputSize#, PassFeedbackSize#
                const QRegularExpressionMatch match = numbered.match(member);
                const QString texture = match.hasMatch() ? match.captured(1) + match.captured(2) : member.chopped(4);
                Binding& binding = textures[texture];
                if (!resolve(static_cast<int>(i), texture, binding))
                    return false;
                binding.size = location;
            }
            // Anything else (a member no #pragma parameter names) stays 0, as in librashader
        }
        for (auto& [name, binding] : textures)
            pass.bindings.push_back(binding);
    }

    int historyDepth = 0;
    for (const Pass& pass : _passes)
        for (const Binding& binding : pass.bindings)
            if (binding.source == Source::History)
                historyDepth = std::max(historyDepth, binding.index);
    _history.resize(static_cast<size_t>(historyDepth));

    glGenVertexArrays(1, &_vao);
    glGenBuffers(1, &_vbo);
    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(4 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    _frameCount = 0;
    return true;
}

bool SlangPresetRunner::resolve(int passIndex, const QString& name, Binding& binding)
{
    static const QRegularExpression numbered(QStringLiteral("^(OriginalHistory|PassOutput|PassFeedback)(\\d+)$"));
    if (name == QLatin1String("Original"))
    {
        binding.source = Source::Original;
        return true;
    }
    if (name == QLatin1String("Source"))
    {
        binding.source = Source::Source;
        return true;
    }
    if (const QRegularExpressionMatch match = numbered.match(name); match.hasMatch())
    {
        const QString kind = match.captured(1);
        binding.index = match.captured(2).toInt();
        if (kind == QLatin1String("OriginalHistory"))
            binding.source = binding.index == 0 ? Source::Original : Source::History;
        else
            binding.source = kind == QLatin1String("PassOutput") ? Source::Output : Source::Feedback;
        if (binding.source == Source::Output && binding.index >= passIndex)
            return false;
        if (binding.source == Source::Feedback && binding.index >= static_cast<int>(_passes.size()))
            return false;
        if (binding.source == Source::Feedback)
            _passes[static_cast<size_t>(binding.index)].hasFeedback = true;
        return true;
    }
    for (size_t j = 0; j < _passes.size(); ++j)
    {
        const QString& alias = _passes[j].alias;
        if (alias.isEmpty())
            continue;
        if (name == alias && static_cast<int>(j) < passIndex)
        {
            binding.source = Source::Output;
            binding.index = static_cast<int>(j);
            return true;
        }
        if (name == alias + QLatin1String("Feedback"))
        {
            binding.source = Source::Feedback;
            binding.index = static_cast<int>(j);
            _passes[j].hasFeedback = true;
            return true;
        }
    }
    for (size_t j = 0; j < _luts.size(); ++j)
        if (name == _luts[j].name)
        {
            binding.source = Source::Lut;
            binding.index = static_cast<int>(j);
            return true;
        }
    _error = QStringLiteral("pass %1: unknown texture semantic %2").arg(passIndex).arg(name);
    return false;
}

void SlangPresetRunner::release()
{
    if (_initialized)
    {
        for (Pass& pass : _passes)
        {
            destroy(pass.output);
            destroy(pass.feedback);
        }
        for (Lut& lut : _luts)
            destroy(lut.texture);
        for (Texture& texture : _history)
            destroy(texture);
        destroy(_original);
        for (auto& [key, id] : _samplers)
            glDeleteSamplers(1, &id);
        if (_vbo)
            glDeleteBuffers(1, &_vbo);
        if (_vao)
            glDeleteVertexArrays(1, &_vao);
    }
    _passes.clear();
    _luts.clear();
    _history.clear();
    _samplers.clear();
    _parameters.clear();
    _vao = 0;
    _vbo = 0;
    _outputSize = QSize();
}

bool SlangPresetRunner::setParameter(const QString& name, float value)
{
    for (Parameter& parameter : _parameters)
        if (parameter.name == name)
        {
            parameter.value = std::clamp(value, std::min(parameter.minimum, parameter.maximum),
                                         std::max(parameter.minimum, parameter.maximum));
            return true;
        }
    return false;
}

void SlangPresetRunner::allocate(Texture& texture, QSize size, GLenum format)
{
    if (texture.id && texture.size == size && texture.format == format)
        return;
    destroy(texture);
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    // Every level the size allows: a pass with mipmap_input generates them
    const int levels = 1 + static_cast<int>(std::floor(std::log2(std::max(size.width(), size.height()))));
    const bool isFloat = format == GL_RGBA16F || format == GL_RGBA32F || format == GL_RG16F || format == GL_RG32F ||
                         format == GL_R16F || format == GL_R32F;
    for (int level = 0, w = size.width(), h = size.height(); level < levels; ++level)
    {
        glTexImage2D(GL_TEXTURE_2D, level, static_cast<GLint>(format), w, h, 0, GL_RGBA,
                     isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    glGenFramebuffers(1, &texture.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, texture.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    texture.size = size;
    texture.format = format;
}

void SlangPresetRunner::destroy(Texture& texture)
{
    if (texture.framebuffer)
        glDeleteFramebuffers(1, &texture.framebuffer);
    if (texture.id)
        glDeleteTextures(1, &texture.id);
    texture = Texture();
}

GLuint SlangPresetRunner::sampler(bool linear, GLenum wrap, bool mipmap)
{
    const uint32_t key = (linear ? 1u : 0u) | (mipmap ? 2u : 0u) | (static_cast<uint32_t>(wrap) << 2);
    if (const auto it = _samplers.find(key); it != _samplers.end())
        return it->second;
    GLuint id = 0;
    glGenSamplers(1, &id);
    const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, filter);
    glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER,
                        mipmap ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : filter);
    glSamplerParameteri(id, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    glSamplerParameteri(id, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    static const float kBorder[] = {0.0f, 0.0f, 0.0f, 0.0f};
    glSamplerParameterfv(id, GL_TEXTURE_BORDER_COLOR, kBorder);
    _samplers[key] = id;
    return id;
}

void SlangPresetRunner::bindInput(int unit, const Texture& texture, bool linear, GLenum wrap, bool mipmap)
{
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glBindSampler(static_cast<GLuint>(unit), sampler(linear, wrap, mipmap));
}

QSize SlangPresetRunner::passSize(const Pass& pass, QSize input, QSize viewport) const
{
    auto axis = [](const Scale& scale, int in, int view) {
        float size = static_cast<float>(in) * scale.factor;
        if (scale.type == QLatin1String("Viewport"))
            size = static_cast<float>(view) * scale.factor;
        else if (scale.type == QLatin1String("Absolute"))
            size = scale.factor;
        return std::max(1, static_cast<int>(std::lround(size)));
    };
    if (!pass.scaleValid)
        return input;
    return QSize(axis(pass.scaleX, input.width(), viewport.width()), axis(pass.scaleY, input.height(), viewport.height()));
}

void SlangPresetRunner::render(GLuint source, QRect sourceRect, QSize outputSize)
{
    if (_passes.empty() || sourceRect.isEmpty() || outputSize.isEmpty())
        return;
    _outputSize = outputSize;

    // The previous originals move one step back; the oldest texture takes the new frame
    if (!_history.empty())
    {
        Texture oldest = _history.back();
        for (size_t i = _history.size() - 1; i > 0; --i)
            _history[i] = _history[i - 1];
        _history[0] = _original;
        _original = oldest;
    }
    allocate(_original, sourceRect.size(), GL_RGBA8);
    for (Texture& texture : _history)
        if (!texture.id)
            allocate(texture, sourceRect.size(), GL_RGBA8);

    // The picture into Original, rows top first like the source
    GLuint read = 0;
    glGenFramebuffers(1, &read);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _original.framebuffer);
    glBlitFramebuffer(sourceRect.left(), sourceRect.top(), sourceRect.right() + 1, sourceRect.bottom() + 1, 0, 0,
                      sourceRect.width(), sourceRect.height(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &read);

    for (Pass& pass : _passes)
        if (pass.hasFeedback)
            std::swap(pass.output, pass.feedback);

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DITHER);
    glBindVertexArray(_vao);

    // Every pass's framebuffers before the first draws: a pass reads the feedback of a later one, black on the first
    // frame (newpixie's accumulation)
    QSize input = _original.size;
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        input = i + 1 == _passes.size() ? outputSize : passSize(pass, input, outputSize);
        allocate(pass.output, input, pass.format);
        if (pass.hasFeedback && pass.feedback.size != input)
            allocate(pass.feedback, input, pass.format);
    }

    input = _original.size;
    const Texture* previous = &_original;
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        const QSize size = pass.output.size;

        if (pass.mipmapInput && previous->id)
        {
            glBindTexture(GL_TEXTURE_2D, previous->id);
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, pass.output.framebuffer);
        glViewport(0, 0, size.width(), size.height());
        if (pass.srgb)
            glEnable(GL_FRAMEBUFFER_SRGB);
        pass.program->bind();
        auto setSize = [this](GLint location, QSize s) {
            if (location >= 0)
                glUniform4f(location, static_cast<float>(s.width()), static_cast<float>(s.height()),
                            1.0f / static_cast<float>(s.width()), 1.0f / static_cast<float>(s.height()));
        };
        if (pass.mvp >= 0)
            glUniformMatrix4fv(pass.mvp, 1, GL_FALSE, kMvp);
        setSize(pass.outputSize, size);
        setSize(pass.finalViewportSize, outputSize);
        if (pass.frameCount >= 0)
            glUniform1ui(pass.frameCount, pass.frameCountMod > 0 ? _frameCount % static_cast<uint32_t>(pass.frameCountMod)
                                                                   : _frameCount);
        if (pass.frameDirection >= 0)
            glUniform1i(pass.frameDirection, 1);
        if (pass.frameTimeDelta >= 0)
            glUniform1ui(pass.frameTimeDelta, 20);
        if (pass.originalFps >= 0)
            glUniform1f(pass.originalFps, 50.0f);
        for (const auto& [location, index] : pass.parameterUniforms)
            glUniform1f(location, _parameters[static_cast<size_t>(index)].value);

        int unit = 0;
        for (const Binding& binding : pass.bindings)
        {
            const Texture* texture = nullptr;
            bool linear = pass.linear;
            GLenum wrap = pass.wrap;
            bool mipmap = false;
            switch (binding.source)
            {
                case Source::Original:
                    texture = &_original;
                    linear = _passes.front().linear;
                    wrap = _passes.front().wrap;
                    break;
                case Source::Source:
                    texture = previous;
                    mipmap = pass.mipmapInput;
                    break;
                case Source::History:
                    texture = &_history[static_cast<size_t>(binding.index - 1)];
                    linear = _passes.front().linear;
                    wrap = _passes.front().wrap;
                    break;
                case Source::Output:
                case Source::Feedback:
                {
                    // A pass's output is read with the sampling of the pass that wrote it
                    const Pass& writer = _passes[static_cast<size_t>(binding.index)];
                    texture = binding.source == Source::Output ? &writer.output : &writer.feedback;
                    linear = writer.linear;
                    wrap = writer.wrap;
                    break;
                }
                case Source::Lut:
                {
                    const Lut& lut = _luts[static_cast<size_t>(binding.index)];
                    texture = &lut.texture;
                    linear = lut.linear;
                    wrap = lut.wrap;
                    mipmap = lut.mipmap;
                    break;
                }
            }
            setSize(binding.size, texture->size);
            if (binding.sampler >= 0)
            {
                bindInput(unit, *texture, linear, wrap, mipmap);
                glUniform1i(binding.sampler, unit);
                ++unit;
            }
        }

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        for (int u = unit - 1; u >= 0; --u)
        {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(u));
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindSampler(static_cast<GLuint>(u), 0);
        }
        pass.program->release();
        if (pass.srgb)
            glDisable(GL_FRAMEBUFFER_SRGB);
        input = size;
        previous = &pass.output;
    }
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ++_frameCount;
}

void SlangPresetRunner::drawTo(GLuint framebuffer, QRect viewport)
{
    if (_passes.empty() || !_passes.back().output.id)
        return;
    const Texture& output = _passes.back().output;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, output.framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    // Row 0 is the picture's top: upside down into GL's bottom-up rows
    glBlitFramebuffer(0, 0, output.size.width(), output.size.height(), viewport.left(),
                      viewport.top() + viewport.height(), viewport.left() + viewport.width(), viewport.top(),
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    // Opaque: passes leave values in alpha (guest's PAL composite), which an image of the target would take as
    // coverage (QOpenGLFramebufferObject::toImage un-premultiplies the colour by it)
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport.left(), viewport.top(), viewport.width(), viewport.height());
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (!scissor)
        glDisable(GL_SCISSOR_TEST);
}

QImage SlangPresetRunner::outputImage()
{
    if (_passes.empty() || !_passes.back().output.id)
        return {};
    const Texture& output = _passes.back().output;
    QImage image(output.size, QImage::Format_RGBA8888);
    glBindFramebuffer(GL_FRAMEBUFFER, output.framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, output.size.width(), output.size.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return image;
}
