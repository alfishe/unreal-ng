#include "vulkandevice.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace gpu
{
namespace
{
constexpr int kFrames = 8;  ///< submissions in flight: a frame of smooth motion's stripes flushes several times
constexpr VkDeviceSize kArenaChunk = 1u << 20;

// GL's uniform types as glslang's reflection reports them
constexpr int kGlFloat = 0x1406;
constexpr int kGlFloatMat4 = 0x8B5C;
constexpr int kGlIntSampler2D = 0x8DCA;
constexpr int kGlUnsignedIntSampler2D = 0x8DD2;

bool EnvOn(const char* name)
{
    const char* v = std::getenv(name);
    return v && *v && std::strcmp(v, "0") != 0;
}

VkFormat ToVk(Format format)
{
    switch (format)
    {
        case Format::RGBA8:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::SRGBA8:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::RGBA16F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::RGBA32F:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::R8UI:
            return VK_FORMAT_R8_UINT;
        case Format::R16UI:
            return VK_FORMAT_R16_UINT;
        case Format::R16I:
            return VK_FORMAT_R16_SINT;
        case Format::R32UI:
            return VK_FORMAT_R32_UINT;
        case Format::R32I:
            return VK_FORMAT_R32_SINT;
        case Format::RG16UI:
            return VK_FORMAT_R16G16_UINT;
        case Format::RG32I:
            return VK_FORMAT_R32G32_SINT;
        case Format::RG32UI:
            return VK_FORMAT_R32G32_UINT;
        case Format::RGBA8UI:
            return VK_FORMAT_R8G8B8A8_UINT;
        case Format::RGBA16UI:
            return VK_FORMAT_R16G16B16A16_UINT;
        case Format::RGBA16I:
            return VK_FORMAT_R16G16B16A16_SINT;
        case Format::RGBA32UI:
            return VK_FORMAT_R32G32B32A32_UINT;
        case Format::RGBA32I:
            return VK_FORMAT_R32G32B32A32_SINT;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

bool IsSigned(Format format)
{
    return format == Format::R16I || format == Format::R32I || format == Format::RG32I ||
           format == Format::RGBA16I || format == Format::RGBA32I;
}

/// A host-visible buffer chunk, persistently mapped
struct HostBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    uint8_t* mapped = nullptr;
};

/// Bump allocation in host-visible chunks; reset when its frame's submission is done
struct Arena
{
    VkBufferUsageFlags usage = 0;
    std::vector<HostBuffer> chunks;
    size_t current = 0;
    VkDeviceSize offset = 0;
};

struct Retired
{
    uint64_t serial = 0;
    VkImage image = VK_NULL_HANDLE;
    VkImageView views[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayouts[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkShaderModule modules[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
};

struct Frame
{
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool submitted = false;
    uint64_t serial = 0;
    Arena uniforms;
    Arena staging;
    std::vector<VkDescriptorPool> pools;
    size_t nextPool = 0;  ///< the descriptor pool sets come from
};

/// A program's compiled shaders and what reflection found in them (CreateProgram), kept by their sources: a pass
/// created again (the screen rebuilt, a preset reloaded, a test's own passes) does not run glslang again
struct CompiledProgram
{
    struct UniformInfo
    {
        std::string name;
        uint32_t offset = 0;
        uint32_t stride = 0;
        int count = 1;
        bool isFloat = false;
    };
    struct SamplerInfo
    {
        std::string name;
        uint32_t binding = 0;
        int glType = 0;
    };
    std::vector<uint32_t> spirv[2];
    uint32_t blockSize = 0;
    std::vector<UniformInfo> uniforms;
    std::vector<SamplerInfo> samplers;
};
/// The compiled programs kept: beyond it the cache starts again (a long session through many presets)
constexpr size_t kCompiledProgramsMax = 512;

class VkTexture;
class VkProgram;
}  // namespace

struct VulkanDevice::Impl
{
    VulkanDevice* owner = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::vector<std::string> extensions;

    std::array<Frame, kFrames> frames;
    int current = 0;
    bool recording = false;
    uint64_t nextSerial = 1;      ///< the serial of the recording (next) submission
    uint64_t completedSerial = 0;  ///< every submission up to this one is done
    std::vector<Retired> retired;

    HostBuffer quad;
    HostBuffer readback;
    std::map<std::tuple<int, int, int, int>, VkSampler> samplers;
    std::unique_ptr<VkTexture> dummyFloat, dummyUint, dummyInt;
    std::unordered_map<std::string, CompiledProgram> compiled;  ///< by the sources (vertex, a 0 byte, fragment)
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;              ///< every pipeline's (the driver's compiles)

    // Timestamps (CreateTimestamps): a query reads only once the submission that wrote it finished
    uint32_t timestampBits = 0;  ///< the queue family's timestampValidBits (0: none)
    VkQueryPool timestamps = VK_NULL_HANDLE;
    std::vector<uint64_t> timestampSerial;  ///< per query the submission that wrote it (0: never written)

    // Helpers
    uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags want) const
    {
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & want) == want)
                return i;
        return UINT32_MAX;
    }
    bool CreateHostBuffer(HostBuffer& b, VkDeviceSize size, VkBufferUsageFlags usage, bool cached);
    void DestroyHostBuffer(HostBuffer& b);
    /// `size` bytes of `arena` aligned to `align`: the buffer, its offset and the mapped pointer
    bool Allocate(Arena& arena, VkDeviceSize size, VkDeviceSize align, VkBuffer& buffer, VkDeviceSize& offset,
                  uint8_t*& mapped);

    VkCommandBuffer Commands();  ///< the recording command buffer (begins a frame when none records)
    void Begin();
    void Collect();  ///< destroys what the finished submissions no longer use
    void Poll();     ///< completedSerial from the fences that signalled, never waiting
    void Retire(Retired r)
    {
        r.serial = nextSerial;
        retired.push_back(r);
    }
    bool Check(VkResult result, const char* where)
    {
        if (result == VK_SUCCESS)
            return true;
        if (result == VK_ERROR_DEVICE_LOST)
            owner->MarkLost(where, result);
        return false;
    }
    void Barrier(VkTexture& texture, VkImageLayout layout);
    VkSampler GetSampler(Sampler s, bool integer, bool signedInt);
    VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout);
};

namespace
{
class VkTexture final : public Texture
{
public:
    VkTexture(VulkanDevice::Impl* impl, int width, int height, Format format, int levels)
        : Texture(width, height, format, levels), impl(impl)
    {
    }
    ~VkTexture() override
    {
        Retired r;
        r.image = image;
        r.views[0] = view;
        r.views[1] = attachView;
        r.memory = memory;
        impl->Retire(r);
    }

    VulkanDevice::Impl* impl;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;        ///< every level, sampled
    VkImageView attachView = VK_NULL_HANDLE;  ///< level 0, the render target
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Uniform
{
    uint32_t offset = 0;
    uint32_t stride = 0;  ///< array elements (std140: 16 per vector, 64 per mat4)
    int count = 1;
    bool isFloat = false;
};

struct SamplerSlot
{
    uint32_t binding = 0;
    int glType = 0;
    Texture* texture = nullptr;
    Sampler sampler;
};

class VkProgram final : public Program
{
public:
    explicit VkProgram(VulkanDevice::Impl* impl) : impl(impl) {}
    ~VkProgram() override
    {
        for (auto& [key, pipeline] : pipelines)
        {
            Retired r;
            r.pipeline = pipeline;
            impl->Retire(r);
        }
        Retired r;
        r.layout = layout;
        r.setLayouts[0] = setLayouts[0];
        r.setLayouts[1] = setLayouts[1];
        r.modules[0] = modules[0];
        r.modules[1] = modules[1];
        impl->Retire(r);
    }

    static std::string Key(const char* name)
    {
        std::string key(name);
        if (key.size() > 3 && key.compare(key.size() - 3, 3, "[0]") == 0)
            key.resize(key.size() - 3);
        return key;
    }
    bool Has(const char* name) const override
    {
        const std::string key = Key(name);
        return uniforms.count(key) || samplerIndex.count(key);
    }
    std::vector<std::string> Names() const override
    {
        std::vector<std::string> names;
        for (const auto& [name, u] : uniforms)
            names.push_back(name);
        for (const auto& [name, i] : samplerIndex)
            names.push_back(name);
        return names;
    }
    template <typename T>
    void Write(const char* name, const T* values, int components, int count)
    {
        auto it = uniforms.find(Key(name));
        if (it == uniforms.end() || !values || components <= 0)
            return;
        const Uniform& u = it->second;
        count = std::min(count, u.count);
        for (int e = 0; e < count; ++e)
        {
            uint8_t* at = data.data() + u.offset + static_cast<size_t>(e) * u.stride;
            for (int c = 0; c < components; ++c)
            {
                const T v = values[e * components + c];
                if (u.isFloat)
                {
                    const float f = static_cast<float>(v);
                    std::memcpy(at + c * 4, &f, 4);
                }
                else
                {
                    const int32_t i = static_cast<int32_t>(v);
                    std::memcpy(at + c * 4, &i, 4);
                }
            }
        }
    }
    void SetInts(const char* name, const int32_t* values, int components, int count) override
    {
        Write(name, values, components, count);
    }
    void SetUints(const char* name, const uint32_t* values, int components, int count) override
    {
        Write(name, values, components, count);
    }
    void SetFloats(const char* name, const float* values, int components, int count) override
    {
        auto it = uniforms.find(Key(name));
        if (it == uniforms.end() || !values || components <= 0)
            return;
        const Uniform& u = it->second;
        if (components == 16)  // mat4: four std140 columns, tight
        {
            std::memcpy(data.data() + u.offset, values, 64);
            return;
        }
        if (!u.isFloat)
        {
            Write(name, values, components, count);
            return;
        }
        count = std::min(count, u.count);
        for (int e = 0; e < count; ++e)
            std::memcpy(data.data() + u.offset + static_cast<size_t>(e) * u.stride, values + e * components,
                        static_cast<size_t>(components) * 4);
    }
    void SetTexture(const char* name, Texture* texture, Sampler sampler) override
    {
        auto it = samplerIndex.find(Key(name));
        if (it == samplerIndex.end())
            return;
        samplers[it->second].texture = texture;
        samplers[it->second].sampler = sampler;
    }

    VkPipeline Pipeline(VkFormat format, Blend blend);

    VulkanDevice::Impl* impl;
    VkShaderModule modules[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSetLayout setLayouts[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::map<std::pair<VkFormat, Blend>, VkPipeline> pipelines;
    std::unordered_map<std::string, Uniform> uniforms;
    std::unordered_map<std::string, size_t> samplerIndex;
    std::vector<SamplerSlot> samplers;
    std::vector<uint8_t> data;  ///< the uniform block
};

VkPipeline VkProgram::Pipeline(VkFormat format, Blend blend)
{
    auto it = pipelines.find({format, blend});
    if (it != pipelines.end())
        return it->second;

    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int i = 0; i < 2; ++i)
    {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = modules[i];
        stages[i].pName = "main";
    }
    const VkVertexInputBindingDescription binding{0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[2] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, 4 * sizeof(float)},
    };
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = 1;
    vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount = 2;
    vertex.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                VK_COLOR_COMPONENT_A_BIT;
    if (blend == Blend::Premultiplied)
    {
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &attachment;
    const VkDynamicState dynamics[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!impl->Check(vkCreateGraphicsPipelines(impl->device, impl->pipelineCache, 1, &info, nullptr, &pipeline),
                     "vkCreateGraphicsPipelines"))
        return VK_NULL_HANDLE;
    pipelines[{format, blend}] = pipeline;
    return pipeline;
}

bool CompileProgram(const std::string& vertex, const std::string& fragment, std::vector<uint32_t> (&spirv)[2],
                    glslang::TProgram& program, std::vector<std::unique_ptr<glslang::TShader>>& shaders,
                    std::string& log)
{
    static const bool initialised = glslang::InitializeProcess();
    if (!initialised)
    {
        log = "glslang: InitializeProcess failed";
        return false;
    }
    const std::string* sources[2] = {&vertex, &fragment};
    const EShLanguage stages[2] = {EShLangVertex, EShLangFragment};
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    for (int i = 0; i < 2; ++i)
    {
        auto shader = std::make_unique<glslang::TShader>(stages[i]);
        const char* text = sources[i]->c_str();
        shader->setStrings(&text, 1);
        shader->setEnvInput(glslang::EShSourceGlsl, stages[i], glslang::EShClientVulkan, 100);
        shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
        shader->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);
        shader->setEnvInputVulkanRulesRelaxed();
        shader->setAutoMapBindings(true);
        shader->setAutoMapLocations(true);
        shader->setGlobalUniformBlockName("UnrealGlobals");
        shader->setGlobalUniformSet(1);
        shader->setGlobalUniformBinding(0);
        if (!shader->parse(GetDefaultResources(), 330, ENoProfile, false, false, messages))
        {
            log = std::string(i == 0 ? "vertex: " : "fragment: ") + shader->getInfoLog();
            return false;
        }
        program.addShader(shader.get());
        shaders.push_back(std::move(shader));
    }
    // The GLSL mapper pairs the stages' varyings by name (the default one numbers each stage in its own declaration
    // order: crt-pi declares its two in opposite orders)
    if (!program.link(messages))
    {
        log = std::string("link: ") + program.getInfoLog();
        return false;
    }
    const std::unique_ptr<glslang::TIoMapper> mapper(glslang::GetGlslIoMapper());
    const std::unique_ptr<glslang::TIoMapResolver> resolver(program.getGlslIoResolver(stages[0]));
    if (!resolver || !program.mapIO(resolver.get(), mapper.get()))
    {
        log = std::string("mapIO: ") + program.getInfoLog();
        return false;
    }
    if (!program.buildReflection(EShReflectionAllBlockVariables | EShReflectionSeparateBuffers))
    {
        log = "reflection failed";
        return false;
    }
    for (int i = 0; i < 2; ++i)
    {
        glslang::SpvOptions options;
        options.generateDebugInfo = false;
        options.disableOptimizer = true;
        glslang::GlslangToSpv(*program.getIntermediate(stages[i]), spirv[i], &options);
        if (spirv[i].empty())
        {
            log = "SPIR-V generation failed";
            return false;
        }
    }
    return true;
}
}  // namespace

// Impl helpers

bool VulkanDevice::Impl::CreateHostBuffer(HostBuffer& b, VkDeviceSize size, VkBufferUsageFlags usage, bool cached)
{
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    if (!Check(vkCreateBuffer(device, &info, nullptr, &b.buffer), "vkCreateBuffer"))
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, b.buffer, &req);
    uint32_t type = UINT32_MAX;
    if (cached)
        type = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (type == UINT32_MAX)
        type = MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    if (type == UINT32_MAX || !Check(vkAllocateMemory(device, &alloc, nullptr, &b.memory), "vkAllocateMemory"))
    {
        vkDestroyBuffer(device, b.buffer, nullptr);
        b.buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device, b.buffer, b.memory, 0);
    void* mapped = nullptr;
    vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    b.mapped = static_cast<uint8_t*>(mapped);
    b.size = size;
    return true;
}

void VulkanDevice::Impl::DestroyHostBuffer(HostBuffer& b)
{
    if (b.buffer)
        vkDestroyBuffer(device, b.buffer, nullptr);
    if (b.memory)
        vkFreeMemory(device, b.memory, nullptr);
    b = HostBuffer{};
}

bool VulkanDevice::Impl::Allocate(Arena& arena, VkDeviceSize size, VkDeviceSize align, VkBuffer& buffer,
                                  VkDeviceSize& offset, uint8_t*& mapped)
{
    for (;;)
    {
        if (arena.current < arena.chunks.size())
        {
            HostBuffer& chunk = arena.chunks[arena.current];
            const VkDeviceSize at = (arena.offset + align - 1) / align * align;
            if (at + size <= chunk.size)
            {
                buffer = chunk.buffer;
                offset = at;
                mapped = chunk.mapped + at;
                arena.offset = at + size;
                return true;
            }
            ++arena.current;
            arena.offset = 0;
            continue;
        }
        HostBuffer chunk;
        if (!CreateHostBuffer(chunk, std::max(kArenaChunk, size), arena.usage, false))
            return false;
        arena.chunks.push_back(chunk);
        arena.current = arena.chunks.size() - 1;
        arena.offset = 0;
    }
}

void VulkanDevice::Impl::Collect()
{
    for (Frame& f : frames)
        if (f.submitted && vkGetFenceStatus(device, f.fence) == VK_SUCCESS)
            completedSerial = std::max(completedSerial, f.serial);
    auto done = std::stable_partition(retired.begin(), retired.end(),
                                      [&](const Retired& r) { return r.serial > completedSerial; });
    for (auto it = done; it != retired.end(); ++it)
    {
        for (VkImageView v : it->views)
            if (v)
                vkDestroyImageView(device, v, nullptr);
        if (it->image)
            vkDestroyImage(device, it->image, nullptr);
        if (it->buffer)
            vkDestroyBuffer(device, it->buffer, nullptr);
        if (it->memory)
            vkFreeMemory(device, it->memory, nullptr);
        if (it->pipeline)
            vkDestroyPipeline(device, it->pipeline, nullptr);
        if (it->layout)
            vkDestroyPipelineLayout(device, it->layout, nullptr);
        for (VkDescriptorSetLayout l : it->setLayouts)
            if (l)
                vkDestroyDescriptorSetLayout(device, l, nullptr);
        for (VkShaderModule m : it->modules)
            if (m)
                vkDestroyShaderModule(device, m, nullptr);
    }
    retired.erase(done, retired.end());
}

void VulkanDevice::Impl::Begin()
{
    Frame& f = frames[current];
    if (f.submitted)
    {
        Check(vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        completedSerial = std::max(completedSerial, f.serial);
        f.submitted = false;
    }
    Collect();
    vkResetFences(device, 1, &f.fence);
    vkResetCommandPool(device, f.pool, 0);
    f.uniforms.current = f.staging.current = 0;
    f.uniforms.offset = f.staging.offset = 0;
    for (VkDescriptorPool p : f.pools)
        vkResetDescriptorPool(device, p, 0);
    f.nextPool = 0;
    f.serial = nextSerial;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.commands, &begin);
    recording = true;
}

void VulkanDevice::Impl::Poll()
{
    for (Frame& f : frames)
        if (f.submitted && f.serial > completedSerial && vkGetFenceStatus(device, f.fence) == VK_SUCCESS)
            completedSerial = std::max(completedSerial, f.serial);
}

VkCommandBuffer VulkanDevice::Impl::Commands()
{
    if (!recording)
        Begin();
    return frames[current].commands;
}

void VulkanDevice::Impl::Barrier(VkTexture& texture, VkImageLayout layout)
{
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = texture.layout;
    b.newLayout = layout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = texture.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(Commands(), &dep);
    texture.layout = layout;
}

VkSampler VulkanDevice::Impl::GetSampler(Sampler s, bool integer, bool signedInt)
{
    const Filter filter = integer ? Filter::Nearest : s.filter;
    const auto key = std::make_tuple(static_cast<int>(filter), static_cast<int>(s.wrap), s.mipmap && !integer ? 1 : 0,
                                     integer ? (signedInt ? 2 : 1) : 0);
    auto it = samplers.find(key);
    if (it != samplers.end())
        return it->second;
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = info.minFilter = filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = filter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerAddressMode mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    switch (s.wrap)
    {
        case Wrap::ClampToEdge:
            mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            break;
        case Wrap::ClampToBorder:
            mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            break;
        case Wrap::Repeat:
            mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            break;
        case Wrap::MirroredRepeat:
            mode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            break;
    }
    info.addressModeU = info.addressModeV = info.addressModeW = mode;
    info.borderColor = integer ? VK_BORDER_COLOR_INT_TRANSPARENT_BLACK : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.minLod = 0.0f;
    info.maxLod = s.mipmap && !integer ? VK_LOD_CLAMP_NONE : 0.0f;
    VkSampler sampler = VK_NULL_HANDLE;
    if (!Check(vkCreateSampler(device, &info, nullptr, &sampler), "vkCreateSampler"))
        return VK_NULL_HANDLE;
    samplers[key] = sampler;
    return sampler;
}

VkDescriptorSet VulkanDevice::Impl::AllocateSet(VkDescriptorSetLayout layout)
{
    Frame& f = frames[current];
    for (;;)
    {
        if (f.nextPool == f.pools.size())
        {
            const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048},
                                                   {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512}};
            VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            info.maxSets = 1024;
            info.poolSizeCount = 2;
            info.pPoolSizes = sizes;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (!Check(vkCreateDescriptorPool(device, &info, nullptr, &pool), "vkCreateDescriptorPool"))
                return VK_NULL_HANDLE;
            f.pools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = f.pools[f.nextPool];
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult r = vkAllocateDescriptorSets(device, &alloc, &set);
        if (r == VK_SUCCESS)
            return set;
        if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL)
        {
            Check(r, "vkAllocateDescriptorSets");
            return VK_NULL_HANDLE;
        }
        ++f.nextPool;
    }
}

// The device

VulkanDevice::VulkanDevice() : _impl(std::make_unique<Impl>())
{
    _impl->owner = this;
}

VkInstance VulkanDevice::CreateInstance(bool surfaces, std::string& error)
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
    auto has = [&](const char* name) {
        return std::any_of(available.begin(), available.end(),
                           [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
    };
    std::vector<const char*> extensions;
    VkInstanceCreateFlags flags = 0;
    if (has(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    if (has(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (surfaces)
    {
        static const char* const kSurfaces[] = {"VK_KHR_surface",         "VK_EXT_metal_surface",
                                                "VK_KHR_wayland_surface", "VK_KHR_xcb_surface",
                                                "VK_KHR_xlib_surface",    "VK_KHR_win32_surface",
                                                "VK_EXT_surface_maintenance1", "VK_KHR_get_surface_capabilities2"};
        for (const char* name : kSurfaces)
            if (has(name))
                extensions.push_back(name);
        if (!has("VK_KHR_surface"))
        {
            error = "Vulkan: no VK_KHR_surface";
            return VK_NULL_HANDLE;
        }
    }
    std::vector<const char*> layers;
    if (EnvOn("UNREAL_VULKAN_VALIDATION"))
        layers.push_back("VK_LAYER_KHRONOS_validation");

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "unreal-ng";
    app.pEngineName = "unreal-ng";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.flags = flags;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    VkInstance instance = VK_NULL_HANDLE;
    VkResult r = vkCreateInstance(&info, nullptr, &instance);
    if (r == VK_ERROR_LAYER_NOT_PRESENT)
    {
        info.enabledLayerCount = 0;
        r = vkCreateInstance(&info, nullptr, &instance);
    }
    if (r != VK_SUCCESS)
    {
        error = "vkCreateInstance: " + std::to_string(r);
        return VK_NULL_HANDLE;
    }
    return instance;
}

namespace
{
/// Linux: the GPU the firmware brought the display up on (`boot_vga` of its PCI device), through VK_EXT_pci_bus_info.
/// Elsewhere, or not told: false
bool DrivesDisplay(VkPhysicalDevice gpu)
{
#ifdef __linux__
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data());
    const bool pci = std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& e) {
        return std::strcmp(e.extensionName, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME) == 0;
    });
    if (!pci)
        return false;
    VkPhysicalDevicePCIBusInfoPropertiesEXT bus{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &bus};
    vkGetPhysicalDeviceProperties2(gpu, &properties);
    char path[96];
    std::snprintf(path, sizeof(path), "/sys/bus/pci/devices/%04x:%02x:%02x.%x/boot_vga", bus.pciDomain, bus.pciBus,
                  bus.pciDevice, bus.pciFunction);
    std::FILE* file = std::fopen(path, "r");
    if (!file)
        return false;
    const int flag = std::fgetc(file);
    std::fclose(file);
    return flag == '1';
#else
    (void)gpu;
    return false;
#endif
}
}  // namespace

std::unique_ptr<VulkanDevice> VulkanDevice::Create(const Setup& setup, std::string& error)
{
    std::unique_ptr<VulkanDevice> d(new VulkanDevice());
    d->_instance = setup.instance;
    if (!d->_instance)
    {
        d->_instance = CreateInstance(false, error);
        if (!d->_instance)
            return nullptr;
        d->_ownInstance = true;
    }

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(d->_instance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(d->_instance, &count, gpus.data());
    std::string preferred = setup.preferredDevice;
    if (preferred.empty())
    {
        const char* env = std::getenv("UNREAL_VULKAN_DEVICE");
        preferred = env ? env : "";
    }
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    preferred = lower(preferred);
    int bestScore = -1;
    for (VkPhysicalDevice gpu : gpus)
    {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(gpu, &p);
        if (p.apiVersion < VK_API_VERSION_1_3)
            continue;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f13};
        vkGetPhysicalDeviceFeatures2(gpu, &f2);
        if (!f13.dynamicRendering || !f13.synchronization2)
            continue;
        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &families, props.data());
        for (uint32_t i = 0; i < families; ++i)
        {
            if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                continue;
            if (setup.surface)
            {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, setup.surface, &present);
                if (!present)
                    continue;
            }
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                              : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                       : 1;
            // A window: the GPU the display hangs on before the faster one (a hybrid laptop's discrete GPU hands each
            // frame over to the integrated one the compositor runs on: .238's NVIDIA showed one frame in ten a refresh
            // or two late on Wayland, its Intel almost none; GL draws on that GPU too)
            if (setup.surface && DrivesDisplay(gpu))
                score += 5;
            const bool named = !preferred.empty() && lower(p.deviceName).find(preferred) != std::string::npos;
            score += named ? 10 : 0;
            if (score > bestScore)
            {
                bestScore = score;
                d->_gpu = gpu;
                d->_family = i;
            }
            break;
        }
    }
    if (!d->_gpu)
    {
        error = "Vulkan: no Vulkan 1.3 GPU with dynamic rendering" +
                std::string(setup.surface ? " presenting to the window" : "");
        return nullptr;
    }

    count = 0;
    vkEnumerateDeviceExtensionProperties(d->_gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(d->_gpu, nullptr, &count, available.data());
    auto has = [&](const char* name) {
        return std::any_of(available.begin(), available.end(),
                           [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
    };
    std::vector<const char*> extensions;
    if (has("VK_KHR_portability_subset"))
        extensions.push_back("VK_KHR_portability_subset");
    if (setup.surface)
    {
        if (!has(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            error = "Vulkan: no VK_KHR_swapchain";
            return nullptr;
        }
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    for (const char* name : setup.optionalExtensions)
        if (has(name))
            extensions.push_back(name);

    // present_id / present_wait need their feature bits as well
    VkPhysicalDevicePresentIdFeaturesKHR presentIdQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR presentWaitQuery{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    presentIdQuery.pNext = &presentWaitQuery;
#ifdef VK_EXT_present_timing
    // present_timing (Mesa 26.1+): a present at an absolute time, the compositor holding the commit for it
    VkPhysicalDevicePresentId2FeaturesKHR presentId2Query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR};
    VkPhysicalDevicePresentTimingFeaturesEXT timingQuery{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT};
    presentWaitQuery.pNext = &presentId2Query;
    presentId2Query.pNext = &timingQuery;
#endif
    VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &presentIdQuery};
    vkGetPhysicalDeviceFeatures2(d->_gpu, &query);
    auto enabled = [&](const char* name) {
        return std::any_of(extensions.begin(), extensions.end(),
                           [&](const char* e) { return std::strcmp(e, name) == 0; });
    };
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDevicePresentIdFeaturesKHR presentId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR presentWait{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    void** tail = &f13.pNext;
    if (enabled(VK_KHR_PRESENT_ID_EXTENSION_NAME) && presentIdQuery.presentId)
    {
        presentId.presentId = VK_TRUE;
        *tail = &presentId;
        tail = &presentId.pNext;
    }
    else
        extensions.erase(std::remove_if(extensions.begin(), extensions.end(),
                                        [](const char* e) { return std::strcmp(e, VK_KHR_PRESENT_ID_EXTENSION_NAME) == 0 ||
                                                                   std::strcmp(e, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) == 0; }),
                         extensions.end());
    if (enabled(VK_KHR_PRESENT_WAIT_EXTENSION_NAME) && presentWaitQuery.presentWait)
    {
        presentWait.presentWait = VK_TRUE;
        *tail = &presentWait;
        tail = &presentWait.pNext;
    }
    else
        extensions.erase(std::remove_if(extensions.begin(), extensions.end(),
                                        [](const char* e) { return std::strcmp(e, VK_KHR_PRESENT_WAIT_EXTENSION_NAME) == 0; }),
                         extensions.end());

#ifdef VK_EXT_present_timing
    VkPhysicalDevicePresentId2FeaturesKHR presentId2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR};
    VkPhysicalDevicePresentTimingFeaturesEXT timing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT};
    const bool timed = enabled(VK_EXT_PRESENT_TIMING_EXTENSION_NAME) && enabled(VK_KHR_PRESENT_ID_2_EXTENSION_NAME) &&
                       enabled(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) && presentId2Query.presentId2 &&
                       timingQuery.presentTiming && timingQuery.presentAtAbsoluteTime;
    if (timed)
    {
        presentId2.presentId2 = VK_TRUE;
        timing.presentTiming = VK_TRUE;
        timing.presentAtAbsoluteTime = VK_TRUE;
        *tail = &presentId2;
        presentId2.pNext = &timing;
    }
    else
        extensions.erase(std::remove_if(extensions.begin(), extensions.end(),
                                        [](const char* e) {
                                            return std::strcmp(e, VK_EXT_PRESENT_TIMING_EXTENSION_NAME) == 0 ||
                                                   std::strcmp(e, VK_KHR_PRESENT_ID_2_EXTENSION_NAME) == 0;
                                        }),
                         extensions.end());
#endif

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = d->_family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &f13;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    const VkResult r = vkCreateDevice(d->_gpu, &info, nullptr, &d->_device);
    if (r != VK_SUCCESS)
    {
        error = "vkCreateDevice: " + std::to_string(r);
        return nullptr;
    }
    vkGetDeviceQueue(d->_device, d->_family, 0, &d->_queue);

    Impl& impl = *d->_impl;
    impl.device = d->_device;
    for (const char* e : extensions)
        impl.extensions.emplace_back(e);
    vkGetPhysicalDeviceProperties(d->_gpu, &impl.properties);
    vkGetPhysicalDeviceMemoryProperties(d->_gpu, &impl.memory);
    // In memory only: the driver keeps its own on disk (NVIDIA, Mesa); this one spares a pipeline compiled again
    const VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if (vkCreatePipelineCache(d->_device, &cacheInfo, nullptr, &impl.pipelineCache) != VK_SUCCESS)
        impl.pipelineCache = VK_NULL_HANDLE;  // pipelines made without it
    {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d->_gpu, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d->_gpu, &familyCount, families.data());
        if (d->_family < familyCount)
            impl.timestampBits = families[d->_family].timestampValidBits;
    }
    for (Frame& f : impl.frames)
    {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = d->_family;
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (vkCreateCommandPool(d->_device, &pool, nullptr, &f.pool) != VK_SUCCESS ||
            (alloc.commandPool = f.pool, vkAllocateCommandBuffers(d->_device, &alloc, &f.commands)) != VK_SUCCESS ||
            vkCreateFence(d->_device, &fence, nullptr, &f.fence) != VK_SUCCESS)
        {
            error = "Vulkan: command pool / fence";
            return nullptr;
        }
        f.uniforms.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        f.staging.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (!impl.CreateHostBuffer(impl.quad, sizeof(kQuadVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false))
    {
        error = "Vulkan: vertex buffer";
        return nullptr;
    }
    std::memcpy(impl.quad.mapped, kQuadVertices, sizeof(kQuadVertices));

    auto dummy = [&](Format format) {
        auto t = d->CreateTexture(1, 1, format);
        return std::unique_ptr<VkTexture>(static_cast<VkTexture*>(t.release()));
    };
    impl.dummyFloat = dummy(Format::RGBA8);
    impl.dummyUint = dummy(Format::RGBA32UI);
    impl.dummyInt = dummy(Format::RGBA32I);
    if (!impl.dummyFloat || !impl.dummyUint || !impl.dummyInt)
    {
        error = "Vulkan: placeholder textures";
        return nullptr;
    }
    return d;
}

VulkanDevice::~VulkanDevice()
{
    if (_device)
    {
        vkDeviceWaitIdle(_device);
        Impl& impl = *_impl;
        if (impl.timestamps)
            vkDestroyQueryPool(_device, impl.timestamps, nullptr);
        impl.dummyFloat.reset();
        impl.dummyUint.reset();
        impl.dummyInt.reset();
        impl.completedSerial = UINT64_MAX;
        for (Frame& f : impl.frames)
            f.submitted = false;
        impl.Collect();
        for (auto& [key, sampler] : impl.samplers)
            vkDestroySampler(_device, sampler, nullptr);
        for (Frame& f : impl.frames)
        {
            for (HostBuffer& b : f.uniforms.chunks)
                impl.DestroyHostBuffer(b);
            for (HostBuffer& b : f.staging.chunks)
                impl.DestroyHostBuffer(b);
            for (VkDescriptorPool p : f.pools)
                vkDestroyDescriptorPool(_device, p, nullptr);
            if (f.fence)
                vkDestroyFence(_device, f.fence, nullptr);
            if (f.pool)
                vkDestroyCommandPool(_device, f.pool, nullptr);
        }
        impl.DestroyHostBuffer(impl.quad);
        impl.DestroyHostBuffer(impl.readback);
        if (impl.pipelineCache)
            vkDestroyPipelineCache(_device, impl.pipelineCache, nullptr);
        vkDestroyDevice(_device, nullptr);
    }
    if (_ownInstance && _instance)
        vkDestroyInstance(_instance, nullptr);
}

std::string VulkanDevice::Describe() const
{
    const VkPhysicalDeviceProperties& p = _impl->properties;
    return "Vulkan " + std::to_string(VK_API_VERSION_MAJOR(p.apiVersion)) + "." +
           std::to_string(VK_API_VERSION_MINOR(p.apiVersion)) + "." +
           std::to_string(VK_API_VERSION_PATCH(p.apiVersion)) + ", " + p.deviceName + ", driver " +
           std::to_string(p.driverVersion);
}

bool VulkanDevice::Enabled(const char* extension) const
{
    return std::find(_impl->extensions.begin(), _impl->extensions.end(), extension) != _impl->extensions.end();
}

void VulkanDevice::MarkLost(const char* /*where*/, VkResult /*result*/)
{
    _lost = true;
}

std::unique_ptr<Texture> VulkanDevice::CreateTexture(int width, int height, Format format, int mipLevels)
{
    if (_lost || width <= 0 || height <= 0)
        return nullptr;
    Impl& impl = *_impl;
    auto t = std::make_unique<VkTexture>(&impl, width, height, format, std::max(1, mipLevels));
    t->format = ToVk(format);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = t->format;
    info.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    info.mipLevels = static_cast<uint32_t>(t->MipLevels());
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!impl.Check(vkCreateImage(_device, &info, nullptr, &t->image), "vkCreateImage"))
        return nullptr;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(_device, t->image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = impl.MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX)
        alloc.memoryTypeIndex = impl.MemoryType(req.memoryTypeBits, 0);
    if (!impl.Check(vkAllocateMemory(_device, &alloc, nullptr, &t->memory), "vkAllocateMemory"))
        return nullptr;
    vkBindImageMemory(_device, t->image, t->memory, 0);
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = t->image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = t->format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, info.mipLevels, 0, 1};
    if (!impl.Check(vkCreateImageView(_device, &view, nullptr, &t->view), "vkCreateImageView"))
        return nullptr;
    view.subresourceRange.levelCount = 1;
    if (!impl.Check(vkCreateImageView(_device, &view, nullptr, &t->attachView), "vkCreateImageView"))
        return nullptr;

    // Zero, as GL's glTexImage2D with no data reads in practice (the passes rely on black first frames)
    impl.Barrier(*t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearColorValue zero{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, info.mipLevels, 0, 1};
    vkCmdClearColorImage(impl.Commands(), t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    return t;
}

void VulkanDevice::Upload(Texture& texture, Rect rect, const void* data, int rowTexels)
{
    if (_lost || rect.Empty() || !data)
        return;
    Impl& impl = *_impl;
    auto& t = static_cast<VkTexture&>(texture);
    const int texel = BytesPerTexel(t.GetFormat());
    const int row = rowTexels > 0 ? rowTexels : rect.width;
    const VkDeviceSize rowBytes = static_cast<VkDeviceSize>(rect.width) * texel;
    impl.Commands();  // the staging belongs to the recording frame
    const auto* src = static_cast<const uint8_t*>(data);
    VkBuffer stagingBuffer;
    VkDeviceSize stagingOffset;
    uint8_t* stagingMapped;
    if (!impl.Allocate(impl.frames[impl.current].staging, rowBytes * rect.height, 16, stagingBuffer, stagingOffset,
                       stagingMapped))
        return;
    for (int y = 0; y < rect.height; ++y)
        std::memcpy(stagingMapped + y * rowBytes, src + static_cast<size_t>(y) * row * texel, rowBytes);
    impl.Barrier(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffset;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {rect.x, rect.y, 0};
    region.imageExtent = {static_cast<uint32_t>(rect.width), static_cast<uint32_t>(rect.height), 1};
    vkCmdCopyBufferToImage(impl.Commands(), stagingBuffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

bool VulkanDevice::Read(Texture& texture, Rect rect, void* out)
{
    if (_lost || rect.Empty() || !out)
        return false;
    Impl& impl = *_impl;
    auto& t = static_cast<VkTexture&>(texture);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(rect.width) * rect.height * BytesPerTexel(t.GetFormat());
    if (impl.readback.size < bytes)
    {
        Finish();
        impl.DestroyHostBuffer(impl.readback);
        if (!impl.CreateHostBuffer(impl.readback, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
            return false;
    }
    impl.Barrier(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {rect.x, rect.y, 0};
    region.imageExtent = {static_cast<uint32_t>(rect.width), static_cast<uint32_t>(rect.height), 1};
    vkCmdCopyImageToBuffer(impl.Commands(), t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, impl.readback.buffer, 1,
                           &region);
    VkMemoryBarrier2 host{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &host;
    vkCmdPipelineBarrier2(impl.Commands(), &dep);
    Finish();
    if (_lost)
        return false;
    std::memcpy(out, impl.readback.mapped, bytes);
    return true;
}

void VulkanDevice::GenerateMipmaps(Texture& texture)
{
    if (_lost || texture.MipLevels() < 2)
        return;
    Impl& impl = *_impl;
    auto& t = static_cast<VkTexture&>(texture);
    VkCommandBuffer commands = impl.Commands();
    impl.Barrier(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    auto level = [&](uint32_t mip, VkImageLayout from, VkImageLayout to) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(commands, &dep);
    };
    int w = t.Width();
    int h = t.Height();
    for (int mip = 1; mip < t.MipLevels(); ++mip)
    {
        level(static_cast<uint32_t>(mip - 1), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const int nw = std::max(1, w / 2);
        const int nh = std::max(1, h / 2);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(mip - 1), 0, 1};
        blit.srcOffsets[1] = {w, h, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(mip), 0, 1};
        blit.dstOffsets[1] = {nw, nh, 1};
        vkCmdBlitImage(commands, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        w = nw;
        h = nh;
    }
    // Levels 0 .. n-2 are TRANSFER_SRC, the last TRANSFER_DST: one layout for all of them again
    level(static_cast<uint32_t>(t.MipLevels() - 1), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    t.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

void VulkanDevice::Blit(Texture& source, Rect from, Texture& target, Rect to, Filter filter)
{
    if (_lost || from.Empty() || to.width <= 0 || to.height == 0)
        return;
    Impl& impl = *_impl;
    auto& s = static_cast<VkTexture&>(source);
    auto& d = static_cast<VkTexture&>(target);
    if (&s == &d)
        return;
    impl.Barrier(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    impl.Barrier(d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {from.x, from.y, 0};
    blit.srcOffsets[1] = {from.x + from.width, from.y + from.height, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    if (to.height > 0)
    {
        blit.dstOffsets[0] = {to.x, to.y, 0};
        blit.dstOffsets[1] = {to.x + to.width, to.y + to.height, 1};
    }
    else
    {
        blit.dstOffsets[0] = {to.x, to.y - to.height, 0};
        blit.dstOffsets[1] = {to.x + to.width, to.y, 1};
    }
    const bool integer = IsInteger(s.GetFormat());
    vkCmdBlitImage(impl.Commands(), s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   filter == Filter::Linear && !integer ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
}

void VulkanDevice::Clear(Texture& texture, float r, float g, float b, float a)
{
    if (_lost)
        return;
    Impl& impl = *_impl;
    auto& t = static_cast<VkTexture&>(texture);
    impl.Barrier(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue value{};
    if (!IsInteger(t.GetFormat()))
    {
        value.float32[0] = r;
        value.float32[1] = g;
        value.float32[2] = b;
        value.float32[3] = a;
    }
    else if (IsSigned(t.GetFormat()))
    {
        value.int32[0] = static_cast<int32_t>(r);
        value.int32[1] = static_cast<int32_t>(g);
        value.int32[2] = static_cast<int32_t>(b);
        value.int32[3] = static_cast<int32_t>(a);
    }
    else
    {
        value.uint32[0] = static_cast<uint32_t>(r);
        value.uint32[1] = static_cast<uint32_t>(g);
        value.uint32[2] = static_cast<uint32_t>(b);
        value.uint32[3] = static_cast<uint32_t>(a);
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
    vkCmdClearColorImage(impl.Commands(), t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
}

std::unique_ptr<Program> VulkanDevice::CreateProgram(const std::string& vertex, const std::string& fragment,
                                                     std::string& log)
{
    if (_lost)
    {
        log = "Vulkan device lost";
        return nullptr;
    }
    Impl& impl = *_impl;
    std::string key = vertex;
    key += '\0';
    key += fragment;
    auto found = impl.compiled.find(key);
    if (found == impl.compiled.end())
    {
        CompiledProgram compiled;
        glslang::TProgram linked;
        std::vector<std::unique_ptr<glslang::TShader>> shaders;
        if (!CompileProgram(vertex, fragment, compiled.spirv, linked, shaders, log))
            return nullptr;
        // The uniform block (set 1, binding 0) and the samplers (set 0, their bindings)
        for (int i = 0; i < linked.getNumUniformBlocks(); ++i)
            compiled.blockSize = std::max(compiled.blockSize, static_cast<uint32_t>(linked.getUniformBlock(i).size));
        for (int i = 0; i < linked.getNumUniformVariables(); ++i)
        {
            const glslang::TObjectReflection& u = linked.getUniform(i);
            const int type = u.glDefineType;
            if (u.index >= 0 && u.offset >= 0)
                compiled.uniforms.push_back({VkProgram::Key(u.name.c_str()), static_cast<uint32_t>(u.offset),
                                             type == kGlFloatMat4 ? 64u : 16u,  // std140 arrays
                                             std::max(1, u.size),
                                             type == kGlFloat || (type >= 0x8B50 && type <= 0x8B52) ||
                                                 type == kGlFloatMat4 || (type >= 0x8B5A && type <= 0x8B5C)});
            else if (u.getBinding() >= 0)
                compiled.samplers.push_back(
                    {VkProgram::Key(u.name.c_str()), static_cast<uint32_t>(u.getBinding()), type});
        }
        if (impl.compiled.size() >= kCompiledProgramsMax)
            impl.compiled.clear();
        found = impl.compiled.emplace(std::move(key), std::move(compiled)).first;
    }
    const CompiledProgram& compiled = found->second;
    const std::vector<uint32_t>(&spirv)[2] = compiled.spirv;

    auto p = std::make_unique<VkProgram>(&impl);
    const uint32_t blockSize = compiled.blockSize;
    std::vector<VkDescriptorSetLayoutBinding> samplerBindings;
    for (const CompiledProgram::UniformInfo& u : compiled.uniforms)
    {
        Uniform entry;
        entry.offset = u.offset;
        entry.count = u.count;
        entry.stride = u.stride;
        entry.isFloat = u.isFloat;
        p->uniforms[u.name] = entry;
    }
    for (const CompiledProgram::SamplerInfo& u : compiled.samplers)
    {
        SamplerSlot slot;
        slot.binding = u.binding;
        slot.glType = u.glType;
        p->samplerIndex[u.name] = p->samplers.size();
        p->samplers.push_back(slot);
        VkDescriptorSetLayoutBinding b{};
        b.binding = slot.binding;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerBindings.push_back(b);
    }
    p->data.assign(std::max<uint32_t>(blockSize, 16), 0);

    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = static_cast<uint32_t>(samplerBindings.size());
    setInfo.pBindings = samplerBindings.data();
    if (!impl.Check(vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &p->setLayouts[0]), "set layout"))
        return nullptr;
    VkDescriptorSetLayoutBinding ubo{};
    ubo.binding = 0;
    ubo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo.descriptorCount = 1;
    ubo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    setInfo.bindingCount = 1;
    setInfo.pBindings = &ubo;
    if (!impl.Check(vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &p->setLayouts[1]), "set layout"))
        return nullptr;
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = p->setLayouts;
    if (!impl.Check(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &p->layout), "pipeline layout"))
        return nullptr;
    for (int i = 0; i < 2; ++i)
    {
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = spirv[i].size() * sizeof(uint32_t);
        module.pCode = spirv[i].data();
        if (!impl.Check(vkCreateShaderModule(_device, &module, nullptr, &p->modules[i]), "vkCreateShaderModule"))
        {
            log = "vkCreateShaderModule failed";
            return nullptr;
        }
    }
    return p;
}

void VulkanDevice::Draw(Program& program, Texture& target, const DrawState& state)
{
    if (_lost || state.viewport.Empty())
        return;
    Impl& impl = *_impl;
    auto& p = static_cast<VkProgram&>(program);
    auto& t = static_cast<VkTexture&>(target);
    VkCommandBuffer commands = impl.Commands();
    Frame& frame = impl.frames[impl.current];

    const Blend blend = IsInteger(t.GetFormat()) ? Blend::None : state.blend;
    VkPipeline pipeline = p.Pipeline(t.format, blend);
    if (!pipeline)
        return;

    // The textures to read first: their barriers go before the rendering starts
    std::vector<VkDescriptorImageInfo> images(p.samplers.size());
    for (size_t i = 0; i < p.samplers.size(); ++i)
    {
        const SamplerSlot& slot = p.samplers[i];
        auto* texture = static_cast<VkTexture*>(slot.texture);
        if (texture == &t)
            texture = nullptr;  // a feedback loop: GL leaves it undefined, here it reads nothing
        if (!texture)
            texture = slot.glType == kGlUnsignedIntSampler2D ? impl.dummyUint.get()
                      : slot.glType == kGlIntSampler2D       ? impl.dummyInt.get()
                                                             : impl.dummyFloat.get();
        if (texture->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            impl.Barrier(*texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const bool integer = IsInteger(texture->GetFormat());
        images[i].sampler = impl.GetSampler(slot.sampler, integer, IsSigned(texture->GetFormat()));
        images[i].imageView = texture->view;
        images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    impl.Barrier(t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkBuffer uboBuffer;
    VkDeviceSize uboOffset;
    uint8_t* uboMapped;
    if (!impl.Allocate(frame.uniforms, p.data.size(), impl.properties.limits.minUniformBufferOffsetAlignment,
                       uboBuffer, uboOffset, uboMapped))
        return;
    std::memcpy(uboMapped, p.data.data(), p.data.size());

    VkDescriptorSet sets[2] = {impl.AllocateSet(p.setLayouts[0]), impl.AllocateSet(p.setLayouts[1])};
    if (!sets[0] || !sets[1])
        return;
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(p.samplers.size() + 1);
    for (size_t i = 0; i < p.samplers.size(); ++i)
    {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = sets[0];
        w.dstBinding = p.samplers[i].binding;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &images[i];
        writes.push_back(w);
    }
    const VkDescriptorBufferInfo bufferInfo{uboBuffer, uboOffset, p.data.size()};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = sets[1];
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bufferInfo;
    writes.push_back(w);
    vkUpdateDescriptorSets(_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = t.attachView;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, {static_cast<uint32_t>(t.Width()), static_cast<uint32_t>(t.Height())}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(commands, &rendering);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 2, sets, 0, nullptr);
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(commands, 0, 1, &impl.quad.buffer, &zero);
    const VkViewport viewport{static_cast<float>(state.viewport.x),
                              static_cast<float>(state.viewport.y),
                              static_cast<float>(state.viewport.width),
                              static_cast<float>(state.viewport.height),
                              0.0f,
                              1.0f};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    // GL's scissor test off is the whole target; on, the rectangle clipped to the target
    int x0 = 0, y0 = 0, x1 = t.Width(), y1 = t.Height();
    if (state.scissor)
    {
        x0 = std::clamp(state.scissorRect.x, 0, t.Width());
        y0 = std::clamp(state.scissorRect.y, 0, t.Height());
        x1 = std::clamp(state.scissorRect.x + state.scissorRect.width, x0, t.Width());
        y1 = std::clamp(state.scissorRect.y + state.scissorRect.height, y0, t.Height());
    }
    const VkRect2D scissor{{x0, y0}, {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}};
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdDraw(commands, 4, 1, 0, 0);
    vkCmdEndRendering(commands);
}

void VulkanDevice::Submit(VkSemaphore wait, VkSemaphore signal)
{
    if (_lost)
        return;
    Impl& impl = *_impl;
    VkCommandBuffer commands = impl.Commands();
    Frame& frame = impl.frames[impl.current];
    vkEndCommandBuffer(commands);
    VkCommandBufferSubmitInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cb.commandBuffer = commands;
    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = wait;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = signal;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cb;
    submit.waitSemaphoreInfoCount = wait ? 1 : 0;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.signalSemaphoreInfoCount = signal ? 1 : 0;
    submit.pSignalSemaphoreInfos = &signalInfo;
    VkResult r;
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        r = vkQueueSubmit2(_queue, 1, &submit, frame.fence);
    }
    impl.recording = false;
    if (!impl.Check(r, "vkQueueSubmit2"))
        return;
    frame.submitted = true;
    ++impl.nextSerial;
    impl.current = (impl.current + 1) % kFrames;
}

int VulkanDevice::MaxTextureSize() const
{
    return static_cast<int>(_impl->properties.limits.maxImageDimension2D);
}

bool VulkanDevice::CreateTimestamps(int count)
{
    DestroyTimestamps(false);
    Impl& impl = *_impl;
    if (_lost || count <= 0 || impl.timestampBits == 0 || impl.properties.limits.timestampPeriod <= 0.0f)
        return false;
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = static_cast<uint32_t>(count);
    if (!impl.Check(vkCreateQueryPool(_device, &info, nullptr, &impl.timestamps), "vkCreateQueryPool"))
    {
        impl.timestamps = VK_NULL_HANDLE;
        return false;
    }
    impl.timestampSerial.assign(static_cast<size_t>(count), 0);
    return true;
}

void VulkanDevice::DestroyTimestamps(bool lost)
{
    Impl& impl = *_impl;
    if (!impl.timestamps)
        return;
    // A recorded write may still be in flight: the pool waits for the work that uses it. `lost` is the caller's
    // (a GL context reset): a device that still works runs that work first
    (void)lost;
    if (!_lost)
        Finish();
    vkDestroyQueryPool(_device, impl.timestamps, nullptr);
    impl.timestamps = VK_NULL_HANDLE;
    impl.timestampSerial.clear();
}

void VulkanDevice::WriteTimestamp(int index)
{
    Impl& impl = *_impl;
    if (_lost || !impl.timestamps || index < 0 || static_cast<size_t>(index) >= impl.timestampSerial.size())
        return;
    // Between draws (each draw is a rendering of its own): outside a render pass, where the reset is allowed
    VkCommandBuffer commands = impl.Commands();
    vkCmdResetQueryPool(commands, impl.timestamps, static_cast<uint32_t>(index), 1);
    vkCmdWriteTimestamp2(commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, impl.timestamps,
                         static_cast<uint32_t>(index));
    impl.timestampSerial[static_cast<size_t>(index)] = impl.nextSerial;
}

bool VulkanDevice::ReadTimestamp(int index, uint64_t& ns)
{
    ns = 0;
    Impl& impl = *_impl;
    if (_lost || !impl.timestamps || index < 0 || static_cast<size_t>(index) >= impl.timestampSerial.size())
        return false;
    const uint64_t serial = impl.timestampSerial[static_cast<size_t>(index)];
    if (serial == 0)
        return false;
    // Not before its submission finished: until then the query may still show its last use (the reset is recorded,
    // not done)
    if (serial > impl.completedSerial)
        impl.Poll();
    if (serial > impl.completedSerial)
        return false;
    uint64_t ticks = 0;
    if (vkGetQueryPoolResults(_device, impl.timestamps, static_cast<uint32_t>(index), 1, sizeof(ticks), &ticks,
                              sizeof(ticks), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return false;
    if (impl.timestampBits < 64)
        ticks &= (uint64_t{1} << impl.timestampBits) - 1;
    ns = static_cast<uint64_t>(static_cast<double>(ticks) * impl.properties.limits.timestampPeriod);
    return true;
}

void VulkanDevice::Flush()
{
    if (!_impl->recording)
        return;
    Submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
}

void VulkanDevice::Finish()
{
    Flush();
    if (_lost)
        return;
    Impl& impl = *_impl;
    for (Frame& f : impl.frames)
        if (f.submitted)
        {
            impl.Check(vkWaitForFences(_device, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
            impl.completedSerial = std::max(impl.completedSerial, f.serial);
        }
    impl.Collect();
}

void VulkanDevice::RecordPresentBlit(Texture& source, VkImage image, VkExtent2D extent, VkImageLayout oldLayout)
{
    if (_lost)
        return;
    Impl& impl = *_impl;
    auto& s = static_cast<VkTexture&>(source);
    VkCommandBuffer commands = impl.Commands();
    impl.Barrier(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    auto barrier = [&](VkImageLayout from, VkImageLayout to) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(commands, &dep);
    };
    barrier(oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {s.Width(), s.Height(), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, static_cast<int32_t>(extent.height), 0};
    blit.dstOffsets[1] = {static_cast<int32_t>(extent.width), 0, 1};
    const bool scaled = s.Width() != static_cast<int>(extent.width) || s.Height() != static_cast<int>(extent.height);
    vkCmdBlitImage(commands, s.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, scaled ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
}  // namespace gpu
