#include "crtpreset.h"

#include "3rdparty/lodepng/lodepng.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace deckpoc
{

namespace
{
/// The quad's corners as Position (x, y, 0, 1) and TexCoord (u, v): a strip over 0..1
constexpr float kQuad[] = {
    0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,  //
    1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,  //
    0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,  //
    1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f,
};

/// 0..1 into clip space, column major, y flipped: SDL_GPU's clip space has y up, the slang passes want row 0 (texture
/// coordinate 0, gl_FragCoord.y 0) at the top
constexpr float kMvp[] = {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f};

SDL_GPUTextureFormat Format(const std::string& name)
{
    static const std::map<std::string, SDL_GPUTextureFormat> kFormats = {
        {"rgba8", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM},
        {"rgba8-srgb", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB},
        {"rgb10a2", SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM},
        {"rgba16f", SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT},
        {"rgba32f", SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT},
        {"rg16f", SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT},
        {"rg32f", SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT},
        {"r16f", SDL_GPU_TEXTUREFORMAT_R16_FLOAT},
        {"r32f", SDL_GPU_TEXTUREFORMAT_R32_FLOAT},
        {"r8", SDL_GPU_TEXTUREFORMAT_R8_UNORM},
        {"rg8", SDL_GPU_TEXTUREFORMAT_R8G8_UNORM},
    };
    const auto it = kFormats.find(name);
    return it == kFormats.end() ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM : it->second;
}

std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void Put(std::vector<uint8_t>& data, uint32_t offset, const void* value, size_t size)
{
    if (offset + size <= data.size())
        std::memcpy(data.data() + offset, value, size);
}

/// A scalar in the member's own type
void PutScalar(std::vector<uint8_t>& data, uint32_t offset, const std::string& type, double value)
{
    if (type == "uint")
    {
        const uint32_t v = static_cast<uint32_t>(value);
        Put(data, offset, &v, 4);
    }
    else if (type == "int" || type == "bool")
    {
        const int32_t v = static_cast<int32_t>(value);
        Put(data, offset, &v, 4);
    }
    else
    {
        const float v = static_cast<float>(value);
        Put(data, offset, &v, 4);
    }
}

void PutSize(std::vector<uint8_t>& data, uint32_t offset, uint32_t w, uint32_t h)
{
    const float v[4] = {static_cast<float>(w), static_cast<float>(h), w ? 1.0f / static_cast<float>(w) : 0.0f,
                        h ? 1.0f / static_cast<float>(h) : 0.0f};
    Put(data, offset, v, sizeof(v));
}

uint32_t Levels(uint32_t w, uint32_t h)
{
    return 1 + static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(std::max(1u, std::max(w, h))))));
}
}  // namespace

std::vector<std::string> CrtPreset::Available(const std::string& root)
{
    std::vector<std::string> keys;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        if (entry.is_directory() && std::filesystem::exists(entry.path() / "manifest.txt"))
            keys.push_back(entry.path().filename().string());
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool CrtPreset::Fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    Release();
    return false;
}

bool CrtPreset::Load(SDL_GPUDevice* device, const std::string& directory, std::string* error)
{
    Release();
    _device = device;
    if (!(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV))
        return Fail(error, "the GPU device takes no SPIR-V (the CRT presets are Vulkan only)");

    std::ifstream manifest(directory + "/manifest.txt");
    if (!manifest)
        return Fail(error, "no manifest.txt in " + directory);

    // The manifest (crt-convert.py): param / lut / pass lines, each pass followed by its vert and frag stages
    struct LutFile
    {
        std::string file;
    };
    std::vector<LutFile> lutFiles;
    std::map<std::string, int> parameterIndex;
    Stage* stage = nullptr;
    std::string line;
    while (std::getline(manifest, line))
    {
        std::istringstream in(line);
        std::string word;
        in >> word;
        if (word == "canvas")
        {
            int on = 0;
            in >> on;
            _wholeLines = on != 0;
        }
        else if (word == "param")
        {
            Parameter p;
            float minimum = 0, maximum = 0;
            in >> p.name >> p.value >> minimum >> maximum;
            parameterIndex[p.name] = static_cast<int>(_parameters.size());
            _parameters.push_back(p);
        }
        else if (word == "lut")
        {
            Lut lut;
            std::string file, filter, wrap;
            int mipmap = 0;
            in >> lut.name >> file >> filter >> wrap >> mipmap;
            lut.linear = filter == "linear";
            lut.wrap = wrap == "edge" ? Wrap::Edge : wrap == "repeat" ? Wrap::Repeat : wrap == "mirror" ? Wrap::Mirror
                                                                                                           : Wrap::Border;
            lut.mipmap = mipmap != 0;
            _luts.push_back(lut);
            lutFiles.push_back({file});
        }
        else if (word == "pass")
        {
            Pass pass;
            int index = 0, mipmap = 0;
            std::string filter, wrap, format;
            in >> index >> pass.alias >> filter >> wrap >> mipmap >> pass.frameCountMod >> format >> pass.scaleTypeX >>
                pass.scaleX >> pass.scaleTypeY >> pass.scaleY;
            if (pass.alias == "-")
                pass.alias.clear();
            pass.linear = filter == "linear";
            pass.wrap = wrap == "edge" ? Wrap::Edge : wrap == "repeat" ? Wrap::Repeat : wrap == "mirror" ? Wrap::Mirror
                                                                                                            : Wrap::Border;
            pass.mipmapInput = mipmap != 0;
            pass.format = Format(format);
            _passes.push_back(std::move(pass));
            stage = nullptr;
        }
        else if ((word == "vert" || word == "frag") && !_passes.empty())
        {
            stage = word == "vert" ? &_passes.back().vert : &_passes.back().frag;
            size_t samplers = 0;
            in >> stage->size >> samplers;
        }
        else if (word == "u" && stage)
        {
            std::string name, type;
            uint32_t offset = 0;
            in >> name >> type >> offset;
            stage->raw.push_back({name, {type, offset}});
        }
        else if (word == "s" && stage)
        {
            std::string name;
            in >> name;
            stage->samplers.push_back(name);
        }
    }
    if (_passes.empty())
        return Fail(error, "no passes in " + directory);

    // Uniform members and samplers: resolved once every alias is known
    for (size_t i = 0; i < _passes.size(); ++i)
        if (!Classify(static_cast<int>(i), _passes[i].vert, error) || !Classify(static_cast<int>(i), _passes[i].frag, error))
            return Fail(error, error ? *error : "");
    int historyDepth = 0;
    for (const Pass& pass : _passes)
        for (const auto* bindings : {&pass.frag.bindings, &pass.frag.sizes, &pass.vert.sizes})
            for (const Binding& binding : *bindings)
                if (binding.from == From::History)
                    historyDepth = std::max(historyDepth, binding.index);
    _history.resize(static_cast<size_t>(historyDepth));

    // Shaders and pipelines
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        SDL_GPUShader* shaders[2] = {};
        for (int s = 0; s < 2; ++s)
        {
            const Stage& st = s == 0 ? pass.vert : pass.frag;
            const std::string path = directory + "/pass" + std::to_string(i) + (s == 0 ? ".vert.spv" : ".frag.spv");
            const std::vector<uint8_t> code = ReadFile(path);
            if (code.empty())
                return Fail(error, "missing " + path);
            SDL_GPUShaderCreateInfo si{};
            si.code_size = code.size();
            si.code = code.data();
            si.entrypoint = "main";
            si.format = SDL_GPU_SHADERFORMAT_SPIRV;
            si.stage = s == 0 ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
            si.num_samplers = static_cast<Uint32>(st.samplers.size());
            si.num_uniform_buffers = 1;
            shaders[s] = SDL_CreateGPUShader(device, &si);
            if (!shaders[s])
            {
                if (shaders[0])
                    SDL_ReleaseGPUShader(device, shaders[0]);
                return Fail(error, path + ": " + SDL_GetError());
            }
        }

        SDL_GPUVertexBufferDescription buffer{};
        buffer.slot = 0;
        buffer.pitch = 6 * sizeof(float);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[2] = {};
        attributes[0].location = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[0].offset = 0;
        attributes[1].location = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[1].offset = 4 * sizeof(float);
        SDL_GPUColorTargetDescription target{};
        target.format = pass.format;
        SDL_GPUGraphicsPipelineCreateInfo pi{};
        pi.vertex_shader = shaders[0];
        pi.fragment_shader = shaders[1];
        pi.vertex_input_state.vertex_buffer_descriptions = &buffer;
        pi.vertex_input_state.num_vertex_buffers = 1;
        pi.vertex_input_state.vertex_attributes = attributes;
        pi.vertex_input_state.num_vertex_attributes = 2;
        pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pi.target_info.color_target_descriptions = &target;
        pi.target_info.num_color_targets = 1;
        pass.pipeline = SDL_CreateGPUGraphicsPipeline(device, &pi);
        SDL_ReleaseGPUShader(device, shaders[0]);
        SDL_ReleaseGPUShader(device, shaders[1]);
        if (!pass.pipeline)
            return Fail(error, "pass " + std::to_string(i) + " pipeline: " + SDL_GetError());
    }

    // The quad and the LUT images, uploaded in one go
    std::vector<std::vector<unsigned char>> images(_luts.size());
    std::vector<std::pair<unsigned, unsigned>> sizes(_luts.size());
    size_t uploadSize = sizeof(kQuad);
    for (size_t i = 0; i < _luts.size(); ++i)
    {
        const std::string path = directory + "/" + lutFiles[i].file;
        if (lodepng::decode(images[i], sizes[i].first, sizes[i].second, path) != 0)
            return Fail(error, "LUT image unreadable: " + path);
        if (!Allocate(_luts[i].texture, sizes[i].first, sizes[i].second, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM))
            return Fail(error, std::string("LUT texture: ") + SDL_GetError());
        _luts[i].texture.fresh = false;
        uploadSize += images[i].size();
    }
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = sizeof(kQuad);
    _quad = SDL_CreateGPUBuffer(device, &bi);
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = static_cast<Uint32>(uploadSize);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &ti);
    if (!_quad || !transfer)
        return Fail(error, std::string("buffers: ") + SDL_GetError());
    auto* mapped = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, transfer, false));
    std::memcpy(mapped, kQuad, sizeof(kQuad));
    size_t offset = sizeof(kQuad);
    for (const auto& image : images)
    {
        std::memcpy(mapped + offset, image.data(), image.size());
        offset += image.size();
    }
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation from{transfer, 0};
    SDL_GPUBufferRegion to{_quad, 0, sizeof(kQuad)};
    SDL_UploadToGPUBuffer(copy, &from, &to, false);
    offset = sizeof(kQuad);
    for (size_t i = 0; i < _luts.size(); ++i)
    {
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = transfer;
        src.offset = static_cast<Uint32>(offset);
        src.pixels_per_row = sizes[i].first;
        src.rows_per_layer = sizes[i].second;
        SDL_GPUTextureRegion dst{};
        dst.texture = _luts[i].texture.texture;
        dst.w = sizes[i].first;
        dst.h = sizes[i].second;
        dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
        offset += images[i].size();
    }
    SDL_EndGPUCopyPass(copy);
    for (const Lut& lut : _luts)
        if (lut.mipmap)
            SDL_GenerateMipmapsForGPUTexture(cmd, lut.texture.texture);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    _frameCount = 0;
    LoadResolve(directory);   // optional: without it the passes draw at the output size
    return true;
}

/// The Lanczos pass that resamples the canvas to the output (resolve.vert.spv / resolve.frag.spv in the directory above
/// the preset's)
bool CrtPreset::LoadResolve(const std::string& directory)
{
    const std::string root = directory + "/../resolve";
    SDL_GPUShader* shaders[2] = {};
    for (int s = 0; s < 2; ++s)
    {
        const std::vector<uint8_t> code = ReadFile(root + (s == 0 ? ".vert.spv" : ".frag.spv"));
        if (code.empty())
            break;
        SDL_GPUShaderCreateInfo si{};
        si.code_size = code.size();
        si.code = code.data();
        si.entrypoint = "main";
        si.format = SDL_GPU_SHADERFORMAT_SPIRV;
        si.stage = s == 0 ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        si.num_samplers = s == 0 ? 0 : 1;
        si.num_uniform_buffers = s == 0 ? 0 : 1;
        shaders[s] = SDL_CreateGPUShader(_device, &si);
        if (!shaders[s])
            break;
    }
    if (!shaders[0] || !shaders[1])
    {
        for (SDL_GPUShader* shader : shaders)
            if (shader)
                SDL_ReleaseGPUShader(_device, shader);
        return false;
    }
    SDL_GPUVertexBufferDescription buffer{};
    buffer.slot = 0;
    buffer.pitch = 6 * sizeof(float);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[2] = {};
    attributes[0].location = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = 4 * sizeof(float);
    SDL_GPUColorTargetDescription target{};
    target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = shaders[0];
    pi.fragment_shader = shaders[1];
    pi.vertex_input_state.vertex_buffer_descriptions = &buffer;
    pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = attributes;
    pi.vertex_input_state.num_vertex_attributes = 2;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions = &target;
    pi.target_info.num_color_targets = 1;
    _resolve = SDL_CreateGPUGraphicsPipeline(_device, &pi);
    SDL_ReleaseGPUShader(_device, shaders[0]);
    SDL_ReleaseGPUShader(_device, shaders[1]);
    return _resolve != nullptr;
}

/// One axis of the Lanczos resample: `to` has the size the result is to have; the other axis is copied 1:1
void CrtPreset::Resample(SDL_GPUCommandBuffer* cmd, const Texture& from, Texture& to, bool horizontal)
{
    // std140: axis (vec2), source size in texels (vec2), source texels per output texel on this axis
    const float ratio = horizontal ? static_cast<float>(from.w) / static_cast<float>(to.w)
                                   : static_cast<float>(from.h) / static_cast<float>(to.h);
    const float uniforms[8] = {horizontal ? 1.0f : 0.0f, horizontal ? 0.0f : 1.0f, static_cast<float>(from.w),
                               static_cast<float>(from.h), ratio, 0.0f, 0.0f, 0.0f};
    SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, sizeof(uniforms));
    SDL_GPUTextureSamplerBinding binding{from.texture, Sampler(false, Wrap::Edge, false)};
    SDL_GPUColorTargetInfo target{};
    target.texture = to.texture;
    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(rp, _resolve);
    SDL_GPUBufferBinding vertices{_quad, 0};
    SDL_BindGPUVertexBuffers(rp, 0, &vertices, 1);
    SDL_BindGPUFragmentSamplers(rp, 0, &binding, 1);
    SDL_DrawGPUPrimitives(rp, 4, 1, 0, 0);
    SDL_EndGPURenderPass(rp);
}

bool CrtPreset::Resolve(int passIndex, const std::string& name, Binding& binding, std::string* error)
{
    static const std::regex numbered("^(OriginalHistory|PassOutput|PassFeedback)(\\d+)$");
    if (name == "Original")
    {
        binding.from = From::Original;
        return true;
    }
    if (name == "Source")
    {
        binding.from = From::Source;
        return true;
    }
    std::smatch match;
    if (std::regex_match(name, match, numbered))
    {
        const std::string kind = match[1];
        binding.index = std::stoi(match[2]);
        if (kind == "OriginalHistory")
            binding.from = binding.index == 0 ? From::Original : From::History;
        else
            binding.from = kind == "PassOutput" ? From::Output : From::Feedback;
        if ((binding.from == From::Output && binding.index >= passIndex) ||
            (binding.from == From::Feedback && binding.index >= static_cast<int>(_passes.size())))
        {
            if (error)
                *error = "pass " + std::to_string(passIndex) + ": " + name + " does not exist yet";
            return false;
        }
        if (binding.from == From::Feedback)
            _passes[static_cast<size_t>(binding.index)].hasFeedback = true;
        return true;
    }
    for (size_t j = 0; j < _passes.size(); ++j)
    {
        const std::string& alias = _passes[j].alias;
        if (alias.empty())
            continue;
        if (name == alias && static_cast<int>(j) < passIndex)
        {
            binding.from = From::Output;
            binding.index = static_cast<int>(j);
            return true;
        }
        if (name == alias + "Feedback")
        {
            binding.from = From::Feedback;
            binding.index = static_cast<int>(j);
            _passes[j].hasFeedback = true;
            return true;
        }
    }
    for (size_t j = 0; j < _luts.size(); ++j)
        if (name == _luts[j].name)
        {
            binding.from = From::Lut;
            binding.index = static_cast<int>(j);
            return true;
        }
    if (error)
        *error = "pass " + std::to_string(passIndex) + ": unknown texture semantic " + name;
    return false;
}

bool CrtPreset::Classify(int passIndex, Stage& stage, std::string* error)
{
    static const std::regex numberedSize("^(OriginalHistory|PassOutput|PassFeedback)Size(\\d+)$");
    static const std::map<std::string, Member::Kind> kBuiltins = {
        {"MVP", Member::Mvp},
        {"OutputSize", Member::OutputSize},
        {"FinalViewportSize", Member::FinalViewportSize},
        {"FrameCount", Member::FrameCount},
        {"FrameDirection", Member::FrameDirection},
        {"FrameTimeDelta", Member::FrameTimeDelta},
        {"OriginalFPS", Member::OriginalFps},
        {"INTERNAL_BorderMask", Member::BorderMask},
        {"INTERNAL_Pad", Member::Zero},
    };
    for (const auto& [name, typed] : stage.raw)
    {
        Member member;
        member.type = typed.first;
        member.offset = typed.second;
        if (const auto it = kBuiltins.find(name); it != kBuiltins.end())
            member.kind = it->second;
        else if (const auto p = std::find_if(_parameters.begin(), _parameters.end(),
                                             [&](const Parameter& x) { return x.name == name; });
                 p != _parameters.end())
        {
            member.kind = Member::Parameter;
            member.index = static_cast<int>(p - _parameters.begin());
        }
        else if (name.size() > 4 && name.compare(name.size() - 4, 4, "Size") == 0)
        {
            // <texture>Size, OriginalHistorySize#, PassOutputSize#, PassFeedbackSize#
            std::smatch match;
            const std::string texture = std::regex_match(name, match, numberedSize)
                                            ? std::string(match[1]) + std::string(match[2])
                                            : name.substr(0, name.size() - 4);
            // A size of a texture the pass does not have (crt-yah declares an unused InputSize) stays 0: GL drops
            // such a uniform, the std140 block keeps it
            Binding binding;
            if (Resolve(passIndex, texture, binding, nullptr))
            {
                member.kind = Member::TextureSize;
                member.index = static_cast<int>(stage.sizes.size());
                stage.sizes.push_back(binding);
            }
        }
        // Anything else (a member no #pragma parameter names) stays 0, as in librashader
        stage.members.push_back(member);
    }
    for (const std::string& sampler : stage.samplers)
    {
        Binding binding;
        if (!Resolve(passIndex, sampler, binding, error))
            return false;
        stage.bindings.push_back(binding);
    }
    stage.data.assign(stage.size, 0);
    return true;
}

void CrtPreset::Release()
{
    if (_device)
    {
        for (Pass& pass : _passes)
        {
            Destroy(pass.output);
            Destroy(pass.feedback);
            if (pass.pipeline)
                SDL_ReleaseGPUGraphicsPipeline(_device, pass.pipeline);
        }
        for (Lut& lut : _luts)
            Destroy(lut.texture);
        for (Texture& texture : _history)
            Destroy(texture);
        Destroy(_original);
        Destroy(_resolveH);
        Destroy(_resolveV);
        if (_resolve)
            SDL_ReleaseGPUGraphicsPipeline(_device, _resolve);
        for (auto& [key, sampler] : _samplers)
            SDL_ReleaseGPUSampler(_device, sampler);
        if (_quad)
            SDL_ReleaseGPUBuffer(_device, _quad);
    }
    _passes.clear();
    _luts.clear();
    _parameters.clear();
    _history.clear();
    _samplers.clear();
    _quad = nullptr;
    _resolve = nullptr;
    _wholeLines = false;
}

bool CrtPreset::Allocate(Texture& texture, uint32_t w, uint32_t h, SDL_GPUTextureFormat format)
{
    if (texture.texture && texture.w == w && texture.h == h && texture.format == format)
        return true;
    Destroy(texture);
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = format;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = Levels(w, h);   // every level the size allows: a pass with mipmap_input generates them
    texture.texture = SDL_CreateGPUTexture(_device, &ti);
    texture.w = w;
    texture.h = h;
    texture.format = format;
    texture.fresh = true;
    return texture.texture != nullptr;
}

void CrtPreset::Destroy(Texture& texture)
{
    if (texture.texture && _device)
        SDL_ReleaseGPUTexture(_device, texture.texture);
    texture = Texture();
}

SDL_GPUSampler* CrtPreset::Sampler(bool linear, Wrap wrap, bool mipmap)
{
    const uint32_t key = (linear ? 1u : 0u) | (mipmap ? 2u : 0u) | (static_cast<uint32_t>(wrap) << 2);
    if (const auto it = _samplers.find(key); it != _samplers.end())
        return it->second;
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = si.mag_filter = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    si.mipmap_mode = linear ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    // Border: clamped here, the shader answers black outside (crt-convert.py)
    const SDL_GPUSamplerAddressMode mode = wrap == Wrap::Repeat   ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT
                                           : wrap == Wrap::Mirror ? SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT
                                                                  : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_u = si.address_mode_v = si.address_mode_w = mode;
    si.min_lod = 0.0f;
    si.max_lod = mipmap ? 1000.0f : 0.0f;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(_device, &si);
    _samplers[key] = sampler;
    return sampler;
}

const CrtPreset::Texture& CrtPreset::Bound(const Binding& binding, size_t passIndex, const Texture* previous,
                                           bool& linear, Wrap& wrap, bool& mipmap) const
{
    const Pass& pass = _passes[passIndex];
    linear = pass.linear;
    wrap = pass.wrap;
    mipmap = false;
    switch (binding.from)
    {
        case From::Original:
            linear = _passes.front().linear;
            wrap = _passes.front().wrap;
            return _original;
        case From::Source:
            mipmap = pass.mipmapInput;
            return *previous;
        case From::History:
            linear = _passes.front().linear;
            wrap = _passes.front().wrap;
            return _history[static_cast<size_t>(binding.index - 1)];
        case From::Output:
        case From::Feedback:
        {
            // A pass's output is read with the sampling of the pass that wrote it
            const Pass& writer = _passes[static_cast<size_t>(binding.index)];
            linear = writer.linear;
            wrap = writer.wrap;
            return binding.from == From::Output ? writer.output : writer.feedback;
        }
        case From::Lut:
        {
            const Lut& lut = _luts[static_cast<size_t>(binding.index)];
            linear = lut.linear;
            wrap = lut.wrap;
            mipmap = lut.mipmap;
            return lut.texture;
        }
    }
    return *previous;
}

void CrtPreset::FillUniforms(Stage& stage, const Pass& pass, size_t passIndex, const Texture* previous, uint32_t outW,
                             uint32_t outH)
{
    uint32_t borderMask = 0;
    for (size_t i = 0; i < stage.bindings.size(); ++i)
    {
        bool linear = false, mipmap = false;
        Wrap wrap = Wrap::Border;
        Bound(stage.bindings[i], passIndex, previous, linear, wrap, mipmap);
        if (wrap == Wrap::Border)
            borderMask |= 1u << i;
    }
    for (const Member& m : stage.members)
    {
        switch (m.kind)
        {
            case Member::Mvp: Put(stage.data, m.offset, kMvp, sizeof(kMvp)); break;
            case Member::OutputSize: PutSize(stage.data, m.offset, pass.output.w, pass.output.h); break;
            case Member::FinalViewportSize: PutSize(stage.data, m.offset, outW, outH); break;
            case Member::FrameCount:
                PutScalar(stage.data, m.offset, m.type, pass.frameCountMod ? _frameCount % pass.frameCountMod : _frameCount);
                break;
            case Member::FrameDirection: PutScalar(stage.data, m.offset, m.type, 1); break;
            case Member::FrameTimeDelta: PutScalar(stage.data, m.offset, m.type, 20); break;
            case Member::OriginalFps: PutScalar(stage.data, m.offset, m.type, 50.0); break;
            case Member::BorderMask: PutScalar(stage.data, m.offset, m.type, borderMask); break;
            case Member::Parameter:
                PutScalar(stage.data, m.offset, m.type, _parameters[static_cast<size_t>(m.index)].value);
                break;
            case Member::TextureSize:
            {
                bool linear = false, mipmap = false;
                Wrap wrap = Wrap::Border;
                const Texture& t = Bound(stage.sizes[static_cast<size_t>(m.index)], passIndex, previous, linear, wrap, mipmap);
                PutSize(stage.data, m.offset, t.w, t.h);
                break;
            }
            case Member::Zero: break;
        }
    }
}

SDL_GPUTexture* CrtPreset::Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* source, uint32_t sx, uint32_t sy,
                                  uint32_t sw, uint32_t sh, uint32_t outW, uint32_t outH)
{
    if (_passes.empty() || sw == 0 || sh == 0 || outW == 0 || outH == 0)
        return nullptr;

    // The canvas the passes draw on: the smallest whole multiple of the source lines that covers the output, in the
    // output's shape. The output itself when it already is one (or without the resample pass)
    const uint32_t outputW = outW, outputH = outH;
    if (_resolve && _wholeLines)
    {
        const uint32_t lines = (outH + sh - 1) / sh * sh;
        outW = static_cast<uint32_t>(std::lround(static_cast<double>(outputW) * lines / outputH));
        outH = lines;
    }

    // The previous originals move one step back; the oldest texture takes the new frame
    if (!_history.empty())
    {
        Texture oldest = _history.back();
        for (size_t i = _history.size() - 1; i > 0; --i)
            _history[i] = _history[i - 1];
        _history[0] = _original;
        _original = oldest;
    }
    if (!Allocate(_original, sw, sh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM))
        return nullptr;
    _original.fresh = false;   // the blit below fills it
    for (Texture& texture : _history)
        if (!texture.texture && !Allocate(texture, sw, sh, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM))
            return nullptr;

    SDL_GPUBlitInfo blit{};
    blit.source.texture = source;
    blit.source.x = sx;
    blit.source.y = sy;
    blit.source.w = sw;
    blit.source.h = sh;
    blit.destination.texture = _original.texture;
    blit.destination.w = sw;
    blit.destination.h = sh;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(cmd, &blit);

    for (Pass& pass : _passes)
        if (pass.hasFeedback)
            std::swap(pass.output, pass.feedback);

    // Every pass's textures before the first draws: a pass may read the feedback of a later one (black at first)
    uint32_t w = sw, h = sh;
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        if (i + 1 == _passes.size())
        {
            w = outW;
            h = outH;
        }
        else
        {
            auto axis = [](const std::string& type, float factor, uint32_t in, uint32_t view) {
                float size = static_cast<float>(in) * factor;
                if (type == "Viewport")
                    size = static_cast<float>(view) * factor;
                else if (type == "Absolute")
                    size = factor;
                return static_cast<uint32_t>(std::max(1L, std::lround(size)));
            };
            w = axis(pass.scaleTypeX, pass.scaleX, w, outW);
            h = axis(pass.scaleTypeY, pass.scaleY, h, outH);
        }
        if (!Allocate(pass.output, w, h, pass.format))
            return nullptr;
        if (pass.hasFeedback && !Allocate(pass.feedback, w, h, pass.format))
            return nullptr;
    }
    auto clearFresh = [&](Texture& texture) {
        if (!texture.texture || !texture.fresh)
            return;
        SDL_GPUColorTargetInfo target{};
        target.texture = texture.texture;
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        target.clear_color = SDL_FColor{0, 0, 0, 0};
        SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &target, 1, nullptr));
        texture.fresh = false;
    };
    for (Pass& pass : _passes)
    {
        clearFresh(pass.output);
        clearFresh(pass.feedback);
    }
    for (Texture& texture : _history)
        clearFresh(texture);

    const Texture* previous = &_original;
    for (size_t i = 0; i < _passes.size(); ++i)
    {
        Pass& pass = _passes[i];
        if (pass.mipmapInput && previous->texture)
            SDL_GenerateMipmapsForGPUTexture(cmd, previous->texture);

        FillUniforms(pass.vert, pass, i, previous, outW, outH);
        FillUniforms(pass.frag, pass, i, previous, outW, outH);
        SDL_PushGPUVertexUniformData(cmd, 0, pass.vert.data.data(), static_cast<Uint32>(pass.vert.data.size()));
        SDL_PushGPUFragmentUniformData(cmd, 0, pass.frag.data.data(), static_cast<Uint32>(pass.frag.data.size()));

        std::vector<SDL_GPUTextureSamplerBinding> samplers(pass.frag.bindings.size());
        for (size_t b = 0; b < samplers.size(); ++b)
        {
            bool linear = false, mipmap = false;
            Wrap wrap = Wrap::Border;
            const Texture& t = Bound(pass.frag.bindings[b], i, previous, linear, wrap, mipmap);
            samplers[b].texture = t.texture;
            samplers[b].sampler = Sampler(linear, wrap, mipmap);
        }

        SDL_GPUColorTargetInfo target{};
        target.texture = pass.output.texture;
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(rp, pass.pipeline);
        SDL_GPUBufferBinding vertices{_quad, 0};
        SDL_BindGPUVertexBuffers(rp, 0, &vertices, 1);
        if (!samplers.empty())
            SDL_BindGPUFragmentSamplers(rp, 0, samplers.data(), static_cast<Uint32>(samplers.size()));
        SDL_DrawGPUPrimitives(rp, 4, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
        previous = &pass.output;
    }
    ++_frameCount;
    // The last pass is on the canvas, or at a size of its own (a preset with absolute sizes): to the output
    const Texture& last = _passes.back().output;
    if (last.w == outputW && last.h == outputH)
        return last.texture;
    if (!_resolve)
        return last.texture;   // only reached when the preset's own size differs from the output and nothing resamples it
    // Last pass -> output width (all of its lines) -> output height
    if (!Allocate(_resolveH, outputW, last.h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) ||
        !Allocate(_resolveV, outputW, outputH, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM))
        return nullptr;
    Resample(cmd, last, _resolveH, true);
    Resample(cmd, _resolveH, _resolveV, false);
    return _resolveV.texture;
}

} // namespace deckpoc
