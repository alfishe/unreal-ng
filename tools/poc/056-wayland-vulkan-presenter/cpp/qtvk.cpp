// POC 056, questions 3 and 4: GL draws, Vulkan shows, inside a Qt 6 window on Wayland.
//
//   qtvk --window top|container --source gl|vk [--frames N] [--lead-ms N] [--gl-size WxH] [--out frames.csv]
//
// top:       the Vulkan QWindow is a top-level window.
// container: the Vulkan QWindow sits in a QWidget through QWidget::createWindowContainer (as the screen window).
// gl:        a Qt QOpenGLContext draws each frame into a texture whose memory is a VkImage exported with
//            VK_KHR_external_memory_fd (GL_EXT_memory_object_fd), synchronised by two exported semaphores
//            (GL_EXT_semaphore_fd); Vulkan blits it (scaled, flipped to GL's bottom-up rows) into the swapchain.
// vk:        Vulkan clears the swapchain image itself (the baseline of question 4).
// Presents: a present thread at target - lead, FIFO (the best variant of round 1). Screen times from wp_presentation
// on the QWindow's wl_surface (our own event queue next to Qt's).
// CSV per frame: k, target, call, waited, presented, refresh, flags, discarded, blit_ns (GPU), gl_cpu_ns, check.
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <wayland-client.h>

#include "presentation-time-client.h"

#include <QApplication>
#include <QLabel>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QVBoxLayout>
#include <QVulkanInstance>
#include <QWidget>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include <poll.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
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
            std::_Exit(2);                                                                                             \
        }                                                                                                              \
    } while (0)

// GL_EXT_memory_object(_fd), GL_EXT_semaphore(_fd)
constexpr GLenum kTextureTiling = 0x9580;
constexpr GLenum kOptimalTiling = 0x9584;
constexpr GLenum kDedicatedMemoryObject = 0x9581;
constexpr GLenum kHandleOpaqueFd = 0x9586;
constexpr GLenum kLayoutTransferSrc = 0x9593;
constexpr GLenum kNumDeviceUuids = 0x9596;
constexpr GLenum kDeviceUuid = 0x9597;
using FnCreateMemoryObjects = void (*)(GLsizei, GLuint*);
using FnMemoryObjectParameteriv = void (*)(GLuint, GLenum, const GLint*);
using FnImportMemoryFd = void (*)(GLuint, GLuint64, GLenum, GLint);
using FnTexStorageMem2D = void (*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);
using FnGenSemaphores = void (*)(GLsizei, GLuint*);
using FnImportSemaphoreFd = void (*)(GLuint, GLenum, GLint);
using FnWaitSemaphore = void (*)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
using FnSignalSemaphore = void (*)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
using FnGetUnsignedBytei = void (*)(GLenum, GLuint, GLubyte*);

struct Options
{
    bool container = false;
    bool gl = true;
    int frames = 3000;
    int64_t leadNs = 4'000'000;
    int64_t periodNs = 20'000'000;
    uint32_t glWidth = 1920;
    uint32_t glHeight = 1080;
    std::string out = "frames.csv";
};

struct FrameRecord
{
    int64_t k = 0, target = 0, call = 0, waited = 0, presented = 0;
    uint32_t refresh = 0, flags = 0;
    bool discarded = false;
    int64_t blitNs = 0, glCpuNs = 0;
    int check = 0;  // 0 none, 1 picture right, -1 wrong
};

std::mutex g_recMutex;
std::vector<FrameRecord> g_records;

// ---- wp_presentation on Qt's wl_surface, our own queue --------------------------------------------------------------

wl_display* g_display = nullptr;
wl_event_queue* g_queue = nullptr;
wp_presentation* g_presentation = nullptr;
wl_surface* g_surface = nullptr;
std::atomic<bool> g_eventsRun{true};

void OnGlobal(void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t)
{
    if (!std::strcmp(interface, wp_presentation_interface.name))
        g_presentation = static_cast<wp_presentation*>(wl_registry_bind(registry, name, &wp_presentation_interface, 1));
}
void OnGlobalRemove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener kRegistry{OnGlobal, OnGlobalRemove};

void OnSyncOutput(void*, struct wp_presentation_feedback*, wl_output*) {}
void OnPresented(void* data, struct wp_presentation_feedback* fb, uint32_t secHi, uint32_t secLo, uint32_t nsec,
                 uint32_t refresh, uint32_t, uint32_t, uint32_t flags)
{
    const size_t i = reinterpret_cast<size_t>(data);
    const int64_t t = ((static_cast<int64_t>(secHi) << 32) | secLo) * 1'000'000'000 + nsec;
    {
        std::lock_guard<std::mutex> lock(g_recMutex);
        g_records[i].presented = t;
        g_records[i].refresh = refresh;
        g_records[i].flags = flags;
    }
    wp_presentation_feedback_destroy(fb);
}
void OnDiscarded(void* data, struct wp_presentation_feedback* fb)
{
    {
        std::lock_guard<std::mutex> lock(g_recMutex);
        g_records[reinterpret_cast<size_t>(data)].discarded = true;
    }
    wp_presentation_feedback_destroy(fb);
}
const wp_presentation_feedback_listener kFeedback{OnSyncOutput, OnPresented, OnDiscarded};

void EventThread()
{
    const int fd = wl_display_get_fd(g_display);
    while (g_eventsRun)
    {
        while (wl_display_prepare_read_queue(g_display, g_queue) != 0)
            wl_display_dispatch_queue_pending(g_display, g_queue);
        wl_display_flush(g_display);
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, 5) > 0)
            wl_display_read_events(g_display);
        else
            wl_display_cancel_read(g_display);
        wl_display_dispatch_queue_pending(g_display, g_queue);
    }
}

// ---- The render thread: GL, Vulkan, presents ------------------------------------------------------------------------

struct Shared
{
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkExtent2D extent{};
    QOffscreenSurface* offscreen = nullptr;
};

uint32_t MemoryType(VkPhysicalDevice gpu, uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    std::fprintf(stderr, "no memory type\n");
    std::_Exit(2);
}

VkImageMemoryBarrier2 Barrier(VkImage image, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.oldLayout = from;
    b.newLayout = to;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}

void Barriers(VkCommandBuffer cmd, std::initializer_list<VkImageMemoryBarrier2> list)
{
    std::vector<VkImageMemoryBarrier2> v(list);
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(v.size());
    dep.pImageMemoryBarriers = v.data();
    vkCmdPipelineBarrier2(cmd, &dep);
}

// The GL pattern, GL coordinates (origin bottom-left): red block at the bottom-left, green block at the top-left,
// a white bar moving 16 px a frame, the background alternating per frame
uint32_t BarX(int64_t k, uint32_t width) { return static_cast<uint32_t>((k * 16) % (width - 64)); }

void Render(const Options& opt, const Shared& sh, std::atomic<bool>& done)
{
    // GL first: its device UUID picks the Vulkan device
    QOpenGLContext ctx;
    ctx.setFormat(sh.offscreen->requestedFormat());
    QOpenGLExtraFunctions* gl = nullptr;
    GLubyte glUuid[16] = {};
    FnCreateMemoryObjects glCreateMemoryObjects = nullptr;
    FnMemoryObjectParameteriv glMemoryObjectParameteriv = nullptr;
    FnImportMemoryFd glImportMemoryFd = nullptr;
    FnTexStorageMem2D glTexStorageMem2D = nullptr;
    FnGenSemaphores glGenSemaphores = nullptr;
    FnImportSemaphoreFd glImportSemaphoreFd = nullptr;
    FnWaitSemaphore glWaitSemaphore = nullptr;
    FnSignalSemaphore glSignalSemaphore = nullptr;
    if (opt.gl)
    {
        if (!ctx.create() || !ctx.makeCurrent(sh.offscreen))
        {
            std::fprintf(stderr, "no GL context\n");
            std::_Exit(2);
        }
        gl = ctx.extraFunctions();
        std::fprintf(stderr, "GL %s / %s, memory_object_fd %d, semaphore_fd %d\n",
                     reinterpret_cast<const char*>(gl->glGetString(GL_VERSION)),
                     reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER)),
                     ctx.hasExtension("GL_EXT_memory_object_fd"), ctx.hasExtension("GL_EXT_semaphore_fd"));
        auto proc = [&](const char* name) {
            QFunctionPointer f = ctx.getProcAddress(name);
            if (!f)
            {
                std::fprintf(stderr, "no %s\n", name);
                std::_Exit(2);
            }
            return f;
        };
        glCreateMemoryObjects = reinterpret_cast<FnCreateMemoryObjects>(proc("glCreateMemoryObjectsEXT"));
        glMemoryObjectParameteriv = reinterpret_cast<FnMemoryObjectParameteriv>(proc("glMemoryObjectParameterivEXT"));
        glImportMemoryFd = reinterpret_cast<FnImportMemoryFd>(proc("glImportMemoryFdEXT"));
        glTexStorageMem2D = reinterpret_cast<FnTexStorageMem2D>(proc("glTexStorageMem2DEXT"));
        glGenSemaphores = reinterpret_cast<FnGenSemaphores>(proc("glGenSemaphoresEXT"));
        glImportSemaphoreFd = reinterpret_cast<FnImportSemaphoreFd>(proc("glImportSemaphoreFdEXT"));
        glWaitSemaphore = reinterpret_cast<FnWaitSemaphore>(proc("glWaitSemaphoreEXT"));
        glSignalSemaphore = reinterpret_cast<FnSignalSemaphore>(proc("glSignalSemaphoreEXT"));
        auto getUuid = reinterpret_cast<FnGetUnsignedBytei>(proc("glGetUnsignedBytei_vEXT"));
        GLint n = 0;
        gl->glGetIntegerv(kNumDeviceUuids, &n);
        if (n > 0)
            getUuid(kDeviceUuid, 0, glUuid);
    }

    // Vulkan device: the GL device's UUID, else the first non-CPU device
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(sh.instance, &n, nullptr);
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(sh.instance, &n, gpus.data());
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    for (VkPhysicalDevice d : gpus)
    {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &id;
        vkGetPhysicalDeviceProperties2(d, &p2);
        const bool match = opt.gl ? !std::memcmp(id.deviceUUID, glUuid, 16)
                                  : p2.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU;
        if (match)
        {
            gpu = d;
            props = p2.properties;
            break;
        }
    }
    if (!gpu)
    {
        std::fprintf(stderr, "no Vulkan device matches the GL device UUID\n");
        std::_Exit(2);
    }
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fams(n);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &n, fams.data());
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < n && family == UINT32_MAX; i++)
    {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, sh.surface, &present);
        if ((fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
            family = i;
    }
    std::fprintf(stderr, "vulkan %s, family %u, timestamp bits %u\n", props.deviceName, family,
                 fams[family].timestampValidBits);

    const char* devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PRESENT_ID_EXTENSION_NAME,
                            VK_KHR_PRESENT_WAIT_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    v13.synchronization2 = VK_TRUE;
    VkPhysicalDevicePresentWaitFeaturesKHR waitFeat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    waitFeat.presentWait = VK_TRUE;
    waitFeat.pNext = &v13;
    VkPhysicalDevicePresentIdFeaturesKHR idFeat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    idFeat.presentId = VK_TRUE;
    idFeat.pNext = &waitFeat;
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &idFeat;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 5;
    dci.ppEnabledExtensionNames = devExt;
    VkDevice device{};
    VKCHECK(vkCreateDevice(gpu, &dci, nullptr, &device));
    VkQueue queue{};
    vkGetDeviceQueue(device, family, 0, &queue);
    auto waitForPresent = reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(device, "vkWaitForPresentKHR"));
    auto getMemoryFd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
    auto getSemaphoreFd = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(device, "vkGetSemaphoreFdKHR"));

    // Swapchain, FIFO
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, sh.surface, &caps);
    const VkExtent2D extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : sh.extent;
    VkSwapchainCreateInfoKHR sw{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sw.surface = sh.surface;
    sw.minImageCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount)
        sw.minImageCount = std::min(sw.minImageCount, caps.maxImageCount);
    sw.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    sw.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sw.imageExtent = extent;
    sw.imageArrayLayers = 1;
    sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sw.preTransform = caps.currentTransform;
    sw.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sw.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sw.clipped = VK_TRUE;
    VkSwapchainKHR swapchain{};
    VKCHECK(vkCreateSwapchainKHR(device, &sw, nullptr, &swapchain));
    vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
    std::vector<VkImage> images(n);
    vkGetSwapchainImagesKHR(device, swapchain, &n, images.data());
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> rendered;
    for (VkImage img : images)
    {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = img;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = sw.imageFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v{};
        VKCHECK(vkCreateImageView(device, &vci, nullptr, &v));
        views.push_back(v);
        VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore s{};
        VKCHECK(vkCreateSemaphore(device, &semi, nullptr, &s));
        rendered.push_back(s);
    }
    std::fprintf(stderr, "swapchain %ux%u, %zu images\n", extent.width, extent.height, images.size());

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family;
    VkCommandPool pool{};
    VKCHECK(vkCreateCommandPool(device, &pci, nullptr, &pool));
    VkCommandBuffer cmds[3]{};
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 3;
    VKCHECK(vkAllocateCommandBuffers(device, &cai, cmds));
    VkFence fences[2]{};
    VkSemaphore acquired[2]{};
    for (int i = 0; i < 2; i++)
    {
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKCHECK(vkCreateFence(device, &fci, nullptr, &fences[i]));
        VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VKCHECK(vkCreateSemaphore(device, &semi, nullptr, &acquired[i]));
    }
    VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpi.queryCount = 4;
    VkQueryPool queries{};
    VKCHECK(vkCreateQueryPool(device, &qpi, nullptr, &queries));

    // The shared image (GL draws, Vulkan reads), its two semaphores, a readback buffer for the picture check
    VkImage shared = VK_NULL_HANDLE;
    VkDeviceMemory sharedMem = VK_NULL_HANDLE;
    VkSemaphore glReady = VK_NULL_HANDLE, vkReleased = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem = VK_NULL_HANDLE;
    void* readbackPtr = nullptr;
    GLuint texture = 0, fbo = 0, glReadySem = 0, vkReleasedSem = 0;
    const uint32_t gw = opt.glWidth, gh = opt.glHeight;
    if (opt.gl)
    {
        VkExternalMemoryImageCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        ext.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.pNext = &ext;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = {gw, gh, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VKCHECK(vkCreateImage(device, &ici, nullptr, &shared));
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, shared, &req);
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.image = shared;
        VkExportMemoryAllocateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
        exportInfo.pNext = &dedicated;
        exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.pNext = &exportInfo;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = MemoryType(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VKCHECK(vkAllocateMemory(device, &mai, nullptr, &sharedMem));
        VKCHECK(vkBindImageMemory(device, shared, sharedMem, 0));
        VkMemoryGetFdInfoKHR mfd{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
        mfd.memory = sharedMem;
        mfd.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        int memFd = -1;
        VKCHECK(getMemoryFd(device, &mfd, &memFd));

        auto exportedSemaphore = [&](VkSemaphore& s) {
            VkExportSemaphoreCreateInfo esi{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
            esi.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
            VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            semi.pNext = &esi;
            VKCHECK(vkCreateSemaphore(device, &semi, nullptr, &s));
            VkSemaphoreGetFdInfoKHR sfd{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
            sfd.semaphore = s;
            sfd.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
            int fd = -1;
            VKCHECK(getSemaphoreFd(device, &sfd, &fd));
            return fd;
        };
        const int glReadyFd = exportedSemaphore(glReady);
        const int vkReleasedFd = exportedSemaphore(vkReleased);

        GLuint memObj = 0;
        glCreateMemoryObjects(1, &memObj);
        const GLint one = 1;
        glMemoryObjectParameteriv(memObj, kDedicatedMemoryObject, &one);
        glImportMemoryFd(memObj, req.size, kHandleOpaqueFd, memFd);
        gl->glGenTextures(1, &texture);
        gl->glBindTexture(GL_TEXTURE_2D, texture);
        gl->glTexParameteri(GL_TEXTURE_2D, kTextureTiling, static_cast<GLint>(kOptimalTiling));
        glTexStorageMem2D(GL_TEXTURE_2D, 1, GL_RGBA8, static_cast<GLsizei>(gw), static_cast<GLsizei>(gh), memObj, 0);
        gl->glGenFramebuffers(1, &fbo);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            std::fprintf(stderr, "GL framebuffer on the imported memory is incomplete\n");
            std::_Exit(2);
        }
        glGenSemaphores(1, &glReadySem);
        glImportSemaphoreFd(glReadySem, kHandleOpaqueFd, glReadyFd);
        glGenSemaphores(1, &vkReleasedSem);
        glImportSemaphoreFd(vkReleasedSem, kHandleOpaqueFd, vkReleasedFd);
        std::fprintf(stderr, "GL texture on the Vulkan image: %ux%u RGBA8, %llu bytes, GL error %#x\n", gw, gh,
                     static_cast<unsigned long long>(req.size), gl->glGetError());

        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = static_cast<VkDeviceSize>(gw) * gh * 4;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VKCHECK(vkCreateBuffer(device, &bci, nullptr, &readback));
        vkGetBufferMemoryRequirements(device, readback, &req);
        VkMemoryAllocateInfo rai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        rai.allocationSize = req.size;
        rai.memoryTypeIndex = MemoryType(gpu, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VKCHECK(vkAllocateMemory(device, &rai, nullptr, &readbackMem));
        VKCHECK(vkBindBufferMemory(device, readback, readbackMem, 0));
        VKCHECK(vkMapMemory(device, readbackMem, 0, VK_WHOLE_SIZE, 0, &readbackPtr));

        // Once: UNDEFINED -> TRANSFER_SRC, released to GL, vkReleased signalled (GL's first wait)
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VKCHECK(vkBeginCommandBuffer(cmds[2], &bi));
        VkImageMemoryBarrier2 b =
            Barrier(shared, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        b.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        b.srcQueueFamilyIndex = family;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        Barriers(cmds[2], {b});
        VKCHECK(vkEndCommandBuffer(cmds[2]));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmds[2];
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &vkReleased;
        VKCHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
        VKCHECK(vkQueueWaitIdle(queue));
    }

    // present_wait in its own thread
    std::mutex waitMutex;
    std::condition_variable waitCv;
    std::deque<std::pair<uint64_t, size_t>> waitQueue;
    bool waitStop = false;
    std::thread waiter([&] {
        for (;;)
        {
            std::pair<uint64_t, size_t> item;
            {
                std::unique_lock<std::mutex> lock(waitMutex);
                waitCv.wait(lock, [&] { return waitStop || !waitQueue.empty(); });
                if (waitQueue.empty())
                    return;
                item = waitQueue.front();
                waitQueue.pop_front();
            }
            const VkResult r = waitForPresent(device, swapchain, item.first, 200'000'000);
            const int64_t t = NowNs();
            std::lock_guard<std::mutex> lock(g_recMutex);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
                g_records[item.second].waited = t;
        }
    });

    const double tsPeriod = props.limits.timestampPeriod;
    int checksOk = 0, checksBad = 0;
    const int64_t t0 = NowNs() + 500'000'000;
    for (int k = 0; k < opt.frames; k++)
    {
        const int64_t target = t0 + k * opt.periodNs;
        SleepUntil(target - opt.leadNs);
        const uint32_t slot = static_cast<uint32_t>(k % 2);
        VKCHECK(vkWaitForFences(device, 1, &fences[slot], VK_TRUE, UINT64_MAX));
        if (k >= 2)
        {
            uint64_t ts[2] = {};
            if (vkGetQueryPoolResults(device, queries, slot * 2, 2, sizeof(ts), ts, sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            {
                std::lock_guard<std::mutex> lock(g_recMutex);
                g_records[static_cast<size_t>(k - 2)].blitNs = static_cast<int64_t>((ts[1] - ts[0]) * tsPeriod);
            }
        }
        VKCHECK(vkResetFences(device, 1, &fences[slot]));

        // GL draws frame k into the shared image
        int64_t glCpu = 0;
        if (opt.gl)
        {
            const int64_t g0 = NowNs();
            const GLenum layout = kLayoutTransferSrc;
            glWaitSemaphore(vkReleasedSem, 0, nullptr, 1, &texture, &layout);
            gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            gl->glViewport(0, 0, static_cast<GLsizei>(gw), static_cast<GLsizei>(gh));
            gl->glDisable(GL_SCISSOR_TEST);
            const float shade = (k & 1) ? 0.25f : 0.15f;
            gl->glClearColor(shade, shade, shade, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT);
            gl->glEnable(GL_SCISSOR_TEST);
            auto rect = [&](GLint x, GLint y, GLsizei w, GLsizei h, float r, float g, float b) {
                gl->glScissor(x, y, w, h);
                gl->glClearColor(r, g, b, 1.0f);
                gl->glClear(GL_COLOR_BUFFER_BIT);
            };
            rect(static_cast<GLint>(BarX(k, gw)), 0, 64, static_cast<GLsizei>(gh), 1.0f, 1.0f, 1.0f);
            rect(0, 0, 200, 200, 1.0f, 0.0f, 0.0f);                                   // bottom-left: red
            rect(0, static_cast<GLint>(gh) - 200, 200, 200, 0.0f, 1.0f, 0.0f);        // top-left: green
            gl->glDisable(GL_SCISSOR_TEST);
            glSignalSemaphore(glReadySem, 0, nullptr, 1, &texture, &layout);
            gl->glFlush();
            glCpu = NowNs() - g0;
        }

        uint32_t image = 0;
        VKCHECK(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired[slot], VK_NULL_HANDLE, &image));
        const bool check = opt.gl && (k % 250 == 20);
        VkCommandBuffer cmd = cmds[slot];
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VKCHECK(vkBeginCommandBuffer(cmd, &bi));
        vkCmdResetQueryPool(cmd, queries, slot * 2, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, slot * 2);
        if (opt.gl)
        {
            VkImageMemoryBarrier2 acq =
                Barrier(shared, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            acq.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            acq.dstQueueFamilyIndex = family;
            acq.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            acq.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            acq.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            VkImageMemoryBarrier2 dst =
                Barrier(images[image], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            dst.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            dst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            dst.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            Barriers(cmd, {acq, dst});
            // GL's rows are bottom-up: the blit flips them, and scales to the window
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.srcOffsets[1] = {static_cast<int32_t>(gw), static_cast<int32_t>(gh), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstOffsets[0] = {0, static_cast<int32_t>(extent.height), 0};
            blit.dstOffsets[1] = {static_cast<int32_t>(extent.width), 0, 1};
            vkCmdBlitImage(cmd, shared, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, images[image],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, queries, slot * 2 + 1);
            if (check)
            {
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {gw, gh, 1};
                vkCmdCopyImageToBuffer(cmd, shared, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
            }
            VkImageMemoryBarrier2 rel = acq;
            rel.srcQueueFamilyIndex = family;
            rel.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
            rel.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            rel.srcAccessMask = 0;
            rel.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            rel.dstAccessMask = 0;
            VkImageMemoryBarrier2 pres =
                Barrier(images[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            pres.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            pres.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            pres.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            Barriers(cmd, {rel, pres});
        }
        else
        {
            VkImageMemoryBarrier2 toColor =
                Barrier(images[image], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            toColor.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            Barriers(cmd, {toColor});
            VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            att.imageView = views[image];
            att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            const float shade = (k & 1) ? 0.25f : 0.15f;
            att.clearValue.color = {{shade, shade, shade, 1.0f}};
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ri.renderArea = {{0, 0}, extent};
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &att;
            vkCmdBeginRendering(cmd, &ri);
            VkClearRect bar{{{static_cast<int32_t>(BarX(k, extent.width)), 0}, {64, extent.height}}, 0, 1};
            VkClearAttachment white{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
            white.clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
            vkCmdClearAttachments(cmd, 1, &white, 1, &bar);
            vkCmdEndRendering(cmd);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, queries, slot * 2 + 1);
            VkImageMemoryBarrier2 pres =
                Barrier(images[image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            pres.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            pres.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            pres.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            Barriers(cmd, {pres});
        }
        VKCHECK(vkEndCommandBuffer(cmd));
        VkSemaphore waits[2] = {acquired[slot], glReady};
        VkPipelineStageFlags stages[2] = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
        VkSemaphore signalList[2] = {rendered[image], vkReleased};
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = opt.gl ? 2 : 1;
        si.pWaitSemaphores = waits;
        si.pWaitDstStageMask = stages;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = opt.gl ? 2 : 1;
        si.pSignalSemaphores = signalList;
        VKCHECK(vkQueueSubmit(queue, 1, &si, fences[slot]));

        if (g_presentation)
        {
            struct wp_presentation_feedback* fb = wp_presentation_feedback(g_presentation, g_surface);
            wp_presentation_feedback_add_listener(fb, &kFeedback, reinterpret_cast<void*>(static_cast<size_t>(k)));
        }
        const uint64_t presentId = static_cast<uint64_t>(k) + 1;
        VkPresentIdKHR pid{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
        pid.swapchainCount = 1;
        pid.pPresentIds = &presentId;
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.pNext = &pid;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &rendered[image];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &image;
        const int64_t call = NowNs();
        VKCHECK(vkQueuePresentKHR(queue, &pi));

        int checkResult = 0;
        if (check)
        {
            // The picture: Vulkan's row 0 must be GL's y 0 (red at the bottom-left of GL), exact RGBA bytes, and
            // the bar where frame k put it (not an older frame)
            VKCHECK(vkWaitForFences(device, 1, &fences[slot], VK_TRUE, UINT64_MAX));
            const auto* px = static_cast<const uint8_t*>(readbackPtr);
            auto at = [&](uint32_t x, uint32_t y) { return px + (static_cast<size_t>(y) * gw + x) * 4; };
            const uint8_t* row0 = at(100, 100);
            const uint8_t* rowTop = at(100, gh - 100);
            const uint32_t mid = gh / 2;
            uint32_t barStart = UINT32_MAX;
            for (uint32_t x = 0; x < gw && barStart == UINT32_MAX; x++)
                if (at(x, mid)[0] == 255 && at(x, mid)[1] == 255 && at(x, mid)[2] == 255)
                    barStart = x;
            const bool redLow = row0[0] == 255 && row0[1] == 0 && row0[2] == 0 && row0[3] == 255;
            const bool greenHigh = rowTop[0] == 0 && rowTop[1] == 255 && rowTop[2] == 0;
            const bool barOk = barStart == BarX(k, gw);
            checkResult = (redLow && greenHigh && barOk) ? 1 : -1;
            (checkResult > 0 ? checksOk : checksBad)++;
            if (checkResult < 0 || checksOk == 1)
                std::fprintf(stderr,
                             "check k=%d: Vulkan row 100 = (%u,%u,%u,%u) %s, row %u = (%u,%u,%u) %s, bar at %u "
                             "(frame k: %u)\n",
                             k, row0[0], row0[1], row0[2], row0[3], redLow ? "red = GL bottom" : "NOT red",
                             gh - 100, rowTop[0], rowTop[1], rowTop[2], greenHigh ? "green = GL top" : "NOT green",
                             barStart, BarX(k, gw));
        }
        {
            std::lock_guard<std::mutex> lock(g_recMutex);
            FrameRecord& r = g_records[static_cast<size_t>(k)];
            r.k = k;
            r.target = target;
            r.call = call;
            r.glCpuNs = glCpu;
            r.check = checkResult;
        }
        {
            std::lock_guard<std::mutex> lock(waitMutex);
            waitQueue.emplace_back(presentId, static_cast<size_t>(k));
            waitCv.notify_one();
        }
    }
    vkDeviceWaitIdle(device);
    SleepUntil(NowNs() + 300'000'000);
    {
        std::lock_guard<std::mutex> lock(waitMutex);
        waitStop = true;
        waitCv.notify_one();
    }
    waiter.join();
    std::fprintf(stderr, "picture checks: %d right, %d wrong\n", checksOk, checksBad);
    if (opt.gl)
        ctx.doneCurrent();
    done = true;
}

void WriteCsv(const Options& opt)
{
    FILE* f = std::fopen(opt.out.c_str(), "w");
    std::fprintf(f, "k,target,call,waited,presented,refresh,flags,discarded,blit_ns,gl_cpu_ns,check\n");
    {
        std::lock_guard<std::mutex> lock(g_recMutex);
        for (const FrameRecord& r : g_records)
            if (r.target)
                std::fprintf(f, "%lld,%lld,%lld,%lld,%lld,%u,%u,%d,%lld,%lld,%d\n", static_cast<long long>(r.k),
                             static_cast<long long>(r.target), static_cast<long long>(r.call),
                             static_cast<long long>(r.waited), static_cast<long long>(r.presented), r.refresh,
                             r.flags, r.discarded ? 1 : 0, static_cast<long long>(r.blitNs),
                             static_cast<long long>(r.glCpuNs), r.check);
    }
    std::fclose(f);
    std::fprintf(stderr, "wrote %s\n", opt.out.c_str());
}

Options Parse(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--window")
            o.container = next() == "container";
        else if (a == "--source")
            o.gl = next() == "gl";
        else if (a == "--frames")
            o.frames = std::atoi(next().c_str());
        else if (a == "--lead-ms")
            o.leadNs = static_cast<int64_t>(std::atof(next().c_str()) * 1e6);
        else if (a == "--gl-size")
            std::sscanf(next().c_str(), "%ux%u", &o.glWidth, &o.glHeight);
        else if (a == "--out")
            o.out = next();
    }
    return o;
}

}  // namespace

int main(int argc, char** argv)
{
    const Options opt = Parse(argc, argv);
    qputenv("QT_QPA_PLATFORM", "wayland");
    QApplication app(argc, argv);

    // Our VkInstance, adopted by QVulkanInstance so that Qt makes the QWindow a Vulkan surface
    const char* instExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "poc054-qt";
    appInfo.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &appInfo;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = instExt;
    Shared sh;
    VKCHECK(vkCreateInstance(&ici, nullptr, &sh.instance));
    QVulkanInstance qvk;
    qvk.setVkInstance(sh.instance);
    if (!qvk.create())
    {
        std::fprintf(stderr, "QVulkanInstance::create failed\n");
        return 2;
    }

    auto* vkWindow = new QWindow;
    vkWindow->setSurfaceType(QSurface::VulkanSurface);
    vkWindow->setVulkanInstance(&qvk);
    QWidget* top = nullptr;
    if (opt.container)
    {
        top = new QWidget;
        auto* layout = new QVBoxLayout(top);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(new QLabel("POC 056: a Vulkan QWindow inside QWidget::createWindowContainer"));
        QWidget* container = QWidget::createWindowContainer(vkWindow, top);
        container->setFixedSize(1280, 720);
        layout->addWidget(container);
        top->setWindowTitle("POC 056 container");
        top->show();
    }
    else
    {
        vkWindow->setTitle("POC 056 top");
        vkWindow->resize(1280, 720);
        vkWindow->show();
    }
    const int64_t deadline = NowNs() + 5'000'000'000;
    while (!vkWindow->isExposed() && NowNs() < deadline)
        app.processEvents(QEventLoop::AllEvents, 50);
    if (!vkWindow->isExposed())
    {
        std::fprintf(stderr, "the Vulkan window was never exposed\n");
        return 2;
    }

    auto* waylandApp = app.nativeInterface<QNativeInterface::QWaylandApplication>();
    g_display = waylandApp ? waylandApp->display() : nullptr;
    g_surface = static_cast<wl_surface*>(
        QGuiApplication::platformNativeInterface()->nativeResourceForWindow("surface", vkWindow));
    if (!g_display || !g_surface)
    {
        std::fprintf(stderr, "no wl_display / wl_surface from Qt\n");
        return 2;
    }
    g_queue = wl_display_create_queue(g_display);
    auto* wrapped = static_cast<wl_display*>(wl_proxy_create_wrapper(g_display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapped), g_queue);
    wl_registry* registry = wl_display_get_registry(wrapped);
    wl_proxy_wrapper_destroy(wrapped);
    wl_registry_add_listener(registry, &kRegistry, nullptr);
    wl_display_roundtrip_queue(g_display, g_queue);

    sh.surface = QVulkanInstance::surfaceForWindow(vkWindow);
    const qreal dpr = vkWindow->devicePixelRatio();
    sh.extent = {static_cast<uint32_t>(vkWindow->width() * dpr), static_cast<uint32_t>(vkWindow->height() * dpr)};
    std::fprintf(stderr, "window %s, %dx%d dpr %.2f, surface %p, presentation %s\n",
                 opt.container ? "container" : "top", vkWindow->width(), vkWindow->height(), dpr,
                 static_cast<void*>(g_surface), g_presentation ? "yes" : "no");
    sh.offscreen = new QOffscreenSurface;
    sh.offscreen->create();

    g_records.resize(static_cast<size_t>(opt.frames));
    std::thread events(EventThread);
    std::atomic<bool> done{false};
    std::thread render([&] {
        Render(opt, sh, done);
        WriteCsv(opt);  // before quit: Qt tears the window's surface down on the way out
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });
    if (std::getenv("POC054_BUSY_LOOP"))
        while (!done)
            app.processEvents(QEventLoop::AllEvents, 20);
    else
        app.exec();
    render.join();
    g_eventsRun = false;
    events.join();

    std::_Exit(0);  // skip teardown: the POC measures, it does not clean up Vulkan/GL objects
}
