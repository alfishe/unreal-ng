#include "gldevice.h"

#include <QOpenGLContext>

#include <algorithm>
#include <unordered_map>
#include <vector>

#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

namespace gpu
{
namespace
{
constexpr GLenum kGlTimestamp = 0x8E28;  // GL_TIMESTAMP (GL 3.3, not in the GLES headers)

struct GlFormat
{
    GLenum internal;
    GLenum format;
    GLenum type;
};

// Desktop GL names the GLES headers lack
constexpr GLenum kGlBgra = 0x80E1;                 // GL_BGRA
constexpr GLenum kGlUnsignedInt8888Rev = 0x8367;  // GL_UNSIGNED_INT_8_8_8_8_REV

GlFormat ToGl(Format format)
{
    switch (format)
    {
        case Format::RGBA8:
            return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case Format::SRGBA8:
            return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case Format::RGBA16F:
            return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
        case Format::RGBA32F:
            return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
        case Format::R8UI:
            return {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE};
        case Format::R16UI:
            return {GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT};
        case Format::R16I:
            return {GL_R16I, GL_RED_INTEGER, GL_SHORT};
        case Format::R32UI:
            return {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT};
        case Format::R32I:
            return {GL_R32I, GL_RED_INTEGER, GL_INT};
        case Format::RG16UI:
            return {GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT};
        case Format::RG32I:
            return {GL_RG32I, GL_RG_INTEGER, GL_INT};
        case Format::RG32UI:
            return {GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT};
        case Format::RGBA8UI:
            return {GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE};
        case Format::RGBA16UI:
            return {GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT};
        case Format::RGBA16I:
            return {GL_RGBA16I, GL_RGBA_INTEGER, GL_SHORT};
        case Format::RGBA32UI:
            return {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT};
        case Format::RGBA32I:
            return {GL_RGBA32I, GL_RGBA_INTEGER, GL_INT};
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

bool IsSignedInt(Format format)
{
    return format == Format::R16I || format == Format::R32I || format == Format::RG32I ||
           format == Format::RGBA16I || format == Format::RGBA32I;
}

class GlTexture final : public Texture
{
public:
    GlTexture(QOpenGLExtraFunctions* gl, int width, int height, Format format, int levels)
        : Texture(width, height, format, levels), gl(gl)
    {
    }
    ~GlTexture() override
    {
        if (ownFramebuffer && framebuffer)
            gl->glDeleteFramebuffers(1, &framebuffer);
        if (ownTexture && texture)
            gl->glDeleteTextures(1, &texture);
    }
    QOpenGLExtraFunctions* gl;
    GLuint texture = 0;
    GLuint framebuffer = 0;
    bool ownFramebuffer = true;
    bool ownTexture = true;
};

class GlProgram final : public Program
{
public:
    explicit GlProgram(QOpenGLExtraFunctions* gl) : gl(gl) {}
    ~GlProgram() override
    {
        if (id)
            gl->glDeleteProgram(id);
    }

    GLint Location(const char* name)
    {
        auto it = locations.find(name);
        if (it != locations.end())
            return it->second;
        const GLint location = gl->glGetUniformLocation(id, name);
        locations.emplace(name, location);
        return location;
    }
    bool Has(const char* name) const override { return const_cast<GlProgram*>(this)->Location(name) >= 0; }
    std::vector<std::string> Names() const override
    {
        std::vector<std::string> names;
        GLint count = 0;
        gl->glGetProgramiv(id, GL_ACTIVE_UNIFORMS, &count);
        for (GLint u = 0; u < count; ++u)
        {
            char raw[256];
            GLsizei length = 0;
            GLint size = 0;
            GLenum type = 0;
            gl->glGetActiveUniform(id, static_cast<GLuint>(u), sizeof(raw), &length, &size, &type, raw);
            std::string name(raw, static_cast<size_t>(length));
            if (name.size() > 3 && name.compare(name.size() - 3, 3, "[0]") == 0)
                name.resize(name.size() - 3);
            names.push_back(name);
        }
        return names;
    }
    void SetInts(const char* name, const int32_t* v, int components, int count) override
    {
        const GLint l = Location(name);
        if (l < 0)
            return;
        gl->glUseProgram(id);
        switch (components)
        {
            case 1:
                gl->glUniform1iv(l, count, v);
                break;
            case 2:
                gl->glUniform2iv(l, count, v);
                break;
            case 3:
                gl->glUniform3iv(l, count, v);
                break;
            default:
                gl->glUniform4iv(l, count, v);
                break;
        }
    }
    void SetUints(const char* name, const uint32_t* v, int components, int count) override
    {
        const GLint l = Location(name);
        if (l < 0)
            return;
        gl->glUseProgram(id);
        switch (components)
        {
            case 1:
                gl->glUniform1uiv(l, count, v);
                break;
            case 2:
                gl->glUniform2uiv(l, count, v);
                break;
            case 3:
                gl->glUniform3uiv(l, count, v);
                break;
            default:
                gl->glUniform4uiv(l, count, v);
                break;
        }
    }
    void SetFloats(const char* name, const float* v, int components, int count) override
    {
        const GLint l = Location(name);
        if (l < 0)
            return;
        gl->glUseProgram(id);
        switch (components)
        {
            case 1:
                gl->glUniform1fv(l, count, v);
                break;
            case 2:
                gl->glUniform2fv(l, count, v);
                break;
            case 3:
                gl->glUniform3fv(l, count, v);
                break;
            case 16:
                gl->glUniformMatrix4fv(l, count, GL_FALSE, v);
                break;
            default:
                gl->glUniform4fv(l, count, v);
                break;
        }
    }
    void SetTexture(const char* name, Texture* texture, Sampler sampler) override
    {
        const GLint l = Location(name);
        if (l < 0)
            return;
        auto it = std::find_if(bindings.begin(), bindings.end(), [&](const Slot& s) { return s.location == l; });
        if (it == bindings.end())
        {
            Slot slot;
            slot.location = l;
            slot.unit = static_cast<int>(bindings.size());
            gl->glUseProgram(id);
            gl->glUniform1i(l, slot.unit);
            bindings.push_back(slot);
            it = bindings.end() - 1;
        }
        it->texture = static_cast<GlTexture*>(texture);
        it->sampler = sampler;
    }

    struct Slot
    {
        GLint location = -1;
        int unit = 0;
        GlTexture* texture = nullptr;
        Sampler sampler;
    };
    QOpenGLExtraFunctions* gl;
    GLuint id = 0;
    std::unordered_map<std::string, GLint> locations;
    std::vector<Slot> bindings;
};

GLuint Compile(QOpenGLExtraFunctions* gl, GLenum stage, const std::string& source, std::string& log)
{
    const GLuint shader = gl->glCreateShader(stage);
    const char* text = source.c_str();
    gl->glShaderSource(shader, 1, &text, nullptr);
    gl->glCompileShader(shader);
    GLint ok = GL_FALSE;
    gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        GLint length = 0;
        gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string message(static_cast<size_t>(std::max(length, 1)), '\0');
        gl->glGetShaderInfoLog(shader, length, nullptr, message.data());
        log = std::string(stage == GL_VERTEX_SHADER ? "vertex: " : "fragment: ") + message.c_str();
        gl->glDeleteShader(shader);
        return 0;
    }
    return shader;
}
}  // namespace

std::unique_ptr<GlDevice> GlDevice::Create()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
        return nullptr;
    std::unique_ptr<GlDevice> d(new GlDevice());
    d->_context = context;
    d->initializeOpenGLFunctions();
    d->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &d->_maxTextureSize);
    d->glGenVertexArrays(1, &d->_vao);
    d->glBindVertexArray(d->_vao);
    d->glGenBuffers(1, &d->_quad);
    d->glBindBuffer(GL_ARRAY_BUFFER, d->_quad);
    d->glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    d->glEnableVertexAttribArray(0);
    d->glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    d->glEnableVertexAttribArray(1);
    d->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                             reinterpret_cast<const void*>(4 * sizeof(float)));
    d->glBindBuffer(GL_ARRAY_BUFFER, 0);
    d->glBindVertexArray(0);
    return d;
}

GlDevice::~GlDevice()
{
    if (QOpenGLContext::currentContext() != _context)
        return;  // the context is gone: its objects with it
    DestroyTimestamps(false);
    for (PixelRead& read : _reads)
    {
        if (read.fence)
            glDeleteSync(read.fence);
        if (read.buffer)
            glDeleteBuffers(1, &read.buffer);
    }
    for (GLuint s : _samplers)
        if (s)
            glDeleteSamplers(1, &s);
    if (_quad)
        glDeleteBuffers(1, &_quad);
    if (_vao)
        glDeleteVertexArrays(1, &_vao);
}

std::string GlDevice::Describe() const
{
    auto* self = const_cast<GlDevice*>(this);
    const auto* version = reinterpret_cast<const char*>(self->glGetString(GL_VERSION));
    const auto* renderer = reinterpret_cast<const char*>(self->glGetString(GL_RENDERER));
    return std::string("OpenGL ") + (version ? version : "?") + ", " + (renderer ? renderer : "?");
}

GLuint GlDevice::TextureId(Texture& texture)
{
    return static_cast<GlTexture&>(texture).texture;
}

GLuint GlDevice::FramebufferId(Texture& texture)
{
    auto& t = static_cast<GlTexture&>(texture);
    if (!t.framebuffer && t.texture)
    {
        glGenFramebuffers(1, &t.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    }
    return t.framebuffer;
}

std::unique_ptr<Texture> GlDevice::WrapFramebuffer(GLuint framebuffer, int width, int height, Format format)
{
    auto t = std::make_unique<GlTexture>(static_cast<QOpenGLExtraFunctions*>(this), width, height, format, 1);
    t->framebuffer = framebuffer;
    t->ownFramebuffer = false;
    return t;
}

std::unique_ptr<Texture> GlDevice::WrapTexture(GLuint texture, int width, int height, Format format)
{
    auto t = std::make_unique<GlTexture>(static_cast<QOpenGLExtraFunctions*>(this), width, height, format, 1);
    t->texture = texture;
    t->ownTexture = false;
    return t;
}

std::unique_ptr<Texture> GlDevice::CreateTexture(int width, int height, Format format, int mipLevels)
{
    if (width <= 0 || height <= 0)
        return nullptr;
    auto t = std::make_unique<GlTexture>(static_cast<QOpenGLExtraFunctions*>(this), width, height, format, std::max(1, mipLevels));
    const GlFormat f = ToGl(format);
    glGenTextures(1, &t->texture);
    glBindTexture(GL_TEXTURE_2D, t->texture);
    int w = width;
    int h = height;
    for (int level = 0; level < t->MipLevels(); ++level)
    {
        glTexImage2D(GL_TEXTURE_2D, level, static_cast<GLint>(f.internal), w, h, 0, f.format, f.type, nullptr);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, t->MipLevels() - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR)
        return nullptr;
    Clear(*t, 0, 0, 0, 0);
    if (t->MipLevels() > 1)
        GenerateMipmaps(*t);
    return t;
}

void GlDevice::Upload(Texture& texture, Rect rect, const void* data, int rowTexels)
{
    auto& t = static_cast<GlTexture&>(texture);
    if (!t.texture || rect.Empty() || !data)
        return;
    const GlFormat f = ToGl(t.GetFormat());
    glBindTexture(GL_TEXTURE_2D, t.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowTexels > 0 ? rowTexels : 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, rect.y, rect.width, rect.height, f.format, f.type, data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool GlDevice::Read(Texture& texture, Rect rect, void* out)
{
    if (rect.Empty() || !out)
        return false;
    const GlFormat f = ToGl(texture.GetFormat());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, FramebufferId(texture));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(rect.x, rect.y, rect.width, rect.height, f.format, f.type, out);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return glGetError() == GL_NO_ERROR;
}

bool GlDevice::BeginRead(int slot, Texture& texture, Rect rect, bool bgra)
{
    if (slot < 0 || slot >= kReadSlots || rect.Empty())
        return false;
    PixelRead& read = _reads[slot];
    if (read.fence)
    {
        glDeleteSync(read.fence);  // not taken: dropped
        read.fence = nullptr;
    }
    read.texel = BytesPerTexel(texture.GetFormat());
    read.rect = rect;
    const auto bytes = static_cast<GLsizeiptr>(rect.width) * rect.height * read.texel;
    if (!read.buffer)
        glGenBuffers(1, &read.buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, read.buffer);
    if (read.capacity < bytes)
    {
        glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
        read.capacity = bytes;
    }
    GlFormat f = ToGl(texture.GetFormat());
    if (bgra && texture.GetFormat() == Format::RGBA8)
    {
        // The order macOS reads without converting (desktop GL 1.2+ formats; the context is 3.3 core)
        f.format = kGlBgra;
        f.type = kGlUnsignedInt8888Rev;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, FramebufferId(texture));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(rect.x, rect.y, rect.width, rect.height, f.format, f.type, nullptr);  // into the buffer: no wait
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (glGetError() != GL_NO_ERROR)
        return false;
    read.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();  // on its way now: done when it is taken a frame later
    return read.fence != nullptr;
}

bool GlDevice::FinishRead(int slot, uint8_t* out, size_t outStride, bool flip)
{
    if (slot < 0 || slot >= kReadSlots || !_reads[slot].fence || !out)
        return false;
    PixelRead& read = _reads[slot];
    // Started a frame ago: done, or nearly. A second at most (a GPU that stopped)
    const GLenum waited = glClientWaitSync(read.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000'000);
    glDeleteSync(read.fence);
    read.fence = nullptr;
    if (waited != GL_ALREADY_SIGNALED && waited != GL_CONDITION_SATISFIED)
        return false;
    const size_t rowBytes = static_cast<size_t>(read.rect.width) * read.texel;
    const auto bytes = static_cast<GLsizeiptr>(rowBytes * read.rect.height);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, read.buffer);
    const auto* mapped = static_cast<const uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT));
    if (mapped)
    {
        CopyRows(mapped, rowBytes, read.rect.height, out, outStride, flip);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return mapped != nullptr;
}

void GlDevice::GenerateMipmaps(Texture& texture)
{
    auto& t = static_cast<GlTexture&>(texture);
    if (!t.texture || t.MipLevels() < 2)
        return;
    glBindTexture(GL_TEXTURE_2D, t.texture);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GlDevice::Blit(Texture& source, Rect from, Texture& target, Rect to, Filter filter)
{
    if (from.Empty() || to.width <= 0 || to.height == 0)
        return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, FramebufferId(source));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, FramebufferId(target));
    const GLint y0 = to.height > 0 ? to.y : to.y - to.height;
    const GLint y1 = to.height > 0 ? to.y + to.height : to.y;
    const bool linear = filter == Filter::Linear && !IsInteger(source.GetFormat());
    glBlitFramebuffer(from.x, from.y, from.x + from.width, from.y + from.height, to.x, y0, to.x + to.width, y1,
                      GL_COLOR_BUFFER_BIT, linear ? GL_LINEAR : GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlDevice::Clear(Texture& texture, float r, float g, float b, float a)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, FramebufferId(texture));
    glDisable(GL_SCISSOR_TEST);
    if (!IsInteger(texture.GetFormat()))
    {
        const GLfloat v[4] = {r, g, b, a};
        glClearBufferfv(GL_COLOR, 0, v);
    }
    else if (IsSignedInt(texture.GetFormat()))
    {
        const GLint v[4] = {static_cast<GLint>(r), static_cast<GLint>(g), static_cast<GLint>(b),
                            static_cast<GLint>(a)};
        glClearBufferiv(GL_COLOR, 0, v);
    }
    else
    {
        const GLuint v[4] = {static_cast<GLuint>(r), static_cast<GLuint>(g), static_cast<GLuint>(b),
                             static_cast<GLuint>(a)};
        glClearBufferuiv(GL_COLOR, 0, v);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

std::unique_ptr<Program> GlDevice::CreateProgram(const std::string& vertex, const std::string& fragment,
                                                 std::string& log)
{
    const GLuint vs = Compile(this, GL_VERTEX_SHADER, vertex, log);
    if (!vs)
        return nullptr;
    const GLuint fs = Compile(this, GL_FRAGMENT_SHADER, fragment, log);
    if (!fs)
    {
        glDeleteShader(vs);
        return nullptr;
    }
    auto p = std::make_unique<GlProgram>(static_cast<QOpenGLExtraFunctions*>(this));
    p->id = glCreateProgram();
    glAttachShader(p->id, vs);
    glAttachShader(p->id, fs);
    // The slang layout for sources without explicit locations
    glBindAttribLocation(p->id, 0, "Position");
    glBindAttribLocation(p->id, 1, "TexCoord");
    glLinkProgram(p->id);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(p->id, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        GLint length = 0;
        glGetProgramiv(p->id, GL_INFO_LOG_LENGTH, &length);
        std::string message(static_cast<size_t>(std::max(length, 1)), '\0');
        glGetProgramInfoLog(p->id, length, nullptr, message.data());
        log = std::string("link: ") + message.c_str();
        return nullptr;
    }
    return p;
}

GLuint GlDevice::SamplerFor(Sampler s, bool integer)
{
    const Filter filter = integer ? Filter::Nearest : s.filter;
    const bool mipmap = s.mipmap && !integer;
    const int index = (static_cast<int>(filter) * 4 + static_cast<int>(s.wrap)) * 2 + (mipmap ? 1 : 0);
    GLuint& sampler = _samplers[index];
    if (sampler)
        return sampler;
    glGenSamplers(1, &sampler);
    const GLint mag = filter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
    const GLint min = mipmap ? (filter == Filter::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : mag;
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, mag);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, min);
    GLint wrap = GL_CLAMP_TO_EDGE;
    switch (s.wrap)
    {
        case Wrap::ClampToEdge:
            wrap = GL_CLAMP_TO_EDGE;
            break;
        case Wrap::ClampToBorder:
            wrap = GL_CLAMP_TO_BORDER;
            break;
        case Wrap::Repeat:
            wrap = GL_REPEAT;
            break;
        case Wrap::MirroredRepeat:
            wrap = GL_MIRRORED_REPEAT;
            break;
    }
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, wrap);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, wrap);
    return sampler;
}

void GlDevice::Draw(Program& program, Texture& target, const DrawState& state)
{
    if (state.viewport.Empty())
        return;
    auto& p = static_cast<GlProgram&>(program);
    glBindFramebuffer(GL_FRAMEBUFFER, FramebufferId(target));
    glViewport(state.viewport.x, state.viewport.y, state.viewport.width, state.viewport.height);
    if (state.scissor)
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(state.scissorRect.x, state.scissorRect.y, std::max(0, state.scissorRect.width),
                  std::max(0, state.scissorRect.height));
    }
    else
        glDisable(GL_SCISSOR_TEST);
    const bool integerTarget = IsInteger(target.GetFormat());
    if (state.blend == Blend::Premultiplied && !integerTarget)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
        glDisable(GL_BLEND);
    if (target.GetFormat() == Format::SRGBA8)
        glEnable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(p.id);
    for (const GlProgram::Slot& slot : p.bindings)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot.unit));
        GlTexture* t = slot.texture && slot.texture != &target ? slot.texture : nullptr;
        glBindTexture(GL_TEXTURE_2D, t ? t->texture : 0);
        glBindSampler(static_cast<GLuint>(slot.unit), t ? SamplerFor(slot.sampler, IsInteger(t->GetFormat())) : 0);
    }
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    for (const GlProgram::Slot& slot : p.bindings)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot.unit));
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindSampler(static_cast<GLuint>(slot.unit), 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    if (target.GetFormat() == Format::SRGBA8)
        glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlDevice::Flush()
{
    glFlush();
}

bool GlDevice::CreateTimestamps(int count)
{
    DestroyTimestamps(false);
    if (count <= 0 || _context->isOpenGLES())
        return false;
    if (_context->format().version() < qMakePair(3, 3) && !_context->hasExtension("GL_ARB_timer_query"))
        return false;
    const auto gen = reinterpret_cast<GenQueries>(_context->getProcAddress("glGenQueries"));
    _deleteQueries = reinterpret_cast<DeleteQueries>(_context->getProcAddress("glDeleteQueries"));
    _queryCounter = reinterpret_cast<QueryCounter>(_context->getProcAddress("glQueryCounter"));
    _queryAvailable = reinterpret_cast<GetQueryObjectiv>(_context->getProcAddress("glGetQueryObjectiv"));
    _queryResult = reinterpret_cast<GetQueryObjectui64v>(_context->getProcAddress("glGetQueryObjectui64v"));
    if (!gen || !_deleteQueries || !_queryCounter || !_queryAvailable || !_queryResult)
        return false;
    // A GL that has the calls but no clock behind them (Apple's GL over Metal reads 0 for every timestamp)
    // measures nothing: none, rather than zeros
    using GetInteger64v = void(QOPENGLF_APIENTRYP)(GLenum, GLint64*);
    const auto now = reinterpret_cast<GetInteger64v>(_context->getProcAddress("glGetInteger64v"));
    GLint64 clock = 0;
    if (now)
        now(kGlTimestamp, &clock);
    if (clock == 0)
        return false;
    _timestamps.resize(static_cast<size_t>(count));
    gen(count, _timestamps.data());
    return true;
}

void GlDevice::DestroyTimestamps(bool lost)
{
    if (!_timestamps.empty() && !lost && QOpenGLContext::currentContext() == _context)
        _deleteQueries(static_cast<GLsizei>(_timestamps.size()), _timestamps.data());
    _timestamps.clear();
}

void GlDevice::WriteTimestamp(int index)
{
    if (index >= 0 && static_cast<size_t>(index) < _timestamps.size())
        _queryCounter(_timestamps[static_cast<size_t>(index)], kGlTimestamp);
}

bool GlDevice::ReadTimestamp(int index, uint64_t& ns)
{
    ns = 0;
    if (index < 0 || static_cast<size_t>(index) >= _timestamps.size())
        return false;
    const GLuint query = _timestamps[static_cast<size_t>(index)];
    GLint available = 0;
    _queryAvailable(query, 0x8867 /* GL_QUERY_RESULT_AVAILABLE */, &available);
    if (!available)
        return false;
    GLuint64 value = 0;
    _queryResult(query, 0x8866 /* GL_QUERY_RESULT */, &value);
    ns = value;
    return true;
}

void GlDevice::Finish()
{
    glFinish();
}
}  // namespace gpu
