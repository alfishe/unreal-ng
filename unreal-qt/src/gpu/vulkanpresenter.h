#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <string>

#include "platform/nativepresenter.h"

class QWindow;

namespace gpu
{
class VulkanDevice;

/// @brief The Vulkan renderer's presenter (docs/inprogress/2026-11-19-vulkan-renderer, section 2.4): the window draws
/// each picture with the VulkanDevice into BeginTexture's texture; PresentFrame blits it into a swapchain image (GUI
/// thread, the device's queue) and a present thread hands that image to vkQueuePresentKHR at the frame's time. FIFO
/// on a fixed display, IMMEDIATE (or FIFO_RELAXED) under VRR. Screen times from VK_GOOGLE_display_timing (MoltenVK)
/// or VK_KHR_present_wait (Mesa, NVIDIA); without either nothing is measured.
///
/// The device and the surface outlive it; BeginTexture, PresentFrame and the destructor on the device's thread
class VulkanPresenter final : public NativePresenter
{
public:
    /// The presenter on `surface` (the device made for it). Null with `error`
    static std::unique_ptr<VulkanPresenter> Create(QWindow* window, VulkanDevice& device, VkSurfaceKHR surface,
                                                   std::string& error);
    ~VulkanPresenter() override;

    unsigned BeginFrame(int width, int height) override;
    gpu::Texture* BeginTexture(int width, int height) override;
    void PresentFrame(int64_t atNs, const TraceInfo& trace) override;
    void SetPolicy(PresentPolicy policy) override;
    bool IsVisible() const override;
    int64_t LeadNs(int64_t refreshPeriodNs) const override;
    bool RefreshGrid(int64_t& refreshNs, int64_t& periodNs) const override;
    uint64_t Presented() const override;
    uint64_t LatePresents() const override;
    uint64_t SteppedPresents() const override;
    bool QueuesPresents() const override;
    bool PresentFailed() const override;
    uint64_t LastLatePresent() const override;
    uint32_t LastLateLag() const override;
    uint64_t LastOnTimePresent() const override;
    std::string LastFailure() const override;
    bool Lost() const override;
    Stats GetStats() const override;
    std::string Describe() const override;

    struct Impl;

private:
    VulkanPresenter();
    std::unique_ptr<Impl> _impl;
};
}  // namespace gpu
