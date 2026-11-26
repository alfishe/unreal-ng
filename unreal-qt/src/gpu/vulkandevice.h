#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gpudevice.h"

namespace gpu
{
/// @brief The Vulkan backend of the GPU layer (Vulkan 1.3: dynamic rendering, synchronization2; MoltenVK on macOS).
///
/// Work is recorded into the current frame's command buffer and goes to the GPU on Flush / Finish / Submit; a ring of
/// frames keeps what the GPU may still read (staging memory, uniform data, descriptor sets, destroyed objects) until
/// its fence signals. Images keep GL's row order (gpudevice.h); each texture's layout is tracked and every use
/// transitions it (full barriers: the passes are few and small).
///
/// Shaders: glslang with the relaxed Vulkan rules; the loose uniforms of both stages are one uniform block, written
/// by name through the reflection's offsets.
class VulkanDevice final : public Device
{
public:
    struct Setup
    {
        VkInstance instance = VK_NULL_HANDLE;  ///< null: an instance of its own (without surface extensions)
        VkSurfaceKHR surface = VK_NULL_HANDLE;  ///< a window's: the queue must present to it, swapchains enabled
        std::vector<const char*> optionalExtensions;  ///< enabled when the GPU has them (present_id, present_wait)
        /// The GPU whose name holds this (any case) first, when there is one; empty: UNREAL_VULKAN_DEVICE, else the
        /// discrete GPU before an integrated one before a software one
        std::string preferredDevice;
    };
    /// An instance with what a window needs (the platform's surface extensions), apiVersion 1.3. Null with `error`.
    /// UNREAL_VULKAN_VALIDATION=1 adds the validation layer
    static VkInstance CreateInstance(bool surfaces, std::string& error);
    /// The device. Null with `error` (no Vulkan 1.3 GPU, no presenting queue, glslang missing)
    static std::unique_ptr<VulkanDevice> Create(const Setup& setup, std::string& error);
    ~VulkanDevice() override;

    Backend GetBackend() const override { return Backend::Vulkan; }
    std::string Describe() const override;
    int MaxTextureSize() const override;

    std::unique_ptr<Texture> CreateTexture(int width, int height, Format format, int mipLevels = 1) override;
    void Upload(Texture& texture, Rect rect, const void* data, int rowTexels = 0) override;
    bool Read(Texture& texture, Rect rect, void* out) override;
    bool BeginRead(int slot, Texture& texture, Rect rect, bool bgra = false) override;
    bool FinishRead(int slot, uint8_t* out, size_t outStride, bool flip) override;
    void GenerateMipmaps(Texture& texture) override;
    void Blit(Texture& source, Rect from, Texture& target, Rect to, Filter filter) override;
    void Clear(Texture& texture, float r, float g, float b, float a) override;
    std::unique_ptr<Program> CreateProgram(const std::string& vertex, const std::string& fragment,
                                           std::string& log) override;
    void Draw(Program& program, Texture& target, const DrawState& state) override;
    void Flush() override;
    void Finish() override;
    bool Lost() const override { return _lost; }
    bool CreateTimestamps(int count) override;
    void DestroyTimestamps(bool lost) override;
    void WriteTimestamp(int index) override;
    bool ReadTimestamp(int index, uint64_t& ns) override;

    // The presenter's part
    VkInstance Instance() const { return _instance; }
    VkPhysicalDevice Physical() const { return _gpu; }
    VkDevice Handle() const { return _device; }
    VkQueue Queue() const { return _queue; }
    uint32_t Family() const { return _family; }
    bool Enabled(const char* extension) const;
    /// vkQueueSubmit and vkQueuePresentKHR from two threads: both under this
    std::mutex& QueueMutex() { return _queueMutex; }
    /// Records the whole of `source` into `image` (`extent`, last in `oldLayout`) turned over (the window's GL rows
    /// into a swapchain's top-first ones) and scaled, left in PRESENT_SRC_KHR
    void RecordPresentBlit(Texture& source, VkImage image, VkExtent2D extent, VkImageLayout oldLayout);
    /// The recorded work to the GPU, waiting for `wait` (null: none) before its transfers and signalling `signal`
    void Submit(VkSemaphore wait, VkSemaphore signal);
    /// A failed call made the device lost (VK_ERROR_DEVICE_LOST): from here on nothing is recorded
    void MarkLost(const char* where, VkResult result);

    struct Impl;

private:
    VulkanDevice();

    VkInstance _instance = VK_NULL_HANDLE;
    bool _ownInstance = false;
    VkPhysicalDevice _gpu = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _queue = VK_NULL_HANDLE;
    uint32_t _family = 0;
    bool _lost = false;
    std::mutex _queueMutex;
    std::unique_ptr<Impl> _impl;
};
}  // namespace gpu
