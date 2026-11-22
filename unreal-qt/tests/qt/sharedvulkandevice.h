#pragma once
/// @file sharedvulkandevice.h
/// @brief One Vulkan device per GPU asked for, for the whole test run: making one (instance, device, the shader
/// compiler) takes ~150-200 ms, a test on it a few, and the programs it compiled stay for the next suite. A test leaves
/// nothing on it (its textures and programs go with the test); main() releases them all after the run.

#ifdef UNREAL_VULKAN

#include <map>
#include <memory>
#include <string>

#include "gpu/vulkandevice.h"

namespace testgpu
{
struct SharedVulkan
{
    std::unique_ptr<gpu::Device> device;
    std::string error;
};

inline std::map<std::string, SharedVulkan>& Shared()
{
    static std::map<std::string, SharedVulkan> shared;
    return shared;
}

/// The run's device for `preferred` (part of a GPU name, empty: the default choice), made on the first ask; null with
/// `error` when there is none (asked once a run)
inline gpu::Device* SharedVulkanDevice(std::string& error, const std::string& preferred = {})
{
    auto [it, made] = Shared().try_emplace(preferred);
    SharedVulkan& shared = it->second;
    if (made)
    {
        gpu::VulkanDevice::Setup setup;
        setup.preferredDevice = preferred;
        shared.device = gpu::VulkanDevice::Create(setup, shared.error);
    }
    error = shared.error;
    return shared.device.get();
}

/// The end of the run (main, before the QApplication goes)
inline void ReleaseSharedVulkanDevices()
{
    Shared().clear();
}
}  // namespace testgpu

#endif
