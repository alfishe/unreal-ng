#pragma once

#include <vulkan/vulkan.h>

#include <string>

class QWindow;

namespace gpu
{
/// The window's Vulkan surface (the instance made by VulkanDevice::CreateInstance(true)): macOS the CAMetalLayer of
/// a QSurface::MetalSurface window (VK_EXT_metal_surface), Windows its HWND, Linux the X11 window or the Wayland
/// wl_surface. Null with `error`; the caller destroys it (vkDestroySurfaceKHR) before the instance
VkSurfaceKHR CreateWindowSurface(VkInstance instance, QWindow* window, std::string& error);
}  // namespace gpu
