#include "vulkanpresenter.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "common/timehelper.h"
#include "emulator/video/screenbeat.h"
#include "platform/presentlead.h"
#include "vulkandevice.h"

namespace gpu
{
namespace
{
/// vkQueuePresentKHR started this much before the frame's time
constexpr int64_t kPresentCostNs = 500'000;
/// The adaptive lead's first margin (the wake's slack, the draw, the blit, the compositor)
constexpr int64_t kLeadStartNs = 8'000'000;
/// The swapchain images beyond the surface's minimum: the most the window holds queued for their time
constexpr uint32_t kExtraImages = 2;
/// Waits on the GUI thread: for an image, for the present thread to drain before a new swapchain
constexpr uint64_t kAcquireTimeoutNs = 100'000'000;

int64_t SteadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

const char* ModeName(VkPresentModeKHR mode)
{
    switch (mode)
    {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR:
            return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return "FIFO_RELAXED";
        default:
            return "other";
    }
}
}  // namespace

struct VulkanPresenter::Impl
{
    struct Item
    {
        uint32_t image = 0;
        int64_t atNs = 0;
        int64_t handedNs = 0;
        uint64_t number = 0;
        uint64_t presentId = 0;
        int64_t submittedNs = 0;  ///< present thread: vkQueuePresentKHR returned
        NativePresenter::TraceInfo trace;
    };

    QWindow* window = nullptr;
    QMetaObject::Connection screenChanged;  ///< the screen's refresh followed (screenPeriodNs)
    VulkanDevice* device = nullptr;
    VkDevice dev = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::string error;

    // GUI thread
    std::unique_ptr<Texture> target;
    bool drawing = false;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::vector<VkSemaphore> acquired;  ///< images + 1, in turn
    size_t nextAcquire = 0;
    std::vector<VkSemaphore> rendered;  ///< one per image
    uint32_t heldMax = 1;               ///< images the window may hold queued (count - the surface's minimum)

    // Both threads under swapchainMutex: vkAcquireNextImageKHR and vkQueuePresentKHR on one swapchain
    std::mutex swapchainMutex;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkPresentModeKHR> modes;  ///< the surface's
    std::atomic<bool> recreate{false};  ///< OUT_OF_DATE / SUBOPTIMAL seen, or another mode wanted

    PFN_vkWaitForPresentKHR waitForPresent = nullptr;
    PFN_vkGetPastPresentationTimingGOOGLE pastTiming = nullptr;
    PFN_vkGetRefreshCycleDurationGOOGLE refreshCycle = nullptr;
    uint64_t nextPresentId = 0;

    // The present thread
    std::thread thread;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::condition_variable idleCv;
    std::deque<Item> queue;
    bool busy = false;  ///< presenting an item taken off the queue
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    WakeSignal wake;
    std::deque<Item> timing;  ///< display timing: presented, the screen time not known yet

    std::atomic<uint8_t> policy{0};
    std::atomic<int64_t> lastScreenNs{0};
    std::atomic<int64_t> refreshPeriodNs{0};
    /// The screen's own refresh (QScreen, read on the GUI thread): the period the measured steps refine
    std::atomic<int64_t> screenPeriodNs{0};
    bool wayland = false;  ///< the window on a Wayland compositor (Create)
    int64_t lastStepNs = 0;  ///< present thread: the step before (a period without the screen's: two alike)
    mutable std::atomic<bool> exposed{true};
    mutable PresentLead lead{kLeadStartNs};

    mutable std::mutex statsMutex;
    std::string failure;
    presentstats::Ring errors;
    presentstats::Ring gpuWaits;
    presentstats::Ring acquireWaits;
    ScreenBeat beat;
    uint64_t presented = 0;
    uint64_t shown = 0;
    uint64_t dropped = 0;
    presentstats::Trace trace;

    PresentPolicy Policy() const { return static_cast<PresentPolicy>(policy.load(std::memory_order_relaxed)); }

    bool Feedback() const { return waitForPresent != nullptr || pastTiming != nullptr; }

    void Fail(const std::string& why)
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        failure = why;
    }

    /// The present mode the policy wants among the surface's
    VkPresentModeKHR WantedMode() const
    {
        auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
        if (Policy() == PresentPolicy::Vrr)
        {
            // Each frame at its own time: the display starts a refresh when it comes
            if (has(VK_PRESENT_MODE_IMMEDIATE_KHR))
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            if (has(VK_PRESENT_MODE_FIFO_RELAXED_KHR))
                return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    void DestroySemaphores()
    {
        for (VkSemaphore s : acquired)
            vkDestroySemaphore(dev, s, nullptr);
        for (VkSemaphore s : rendered)
            vkDestroySemaphore(dev, s, nullptr);
        acquired.clear();
        rendered.clear();
    }

    /// GUI thread, the present thread idle: the swapchain for the window's size and the policy
    bool MakeSwapchain(int width, int height)
    {
        VkSurfaceCapabilitiesKHR caps{};
        VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->Physical(), surface, &caps);
        if (r != VK_SUCCESS)
        {
            Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR: " + std::to_string(r));
            if (r == VK_ERROR_SURFACE_LOST_KHR || r == VK_ERROR_DEVICE_LOST)
                failed = true;
            return false;
        }
        VkExtent2D want = caps.currentExtent;
        if (want.width == 0xFFFFFFFFu)
            want = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        want.width = std::clamp(want.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        want.height = std::clamp(want.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        if (want.width == 0 || want.height == 0)
        {
            Fail("the surface has no size (minimized)");
            return false;
        }
        if (format == VK_FORMAT_UNDEFINED)
        {
            uint32_t count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(device->Physical(), surface, &count, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device->Physical(), surface, &count, formats.data());
            // The picture's bytes as they are (GL's default framebuffer is not sRGB either)
            for (const VkSurfaceFormatKHR& f : formats)
                if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    format = f.format;
                    break;
                }
            if (format == VK_FORMAT_UNDEFINED && !formats.empty())
                format = formats[0].format;
        }
        uint32_t images = caps.minImageCount + kExtraImages;
        if (caps.maxImageCount > 0)
            images = std::min(images, caps.maxImageCount);
        const VkPresentModeKHR wantedMode = WantedMode();

        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface;
        info.minImageCount = images;
        info.imageFormat = format;
        info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        info.imageExtent = want;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                                ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                : caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                              VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR})
            if (caps.supportedCompositeAlpha & a)
            {
                info.compositeAlpha = a;
                break;
            }
        info.presentMode = wantedMode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = swapchain;

        // Nothing of the old one in flight: the present thread drained, the queue idle
        {
            std::lock_guard<std::mutex> lock(device->QueueMutex());
            vkQueueWaitIdle(device->Queue());
        }
        VkSwapchainKHR made = VK_NULL_HANDLE;
        r = vkCreateSwapchainKHR(dev, &info, nullptr, &made);
        std::lock_guard<std::mutex> lock(swapchainMutex);
        if (swapchain)
            vkDestroySwapchainKHR(dev, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        DestroySemaphores();
        if (r != VK_SUCCESS)
        {
            Fail("vkCreateSwapchainKHR: " + std::to_string(r));
            if (r == VK_ERROR_SURFACE_LOST_KHR || r == VK_ERROR_DEVICE_LOST)
                failed = true;
            return false;
        }
        swapchain = made;
        extent = want;
        mode = wantedMode;
        uint32_t count = 0;
        vkGetSwapchainImagesKHR(dev, swapchain, &count, nullptr);
        this->images.resize(count);
        vkGetSwapchainImagesKHR(dev, swapchain, &count, this->images.data());
        heldMax = std::max<uint32_t>(1, count > caps.minImageCount ? count - caps.minImageCount : 1);
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        acquired.resize(count + 1);
        rendered.resize(count);
        for (VkSemaphore& s : acquired)
            vkCreateSemaphore(dev, &semaphore, nullptr, &s);
        for (VkSemaphore& s : rendered)
            vkCreateSemaphore(dev, &semaphore, nullptr, &s);
        nextAcquire = 0;
        timing.clear();
        recreate = false;
        if (refreshCycle)
        {
            VkRefreshCycleDurationGOOGLE cycle{};
            if (refreshCycle(dev, swapchain, &cycle) == VK_SUCCESS && cycle.refreshDuration > 0)
                refreshPeriodNs = static_cast<int64_t>(cycle.refreshDuration);
        }
        return true;
    }

    /// GUI thread: the present thread done with everything handed over (false: it did not drain in time)
    bool WaitDrained()
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        return idleCv.wait_for(lock, std::chrono::nanoseconds(kAcquireTimeoutNs),
                               [&] { return (queue.empty() && !busy) || failed; });
    }

    void Run()
    {
        while (true)
        {
            Item item;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                busy = false;
                idleCv.notify_all();
                // Display timing comes after the present: asked again while nothing waits to be presented
                queueCv.wait_for(lock, std::chrono::milliseconds(timing.empty() ? 1000 : 20),
                                 [&] { return stop || !queue.empty(); });
                if (stop)
                    break;
                if (queue.empty())
                {
                    lock.unlock();
                    PollTiming();
                    continue;
                }
                item = queue.front();
                queue.pop_front();
                busy = true;
            }
            const int64_t wakeNs = item.atNs - kPresentCostNs;
            if (item.atNs > 0 && wakeNs > SteadyNs())
                TimeHelper::WaitUntilPrecise(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(wakeNs)),
                                             [this] { return stop.load(); }, &wake);
            Present(item);
            PollTiming();
        }
    }

    /// Present thread: the item's image to the screen, its screen time when present_wait gives it at once
    void Present(Item& item)
    {
        VkSwapchainKHR chain;
        VkResult r;
        {
            std::lock_guard<std::mutex> lock(swapchainMutex);
            chain = swapchain;
            if (!chain)
                return;
            VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            info.waitSemaphoreCount = 1;
            info.pWaitSemaphores = &rendered[item.image];
            info.swapchainCount = 1;
            info.pSwapchains = &chain;
            info.pImageIndices = &item.image;
            VkPresentIdKHR id{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
            VkPresentTimeGOOGLE time{};
            VkPresentTimesInfoGOOGLE times{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};
            const void** tail = &info.pNext;
            if (waitForPresent)
            {
                id.swapchainCount = 1;
                id.pPresentIds = &item.presentId;
                *tail = &id;
                tail = &id.pNext;
            }
            if (pastTiming)
            {
                time.presentID = static_cast<uint32_t>(item.presentId);
                times.swapchainCount = 1;
                times.pTimes = &time;
                *tail = &times;
            }
            std::lock_guard<std::mutex> queueLock(device->QueueMutex());
            r = vkQueuePresentKHR(device->Queue(), &info);
        }
        item.submittedNs = SteadyNs();
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            recreate = true;
        else if (r == VK_ERROR_DEVICE_LOST || r == VK_ERROR_SURFACE_LOST_KHR)
        {
            failed = true;
            Fail("vkQueuePresentKHR: " + std::to_string(r));
        }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            dropped++;
            trace.Write(item.trace, item.atNs, item.submittedNs, 0, 0);
            return;
        }
        if (waitForPresent && exposed.load(std::memory_order_relaxed))
        {
            // Returns when the image is on screen: that moment, to the wake's slack
            const VkResult w = waitForPresent(dev, chain, item.presentId, kAcquireTimeoutNs);
            Record(item, w == VK_SUCCESS ? SteadyNs() : 0);
        }
        else if (pastTiming)
            timing.push_back(item);
        else
            Record(item, 0);
    }

    /// Present thread: the screen times display timing has for the presents waiting for them
    void PollTiming()
    {
        if (!pastTiming || timing.empty())
            return;
        std::vector<VkPastPresentationTimingGOOGLE> past;
        {
            std::lock_guard<std::mutex> lock(swapchainMutex);
            if (!swapchain)
                return;
            uint32_t count = 0;
            if (pastTiming(dev, swapchain, &count, nullptr) != VK_SUCCESS || count == 0)
                return;
            past.resize(count);
            if (pastTiming(dev, swapchain, &count, past.data()) != VK_SUCCESS)
                return;
            past.resize(count);
        }
        for (const VkPastPresentationTimingGOOGLE& p : past)
            while (!timing.empty() && static_cast<uint32_t>(timing.front().presentId) <= p.presentID)
            {
                const Item item = timing.front();
                timing.pop_front();
                Record(item, static_cast<uint32_t>(item.presentId) == p.presentID
                                 ? static_cast<int64_t>(p.actualPresentTime)
                                 : 0);
            }
        // Never told (an occluded window's presents): not kept for ever
        while (timing.size() > 64)
        {
            Record(timing.front(), 0);
            timing.pop_front();
        }
    }

    /// Without the driver's refresh duration: the period from the steps between two shown presents. A step is 1, 2
    /// or 3 refreshes (a 50 Hz machine on a 60 Hz screen) and the moment present_wait returns jitters by its wake
    /// (Wayland: the compositor's feedback), so the shortest step alone runs short (it read 71-329 Hz on a 60 Hz
    /// screen). With the screen's refresh: the steps near a multiple of it refine it (a running mean); without it:
    /// the shortest step seen twice in a row within 5 %
    void MeasurePeriod(int64_t step)
    {
        const int64_t screen = screenPeriodNs.load(std::memory_order_relaxed);
        int64_t period = refreshPeriodNs.load(std::memory_order_relaxed);
        if (screen > 0)
        {
            if (period <= 0)
                period = screen;
            for (int64_t refreshes = 1; refreshes <= 3; ++refreshes)
            {
                const int64_t one = step / refreshes;
                if (std::llabs(one - screen) * 10 <= screen)
                {
                    period += (one - period) / 16;
                    break;
                }
            }
            refreshPeriodNs.store(period, std::memory_order_relaxed);
            return;
        }
        const bool alike = lastStepNs > 0 && std::llabs(step - lastStepNs) * 20 <= step;
        lastStepNs = step;
        if (alike && step > 2'000'000 && (period == 0 || step < period))
            refreshPeriodNs.store(step, std::memory_order_relaxed);
    }

    void Record(const Item& item, int64_t screenNs)
    {
        if (screenNs > 0)
        {
            const int64_t last = lastScreenNs.exchange(screenNs, std::memory_order_relaxed);
            if (last > 0 && !refreshCycle)
                MeasurePeriod(screenNs - last);
        }
        std::lock_guard<std::mutex> lock(statsMutex);
        if (screenNs > 0)
        {
            shown++;
            const int64_t asked = std::max(item.atNs, item.submittedNs);
            errors.Add(static_cast<double>(screenNs - asked) / 1e6);
            const int64_t own = item.atNs > 0 ? item.atNs : asked;
            // Wayland: a late present was held by the compositor (Mesa's FIFO queues behind its repaint), more lead
            // only queues more (measured on mutter: a lead of 8 or 20 ms shows ~16 ms after the time, 40 ms ~33 ms)
            lead.Shown(screenNs - own, item.number,
                       wayland ? std::numeric_limits<int64_t>::max() : std::max(own, item.handedNs) - item.handedNs);
            const int64_t period = refreshPeriodNs.load(std::memory_order_relaxed);
            beat.Shown(item.trace.picture, item.trace.targetNs, screenNs, period > 0 ? period : 16'666'667);
        }
        trace.Write(item.trace, item.atNs, item.submittedNs, screenNs, 0);
    }
};

VulkanPresenter::VulkanPresenter() : _impl(std::make_unique<Impl>()) {}

std::unique_ptr<VulkanPresenter> VulkanPresenter::Create(QWindow* window, VulkanDevice& device, VkSurfaceKHR surface,
                                                         std::string& error)
{
    if (!surface || !device.Handle() || !device.Enabled(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    {
        error = "the device was not made for a window surface";
        return nullptr;
    }
    std::unique_ptr<VulkanPresenter> p(new VulkanPresenter());
    Impl& impl = *p->_impl;
    impl.window = window;
    impl.device = &device;
    auto screenPeriod = [&impl](QScreen* screen) {
        const double hz = screen ? screen->refreshRate() : 0.0;
        impl.screenPeriodNs.store(hz > 1.0 ? static_cast<int64_t>(1e9 / hz) : 0, std::memory_order_relaxed);
        impl.refreshPeriodNs.store(0, std::memory_order_relaxed);  // measured again against the new screen
    };
    screenPeriod(window->screen());
    impl.wayland = QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
    impl.screenChanged = QObject::connect(window, &QWindow::screenChanged, window, screenPeriod);
    impl.dev = device.Handle();
    impl.surface = surface;
    uint32_t modes = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device.Physical(), surface, &modes, nullptr);
    impl.modes.resize(modes);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device.Physical(), surface, &modes, impl.modes.data());
    if (device.Enabled(VK_KHR_PRESENT_WAIT_EXTENSION_NAME) && device.Enabled(VK_KHR_PRESENT_ID_EXTENSION_NAME))
        impl.waitForPresent =
            reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(impl.dev, "vkWaitForPresentKHR"));
    if (!impl.waitForPresent && device.Enabled(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME))
    {
        impl.pastTiming = reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
            vkGetDeviceProcAddr(impl.dev, "vkGetPastPresentationTimingGOOGLE"));
        impl.refreshCycle = reinterpret_cast<PFN_vkGetRefreshCycleDurationGOOGLE>(
            vkGetDeviceProcAddr(impl.dev, "vkGetRefreshCycleDurationGOOGLE"));
    }
    const QSize pixels = (QSizeF(window->size()) * window->devicePixelRatio()).toSize();
    if (!impl.MakeSwapchain(std::max(1, pixels.width()), std::max(1, pixels.height())))
    {
        std::lock_guard<std::mutex> lock(impl.statsMutex);
        error = impl.failure;
        return nullptr;
    }
    impl.thread = std::thread([&impl] { impl.Run(); });
    return p;
}

VulkanPresenter::~VulkanPresenter()
{
    Impl& impl = *_impl;
    QObject::disconnect(impl.screenChanged);
    {
        std::lock_guard<std::mutex> lock(impl.queueMutex);
        impl.stop = true;
    }
    impl.queueCv.notify_all();
    impl.wake.Notify();
    if (impl.thread.joinable())
        impl.thread.join();
    {
        std::lock_guard<std::mutex> lock(impl.device->QueueMutex());
        vkQueueWaitIdle(impl.device->Queue());
    }
    impl.target.reset();
    impl.device->Finish();  // the target's deferred deletion
    if (impl.swapchain)
        vkDestroySwapchainKHR(impl.dev, impl.swapchain, nullptr);
    impl.DestroySemaphores();
}

unsigned VulkanPresenter::BeginFrame(int, int)
{
    return 0;  // not a GL presenter
}

Texture* VulkanPresenter::BeginTexture(int width, int height)
{
    Impl& impl = *_impl;
    if (width <= 0 || height <= 0)
        return nullptr;
    if (impl.failed || impl.device->Lost())
    {
        impl.Fail("the device or the surface is lost");
        return nullptr;
    }
    // The size the surface has (macOS: the layer's drawable size), else the window's
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(impl.device->Physical(), impl.surface, &caps);
    VkExtent2D want = caps.currentExtent;
    if (want.width == 0xFFFFFFFFu)
        want = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    const bool resized = want.width != impl.extent.width || want.height != impl.extent.height;
    if (!impl.swapchain || resized || impl.recreate || impl.WantedMode() != impl.mode)
    {
        if (!impl.WaitDrained())
        {
            impl.Fail("the present thread did not drain for a new swapchain");
            return nullptr;
        }
        if (!impl.MakeSwapchain(width, height))
            return nullptr;
    }
    if (!impl.target || impl.target->Width() != width || impl.target->Height() != height)
    {
        impl.target = impl.device->CreateTexture(width, height, Format::RGBA8);
        if (!impl.target)
        {
            impl.Fail("no target texture of " + std::to_string(width) + "x" + std::to_string(height));
            return nullptr;
        }
    }
    impl.drawing = true;
    return impl.target.get();
}

void VulkanPresenter::PresentFrame(int64_t atNs, const TraceInfo& trace)
{
    Impl& impl = *_impl;
    if (!impl.drawing)
        return;
    impl.drawing = false;
    // At most heldMax images queued for their time: an acquire beyond them could wait for ever
    {
        std::unique_lock<std::mutex> lock(impl.queueMutex);
        if (!impl.idleCv.wait_for(lock, std::chrono::nanoseconds(kAcquireTimeoutNs), [&] {
                return impl.queue.size() + (impl.busy ? 1 : 0) < impl.heldMax || impl.failed;
            }))
        {
            lock.unlock();
            impl.device->Flush();
            impl.Fail("the present thread holds every image");
            std::lock_guard<std::mutex> stats(impl.statsMutex);
            impl.dropped++;
            return;
        }
    }
    const int64_t acquireStart = SteadyNs();
    uint32_t index = 0;
    VkResult r;
    VkSemaphore acquired = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(impl.swapchainMutex);
        if (!impl.swapchain)
        {
            impl.device->Flush();
            return;
        }
        acquired = impl.acquired[impl.nextAcquire];
        r = vkAcquireNextImageKHR(impl.dev, impl.swapchain, kAcquireTimeoutNs, acquired, VK_NULL_HANDLE, &index);
    }
    const int64_t acquireNs = SteadyNs() - acquireStart;
    if (r == VK_SUBOPTIMAL_KHR)
        impl.recreate = true;
    else if (r != VK_SUCCESS)
    {
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
            impl.recreate = true;
        else if (r == VK_ERROR_DEVICE_LOST || r == VK_ERROR_SURFACE_LOST_KHR)
            impl.failed = true;
        impl.device->Flush();  // the picture's work goes anyway: the next frame's barriers expect it done
        impl.Fail("vkAcquireNextImageKHR: " + std::to_string(r));
        std::lock_guard<std::mutex> stats(impl.statsMutex);
        impl.dropped++;
        return;
    }
    impl.nextAcquire = (impl.nextAcquire + 1) % impl.acquired.size();
    impl.device->RecordPresentBlit(*impl.target, impl.images[index], impl.extent, VK_IMAGE_LAYOUT_UNDEFINED);
    impl.device->Submit(acquired, impl.rendered[index]);
    Impl::Item item;
    item.image = index;
    item.atNs = atNs;
    item.handedNs = SteadyNs();
    item.presentId = ++impl.nextPresentId;
    item.trace = trace;
    {
        std::lock_guard<std::mutex> lock(impl.statsMutex);
        item.number = ++impl.presented;
        impl.acquireWaits.Add(static_cast<double>(acquireNs) / 1e6);
        impl.gpuWaits.Add(0.0);
    }
    {
        std::lock_guard<std::mutex> lock(impl.queueMutex);
        impl.queue.push_back(item);
    }
    impl.queueCv.notify_one();
    impl.wake.Notify();
}

void VulkanPresenter::SetPolicy(PresentPolicy policy)
{
    _impl->policy.store(static_cast<uint8_t>(policy), std::memory_order_relaxed);
}

bool VulkanPresenter::IsVisible() const
{
    const bool exposed = _impl->window->isExposed();
    _impl->exposed.store(exposed, std::memory_order_relaxed);
    return exposed;
}

int64_t VulkanPresenter::LeadNs(int64_t refreshPeriodNs) const
{
    return _impl->lead.Margin(refreshPeriodNs);
}

bool VulkanPresenter::RefreshGrid(int64_t& refreshNs, int64_t& periodNs) const
{
    const Impl& impl = *_impl;
    const int64_t last = impl.lastScreenNs.load(std::memory_order_relaxed);
    const int64_t period = impl.refreshPeriodNs.load(std::memory_order_relaxed);
    if (last == 0 || period <= 0 || impl.mode != VK_PRESENT_MODE_FIFO_KHR)
        return false;
    refreshNs = last;
    periodNs = period;
    return true;
}

uint64_t VulkanPresenter::Presented() const
{
    std::lock_guard<std::mutex> lock(_impl->statsMutex);
    return _impl->presented;
}

uint64_t VulkanPresenter::LastLatePresent() const
{
    return _impl->lead.LastLate();
}

uint32_t VulkanPresenter::LastLateLag() const
{
    return _impl->lead.LastLateLag();
}

uint64_t VulkanPresenter::LastOnTimePresent() const
{
    return _impl->lead.LastOnTime();
}

std::string VulkanPresenter::LastFailure() const
{
    std::lock_guard<std::mutex> lock(_impl->statsMutex);
    return _impl->failure;
}

bool VulkanPresenter::Lost() const
{
    return _impl->failed.load() || _impl->device->Lost();
}

NativePresenter::Stats VulkanPresenter::GetStats() const
{
    const Impl& impl = *_impl;
    Stats s;
    std::vector<double> errors, waits, acquires;
    {
        std::lock_guard<std::mutex> lock(impl.statsMutex);
        errors = impl.errors.Values();
        waits = impl.gpuWaits.Values();
        acquires = impl.acquireWaits.Values();
        s.presented = impl.presented;
        s.shown = impl.shown;
        s.dropped = impl.dropped;
        const ScreenBeat::Stats beat = impl.beat.GetStats();
        s.beatFrames = beat.frames;
        s.offBeat = beat.offBeat;
        s.soundOffsetP50 = beat.offsetP50Ms;
        s.soundOffsetP99 = beat.offsetP99Ms;
    }
    s.late = impl.lead.Late();
    s.leadMs = static_cast<double>(impl.lead.MarginNs()) / 1e6;
    presentstats::Fill(s, std::move(errors), std::move(waits), std::move(acquires));
    const bool vrr = impl.mode != VK_PRESENT_MODE_FIFO_KHR;
    const int64_t period = impl.refreshPeriodNs.load(std::memory_order_relaxed);
    double hz = period > 0 ? 1e9 / static_cast<double>(period) : 0.0;
    if (hz == 0.0 && impl.window->screen())
        hz = impl.window->screen()->refreshRate();
    s.maxRefreshHz = hz;
    s.minRefreshHz = vrr ? 0.0 : hz;
    s.visible = impl.exposed.load(std::memory_order_relaxed);
    s.vrr = vrr;
    s.vrrSupport = vrr ? "yes" : "unknown";
    s.vrrReason = vrr ? std::string("present mode ") + ModeName(impl.mode) : "";
    return s;
}

std::string VulkanPresenter::Describe() const
{
    const Impl& impl = *_impl;
    const Stats s = GetStats();
    char line[640];
    std::snprintf(line, sizeof line,
                  "Vulkan swapchain %s, %zu images %ux%u (policy %s; feedback %s): %llu presented, %llu shown, %llu "
                  "dropped, %llu a refresh late (lead %.1f ms); error p50 %.2f p99 %.2f max %.2f ms; acquire p50 %.2f "
                  "max %.2f ms; display %.2f Hz%s%s",
                  ModeName(impl.mode), impl.images.size(), impl.extent.width, impl.extent.height,
                  PresentPolicyName(impl.Policy()),
                  impl.waitForPresent ? "VK_KHR_present_wait"
                  : impl.pastTiming   ? "VK_GOOGLE_display_timing"
                                      : "none",
                  static_cast<unsigned long long>(s.presented), static_cast<unsigned long long>(s.shown),
                  static_cast<unsigned long long>(s.dropped), static_cast<unsigned long long>(s.late), s.leadMs,
                  s.errorP50, s.errorP99, s.errorMax, s.drawableWaitP50, s.drawableWaitMax, s.maxRefreshHz,
                  s.visible ? "" : "; occluded", Lost() ? "; lost" : "");
    return line;
}
}  // namespace gpu
