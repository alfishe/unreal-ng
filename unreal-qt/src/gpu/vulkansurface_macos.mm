#include "vulkansurface.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <vulkan/vulkan_metal.h>

#include <QWindow>

namespace gpu
{
VkSurfaceKHR CreateWindowSurface(VkInstance instance, QWindow* window, std::string& error)
{
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    if (!view || ![view.layer isKindOfClass:[CAMetalLayer class]])
    {
        error = "the window's view has no CAMetalLayer (not a QSurface::MetalSurface window)";
        return VK_NULL_HANDLE;
    }
    auto create = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (!create)
    {
        error = "no VK_EXT_metal_surface";
        return VK_NULL_HANDLE;
    }
    VkMetalSurfaceCreateInfoEXT info{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
    info.pLayer = static_cast<CAMetalLayer*>(view.layer);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult r = create(instance, &info, nullptr, &surface);
    if (r != VK_SUCCESS)
    {
        error = "vkCreateMetalSurfaceEXT: " + std::to_string(r);
        return VK_NULL_HANDLE;
    }
    return surface;
}
}  // namespace gpu
