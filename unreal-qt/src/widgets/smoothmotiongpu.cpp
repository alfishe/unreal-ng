#include "smoothmotiongpu.h"

#include <QOpenGLContext>

#include <algorithm>

using smoothmotion::MotionField;
using smoothmotion::Picture;

namespace
{
// The quad over the viewport from the vertex index (a strip). Fragment (x, y) is pixel (x, y), rows top first as the
// pictures are uploaded and read back: nothing is turned over
const char* const kVertexShader = R"(
#version 330
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The reference's helpers (smoothmotion.cpp): Picture::At clamped to the edges, Shift = (w c + 128) >> 8 (GLSL shifts
// a signed int arithmetically, as C++20 does)
const char* const kCommon = R"(
#version 330
uniform ivec2 size;
uniform usampler2D frameA;
uniform usampler2D frameB;
uvec4 At(usampler2D t, ivec2 p) { return texelFetch(t, clamp(p, ivec2(0), size - 1), 0); }
int Shift(int w, int c) { return (w * c + 128) >> 8; }
bool Same(uvec4 a, uvec4 b) { return all(equal(a, b)); }
uvec4 Mix(uvec4 a, uvec4 b, int w) { return (a * uint(256 - w) + b * uint(w) + 128u) >> 8u; }
)";

const char* const kBlendShader = R"(
uniform int weight;
out uvec4 color;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    color = Mix(texelFetch(frameA, p, 0), texelFetch(frameB, p, 0), weight);
}
)";

// MotionField::Estimate's cost: the pixels of block `blk` where A and B warped to the middle by v disagree, counted up
// to limit + 1
const char* const kCost = R"(
const int kBlock = 8;
const int kAccept = 4;
int Cost(ivec2 blk, ivec2 v, int limit) {
    ivec2 s = ivec2(Shift(128, v.x), Shift(128, v.y));
    ivec2 ka = -s;
    ivec2 kb = v - s;
    ivec2 p0 = blk * kBlock;
    ivec2 p1 = min(p0 + kBlock, size);
    int bad = 0;
    for (int y = p0.y; y < p1.y; y++)
        for (int x = p0.x; x < p1.x; x++)
            if (!Same(At(frameA, ivec2(x, y) + ka), At(frameB, ivec2(x, y) + kb)) && ++bad > limit)
                return bad;
    return bad;
}
)";

// Pass 1: the first vector of the search order with the fewest mismatches, at most kAccept
const char* const kSearchShader = R"(
uniform isampler2D order;
uniform int orderCount;
out ivec4 result;
void main() {
    ivec2 blk = ivec2(gl_FragCoord.xy);
    int best = kAccept + 1;
    ivec2 bestV = ivec2(0);
    for (int i = 0; i < orderCount; i++) {
        ivec2 v = texelFetch(order, ivec2(i, 0), 0).xy;
        int bad = Cost(blk, v, best - 1);
        if (bad < best) {
            best = bad;
            bestV = v;
            if (best == 0)
                break;
        }
    }
    bool matched = best <= kAccept;
    result = ivec4(matched ? bestV : ivec2(0), matched ? 1 : 0, 0);
}
)";

// A refinement pass from the previous one: zero, the votes, the eight neighbours' (row by row), the first of the
// fewest mismatches within kAccept; none: pass 1's
const char* const kRefineShader = R"(
uniform isampler2D prevField;
uniform isampler2D firstField;
uniform ivec2 blocks;
uniform ivec2 votes[4];
uniform int voteCount;
out ivec4 result;
ivec2 cands[13];
int n = 0;
void Add(ivec2 v) {
    for (int c = 0; c < n; c++)
        if (cands[c] == v)
            return;
    cands[n++] = v;
}
void main() {
    ivec2 blk = ivec2(gl_FragCoord.xy);
    Add(ivec2(0));
    for (int k = 0; k < voteCount; k++)
        Add(votes[k]);
    for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
            ivec2 q = blk + ivec2(di, dj);
            if ((di != 0 || dj != 0) && all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, blocks))) {
                ivec4 b = texelFetch(prevField, q, 0);
                if (b.z != 0)
                    Add(b.xy);
            }
        }
    int best = kAccept + 1;
    ivec2 bestV = ivec2(0);
    for (int c = 0; c < n && best > 0; c++) {
        int bad = Cost(blk, cands[c], best - 1);
        if (bad < best) {
            best = bad;
            bestV = cands[c];
        }
    }
    result = best <= kAccept ? ivec4(bestV, 1, 0) : texelFetch(firstField, blk, 0);
}
)";

// MotionField::Compose per pixel: of the candidates (own, zero, left, right, up, down, the diagonals, the common
// vectors) the one the warped A and B agree on with the most of the 3x3 around, the first of equals; none: the blend,
// or B past it. Extrapolating, a moving vector only draws pixels whose sources changed
const char* const kComposeShader = R"(
uniform isampler2D field;
uniform ivec2 blocks;
uniform ivec2 commonVectors[2];
uniform int commonCount;
uniform int weight;
uniform bool useMask;
uniform usampler2D mask;
uniform usampler2D base;
out uvec4 color;
ivec2 cands[12];
int n = 0;
void Add(ivec2 v) {
    for (int c = 0; c < n; c++)
        if (cands[c] == v)
            return;
    cands[n++] = v;
}
void AddBlock(ivec2 q) {
    if (all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, blocks))) {
        ivec4 b = texelFetch(field, q, 0);
        if (b.z != 0)
            Add(b.xy);
    }
}
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (useMask && texelFetch(mask, p, 0).r == 0u) {
        color = texelFetch(base, p, 0);
        return;
    }
    ivec2 blk = p / 8;
    AddBlock(blk);
    Add(ivec2(0));
    AddBlock(blk + ivec2(-1, 0));
    AddBlock(blk + ivec2(1, 0));
    AddBlock(blk + ivec2(0, -1));
    AddBlock(blk + ivec2(0, 1));
    AddBlock(blk + ivec2(-1, -1));
    AddBlock(blk + ivec2(1, -1));
    AddBlock(blk + ivec2(-1, 1));
    AddBlock(blk + ivec2(1, 1));
    for (int k = 0; k < commonCount; k++)
        Add(commonVectors[k]);
    uvec4 pixel = uvec4(0u);
    int bestScore = 0;
    for (int c = 0; c < n; c++) {
        ivec2 v = cands[c];
        ivec2 s = ivec2(Shift(weight, v.x), Shift(weight, v.y));
        ivec2 sa = p - s;
        ivec2 sb = p + v - s;
        uvec4 va = At(frameA, sa);
        if (!Same(va, At(frameB, sb)))
            continue;
        if (weight > 256 && v != ivec2(0) && Same(At(frameA, sa), At(frameB, sa)) && Same(At(frameA, sb), At(frameB, sb)))
            continue;
        int score = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (Same(At(frameA, sa + ivec2(dx, dy)), At(frameB, sb + ivec2(dx, dy))))
                    score++;
        if (score > bestScore) {
            bestScore = score;
            pixel = va;
        }
    }
    if (bestScore > 0)
        color = pixel;
    else
        color = weight > 256 ? texelFetch(frameB, p, 0) : Mix(texelFetch(frameA, p, 0), texelFetch(frameB, p, 0), weight);
}
)";

QOpenGLShaderProgram* Build(const QString& fragment, QString& log)
{
    auto* program = new QOpenGLShaderProgram();
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
        !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment) || !program->link())
    {
        log += program->log();
        delete program;
        return nullptr;
    }
    return program;
}
}  // namespace

SmoothMotionGPU::~SmoothMotionGPU()
{
    // GL objects need their context: destroy() with it current; here only what is left in CPU memory
    delete _blend;
    delete _search;
    delete _refine;
    delete _compose;
}

bool SmoothMotionGPU::create()
{
    destroy();
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context)
    {
        _log = "no current context";
        return false;
    }
    _log.clear();
    const QString common = QString::fromLatin1(kCommon);
    _blend = Build(common + kBlendShader, _log);
    _search = Build(common + kCost + kSearchShader, _log);
    _refine = Build(common + kCost + kRefineShader, _log);
    _compose = Build(common + kComposeShader, _log);
    if (!_blend || !_search || !_refine || !_compose || !_vao.create())
    {
        destroy();
        return false;
    }
    _gl = context->extraFunctions();
    _gl->glGenFramebuffers(1, &_framebuffer);
    _gl->glGenTextures(TextureCount, _textures);
    for (auto& size : _sizes)
        size[0] = size[1] = 0;
    // Every texture complete from the start: a sampler the pass does not read (the mask without one) still has one
    const int32_t zero[4] = {};
    for (int t = 0; t < TextureCount; t++)
    {
        const auto texture = static_cast<Texture>(t);
        if (texture == Mask)
            allocate(texture, 1, 1, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, zero);
        else if (texture == Order)
            allocate(texture, 1, 1, GL_RG32I, GL_RG_INTEGER, GL_INT, zero);
        else if (texture == FieldOne || texture == FieldPing || texture == FieldPong)
            allocate(texture, 1, 1, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, zero);
        else
            allocate(texture, 1, 1, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, zero);
    }
    _orderRange = -1;
    return true;
}

void SmoothMotionGPU::destroy()
{
    if (_gl)
    {
        _gl->glDeleteTextures(TextureCount, _textures);
        _gl->glDeleteFramebuffers(1, &_framebuffer);
    }
    for (unsigned& texture : _textures)
        texture = 0;
    _framebuffer = 0;
    delete _blend;
    delete _search;
    delete _refine;
    delete _compose;
    _blend = _search = _refine = _compose = nullptr;
    if (_vao.isCreated())
        _vao.destroy();
    _gl = nullptr;
}

void SmoothMotionGPU::allocate(Texture texture, int width, int height, unsigned internalFormat, unsigned format,
                               unsigned type, const void* data)
{
    _gl->glActiveTexture(GL_TEXTURE0);
    _gl->glBindTexture(GL_TEXTURE_2D, _textures[texture]);
    _gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    _gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    if (_sizes[texture][0] != width || _sizes[texture][1] != height)
    {
        // Integer textures are complete with nearest sampling and no mipmaps only
        _gl->glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format, type, data);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        _sizes[texture][0] = width;
        _sizes[texture][1] = height;
    }
    else if (data)
    {
        _gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, type, data);
    }
}

void SmoothMotionGPU::uploadPicture(Texture texture, const Picture& picture)
{
    allocate(texture, picture.width, picture.height, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, picture.pixels);
}

void SmoothMotionGPU::uploadMask(const uint8_t* mask, int width, int height)
{
    allocate(Mask, width, height, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, mask);
}

void SmoothMotionGPU::bindTexture(int unit, Texture texture)
{
    _gl->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    _gl->glBindTexture(GL_TEXTURE_2D, _textures[texture]);
}

bool SmoothMotionGPU::drawInto(QOpenGLShaderProgram* program, Texture target, int width, int height)
{
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
    _gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _textures[target], 0);
    const bool complete = _gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete)
    {
        _gl->glViewport(0, 0, width, height);
        _gl->glDisable(GL_BLEND);
        _gl->glDisable(GL_SCISSOR_TEST);
        _gl->glDisable(GL_DITHER);
        _gl->glUniform2i(program->uniformLocation("size"), _sizes[FrameA][0], _sizes[FrameA][1]);
        _vao.bind();
        _gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        _vao.release();
    }
    else
    {
        _log = "incomplete framebuffer";
    }
    return complete;
}

bool SmoothMotionGPU::readField(int blocksWide, int blocksHigh, std::vector<MotionField::Block>& out)
{
    // From the target drawInto left bound
    std::vector<int32_t> values(static_cast<size_t>(blocksWide) * blocksHigh * 4);
    _gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    _gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    _gl->glReadPixels(0, 0, blocksWide, blocksHigh, GL_RGBA_INTEGER, GL_INT, values.data());
    out.resize(static_cast<size_t>(blocksWide) * blocksHigh);
    for (size_t i = 0; i < out.size(); i++)
    {
        out[i].v = {values[i * 4], values[i * 4 + 1]};
        out[i].matched = values[i * 4 + 2] != 0;
    }
    return _gl->glGetError() == GL_NO_ERROR;
}

bool SmoothMotionGPU::readPicture(int width, int height, uint32_t* out)
{
    // An unsigned integer buffer reads as RGBA_INTEGER / UNSIGNED_INT everywhere (bytes are not guaranteed)
    _readBack.resize(static_cast<size_t>(width) * height * 4);
    _gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    _gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    _gl->glReadPixels(0, 0, width, height, GL_RGBA_INTEGER, GL_UNSIGNED_INT, _readBack.data());
    auto* bytes = reinterpret_cast<uint8_t*>(out);
    for (size_t i = 0; i < _readBack.size(); i++)
        bytes[i] = static_cast<uint8_t>(_readBack[i]);
    return _gl->glGetError() == GL_NO_ERROR;
}

bool SmoothMotionGPU::Estimate(const Picture& a, const Picture& b, int range, MotionField& field)
{
    if (!_gl || a.width <= 0 || a.height <= 0)
        return false;
    GLint previousFramebuffer = 0;
    GLint viewport[4] = {};
    _gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    _gl->glGetIntegerv(GL_VIEWPORT, viewport);
    while (_gl->glGetError() != GL_NO_ERROR)
    {
    }

    const int blocksWide = (a.width + MotionField::kBlock - 1) / MotionField::kBlock;
    const int blocksHigh = (a.height + MotionField::kBlock - 1) / MotionField::kBlock;
    uploadPicture(FrameA, a);
    uploadPicture(FrameB, b);
    if (_orderRange != range)
    {
        const std::vector<MotionField::Vector>& order = MotionField::SearchOrder(range);
        std::vector<int32_t> values;
        for (const MotionField::Vector& v : order)
        {
            values.push_back(v.x);
            values.push_back(v.y);
        }
        _sizes[Order][0] = 0;  // a new length: allocated again
        allocate(Order, static_cast<int>(order.size()), 1, GL_RG32I, GL_RG_INTEGER, GL_INT, values.data());
        _orderRange = range;
        _orderCount = static_cast<int>(order.size());
    }
    for (Texture t : {FieldOne, FieldPing, FieldPong})
        allocate(t, blocksWide, blocksHigh, GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, nullptr);

    bool ok = true;
    // Pass 1
    _search->bind();
    bindTexture(0, FrameA);
    bindTexture(1, FrameB);
    bindTexture(2, Order);
    _search->setUniformValue("frameA", 0);
    _search->setUniformValue("frameB", 1);
    _search->setUniformValue("order", 2);
    _search->setUniformValue("orderCount", _orderCount);
    ok = drawInto(_search, FieldOne, blocksWide, blocksHigh);
    std::vector<MotionField::Block> prev;
    ok = ok && readField(blocksWide, blocksHigh, prev);
    _search->release();

    // The refinement passes, the votes counted on the CPU between them
    Texture source = FieldOne;
    Texture target = FieldPing;
    for (int pass = 0; ok && pass < MotionField::kRefinePasses; pass++)
    {
        const std::vector<MotionField::Vector> votes =
            MotionField::CommonVectors(prev, MotionField::kGlobalVectors, range);
        GLint packed[2 * MotionField::kGlobalVectors] = {};
        for (size_t k = 0; k < votes.size(); k++)
        {
            packed[k * 2] = votes[k].x;
            packed[k * 2 + 1] = votes[k].y;
        }
        _refine->bind();
        bindTexture(0, FrameA);
        bindTexture(1, FrameB);
        bindTexture(2, source);
        bindTexture(3, FieldOne);
        _refine->setUniformValue("frameA", 0);
        _refine->setUniformValue("frameB", 1);
        _refine->setUniformValue("prevField", 2);
        _refine->setUniformValue("firstField", 3);
        _gl->glUniform2i(_refine->uniformLocation("blocks"), blocksWide, blocksHigh);
        _refine->setUniformValue("voteCount", static_cast<int>(votes.size()));
        _gl->glUniform2iv(_refine->uniformLocation("votes"), static_cast<GLsizei>(MotionField::kGlobalVectors), packed);
        ok = drawInto(_refine, target, blocksWide, blocksHigh) && readField(blocksWide, blocksHigh, prev);
        _refine->release();
        source = target;
        target = target == FieldPing ? FieldPong : FieldPing;
    }

    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    if (!ok)
        return false;
    field.Assign(blocksWide, blocksHigh, std::move(prev), range);
    return true;
}

bool SmoothMotionGPU::Blend(const Picture& a, const Picture& b, int w, uint32_t* out)
{
    if (!_gl || a.width <= 0 || a.height <= 0 || !out)
        return false;
    GLint previousFramebuffer = 0;
    GLint viewport[4] = {};
    _gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    _gl->glGetIntegerv(GL_VIEWPORT, viewport);
    while (_gl->glGetError() != GL_NO_ERROR)
    {
    }
    uploadPicture(FrameA, a);
    uploadPicture(FrameB, b);
    allocate(Output, a.width, a.height, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
    _blend->bind();
    bindTexture(0, FrameA);
    bindTexture(1, FrameB);
    _blend->setUniformValue("frameA", 0);
    _blend->setUniformValue("frameB", 1);
    _blend->setUniformValue("weight", w);
    const bool ok = drawInto(_blend, Output, a.width, a.height) && readPicture(a.width, a.height, out);
    _blend->release();
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    return ok;
}

bool SmoothMotionGPU::Compose(const MotionField& field, const Picture& a, const Picture& b, int w, uint32_t* out,
                              const uint8_t* mask)
{
    if (!_gl || a.width <= 0 || a.height <= 0 || !out || field.BlocksWide() <= 0)
        return false;
    GLint previousFramebuffer = 0;
    GLint viewport[4] = {};
    _gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    _gl->glGetIntegerv(GL_VIEWPORT, viewport);
    while (_gl->glGetError() != GL_NO_ERROR)
    {
    }
    uploadPicture(FrameA, a);
    uploadPicture(FrameB, b);
    // The field as the CPU holds it (a field estimated on the CPU, or this one's read back)
    std::vector<int32_t> values;
    values.reserve(static_cast<size_t>(field.BlocksWide()) * field.BlocksHigh() * 4);
    for (int j = 0; j < field.BlocksHigh(); j++)
        for (int i = 0; i < field.BlocksWide(); i++)
        {
            const MotionField::Block& block = field.At(i, j);
            values.insert(values.end(), {block.v.x, block.v.y, block.matched ? 1 : 0, 0});
        }
    allocate(FieldOne, field.BlocksWide(), field.BlocksHigh(), GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, values.data());
    if (mask)
    {
        uploadMask(mask, a.width, a.height);
        uploadPicture(Base, Picture{a.width, a.height, out});
    }
    allocate(Output, a.width, a.height, GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);

    GLint common[2 * MotionField::kComposeCommon] = {};
    const std::vector<MotionField::Vector>& vectors = field.Common();
    const size_t commonCount = std::min(vectors.size(), MotionField::kComposeCommon);
    for (size_t k = 0; k < commonCount; k++)
    {
        common[k * 2] = vectors[k].x;
        common[k * 2 + 1] = vectors[k].y;
    }
    _compose->bind();
    bindTexture(0, FrameA);
    bindTexture(1, FrameB);
    bindTexture(2, FieldOne);
    bindTexture(3, Mask);
    bindTexture(4, Base);
    _compose->setUniformValue("frameA", 0);
    _compose->setUniformValue("frameB", 1);
    _compose->setUniformValue("field", 2);
    _compose->setUniformValue("mask", 3);
    _compose->setUniformValue("base", 4);
    _compose->setUniformValue("useMask", static_cast<GLint>(mask != nullptr));
    _gl->glUniform2i(_compose->uniformLocation("blocks"), field.BlocksWide(), field.BlocksHigh());
    _compose->setUniformValue("weight", w);
    _compose->setUniformValue("commonCount", static_cast<int>(commonCount));
    _gl->glUniform2iv(_compose->uniformLocation("commonVectors"), static_cast<GLsizei>(MotionField::kComposeCommon), common);
    const bool ok = drawInto(_compose, Output, a.width, a.height) && readPicture(a.width, a.height, out);
    _compose->release();
    _gl->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    _gl->glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    _gl->glActiveTexture(GL_TEXTURE0);
    return ok;
}
