#include "backendfactory.h"

#include <atomic>

#if defined(__APPLE__)
#include "platform/macos/iohidbackend.h"
#elif defined(_WIN32)
#include "platform/windows/gameinputbackend.h"
#include "platform/windows/rawinputbackend.h"
#include "platform/windows/xinputbackend.h"
#elif defined(__linux__)
#include "platform/linux/evdevbackend.h"
#endif

namespace controllers
{

std::vector<std::unique_ptr<IBackend>> BackendFactory::CreateNative()
{
    std::vector<std::unique_ptr<IBackend>> backends;
#if defined(__APPLE__)
    backends.push_back(CreateIoHidBackend());
#elif defined(_WIN32)
    // Started in this order (HostInputService::StartNative): GameInput takes the Xbox pads when its runtime loads, Raw
    // Input the HID pads and the Surface Dial, XInput the Xbox pads only where GameInput did not start (D17)
    auto xboxServed = std::make_shared<std::atomic<bool>>(false);
    backends.push_back(CreateGameInputBackend(xboxServed));
    backends.push_back(CreateRawInputBackend());
    backends.push_back(CreateXInputBackend(xboxServed));
#elif defined(__linux__)
    backends.push_back(CreateEvdevBackend());
#endif
    return backends;
}

}  // namespace controllers
