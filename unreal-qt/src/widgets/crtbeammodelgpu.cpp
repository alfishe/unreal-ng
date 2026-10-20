#include "crtbeammodelgpu.h"

#include <QOpenGLContext>
#include <QOpenGLPixelTransferOptions>
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
void Upload(QOpenGLTexture*& texture, const std::vector<T>& table, int width, int channels)
{
    static_assert(sizeof(T) == 2, "16-bit tables");
    constexpr bool kSigned = std::is_signed_v<T>;
    static const T kZero[4] = {};
    const T* data = table.empty() ? kZero : table.data();
    width = table.empty() ? 1 : width;
    const int height = table.empty() ? 1 : static_cast<int>(table.size() / (static_cast<size_t>(width) * channels));
    const auto format = channels == 4 ? (kSigned ? QOpenGLTexture::RGBA16I : QOpenGLTexture::RGBA16U)
                                      : (kSigned ? QOpenGLTexture::R16I : QOpenGLTexture::R16U);
    const auto pixelFormat = channels == 4 ? QOpenGLTexture::RGBA_Integer : QOpenGLTexture::Red_Integer;
    const auto type = kSigned ? QOpenGLTexture::Int16 : QOpenGLTexture::UInt16;
    if (!texture)
        texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    if (!texture->isCreated() || texture->width() != width || texture->height() != height ||
        texture->format() != format)
    {
        if (texture->isCreated())
            texture->destroy();
        texture->create();
        texture->setSize(width, height);
        texture->setFormat(format);
        texture->setMipLevels(1);
        texture->allocateStorage(pixelFormat, type);
        // Integer textures are complete with nearest sampling only
        texture->setMinificationFilter(QOpenGLTexture::Nearest);
        texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
    QOpenGLPixelTransferOptions transfer;
    transfer.setAlignment(2);
    texture->setData(0, pixelFormat, type, data, &transfer);
}
}  // namespace

static_assert(TextureCount == 14, "one texture per sampler");

bool CRTBeamModelGPU::create()
{
    destroy();
    _gl = QOpenGLContext::currentContext()->extraFunctions();
    _program = new QOpenGLShaderProgram();
    if (!_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !_program->addShaderFromSourceCode(QOpenGLShader::Fragment, CRTBeamModel::kShader) || !_program->link())
    {
        _log = _program->log();
        destroy();
        return false;
    }
    _vao.create();
    return true;
}

void CRTBeamModelGPU::destroy()
{
    delete _program;
    _program = nullptr;
    for (QOpenGLTexture*& texture : _textures)
    {
        delete texture;
        texture = nullptr;
    }
    if (_vao.isCreated())
        _vao.destroy();
}

void CRTBeamModelGPU::uploadTables(const CRTBeamModel& model)
{
    QOpenGLTexture** t = _textures;
    Upload(t[Geometry], model.geometry(), model.layout().outWidth, 4);
    Upload(t[Factors], model.factors(), model.layout().outWidth, 4);
    Upload(t[Horizontal], model.horizontalWeights(), CRTBeamModel::kPhases, 4);
    Upload(t[Vertical], model.verticalTable(), 256, 1);
    Upload(t[Interpolation], model.interpolationTable(), CRTBeamModel::kPhases, 4);
    Upload(t[Transfer], model.transferTable(), 256, 1);
    Upload(t[Boost], model.boostTable(), static_cast<int>(model.boostTable().size()), 1);
    Upload(t[GlowPhase], model.glowPhase(), CRTBeamModel::kPhases, 1);
    Upload(t[GlowX], model.glowWeightsX(), CRTBeamModel::kGlowTaps, 1);
    Upload(t[GlowY], model.glowWeightsY(), CRTBeamModel::kGlowRows, 1);
    Upload(t[GlowB], model.glowWeightsB(), CRTBeamModel::kGlowRows, 1);
    Upload(t[Output], model.outputTable(), 256, 1);
}

void CRTBeamModelGPU::uploadFrame(const CRTBeamModel& model)
{
    Upload(_textures[Frame], model.frame(), model.layout().frameWidth, 4);
    Upload(_textures[GlowFrame], model.glowFrame(), model.layout().frameWidth, 4);
}

void CRTBeamModelGPU::draw(const CRTBeamModel& model)
{
    if (!_program || !_textures[Frame] || !_textures[Geometry])
        return;
    const CRTBeamModel::Layout& layout = model.layout();
    const QRect& source = layout.source;
    const int* bw = model.brightnessWeights();
    _program->bind();
    for (int unit = 0; unit < TextureCount; ++unit)
    {
        // An own glow frame or the frame itself
        QOpenGLTexture* texture = unit == GlowFrame && model.glowFrame().empty() ? _textures[Frame] : _textures[unit];
        texture->bind(static_cast<uint>(unit));
        _program->setUniformValue(kSamplers[unit], unit);
    }
    const GLuint program = _program->programId();
    _gl->glUniform4i(_gl->glGetUniformLocation(program, "sourceRect"), source.left(), source.top(), source.right(),
                     source.bottom());
    _gl->glUniform2i(_gl->glGetUniformLocation(program, "outSize"), layout.outWidth, layout.outHeight);
    _gl->glUniform4i(_gl->glGetUniformLocation(program, "brightnessWeights"), bw[0], bw[1], bw[2], bw[3]);
    _program->setUniformValue("beams", model.vertical() == CRTBeamModel::Vertical::Beams ? 1 : 0);
    _program->setUniformValue("beamByBrightness", model.beamByBrightness() ? 1 : 0);
    _program->setUniformValue("fourRows", model.fourRows() ? 1 : 0);
    _program->setUniformValue("clampToCentre", model.clampToCentre() ? 1 : 0);
    _program->setUniformValue("transfer", model.transferTable().empty() ? 0 : 1);
    _program->setUniformValue("boost", model.boostTable().empty() ? 0 : 1);
    _program->setUniformValue("glow", model.glowWeightsY().empty() ? 0 : 1);
    _program->setUniformValue("glowInDisplay", model.glowInDisplay() ? 1 : 0);
    _program->setUniformValue("glowSquared", model.glowSquared() ? 1 : 0);
    _vao.bind();
    _gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    _vao.release();
    for (int unit = TextureCount - 1; unit >= 0; --unit)
    {
        _gl->glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        _gl->glBindTexture(GL_TEXTURE_2D, 0);
    }
    _program->release();
}
