#include "crtbeammodelgpu.h"

#include <type_traits>
#include <vector>

namespace
{
// The quad over the viewport from the vertex index (a strip: top left, top right, bottom left, bottom right), its
// texture coordinate (0, 0) top left
const char* const kVertexShader = R"(
#version 330
out vec2 texCoord;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
    texCoord = corner;
}
)";

/// The textures, in their units
enum Texture
{
    Frame,
    GlowFrame,
    Geometry,
    Factors,
    Horizontal,
    Vertical,
    Interpolation,
    Transfer,
    Boost,
    GlowPhase,
    GlowX,
    GlowY,
    GlowB,
    Output,
    TextureCount
};
const char* const kSamplers[TextureCount] = {
    "frame",         "glowFrame",  "geometry",     "factors",      "horizontalWeights",
    "verticalTable", "interpolationTable", "transferTable", "boostTable", "glowPhase",
    "glowWeightsX",  "glowWeightsY", "glowWeightsB", "outputTable",
};

/// A tight table of 16-bit integers (`channels` 1 or 4) `width` wide into an integer texture, (re)allocated for its
/// size; an empty table as one zero texel (the shader does not read it)
template <typename T>
void Upload(gpu::Device& device, std::unique_ptr<gpu::Texture>& texture, const std::vector<T>& table, int width,
            int channels)
{
    static_assert(sizeof(T) == 2, "16-bit tables");
    constexpr bool kSigned = std::is_signed_v<T>;
    static const T kZero[4] = {};
    const T* data = table.empty() ? kZero : table.data();
    width = table.empty() ? 1 : width;
    const int height = table.empty() ? 1 : static_cast<int>(table.size() / (static_cast<size_t>(width) * channels));
    const gpu::Format format = channels == 4 ? (kSigned ? gpu::Format::RGBA16I : gpu::Format::RGBA16UI)
                                             : (kSigned ? gpu::Format::R16I : gpu::Format::R16UI);
    if (!texture || texture->Width() != width || texture->Height() != height || texture->GetFormat() != format)
        texture = device.CreateTexture(width, height, format);
    if (texture)
        device.Upload(*texture, {0, 0, width, height}, data);
}
}  // namespace

static_assert(TextureCount == 14, "one texture per sampler");

bool CRTBeamModelGPU::create(gpu::Device* device)
{
    destroy();
    _device = device;
    if (!_device)
    {
        _log = QStringLiteral("no GPU device");
        return false;
    }
    std::string log;
    _program = _device->CreateProgram(kVertexShader, CRTBeamModel::kShader, log);
    if (!_program)
    {
        _log = QString::fromStdString(log);
        return false;
    }
    return true;
}

void CRTBeamModelGPU::destroy()
{
    _program.reset();
    for (auto& texture : _textures)
        texture.reset();
}

void CRTBeamModelGPU::uploadTables(const CRTBeamModel& model)
{
    if (!_device)
        return;
    gpu::Device& d = *_device;
    auto* t = _textures;
    Upload(d, t[Geometry], model.geometry(), model.layout().outWidth, 4);
    Upload(d, t[Factors], model.factors(), model.layout().outWidth, 4);
    Upload(d, t[Horizontal], model.horizontalWeights(), CRTBeamModel::kPhases, 4);
    Upload(d, t[Vertical], model.verticalTable(), 256, 1);
    Upload(d, t[Interpolation], model.interpolationTable(), CRTBeamModel::kPhases, 4);
    Upload(d, t[Transfer], model.transferTable(), 256, 1);
    Upload(d, t[Boost], model.boostTable(), static_cast<int>(model.boostTable().size()), 1);
    Upload(d, t[GlowPhase], model.glowPhase(), CRTBeamModel::kPhases, 1);
    Upload(d, t[GlowX], model.glowWeightsX(), CRTBeamModel::kGlowTaps, 1);
    Upload(d, t[GlowY], model.glowWeightsY(), CRTBeamModel::kGlowRows, 1);
    Upload(d, t[GlowB], model.glowWeightsB(), CRTBeamModel::kGlowRows, 1);
    Upload(d, t[Output], model.outputTable(), 256, 1);
}

void CRTBeamModelGPU::uploadFrame(const CRTBeamModel& model)
{
    if (!_device)
        return;
    Upload(*_device, _textures[Frame], model.frame(), model.layout().frameWidth, 4);
    Upload(*_device, _textures[GlowFrame], model.glowFrame(), model.layout().frameWidth, 4);
}

void CRTBeamModelGPU::draw(const CRTBeamModel& model, gpu::Texture& target, gpu::Rect viewport)
{
    if (!_program || !_textures[Frame] || !_textures[Geometry])
        return;
    const CRTBeamModel::Layout& layout = model.layout();
    const QRect& source = layout.source;
    const int* bw = model.brightnessWeights();
    gpu::Program& p = *_program;
    for (int unit = 0; unit < TextureCount; ++unit)
    {
        // An own glow frame or the frame itself
        gpu::Texture* texture =
            unit == GlowFrame && model.glowFrame().empty() ? _textures[Frame].get() : _textures[unit].get();
        p.SetTexture(kSamplers[unit], texture);
    }
    p.SetInt4("sourceRect", source.left(), source.top(), source.right(), source.bottom());
    p.SetInt2("outSize", layout.outWidth, layout.outHeight);
    p.SetInt4("brightnessWeights", bw[0], bw[1], bw[2], bw[3]);
    p.SetInt("beams", model.vertical() == CRTBeamModel::Vertical::Beams ? 1 : 0);
    p.SetInt("beamByBrightness", model.beamByBrightness() ? 1 : 0);
    p.SetInt("fourRows", model.fourRows() ? 1 : 0);
    p.SetInt("clampToCentre", model.clampToCentre() ? 1 : 0);
    p.SetInt("transfer", model.transferTable().empty() ? 0 : 1);
    p.SetInt("boost", model.boostTable().empty() ? 0 : 1);
    p.SetInt("glow", model.glowWeightsY().empty() ? 0 : 1);
    p.SetInt("glowInDisplay", model.glowInDisplay() ? 1 : 0);
    p.SetInt("glowSquared", model.glowSquared() ? 1 : 0);
    gpu::DrawState state;
    state.viewport = viewport;
    _device->Draw(p, target, state);
}
