#include "qsv_probe.h"

#ifdef _WIN32

#include <windows.h>
#include <dxgi.h>

#pragma comment(lib, "dxgi.lib")

namespace QsvProbe
{

namespace
{

bool findIntelGpu(std::string& adapterName)
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    bool found = false;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)))
        {
            if (desc.VendorId == 0x8086)  // Intel
            {
                char name[256];
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
                adapterName = name;
                found = true;
                adapter->Release();
                break;
            }
        }
        adapter->Release();
    }

    factory->Release();
    return found;
}

HMODULE loadQsvLibrary()
{
    HMODULE lib = LoadLibraryA("libvpl.dll");
    if (lib) return lib;

    lib = LoadLibraryA("libmfx-gen.dll");
    if (lib) return lib;

    lib = LoadLibraryA("libmfxhw64.dll");
    if (lib) return lib;

    lib = LoadLibraryA("mfxhw64.dll");
    if (lib) return lib;

    return nullptr;
}

} // anonymous namespace

QsvInfo getInfo()
{
    QsvInfo info;

    if (!findIntelGpu(info.adapterName))
        return info;

    HMODULE lib = loadQsvLibrary();
    if (!lib)
        return info;

    bool hasApi = GetProcAddress(lib, "MFXInit") ||
                  GetProcAddress(lib, "MFXInitEx") ||
                  GetProcAddress(lib, "MFXLoad");

    if (hasApi)
    {
        info.available = true;
        info.supportsH264 = true;
        info.supportsHEVC = true;
    }

    FreeLibrary(lib);
    return info;
}

bool isAvailable()
{
    return getInfo().available;
}

} // namespace QsvProbe

#endif // _WIN32
