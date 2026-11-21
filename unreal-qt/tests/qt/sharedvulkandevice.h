#pragma once
/// @file sharedvulkandevice.h
/// @brief One Vulkan device for a test suite: making one (instance, device, the shader compiler) takes ~150 ms, a test
/// on it a few. A test leaves nothing on it (its textures and programs go with the test); the suite releases it in
/// TearDownTestSuite.

#ifdef UNREAL_VULKAN

#include <memory>
#include <string>

#include "gpu/vulkandevice.h"

namespace testgpu
{
struct SharedVulkan
{
    std::unique_ptr<gpu::Device> device;
    std::string error;
    bool asked = false;
};

inline SharedVulkan& Shared()
{
    static SharedVulkan shared;
    return shared;
}

/// The suite's device, made on the first ask; null with `error` when there is no Vulkan 1.3 GPU (asked once a suite)
inline gpu::Device* SharedVulkanDevice(std::string& error)
{
    SharedVulkan& shared = Shared();
    if (!shared.asked)
    {
        shared.asked = true;
        shared.device = gpu::VulkanDevice::Create({}, shared.error);
    }
    error = shared.error;
    return shared.device.get();
}

/// Its end (a suite's TearDownTestSuite): the next ask makes one again
inline void ReleaseSharedVulkanDevice()
{
    SharedVulkan& shared = Shared();
    shared.device.reset();
    shared.error.clear();
    shared.asked = false;
}
}  // namespace testgpu

#endif
