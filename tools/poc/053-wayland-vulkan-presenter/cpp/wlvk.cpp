// POC 053: a Vulkan swapchain on a Wayland surface, frames timed like the emulator's (one every 20 ms with a target
// on CLOCK_MONOTONIC), screen times from wp_presentation and VK_KHR_present_wait, two ways to show a frame at its time.
//
//   wlvk --variant a|b --mode fifo|mailbox|immediate [--lead-ms N] [--adaptive] [--frames N] [--period-ms 20]
//        [--out frames.csv] [--size WxH]
//
// a: a present thread sleeps until the target minus the lead, draws and presents.
// b: wp_commit_timer_v1.set_timestamp(target) before the present; the present goes out a lead early.
// One CSV row per frame: k, target, call, present_wait return, wp_presentation time, refresh, flags, discarded.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <wayland-client.h>

#include "commit-timing-v1-client.h"
#include "presentation-time-client.h"
#include "xdg-shell-client.h"

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int64_t NowNs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

void SleepUntil(int64_t ns)
{
    timespec ts{static_cast<time_t>(ns / 1'000'000'000), static_cast<long>(ns % 1'000'000'000)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR)
    {
    }
}

#define VKCHECK(x)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        VkResult r_ = (x);                                                                                             \
        if (r_ != VK_SUCCESS && r_ != VK_SUBOPTIMAL_KHR)                                                               \
        {                                                                                                              \
            std::fprintf(stderr, "%s:%d %s = %d\n", __FILE__, __LINE__, #x, static_cast<int>(r_));                    \
            std::exit(2);                                                                                              \
        }                                                                                                              \
    } while (0)

struct Options
{
    char variant = 'a';
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    int64_t leadNs = 4'000'000;
    int64_t gridMarginNs = -1;  // >= 0: present at (first refresh at or after the target) minus this margin
    bool adaptive = false;
    int frames = 3000;
    int64_t periodNs = 20'000'000;
    std::string out = "frames.csv";
    uint32_t width = 1280;
    uint32_t height = 720;
};

struct FrameRecord
{
    int64_t k = 0;
    int64_t target = 0;
    int64_t call = 0;       // vkQueuePresentKHR called
    int64_t waited = 0;     // vkWaitForPresentKHR returned (0: never)
    int64_t presented = 0;  // wp_presentation presented (0: none yet)
    uint32_t refresh = 0;
    uint32_t flags = 0;
    bool discarded = false;
};

// ---- Wayland -----------------------------------------------------------------------------------------------------

struct Wl
{
    wl_display* display = nullptr;
    wl_event_queue* queue = nullptr;  // our objects; Mesa's WSI keeps its own queue
    wl_compositor* compositor = nullptr;
    wl_subcompositor* subcompositor = nullptr;
    wl_shm* shm = nullptr;
    wl_surface* child = nullptr;  // c: the subsurface Vulkan presents to; the timer and the commit stay on the parent
    wl_subsurface* subsurface = nullptr;
    xdg_wm_base* wm = nullptr;
    wp_presentation* presentation = nullptr;
    wp_commit_timing_manager_v1* commitTiming = nullptr;
    uint32_t clockId = 0;
    wl_surface* surface = nullptr;
    xdg_surface* xsurface = nullptr;
    xdg_toplevel* toplevel = nullptr;
    wp_commit_timer_v1* timer = nullptr;
    std::atomic<bool> configured{false};
    std::atomic<bool> closed{false};
    std::vector<std::string> globals;
};

Wl g_wl;
std::mutex g_recMutex;
std::vector<FrameRecord> g_records;

void OnGlobal(void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    g_wl.globals.push_back(std::string(interface) + " v" + std::to_string(version));
    if (!std::strcmp(interface, wl_compositor_interface.name))
        g_wl.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    else if (!std::strcmp(interface, wl_subcompositor_interface.name))
        g_wl.subcompositor =
            static_cast<wl_subcompositor*>(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    else if (!std::strcmp(interface, wl_shm_interface.name))
        g_wl.shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    else if (!std::strcmp(interface, xdg_wm_base_interface.name))
        g_wl.wm = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    else if (!std::strcmp(interface, wp_presentation_interface.name))
        g_wl.presentation =
            static_cast<wp_presentation*>(wl_registry_bind(registry, name, &wp_presentation_interface, 1));
    else if (!std::strcmp(interface, wp_commit_timing_manager_v1_interface.name))
        g_wl.commitTiming = static_cast<wp_commit_timing_manager_v1*>(
            wl_registry_bind(registry, name, &wp_commit_timing_manager_v1_interface, 1));
}
void OnGlobalRemove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener kRegistry{OnGlobal, OnGlobalRemove};

void OnPing(void*, xdg_wm_base* wm, uint32_t serial) { xdg_wm_base_pong(wm, serial); }
const xdg_wm_base_listener kWm{OnPing};

void OnXdgConfigure(void*, xdg_surface* s, uint32_t serial)
{
    xdg_surface_ack_configure(s, serial);
    g_wl.configured = true;
}
const xdg_surface_listener kXdgSurface{OnXdgConfigure};

void OnTopConfigure(void*, xdg_toplevel*, int32_t, int32_t, wl_array*) {}
void OnTopClose(void*, xdg_toplevel*) { g_wl.closed = true; }
void OnTopBounds(void*, xdg_toplevel*, int32_t, int32_t) {}
void OnTopCaps(void*, xdg_toplevel*, wl_array*) {}
const xdg_toplevel_listener kTop{OnTopConfigure, OnTopClose, OnTopBounds, OnTopCaps};

void OnClockId(void*, wp_presentation*, uint32_t clk) { g_wl.clockId = clk; }
const wp_presentation_listener kPresentation{OnClockId};

struct FeedbackCtx
{
    size_t index;
};
void OnSyncOutput(void*, struct wp_presentation_feedback*, wl_output*) {}
void OnPresented(void* data, struct wp_presentation_feedback* fb, uint32_t secHi, uint32_t secLo, uint32_t nsec,
                 uint32_t refresh, uint32_t, uint32_t, uint32_t flags)
{
    auto* ctx = static_cast<FeedbackCtx*>(data);
    const int64_t t = ((static_cast<int64_t>(secHi) << 32) | secLo) * 1'000'000'000 + nsec;
    {
        std::lock_guard<std::mutex> lock(g_recMutex);
        g_records[ctx->index].presented = t;
        g_records[ctx->index].refresh = refresh;
        g_records[ctx->index].flags = flags;
    }
    wp_presentation_feedback_destroy(fb);
    delete ctx;
}
void OnDiscarded(void* data, struct wp_presentation_feedback* fb)
{
    auto* ctx = static_cast<FeedbackCtx*>(data);
    {
        std::lock_guard<std::mutex> lock(g_recMutex);
        g_records[ctx->index].discarded = true;
    }
    wp_presentation_feedback_destroy(fb);
    delete ctx;
}
const wp_presentation_feedback_listener kFeedback{OnSyncOutput, OnPresented, OnDiscarded};

// Our queue's events on a thread of their own, next to Mesa's reads of the same display
std::atomic<bool> g_eventsRun{true};
std::mutex g_wlMutex;  // requests on our proxies from two threads
void EventThread()
{
    const int fd = wl_display_get_fd(g_wl.display);
    while (g_eventsRun)
    {
        while (wl_display_prepare_read_queue(g_wl.display, g_wl.queue) != 0)
            wl_display_dispatch_queue_pending(g_wl.display, g_wl.queue);
        wl_display_flush(g_wl.display);
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, 5) > 0)
            wl_display_read_events(g_wl.display);
        else
            wl_display_cancel_read(g_wl.display);
        wl_display_dispatch_queue_pending(g_wl.display, g_wl.queue);
    }
}

// ---- Vulkan ------------------------------------------------------------------------------------------------------

struct Vk
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmds;
    std::vector<VkFence> fences;
    std::vector<VkSemaphore> acquired;
    std::vector<VkSemaphore> rendered;  // one per swapchain image
    bool presentWait = false;
    PFN_vkWaitForPresentKHR waitForPresent = nullptr;
    std::string gpuName;
};
Vk g_vk;

void CreateVulkan(const Options& opt)
{
    const char* instExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "poc053";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instExt;
    VKCHECK(vkCreateInstance(&ici, nullptr, &g_vk.instance));

    VkWaylandSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
    sci.display = g_wl.display;
    sci.surface = g_wl.child ? g_wl.child : g_wl.surface;
    VKCHECK(vkCreateWaylandSurfaceKHR(g_vk.instance, &sci, nullptr, &g_vk.surface));

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_vk.instance, &n, nullptr);
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(g_vk.instance, &n, gpus.data());
    const char* want = std::getenv("POC053_GPU");  // a substring of the device name
    for (VkPhysicalDevice d : gpus)
    {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (want ? std::strstr(p.deviceName, want) != nullptr : p.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)
        {
            g_vk.gpu = d;
            g_vk.gpuName = p.deviceName;
            break;
        }
    }
    if (!g_vk.gpu)
    {
        std::fprintf(stderr, "no GPU\n");
        std::exit(2);
    }
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.gpu, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fams(n);
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.gpu, &n, fams.data());
    for (uint32_t i = 0; i < n; i++)
    {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_vk.gpu, i, g_vk.surface, &present);
        if ((fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
        {
            g_vk.family = i;
            break;
        }
    }

    vkEnumerateDeviceExtensionProperties(g_vk.gpu, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(g_vk.gpu, nullptr, &n, exts.data());
    auto has = [&](const char* e) {
        return std::any_of(exts.begin(), exts.end(), [&](const VkExtensionProperties& x) { return !std::strcmp(x.extensionName, e); });
    };
    std::vector<const char*> devExt{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDevicePresentIdFeaturesKHR idFeat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR waitFeat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    void* chain = &v13;
    if (has(VK_KHR_PRESENT_ID_EXTENSION_NAME) && has(VK_KHR_PRESENT_WAIT_EXTENSION_NAME))
    {
        devExt.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        devExt.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        idFeat.presentId = VK_TRUE;
        waitFeat.presentWait = VK_TRUE;
        idFeat.pNext = &waitFeat;
        waitFeat.pNext = chain;
        chain = &idFeat;
        g_vk.presentWait = true;
    }
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = g_vk.family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = chain;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(devExt.size());
    dci.ppEnabledExtensionNames = devExt.data();
    VKCHECK(vkCreateDevice(g_vk.gpu, &dci, nullptr, &g_vk.device));
    vkGetDeviceQueue(g_vk.device, g_vk.family, 0, &g_vk.queue);
    if (g_vk.presentWait)
        g_vk.waitForPresent =
            reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(g_vk.device, "vkWaitForPresentKHR"));

    // The swapchain
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk.gpu, g_vk.surface, &caps);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk.gpu, g_vk.surface, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk.gpu, g_vk.surface, &n, modes.data());
    std::fprintf(stderr, "present modes:");
    for (VkPresentModeKHR m : modes)
        std::fprintf(stderr, " %d", static_cast<int>(m));
    std::fprintf(stderr, "\n");
    if (std::find(modes.begin(), modes.end(), opt.mode) == modes.end())
    {
        std::fprintf(stderr, "present mode %d not offered\n", static_cast<int>(opt.mode));
        std::exit(2);
    }
    g_vk.extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{opt.width, opt.height};
    VkSwapchainCreateInfoKHR sw{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sw.surface = g_vk.surface;
    sw.minImageCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount)
        sw.minImageCount = std::min(sw.minImageCount, caps.maxImageCount);
    sw.imageFormat = g_vk.format;
    sw.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sw.imageExtent = g_vk.extent;
    sw.imageArrayLayers = 1;
    sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sw.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sw.preTransform = caps.currentTransform;
    sw.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sw.presentMode = opt.mode;
    sw.clipped = VK_TRUE;
    VKCHECK(vkCreateSwapchainKHR(g_vk.device, &sw, nullptr, &g_vk.swapchain));
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &n, nullptr);
    g_vk.images.resize(n);
    vkGetSwapchainImagesKHR(g_vk.device, g_vk.swapchain, &n, g_vk.images.data());
    for (VkImage img : g_vk.images)
    {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = img;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = g_vk.format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v{};
        VKCHECK(vkCreateImageView(g_vk.device, &vci, nullptr, &v));
        g_vk.views.push_back(v);
        VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore s{};
        VKCHECK(vkCreateSemaphore(g_vk.device, &semi, nullptr, &s));
        g_vk.rendered.push_back(s);
    }
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = g_vk.family;
    VKCHECK(vkCreateCommandPool(g_vk.device, &pci, nullptr, &g_vk.pool));
    const uint32_t inFlight = 2;
    g_vk.cmds.resize(inFlight);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = g_vk.pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = inFlight;
    VKCHECK(vkAllocateCommandBuffers(g_vk.device, &cai, g_vk.cmds.data()));
    for (uint32_t i = 0; i < inFlight; i++)
    {
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkFence f{};
        VKCHECK(vkCreateFence(g_vk.device, &fci, nullptr, &f));
        g_vk.fences.push_back(f);
        VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore s{};
        VKCHECK(vkCreateSemaphore(g_vk.device, &semi, nullptr, &s));
        g_vk.acquired.push_back(s);
    }
}

// A picture that shows doubling and tearing: the background alternates per frame, a bar moves 16 px a frame,
// the frame number's low bits as blocks along the top
void Record(VkCommandBuffer cmd, uint32_t image, int64_t k)
{
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VKCHECK(vkBeginCommandBuffer(cmd, &bi));
    VkImageMemoryBarrier2 toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toColor.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.image = g_vk.images[image];
    toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toColor;
    vkCmdPipelineBarrier2(cmd, &dep);

    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = g_vk.views[image];
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const float shade = (k & 1) ? 0.25f : 0.15f;
    att.clearValue.color = {{shade, shade, shade, 1.0f}};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, g_vk.extent};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &ri);
    std::vector<VkClearRect> rects;
    const uint32_t w = g_vk.extent.width, h = g_vk.extent.height;
    const uint32_t x = static_cast<uint32_t>((k * 16) % (w - 64));
    rects.push_back({{{static_cast<int32_t>(x), 40}, {64, h - 40}}, 0, 1});
    VkClearAttachment white{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
    white.clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
    vkCmdClearAttachments(cmd, 1, &white, static_cast<uint32_t>(rects.size()), rects.data());
    rects.clear();
    for (int bit = 0; bit < 8; bit++)
        if ((k >> bit) & 1)
            rects.push_back({{{static_cast<int32_t>(bit * 40), 0}, {36, 36}}, 0, 1});
    if (!rects.empty())
    {
        VkClearAttachment green{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
        green.clearValue.color = {{0.0f, 0.9f, 0.2f, 1.0f}};
        vkCmdClearAttachments(cmd, 1, &green, static_cast<uint32_t>(rects.size()), rects.data());
    }
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toPresent = toColor;
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dep.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(cmd, &dep);
    VKCHECK(vkEndCommandBuffer(cmd));
}

// present_wait: a thread that waits for each present id in turn and stamps its return
std::mutex g_waitMutex;
std::condition_variable g_waitCv;
std::deque<std::pair<uint64_t, size_t>> g_waitQueue;
bool g_waitStop = false;
void WaitThread()
{
    for (;;)
    {
        std::pair<uint64_t, size_t> item;
        {
            std::unique_lock<std::mutex> lock(g_waitMutex);
            g_waitCv.wait(lock, [] { return g_waitStop || !g_waitQueue.empty(); });
            if (g_waitQueue.empty())
                return;
            item = g_waitQueue.front();
            g_waitQueue.pop_front();
        }
        const VkResult r = g_vk.waitForPresent(g_vk.device, g_vk.swapchain, item.first, 200'000'000);
        const int64_t t = NowNs();
        std::lock_guard<std::mutex> lock(g_recMutex);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
            g_records[item.second].waited = t;
    }
}

Options Parse(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--variant")
            o.variant = next()[0];
        else if (a == "--mode")
        {
            const std::string m = next();
            o.mode = m == "mailbox"     ? VK_PRESENT_MODE_MAILBOX_KHR
                     : m == "immediate" ? VK_PRESENT_MODE_IMMEDIATE_KHR
                     : m == "relaxed"   ? VK_PRESENT_MODE_FIFO_RELAXED_KHR
                                        : VK_PRESENT_MODE_FIFO_KHR;
        }
        else if (a == "--lead-ms")
            o.leadNs = static_cast<int64_t>(std::atof(next().c_str()) * 1e6);
        else if (a == "--grid-ms")
            o.gridMarginNs = static_cast<int64_t>(std::atof(next().c_str()) * 1e6);
        else if (a == "--adaptive")
            o.adaptive = true;
        else if (a == "--frames")
            o.frames = std::atoi(next().c_str());
        else if (a == "--period-ms")
            o.periodNs = static_cast<int64_t>(std::atof(next().c_str()) * 1e6);
        else if (a == "--out")
            o.out = next();
        else if (a == "--size")
            std::sscanf(next().c_str(), "%ux%u", &o.width, &o.height);
    }
    return o;
}

}  // namespace

int main(int argc, char** argv)
{
    const Options opt = Parse(argc, argv);

    g_wl.display = wl_display_connect(nullptr);
    if (!g_wl.display)
    {
        std::fprintf(stderr, "no Wayland display\n");
        return 2;
    }
    g_wl.queue = wl_display_create_queue(g_wl.display);
    auto* wrapped = static_cast<wl_display*>(wl_proxy_create_wrapper(g_wl.display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapped), g_wl.queue);
    wl_registry* registry = wl_display_get_registry(wrapped);
    wl_proxy_wrapper_destroy(wrapped);
    wl_registry_add_listener(registry, &kRegistry, nullptr);
    wl_display_roundtrip_queue(g_wl.display, g_wl.queue);
    if (g_wl.presentation)
        wp_presentation_add_listener(g_wl.presentation, &kPresentation, nullptr);
    xdg_wm_base_add_listener(g_wl.wm, &kWm, nullptr);
    wl_display_roundtrip_queue(g_wl.display, g_wl.queue);
    std::fprintf(stderr, "presentation %s (clock %u), commit timing %s\n", g_wl.presentation ? "yes" : "no",
                 g_wl.clockId, g_wl.commitTiming ? "yes" : "no");
    for (const std::string& gname : g_wl.globals)
        if (gname.find("fifo") != std::string::npos || gname.find("commit") != std::string::npos ||
            gname.find("presentation") != std::string::npos || gname.find("tearing") != std::string::npos)
            std::fprintf(stderr, "  global %s\n", gname.c_str());

    g_wl.surface = wl_compositor_create_surface(g_wl.compositor);
    g_wl.xsurface = xdg_wm_base_get_xdg_surface(g_wl.wm, g_wl.surface);
    xdg_surface_add_listener(g_wl.xsurface, &kXdgSurface, nullptr);
    g_wl.toplevel = xdg_surface_get_toplevel(g_wl.xsurface);
    xdg_toplevel_add_listener(g_wl.toplevel, &kTop, nullptr);
    xdg_toplevel_set_title(g_wl.toplevel, "POC 053");
    xdg_toplevel_set_app_id(g_wl.toplevel, "poc053");
    if (opt.variant == 'b')
    {
        if (!g_wl.commitTiming)
        {
            std::fprintf(stderr, "no wp_commit_timing_manager_v1\n");
            return 2;
        }
        // Before Vulkan touches the surface: if Mesa's WSI makes a timer of its own, the protocol error says so
        g_wl.timer = wp_commit_timing_manager_v1_get_timer(g_wl.commitTiming, g_wl.surface);
    }
    if (opt.variant == 'c')
    {
        // The parent timed by us, the child owned by Mesa's WSI: a synchronized subsurface's commits wait in the
        // cache until the parent's commit, and the parent's commit is held to its timestamp
        g_wl.timer = wp_commit_timing_manager_v1_get_timer(g_wl.commitTiming, g_wl.surface);
        g_wl.child = wl_compositor_create_surface(g_wl.compositor);
        g_wl.subsurface = wl_subcompositor_get_subsurface(g_wl.subcompositor, g_wl.child, g_wl.surface);
        wl_subsurface_set_sync(g_wl.subsurface);
        wl_subsurface_set_position(g_wl.subsurface, 0, 0);
    }
    wl_surface_commit(g_wl.surface);
    while (!g_wl.configured)
        wl_display_dispatch_queue(g_wl.display, g_wl.queue);
    if (opt.variant == 'c')
    {
        // The parent needs content of its own to be mapped: one black shm buffer of the window's size
        const int stride = static_cast<int>(opt.width) * 4, size = stride * static_cast<int>(opt.height);
        const int fd = memfd_create("poc053", 0);
        if (ftruncate(fd, size) != 0)
            return 2;
        void* px = mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        std::memset(px, 0, static_cast<size_t>(size));
        wl_shm_pool* pool = wl_shm_create_pool(g_wl.shm, fd, size);
        wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, static_cast<int>(opt.width), static_cast<int>(opt.height),
                                                   stride, WL_SHM_FORMAT_XRGB8888);
        wl_surface_attach(g_wl.surface, buf, 0, 0);
        wl_surface_damage(g_wl.surface, 0, 0, static_cast<int>(opt.width), static_cast<int>(opt.height));
        wl_surface_commit(g_wl.surface);
        wl_display_roundtrip_queue(g_wl.display, g_wl.queue);
    }

    CreateVulkan(opt);
    std::fprintf(stderr, "gpu %s, extent %ux%u, images %zu, present_wait %s\n", g_vk.gpuName.c_str(),
                 g_vk.extent.width, g_vk.extent.height, g_vk.images.size(), g_vk.presentWait ? "yes" : "no");

    g_records.resize(static_cast<size_t>(opt.frames));
    std::thread events(EventThread);
    std::thread waiter;
    if (g_vk.presentWait)
        waiter = std::thread(WaitThread);

    int64_t lead = opt.leadNs;
    std::vector<int64_t> lateness;  // screen time minus target, the last frames with a time (adaptive lead)
    const int64_t t0 = NowNs() + 500'000'000;
    for (int k = 0; k < opt.frames && !g_wl.closed; k++)
    {
        const int64_t target = t0 + k * opt.periodNs;
        // The variant's moment to present: a lead before the target (b: the compositor then holds it to the time)
        int64_t when = target - lead;
        if (opt.gridMarginNs >= 0)
        {
            // The refresh grid from the last frame with a screen time: the first refresh at or after the target
            int64_t last = 0, period = 0;
            {
                std::lock_guard<std::mutex> lock(g_recMutex);
                for (int j = k - 1; j >= std::max(0, k - 16); j--)
                    if (g_records[static_cast<size_t>(j)].presented)
                    {
                        last = g_records[static_cast<size_t>(j)].presented;
                        period = g_records[static_cast<size_t>(j)].refresh;
                        break;
                    }
            }
            if (last && period)
            {
                const int64_t n = (target - last + period - 1) / period;
                when = last + n * period - opt.gridMarginNs;
            }
        }
        SleepUntil(when);
        const uint32_t slot = static_cast<uint32_t>(k % 2);
        VKCHECK(vkWaitForFences(g_vk.device, 1, &g_vk.fences[slot], VK_TRUE, UINT64_MAX));
        VKCHECK(vkResetFences(g_vk.device, 1, &g_vk.fences[slot]));
        uint32_t image = 0;
        VKCHECK(vkAcquireNextImageKHR(g_vk.device, g_vk.swapchain, UINT64_MAX, g_vk.acquired[slot], VK_NULL_HANDLE,
                                      &image));
        Record(g_vk.cmds[slot], image, k);
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &g_vk.acquired[slot];
        si.pWaitDstStageMask = &stage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &g_vk.cmds[slot];
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &g_vk.rendered[image];
        VKCHECK(vkQueueSubmit(g_vk.queue, 1, &si, g_vk.fences[slot]));

        {
            std::lock_guard<std::mutex> lock(g_wlMutex);
            if (g_wl.presentation && !g_wl.child)
            {
                struct wp_presentation_feedback* fb = wp_presentation_feedback(g_wl.presentation, g_wl.surface);
                wp_presentation_feedback_add_listener(fb, &kFeedback, new FeedbackCtx{static_cast<size_t>(k)});
            }
            if (g_wl.timer && !g_wl.child)
            {
                const int64_t s = target / 1'000'000'000;
                wp_commit_timer_v1_set_timestamp(g_wl.timer, static_cast<uint32_t>(s >> 32),
                                                 static_cast<uint32_t>(s & 0xFFFFFFFF),
                                                 static_cast<uint32_t>(target % 1'000'000'000));
            }
        }
        const uint64_t presentId = static_cast<uint64_t>(k) + 1;
        VkPresentIdKHR pid{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
        pid.swapchainCount = 1;
        pid.pPresentIds = &presentId;
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.pNext = g_vk.presentWait ? &pid : nullptr;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &g_vk.rendered[image];
        pi.swapchainCount = 1;
        pi.pSwapchains = &g_vk.swapchain;
        pi.pImageIndices = &image;
        const int64_t call = NowNs();
        VKCHECK(vkQueuePresentKHR(g_vk.queue, &pi));
        if (g_wl.child)
        {
            std::lock_guard<std::mutex> lock(g_wlMutex);
            const int64_t s = target / 1'000'000'000;
            if (!std::getenv("POC053_NO_TIMER"))
                wp_commit_timer_v1_set_timestamp(g_wl.timer, static_cast<uint32_t>(s >> 32),
                                                 static_cast<uint32_t>(s & 0xFFFFFFFF),
                                                 static_cast<uint32_t>(target % 1'000'000'000));
            // The screen time of the parent's commit: it carries the child's cached state (Mesa commits the child
            // twice per present, the second commit replaces the first in the cache, so feedback on the child is
            // always discarded)
            struct wp_presentation_feedback* fb = wp_presentation_feedback(g_wl.presentation, g_wl.surface);
            wp_presentation_feedback_add_listener(fb, &kFeedback, new FeedbackCtx{static_cast<size_t>(k)});
            if (std::getenv("POC053_PARENT_DAMAGE"))
                wl_surface_damage(g_wl.surface, 0, 0, 1, 1);
            wl_surface_commit(g_wl.surface);
            wl_display_flush(g_wl.display);
        }
        {
            std::lock_guard<std::mutex> lock(g_recMutex);
            g_records[static_cast<size_t>(k)].k = k;
            g_records[static_cast<size_t>(k)].target = target;
            g_records[static_cast<size_t>(k)].call = call;
            // Adaptive lead (variant a): keep the median screen-minus-target at +1 ms from the frames timed so far
            if (opt.adaptive && opt.variant == 'a' && k >= 8)
            {
                lateness.clear();
                for (int j = std::max(0, k - 64); j < k; j++)
                    if (g_records[static_cast<size_t>(j)].presented)
                        lateness.push_back(g_records[static_cast<size_t>(j)].presented -
                                           g_records[static_cast<size_t>(j)].target);
                if (lateness.size() >= 8)
                {
                    std::nth_element(lateness.begin(), lateness.begin() + static_cast<long>(lateness.size() / 2),
                                     lateness.end());
                    const int64_t median = lateness[lateness.size() / 2];
                    lead = std::clamp<int64_t>(lead + (median - 1'000'000) / 8, 0, opt.periodNs);
                }
            }
        }
        if (g_vk.presentWait)
        {
            std::lock_guard<std::mutex> lock(g_waitMutex);
            g_waitQueue.emplace_back(presentId, static_cast<size_t>(k));
            g_waitCv.notify_one();
        }
    }

    vkDeviceWaitIdle(g_vk.device);
    SleepUntil(NowNs() + 300'000'000);  // the last feedback events
    {
        std::lock_guard<std::mutex> lock(g_waitMutex);
        g_waitStop = true;
        g_waitCv.notify_one();
    }
    if (waiter.joinable())
        waiter.join();
    g_eventsRun = false;
    events.join();

    FILE* f = std::fopen(opt.out.c_str(), "w");
    std::fprintf(f, "k,target,call,waited,presented,refresh,flags,discarded\n");
    std::lock_guard<std::mutex> lock(g_recMutex);
    for (const FrameRecord& r : g_records)
        if (r.target)
            std::fprintf(f, "%lld,%lld,%lld,%lld,%lld,%u,%u,%d\n", static_cast<long long>(r.k),
                         static_cast<long long>(r.target), static_cast<long long>(r.call),
                         static_cast<long long>(r.waited), static_cast<long long>(r.presented), r.refresh, r.flags,
                         r.discarded ? 1 : 0);
    std::fclose(f);
    std::fprintf(stderr, "wrote %s, final lead %.2f ms\n", opt.out.c_str(), lead / 1e6);
    return 0;
}
