// DynamicLibrary on Windows: LoadLibrary (see dynamiclibrary.h)

#include "platform/dynamiclibrary.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

bool DynamicLibrary::Open(const std::vector<std::string>& names, std::string& error)
{
    Close();
    error.clear();
    for (const std::string& name : names)
    {
        // A full path loads its own dependencies from its folder (Npcap's wpcap.dll needs Packet.dll beside it)
        const bool fullPath = name.find('\\') != std::string::npos || name.find('/') != std::string::npos;
        if (HMODULE module = LoadLibraryExA(name.c_str(), nullptr, fullPath ? LOAD_WITH_ALTERED_SEARCH_PATH : 0))
        {
            _handle = reinterpret_cast<void*>(module);
            _name = name;
            error.clear();
            return true;
        }
        error += (error.empty() ? "" : "; ") + name + ": error " + std::to_string(GetLastError());
    }
    return false;
}

void DynamicLibrary::Close()
{
    if (_handle)
        FreeLibrary(reinterpret_cast<HMODULE>(_handle));
    _handle = nullptr;
    _name.clear();
}

void* DynamicLibrary::Symbol(const char* name) const
{
    return _handle ? reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(_handle), name)) : nullptr;
}
