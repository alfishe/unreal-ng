#pragma once

// A libretro slang CRT preset on SDL_GPU: unreal-qt's SlangPresetRunner (unreal-qt/src/widgets/slangpresetrunner.cpp)
// for the Deck client. The passes come as SPIR-V from crt-convert.py (shaders/crt/<key>/ next to the binary).
//
// Every pass renders into a texture of its scale, the last one at the output size; the caller blits that 1:1 to the
// screen. The semantics of the slang spec as there: Original, Source, OriginalHistory#, PassOutput#, PassFeedback#,
// aliases and <alias>Feedback, the LUTs; MVP, OutputSize, SourceSize, OriginalSize, FinalViewportSize, <texture>Size,
// FrameCount, FrameDirection, FrameTimeDelta, OriginalFPS and the parameters. Textures keep the slang orientation (row
// 0 = top = texture coordinate 0); SDL_GPU's clip space has y up, so the MVP flips the quad.
// SDL_GPU has no clamp-to-border: crt-convert.py emulates it in the shaders (LIBRA_INTERNAL_BorderMask).
//
// Whole multiples of the source lines: a preset that picks its scanline pitch by itself (crt-yah rounds the pixels per
// source line, crt-sony-megatron has a mask of its own) beats against a picture at a fractional scale (the 352x288 frame
// on the Deck's 800 lines is 2.78). The manifest line `canvas 1` runs such a preset on a canvas that is a whole multiple
// of the source (3x: 1056x864), the shape of the output, and the canvas is resampled to the output with a Lanczos
// filter (resolve.{vert,frag}.spv next to the presets). The others draw at the output's size: their scanlines follow
// the output / source ratio, and their masks stay on whole device pixels. A last pass of an absolute size (the baked
// crt-yo6-pvm) is resampled the same way.
//
// Vulkan / SPIR-V only (the Deck); on a device without SPIR-V (Metal) Load() fails. Not thread safe: the render thread
// owns it.

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace deckpoc
{

class CrtPreset
{
public:
    /// The presets under `root` (directories with a manifest.txt), sorted
    static std::vector<std::string> Available(const std::string& root);

    CrtPreset() = default;
    ~CrtPreset() { Release(); }
    CrtPreset(const CrtPreset&) = delete;
    CrtPreset& operator=(const CrtPreset&) = delete;

    /// The preset in `directory`. False with the reason in `error`
    bool Load(SDL_GPUDevice* device, const std::string& directory, std::string* error);
    void Release();
    bool Loaded() const { return !_passes.empty(); }

    /// One frame: the `sx, sy, sw, sh` area of `source` (RGBA8) through every pass at `outW` x `outH`. Records into
    /// `cmd` outside any pass. @return the output texture (RGBA8, outW x outH) to blit, nullptr when not loaded
    SDL_GPUTexture* Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, uint32_t sx, uint32_t sy, uint32_t sw,
                           uint32_t sh, uint32_t outW, uint32_t outH);

private:
    enum class Wrap { Border, Edge, Repeat, Mirror };
    enum class From { Original, Source, History, Output, Feedback, Lut };

    struct Texture
    {
        SDL_GPUTexture* texture = nullptr;
        uint32_t w = 0, h = 0;
        SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
        bool fresh = false;   ///< created, not yet cleared
    };
    struct Binding
    {
        From from = From::Source;
        int index = 0;
    };
    /// One member of a stage's uniform block and what fills it
    struct Member
    {
        enum Kind { Mvp, OutputSize, FinalViewportSize, FrameCount, FrameDirection, FrameTimeDelta, OriginalFps,
                    BorderMask, Parameter, TextureSize, Zero };
        Kind kind = Zero;
        std::string type;
        uint32_t offset = 0;
        int index = 0;        ///< the parameter, or the TextureSize's binding in Stage::sizes
    };
    struct Stage
    {
        uint32_t size = 0;
        std::vector<std::string> samplers;
        std::vector<std::pair<std::string, std::pair<std::string, uint32_t>>> raw;   ///< name, type, offset
        std::vector<Member> members;
        std::vector<Binding> bindings;   ///< per sampler
        std::vector<Binding> sizes;      ///< per TextureSize member
        std::vector<uint8_t> data;
    };
    struct Pass
    {
        std::string alias;
        bool linear = false;
        Wrap wrap = Wrap::Border;
        bool mipmapInput = false;
        uint32_t frameCountMod = 0;
        SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        std::string scaleTypeX = "Input", scaleTypeY = "Input";
        float scaleX = 1.0f, scaleY = 1.0f;
        Stage vert, frag;
        SDL_GPUGraphicsPipeline* pipeline = nullptr;
        Texture output;
        Texture feedback;
        bool hasFeedback = false;
    };
    struct Lut
    {
        std::string name;
        Texture texture;
        bool linear = false;
        Wrap wrap = Wrap::Border;
        bool mipmap = false;
    };
    struct Parameter
    {
        std::string name;
        float value = 0.0f;
    };

    bool Fail(std::string* error, const std::string& message);
    bool LoadResolve(const std::string& directory);
    void Resample(SDL_GPUCommandBuffer* cmd, const Texture& from, Texture& to, bool horizontal);
    bool Resolve(int passIndex, const std::string& name, Binding& binding, std::string* error);
    bool Classify(int passIndex, Stage& stage, std::string* error);
    bool Allocate(Texture& texture, uint32_t w, uint32_t h, SDL_GPUTextureFormat format);
    void Destroy(Texture& texture);
    SDL_GPUSampler* Sampler(bool linear, Wrap wrap, bool mipmap);
    const Texture& Bound(const Binding& binding, size_t passIndex, const Texture* previous, bool& linear, Wrap& wrap,
                         bool& mipmap) const;
    void FillUniforms(Stage& stage, const Pass& pass, size_t passIndex, const Texture* previous, uint32_t outW,
                      uint32_t outH);

    SDL_GPUDevice* _device = nullptr;
    std::vector<Pass> _passes;
    std::vector<Lut> _luts;
    std::vector<Parameter> _parameters;
    Texture _original;
    std::vector<Texture> _history;   ///< the previous originals, [0] = OriginalHistory1
    std::map<uint32_t, SDL_GPUSampler*> _samplers;
    SDL_GPUBuffer* _quad = nullptr;
    SDL_GPUGraphicsPipeline* _resolve = nullptr;   ///< the Lanczos pass, null when resolve.*.spv is missing
    bool _wholeLines = false;                      ///< the manifest's `canvas 1`
    Texture _resolveH, _resolveV;                  ///< canvas -> output width -> output height
    uint32_t _frameCount = 0;
};

} // namespace deckpoc
