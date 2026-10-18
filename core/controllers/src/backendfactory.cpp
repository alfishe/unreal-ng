#include "backendfactory.h"

namespace controllers
{

std::vector<std::unique_ptr<IBackend>> BackendFactory::CreateNative()
{
    return {};
}

}  // namespace controllers
