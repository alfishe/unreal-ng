#include "slangpresetrunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace
{
/// The slang `#pragma format` names the presets use into the layer's formats (unknown ones are RGBA8, as in
/// RetroArch; the one- and two-channel float formats take their four-channel kin)
gpu::Format PassFormat(const QString& format)
{
    static const std::map<QString, gpu::Format> kFormats = {
        {QStringLiteral("R8G8B8A8Unorm"), gpu::Format::RGBA8},
        {QStringLiteral("R8G8B8A8Srgb"), gpu::Format::SRGBA8},
        {QStringLiteral("R16G16B16A16Sfloat"), gpu::Format::RGBA16F},
        {QStringLiteral("R32G32B32A32Sfloat"), gpu::Format::RGBA32F},
        {QStringLiteral("R16G16Sfloat"), gpu::Format::RGBA16F},
        {QStringLiteral("R16Sfloat"), gpu::Format::RGBA16F},
        {QStringLiteral("R32G32Sfloat"), gpu::Format::RGBA32F},
        {QStringLiteral("R32Sfloat"), gpu::Format::RGBA32F},
    };
    const auto it = kFormats.find(format);
    return it == kFormats.end() ? gpu::Format::RGBA8 : it->second;
}

gpu::Wrap Wrap(const QString& wrap)
{
    if (wrap == QLatin1String("ClampToEdge"))
        return gpu::Wrap::ClampToEdge;
    if (wrap == QLatin1String("Repeat"))
        return gpu::Wrap::Repeat;
    if (wrap == QLatin1String("MirroredRepeat"))
        return gpu::Wrap::MirroredRepeat;
    return gpu::Wrap::ClampToBorder;
}

gpu::Sampler SamplerOf(bool linear, gpu::Wrap wrap, bool mipmap)
{
    return {linear ? gpu::Filter::Linear : gpu::Filter::Nearest, wrap, mipmap};
}

QByteArray ReadFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

/// drawTo's copy: the output 1:1 (texelFetch), rows turned over, alpha 1. Passes leave values in alpha (guest's PAL
/// composite), which an image of the target would take as coverage
const char* const kCopyVertex = R"(
#version 330
layout(location = 0) in vec4 Position;
void main() {
    gl_Position = vec4(Position.xy * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* const kCopyFragment = R"(
#version 330
uniform sampler2D picture;
uniform ivec4 placement;
out vec4 fragColor;
void main() {
    // placement: the viewport's x, y (GL rows) and the picture's height
    ivec2 p = ivec2(gl_FragCoord.xy) - placement.xy;
    fragColor = vec4(texelFetch(picture, ivec2(p.x, placement.z - 1 - p.y), 0).rgb, 1.0);
}
)";
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
    release();
}

bool SlangPresetRunner::load(const QString& directory, gpu::Device* device)
{
    release();
    _device = device;
    _directory = directory;
    _error.clear();
    if (!_device)
    {
        _error = QStringLiteral("no GPU device");
        return false;
    }

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
        allocate(lut.texture, image.size(), gpu::Format::RGBA8);
        if (!lut.texture)
        {
            _error = QStringLiteral("LUT texture refused: ") + lut.name;
            release();
            return false;
        }
        _device->Upload(*lut.texture, {0, 0, image.width(), image.height()}, image.constBits(),
                        static_cast<int>(image.bytesPerLine() / 4));
        if (lut.mipmap)
            _device->GenerateMipmaps(*lut.texture);
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
        pass.format = PassFormat(p.value(QStringLiteral("format")).toString());
        if (p.value(QStringLiteral("float_framebuffer")).toBool())
            pass.format = gpu::Format::RGBA16F;
        if (p.value(QStringLiteral("srgb_framebuffer")).toBool())
            pass.format = gpu::Format::SRGBA8;
        // The last pass draws into the output, whose format the preset does not choose (librashader ignores its flags
        // too): an sRGB one would encode the picture twice (crt-hyllian)
        if (i == passes.size() - 1)
            pass.format = gpu::Format::RGBA8;
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
        std::string log = "missing source";
        if (!vertex.isEmpty() && !fragment.isEmpty())
            pass.program = _device->CreateProgram(vertex.toStdString(), fragment.toStdString(), log);
        if (!pass.program)
        {
            _error = QStringLiteral("pass %1: %2").arg(i).arg(QString::fromStdString(log));
            release();
            return false;
        }
    }

    // The uniforms each pass uses: built-ins, parameters, textures and their sizes
    static const QRegularExpression numbered(QStringLiteral("^(OriginalHistory|PassOutput|PassFeedback)Size(\\d+)$"));
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        std::vector<std::string> names = pass.program->Names();
        std::sort(names.begin(), names.end());
        std::map<QString, Binding> textures;
        for (const std::string& raw : names)
        {
            const QString name = QString::fromStdString(raw);
            if (name.startsWith(QLatin1String("LIBRA_TEXTURE_")))
            {
                const QString texture = name.mid(14);
                Binding& binding = textures[texture];
                if (!resolve(static_cast<int>(i), texture, binding))
                {
                    release();
                    return false;
                }
                binding.sampler = raw;
                continue;
            }
            if (!name.startsWith(QLatin1String("LIBRA_UNIFORM_")))
                continue;
            const QString member = name.mid(14);
            if (member == QLatin1String("MVP"))
                pass.mvp = true;
            else if (member == QLatin1String("OutputSize"))
                pass.outputSize = true;
            else if (member == QLatin1String("FinalViewportSize"))
                pass.finalViewportSize = true;
            else if (member == QLatin1String("FrameCount"))
                pass.frameCount = true;
            else if (member == QLatin1String("FrameDirection"))
                pass.frameDirection = true;
            else if (member == QLatin1String("FrameTimeDelta"))
                pass.frameTimeDelta = true;
            else if (member == QLatin1String("OriginalFPS"))
                pass.originalFps = true;
            else if (const auto it = parameterIndex.find(member); it != parameterIndex.end())
                pass.parameterUniforms.emplace_back(raw, it->second);
            else if (member.endsWith(QLatin1String("Size")))
            {
                // <texture>Size, OriginalHistorySize#, PassOutputSize#, PassFeedbackSize#
                const QRegularExpressionMatch match = numbered.match(member);
                const QString texture = match.hasMatch() ? match.captured(1) + match.captured(2) : member.chopped(4);
                Binding binding = textures.count(texture) ? textures[texture] : Binding{};
                if (!resolve(static_cast<int>(i), texture, binding))
                {
                    // A size of no texture (crt-yah's InputSize) stays 0, as in librashader; GL drops it when unused,
                    // Vulkan's reflection reports every member of the block
                    _error.clear();
                    continue;
                }
                binding.size = raw;
                textures[texture] = binding;
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

    std::string log;
    _copy = _device->CreateProgram(kCopyVertex, kCopyFragment, log);
    if (!_copy)
    {
        _error = QStringLiteral("copy: ") + QString::fromStdString(log);
        release();
        return false;
    }
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
    // The device's objects go while it lives (the owner releases before it destroys the device)
    _passes.clear();
    _luts.clear();
    _history.clear();
    _original.reset();
    _copy.reset();
    _parameters.clear();
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

void SlangPresetRunner::allocate(TexturePtr& texture, QSize size, gpu::Format format)
{
    if (texture && texture->Width() == size.width() && texture->Height() == size.height() &&
        texture->GetFormat() == format)
        return;
    // Every level the size allows: a pass with mipmap_input generates them
    const int levels = 1 + static_cast<int>(std::floor(std::log2(std::max(size.width(), size.height()))));
    texture = _device->CreateTexture(size.width(), size.height(), format, levels);
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

void SlangPresetRunner::render(gpu::Texture& source, QRect sourceRect, QSize outputSize)
{
    if (_passes.empty() || !_device || sourceRect.isEmpty() || outputSize.isEmpty())
        return;
    _outputSize = outputSize;

    // The previous originals move one step back; the oldest texture takes the new frame
    if (!_history.empty())
    {
        TexturePtr oldest = std::move(_history.back());
        for (size_t i = _history.size() - 1; i > 0; --i)
            _history[i] = std::move(_history[i - 1]);
        _history[0] = std::move(_original);
        _original = std::move(oldest);
    }
    allocate(_original, sourceRect.size(), gpu::Format::RGBA8);
    for (TexturePtr& texture : _history)
        if (!texture)
            allocate(texture, sourceRect.size(), gpu::Format::RGBA8);
    if (!_original)
        return;

    // The picture into Original, rows top first like the source
    _device->Blit(source, {sourceRect.x(), sourceRect.y(), sourceRect.width(), sourceRect.height()}, *_original,
                  {0, 0, sourceRect.width(), sourceRect.height()}, gpu::Filter::Nearest);

    for (Pass& pass : _passes)
        if (pass.hasFeedback)
            std::swap(pass.output, pass.feedback);

    // Every pass's textures before the first draws: a pass reads the feedback of a later one, black on the first
    // frame (newpixie's accumulation)
    QSize input(_original->Width(), _original->Height());
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        input = i + 1 == _passes.size() ? outputSize : passSize(pass, input, outputSize);
        allocate(pass.output, input, pass.format);
        if (pass.hasFeedback)
            allocate(pass.feedback, input, pass.format);
        if (!pass.output || (pass.hasFeedback && !pass.feedback))
            return;
    }

    gpu::Texture* previous = _original.get();
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        gpu::Program& program = *pass.program;
        const int width = pass.output->Width();
        const int height = pass.output->Height();

        if (pass.mipmapInput)
            _device->GenerateMipmaps(*previous);

        auto setSize = [&program](const std::string& name, int w, int h) {
            if (!name.empty())
                program.SetFloat4(name.c_str(), static_cast<float>(w), static_cast<float>(h), 1.0f / static_cast<float>(w),
                                  1.0f / static_cast<float>(h));
        };
        if (pass.mvp)
            program.SetFloats("LIBRA_UNIFORM_MVP", gpu::kQuadMvp, 16);
        if (pass.outputSize)
            setSize("LIBRA_UNIFORM_OutputSize", width, height);
        if (pass.finalViewportSize)
            setSize("LIBRA_UNIFORM_FinalViewportSize", outputSize.width(), outputSize.height());
        if (pass.frameCount)
        {
            const uint32_t count =
                pass.frameCountMod > 0 ? _frameCount % static_cast<uint32_t>(pass.frameCountMod) : _frameCount;
            program.SetUints("LIBRA_UNIFORM_FrameCount", &count, 1);
        }
        if (pass.frameDirection)
            program.SetInt("LIBRA_UNIFORM_FrameDirection", 1);
        if (pass.frameTimeDelta)
        {
            const uint32_t delta = 20;
            program.SetUints("LIBRA_UNIFORM_FrameTimeDelta", &delta, 1);
        }
        if (pass.originalFps)
            program.SetFloat("LIBRA_UNIFORM_OriginalFPS", 50.0f);
        for (const auto& [name, index] : pass.parameterUniforms)
            program.SetFloat(name.c_str(), _parameters[static_cast<size_t>(index)].value);

        for (const Binding& binding : pass.bindings)
        {
            gpu::Texture* texture = nullptr;
            bool linear = pass.linear;
            gpu::Wrap wrap = pass.wrap;
            bool mipmap = false;
            switch (binding.source)
            {
                case Source::Original:
                    texture = _original.get();
                    linear = _passes.front().linear;
                    wrap = _passes.front().wrap;
                    break;
                case Source::Source:
                    texture = previous;
                    mipmap = pass.mipmapInput;
                    break;
                case Source::History:
                    texture = _history[static_cast<size_t>(binding.index - 1)].get();
                    linear = _passes.front().linear;
                    wrap = _passes.front().wrap;
                    break;
                case Source::Output:
                case Source::Feedback:
                {
                    // A pass's output is read with the sampling of the pass that wrote it
                    const Pass& writer = _passes[static_cast<size_t>(binding.index)];
                    texture = binding.source == Source::Output ? writer.output.get() : writer.feedback.get();
                    linear = writer.linear;
                    wrap = writer.wrap;
                    break;
                }
                case Source::Lut:
                {
                    const Lut& lut = _luts[static_cast<size_t>(binding.index)];
                    texture = lut.texture.get();
                    linear = lut.linear;
                    wrap = lut.wrap;
                    mipmap = lut.mipmap;
                    break;
                }
            }
            if (texture)
                setSize(binding.size, texture->Width(), texture->Height());
            if (!binding.sampler.empty())
                program.SetTexture(binding.sampler.c_str(), texture, SamplerOf(linear, wrap, mipmap));
        }

        gpu::DrawState state;
        state.viewport = {0, 0, width, height};
        _device->Draw(program, *pass.output, state);
        previous = pass.output.get();
    }
    ++_frameCount;
}

gpu::Texture* SlangPresetRunner::output() const
{
    return _passes.empty() ? nullptr : _passes.back().output.get();
}

void SlangPresetRunner::drawTo(gpu::Texture& target, QRect viewport)
{
    gpu::Texture* picture = output();
    if (!picture || !_copy)
        return;
    _copy->SetTexture("picture", picture);
    _copy->SetInt4("placement", viewport.x(), viewport.y(), picture->Height(), 0);
    gpu::DrawState state;
    state.viewport = {viewport.x(), viewport.y(), viewport.width(), viewport.height()};
    _device->Draw(*_copy, target, state);
}

QImage SlangPresetRunner::outputImage()
{
    gpu::Texture* picture = output();
    if (!picture)
        return {};
    QImage image(picture->Width(), picture->Height(), QImage::Format_RGBA8888);
    if (image.bytesPerLine() != picture->Width() * 4 ||
        !_device->Read(*picture, {0, 0, picture->Width(), picture->Height()}, image.bits()))
        return {};
    return image;
}
