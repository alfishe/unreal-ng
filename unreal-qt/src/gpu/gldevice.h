#pragma once

#include <QOpenGLExtraFunctions>

#include <memory>
#include <string>
#include <vector>

#include "gpudevice.h"

class QOpenGLContext;

namespace gpu
{
/// @brief The GL 3.3 core backend of the GPU layer, on the context current at creation (it must be current for every
/// call and for the destruction of everything it made).
class GlDevice final : public Device, protected QOpenGLExtraFunctions
{
public:
    /// On the current context. Null when there is none
    static std::unique_ptr<GlDevice> Create();
    ~GlDevice() override;

    Backend GetBackend() const override { return Backend::GL; }
    std::string Describe() const override;
    int MaxTextureSize() const override { return _maxTextureSize; }

    std::unique_ptr<Texture> CreateTexture(int width, int height, Format format, int mipLevels = 1) override;
    void Upload(Texture& texture, Rect rect, const void* data, int rowTexels = 0) override;
    bool Read(Texture& texture, Rect rect, void* out) override;
    void GenerateMipmaps(Texture& texture) override;
    void Blit(Texture& source, Rect from, Texture& target, Rect to, Filter filter) override;
    void Clear(Texture& texture, float r, float g, float b, float a) override;
    std::unique_ptr<Program> CreateProgram(const std::string& vertex, const std::string& fragment,
                                           std::string& log) override;
    void Draw(Program& program, Texture& target, const DrawState& state) override;
    void Flush() override;
    void Finish() override;
    bool CreateTimestamps(int count) override;
    void DestroyTimestamps(bool lost) override;
    void WriteTimestamp(int index) override;
    bool ReadTimestamp(int index, uint64_t& ns) override;

    /// A framebuffer of its own (the window's default one) as a render target: drawn into, read, blitted into; never
    /// sampled. `framebuffer` stays the caller's
    std::unique_ptr<Texture> WrapFramebuffer(GLuint framebuffer, int width, int height, Format format = Format::RGBA8);
    /// A texture of the caller's (`texture` stays its) for the passes to read or draw into
    std::unique_ptr<Texture> WrapTexture(GLuint texture, int width, int height, Format format = Format::RGBA8);
    /// The GL name of a texture of this device (0 for a wrapped framebuffer)
    static GLuint TextureId(Texture& texture);
    /// The framebuffer object a texture of this device renders into
    GLuint FramebufferId(Texture& texture);

private:
    GlDevice() = default;
    GLuint SamplerFor(Sampler sampler, bool integer);

    QOpenGLContext* _context = nullptr;
    GLuint _vao = 0;
    GLuint _quad = 0;
    GLuint _samplers[64] = {};
    int _maxTextureSize = 0;
    // Timer queries (GL 3.3 / ARB_timer_query; not in QOpenGLExtraFunctions, which follows GLES 3.0)
    using GenQueries = void(QOPENGLF_APIENTRYP)(GLsizei, GLuint*);
    using DeleteQueries = void(QOPENGLF_APIENTRYP)(GLsizei, const GLuint*);
    using QueryCounter = void(QOPENGLF_APIENTRYP)(GLuint, GLenum);
    using GetQueryObjectiv = void(QOPENGLF_APIENTRYP)(GLuint, GLenum, GLint*);
    using GetQueryObjectui64v = void(QOPENGLF_APIENTRYP)(GLuint, GLenum, GLuint64*);
    DeleteQueries _deleteQueries = nullptr;
    QueryCounter _queryCounter = nullptr;
    GetQueryObjectiv _queryAvailable = nullptr;
    GetQueryObjectui64v _queryResult = nullptr;
    std::vector<GLuint> _timestamps;
};
}  // namespace gpu
