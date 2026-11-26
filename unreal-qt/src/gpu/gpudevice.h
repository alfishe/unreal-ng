#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// @brief The screen's GPU layer (docs/inprogress/2026-11-19-vulkan-renderer): what the picture passes need from a
/// GPU, with a GL 3.3 core backend (GlDevice) and a Vulkan one (VkDevice). The passes are written once against it.
///
/// Rows: GL's memory order on both backends. Row 0 of every texture is GL's row 0 (the bottom of a GL framebuffer),
/// a Rect's y counts from it, texture coordinate t = 0 is row 0, NDC y = -1 draws into row 0 and gl_FragCoord.y is 0.5
/// there: the same shaders give the same bytes. Only the window's last step turns the picture over for a swapchain.
///
/// Shaders: GLSL 330 sources as GL compiles them; the Vulkan backend compiles them with glslang's relaxed Vulkan
/// rules (loose uniforms in one block, bindings and locations mapped, gl_VertexID as gl_VertexIndex).
///
/// Every draw is the 4-vertex strip of one quad: attribute 0 = vec4 position (x, y in 0..1, 0, 1), attribute 1 = vec2
/// texture coordinate (= x, y); corners (0, 0), (1, 0), (0, 1), (1, 1). A shader reads what it declares.
///
/// Threads: one device, one thread at a time (the GL backend: its context current).
namespace gpu
{
enum class Format : uint8_t
{
    RGBA8,    ///< unsigned normalised
    SRGBA8,   ///< sRGB: reads decode, a target encodes
    RGBA16F,
    RGBA32F,
    R8UI,
    R16UI,
    R16I,
    R32UI,
    R32I,
    RG16UI,
    RG32I,
    RG32UI,
    RGBA8UI,
    RGBA16UI,
    RGBA16I,
    RGBA32UI,
    RGBA32I,
};
/// Bytes of one texel in memory (uploads and readbacks are tight in these)
int BytesPerTexel(Format format);
bool IsInteger(Format format);
const char* FormatName(Format format);

enum class Filter : uint8_t
{
    Nearest,
    Linear,
};
enum class Wrap : uint8_t
{
    ClampToEdge,
    ClampToBorder,  ///< transparent black
    Repeat,
    MirroredRepeat,
};
struct Sampler
{
    Filter filter = Filter::Nearest;
    Wrap wrap = Wrap::ClampToEdge;
    bool mipmap = false;  ///< trilinear (linear) / nearest mip (nearest) over the texture's levels
};

/// Pixels of a texture, GL rows (y from row 0)
struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool Empty() const { return width <= 0 || height <= 0; }
};

class Texture
{
public:
    virtual ~Texture() = default;
    int Width() const { return _width; }
    int Height() const { return _height; }
    Format GetFormat() const { return _format; }
    int MipLevels() const { return _mipLevels; }

protected:
    Texture(int width, int height, Format format, int mipLevels)
        : _width(width), _height(height), _format(format), _mipLevels(mipLevels)
    {
    }

private:
    int _width;
    int _height;
    Format _format;
    int _mipLevels;
};

/// A linked vertex + fragment program. Uniforms and samplers are set by name; a name the program does not use is
/// ignored (as GL's location -1). Values stay until set again
class Program
{
public:
    virtual ~Program() = default;
    virtual bool Has(const char* name) const = 0;
    /// The uniforms and samplers the program uses (arrays without "[0]")
    virtual std::vector<std::string> Names() const = 0;
    /// int .. ivec4 (`components` 1..4), bool as int; `count` > 1: an array from its first element
    virtual void SetInts(const char* name, const int32_t* values, int components, int count = 1) = 0;
    virtual void SetUints(const char* name, const uint32_t* values, int components, int count = 1) = 0;
    /// float .. vec4 (`components` 1..4), mat4 (16, column major)
    virtual void SetFloats(const char* name, const float* values, int components, int count = 1) = 0;
    /// The sampler `name` reads `texture` (null: none) with `sampler`
    virtual void SetTexture(const char* name, Texture* texture, Sampler sampler = {}) = 0;

    void SetInt(const char* name, int32_t v) { SetInts(name, &v, 1); }
    void SetInt2(const char* name, int32_t a, int32_t b)
    {
        const int32_t v[2] = {a, b};
        SetInts(name, v, 2);
    }
    void SetInt4(const char* name, int32_t a, int32_t b, int32_t c, int32_t d)
    {
        const int32_t v[4] = {a, b, c, d};
        SetInts(name, v, 4);
    }
    void SetFloat(const char* name, float v) { SetFloats(name, &v, 1); }
    void SetFloat2(const char* name, float a, float b)
    {
        const float v[2] = {a, b};
        SetFloats(name, v, 2);
    }
    void SetFloat4(const char* name, float a, float b, float c, float d)
    {
        const float v[4] = {a, b, c, d};
        SetFloats(name, v, 4);
    }
};

enum class Blend : uint8_t
{
    None,
    Premultiplied,  ///< src * 1 + dst * (1 - src alpha): QPainter's images over the picture
};

struct DrawState
{
    Rect viewport;        ///< in the target
    bool scissor = false;
    Rect scissorRect;
    Blend blend = Blend::None;
};

class Device
{
public:
    enum class Backend
    {
        GL,
        Vulkan,
    };
    virtual ~Device() = default;
    virtual Backend GetBackend() const = 0;
    /// One line for the log: the backend, the GPU, the driver
    virtual std::string Describe() const = 0;

    /// The largest width or height of a texture
    virtual int MaxTextureSize() const = 0;
    /// A texture (any of them a render target), its content zero. nullptr: the size or format refused
    virtual std::unique_ptr<Texture> CreateTexture(int width, int height, Format format, int mipLevels = 1) = 0;
    /// `rect` of level 0 from `data`: rows `rowTexels` apart (0: tight), the first row is rect's row y
    virtual void Upload(Texture& texture, Rect rect, const void* data, int rowTexels = 0) = 0;
    /// `rect` of level 0 into `out`, tight, the first row is rect's row y. Waits for the GPU
    virtual bool Read(Texture& texture, Rect rect, void* out) = 0;
    /// Reads without waiting (the window recording's pictures): `rect` of level 0 starts on its way into the host
    /// memory of `slot` (0 .. kReadSlots - 1); FinishRead takes it, a frame later. A slot started again before it was
    /// finished drops the earlier read. `bgra`: an RGBA8 texture's pixels as B,G,R,A bytes (GL reads that order
    /// natively on macOS: RGBA took its slow converting path, 5 ms for a 1319x1080 picture). This default reads at once
    /// (waits); GL reads into a pixel buffer behind a fence, Vulkan into a host buffer of a submission not waited for
    static constexpr int kReadSlots = 3;
    virtual bool BeginRead(int slot, Texture& texture, Rect rect, bool bgra = false);
    /// The read `slot` started, into `out`: rows `outStride` bytes apart, turned over when `flip` (GL's bottom row
    /// first into top down). Waits only for that read. False: nothing started there, or the read failed
    virtual bool FinishRead(int slot, uint8_t* out, size_t outStride, bool flip);
    /// Levels 1.. from level 0
    virtual void GenerateMipmaps(Texture& texture) = 0;
    /// `from` of `source` into `to` of `target`, scaled with `filter`; a negative height turns the rows over
    virtual void Blit(Texture& source, Rect from, Texture& target, Rect to, Filter filter) = 0;
    /// The whole texture to one value (normalised and float formats; integer ones take the values as they are)
    virtual void Clear(Texture& texture, float r, float g, float b, float a) = 0;

    /// Compile and link. nullptr with the compiler's messages in `log`
    virtual std::unique_ptr<Program> CreateProgram(const std::string& vertex, const std::string& fragment,
                                                   std::string& log) = 0;
    /// The quad with `program`'s values into `target`
    virtual void Draw(Program& program, Texture& target, const DrawState& state) = 0;
    /// The work recorded so far to the GPU (a long pass in stripes flushes between them)
    virtual void Flush() = 0;
    /// Flush and wait for the GPU
    virtual void Finish() = 0;
    /// The device failed for good (lost): every call does nothing from here on
    virtual bool Lost() const { return false; }

    /// GPU timestamps (the window's pass times): `count` queries made once. False: none here (no timer queries, or a
    /// clock that reads 0 as Apple's GL over Metal does); then the other calls do nothing
    virtual bool CreateTimestamps(int count) { return count < 0; }
    /// The queries gone; `lost`: the device is gone, only forgotten
    virtual void DestroyTimestamps(bool lost) { (void)lost; }
    /// Query `index` takes the GPU's time when the stream gets here
    virtual void WriteTimestamp(int index) { (void)index; }
    /// Query `index`'s time, nanoseconds on the GPU's clock; never waits: false until the GPU wrote it
    virtual bool ReadTimestamp(int index, uint64_t& ns)
    {
        (void)index;
        ns = 0;
        return false;
    }

protected:
    /// Rows of `rowBytes` from `from` (tight) into `out`, `outStride` apart, turned over when `flip`; `swap`: 4-byte
    /// texels with their bytes 0 and 2 swapped (RGBA <-> BGRA)
    static void CopyRows(const uint8_t* from, size_t rowBytes, int rows, uint8_t* out, size_t outStride, bool flip,
                         bool swap = false);

private:
    /// The default BeginRead's pixels
    struct ReadSlot
    {
        std::vector<uint8_t> pixels;
        Rect rect;
        int texel = 0;
        bool bgra = false;
        bool started = false;
    };
    ReadSlot _readSlots[kReadSlots];
};

/// The quad's vertices (Position.xyzw, TexCoord.xy per corner)
extern const float kQuadVertices[24];
/// 0..1 into clip space, column major (the slang presets' MVP)
extern const float kQuadMvp[16];
}  // namespace gpu
