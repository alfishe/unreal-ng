#include "platform/nativepresenter.h"

#if !defined(__APPLE__) && !defined(_WIN32)
// No native presenter here yet (Linux: Vulkan / Wayland later): the window presents with GL
std::unique_ptr<NativePresenter> NativePresenter::Create(void*)
{
    return nullptr;
}
#endif
