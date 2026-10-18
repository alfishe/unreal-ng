#include "backendfactory.h"

#if defined(__APPLE__)
#include "platform/macos/iohidbackend.h"
#elif defined(_WIN32)
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
    backends.push_back(CreateXInputBackend());
#elif defined(__linux__)
    backends.push_back(CreateEvdevBackend());
#endif
    return backends;
}

}  // namespace controllers
