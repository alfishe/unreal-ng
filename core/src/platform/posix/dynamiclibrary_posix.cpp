// DynamicLibrary on macOS and Linux: dlopen (see dynamiclibrary.h)

#include "platform/dynamiclibrary.h"

#include <dlfcn.h>

bool DynamicLibrary::Open(const std::vector<std::string>& names, std::string& error)
{
    Close();
    error.clear();
    for (const std::string& name : names)
    {
        if (void* handle = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL))
        {
            _handle = handle;
            _name = name;
            error.clear();
            return true;
        }
        const char* reason = dlerror();
        error += (error.empty() ? "" : "; ") + name + ": " + (reason ? reason : "not found");
    }
    return false;
}

void DynamicLibrary::Close()
{
    if (_handle)
        dlclose(_handle);
    _handle = nullptr;
    _name.clear();
}

void* DynamicLibrary::Symbol(const char* name) const
{
    return _handle ? dlsym(_handle, name) : nullptr;
}
