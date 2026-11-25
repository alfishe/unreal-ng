#include "vulkansurface.h"

#ifndef __APPLE__

#include <QGuiApplication>
#include <QWindow>

#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#elif defined(__linux__)
#if __has_include(<qpa/qplatformnativeinterface.h>)
#include <qpa/qplatformnativeinterface.h>
#define UNREAL_HAS_QPA 1
#endif
// The window systems' handles as the Vulkan headers declare them, without their headers
typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long VisualID;
#include <vulkan/vulkan_xlib.h>
struct wl_display;
struct wl_surface;
#include <vulkan/vulkan_wayland.h>
#endif

namespace gpu
{
namespace
{
template <typename Fn>
[[maybe_unused]] Fn Proc(VkInstance instance, const char* name)
{
    return reinterpret_cast<Fn>(vkGetInstanceProcAddr(instance, name));
}

/// `surface` by reference: read after the create call ran (an argument by value may be copied before it: gcc / MSVC
/// evaluate the arguments right to left)
[[maybe_unused]] VkSurfaceKHR Made(VkResult r, const VkSurfaceKHR& surface, const char* call, std::string& error)
{
    if (r == VK_SUCCESS)
        return surface;
    error = std::string(call) + ": " + std::to_string(r);
    return VK_NULL_HANDLE;
}
}  // namespace

VkSurfaceKHR CreateWindowSurface(VkInstance instance, QWindow* window, std::string& error)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#ifdef _WIN32
    auto create = Proc<PFN_vkCreateWin32SurfaceKHR>(instance, "vkCreateWin32SurfaceKHR");
    if (!create)
    {
        error = "no VK_KHR_win32_surface";
        return VK_NULL_HANDLE;
    }
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hinstance = GetModuleHandleW(nullptr);
    info.hwnd = reinterpret_cast<HWND>(window->winId());
    return Made(create(instance, &info, nullptr, &surface), surface, "vkCreateWin32SurfaceKHR", error);
#elif defined(__linux__) && defined(UNREAL_HAS_QPA)
    QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
    if (!native)
    {
        error = "no platform native interface";
        return VK_NULL_HANDLE;
    }
    const QString platform = QGuiApplication::platformName();
    if (platform.startsWith(QStringLiteral("wayland")))
    {
        auto* display = static_cast<wl_display*>(native->nativeResourceForIntegration("wl_display"));
        auto* wl = static_cast<wl_surface*>(native->nativeResourceForWindow("surface", window));
        auto create = Proc<PFN_vkCreateWaylandSurfaceKHR>(instance, "vkCreateWaylandSurfaceKHR");
        if (!display || !wl || !create)
        {
            error = "no Wayland display, surface or VK_KHR_wayland_surface";
            return VK_NULL_HANDLE;
        }
        VkWaylandSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
        info.display = display;
        info.surface = wl;
        return Made(create(instance, &info, nullptr, &surface), surface, "vkCreateWaylandSurfaceKHR", error);
    }
    if (platform == QStringLiteral("xcb"))
    {
        auto* display = static_cast<Display*>(native->nativeResourceForIntegration("display"));
        auto create = Proc<PFN_vkCreateXlibSurfaceKHR>(instance, "vkCreateXlibSurfaceKHR");
        if (!display || !create)
        {
            error = "no X11 display or VK_KHR_xlib_surface";
            return VK_NULL_HANDLE;
        }
        VkXlibSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
        info.dpy = display;
        info.window = static_cast<Window>(window->winId());
        return Made(create(instance, &info, nullptr, &surface), surface, "vkCreateXlibSurfaceKHR", error);
    }
    error = "no Vulkan surface for the platform " + platform.toStdString();
    return VK_NULL_HANDLE;
#else
    (void)instance;
    (void)window;
    (void)surface;
    error = "no Vulkan surface on this platform (Linux: built without Qt's private Gui headers)";
    return VK_NULL_HANDLE;
#endif
}
}  // namespace gpu

#endif  // !__APPLE__
