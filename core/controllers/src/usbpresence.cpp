#include "usbpresence.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDKeys.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>

#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>
#elif defined(__linux__)
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#endif

namespace controllers
{

int CountUsbDevices(uint16_t vendorId)
{
#if defined(__APPLE__)
    // The HID interfaces of the vendor in the I/O registry (a Stream Deck has one): matched, not opened
    CFMutableDictionaryRef matching = IOServiceMatching(kIOHIDDeviceKey);
    if (!matching)
        return -1;
    const int vendor = vendorId;
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFDictionarySetValue(matching, CFSTR(kIOHIDVendorIDKey), number);
    CFRelease(number);
    io_iterator_t it = IO_OBJECT_NULL;
    // port 0 = the default main port (no deprecated kIOMasterPortDefault); the dictionary is consumed
    if (IOServiceGetMatchingServices(0, matching, &it) != KERN_SUCCESS)
        return -1;
    int count = 0;
    for (io_object_t service = IOIteratorNext(it); service != IO_OBJECT_NULL; service = IOIteratorNext(it))
    {
        ++count;
        IOObjectRelease(service);
    }
    IOObjectRelease(it);
    return count;
#elif defined(_WIN32)
    // Present USB devices whose instance id names the vendor: USB\VID_0FD9&PID_...
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE)
        return -1;
    wchar_t needle[16];
    std::swprintf(needle, sizeof(needle) / sizeof(needle[0]), L"VID_%04X&", vendorId);
    int count = 0;
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    std::vector<wchar_t> id(512);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i)
    {
        if (!SetupDiGetDeviceInstanceIdW(set, &info, id.data(), static_cast<DWORD>(id.size()), nullptr))
            continue;
        std::wstring text(id.data());
        for (wchar_t& c : text)
            c = static_cast<wchar_t>(std::towupper(c));
        // the composite device only, not each of its interfaces (USB\VID_..&PID_..&MI_00)
        if (text.find(needle) != std::wstring::npos && text.find(L"&MI_") == std::wstring::npos)
            ++count;
    }
    SetupDiDestroyDeviceInfoList(set);
    return count;
#elif defined(__linux__)
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it("/sys/bus/usb/devices", ec);
    if (ec)
        return -1;
    char want[8];
    std::snprintf(want, sizeof(want), "%04x", vendorId);
    int count = 0;
    for (const fs::directory_entry& entry : it)
    {
        std::ifstream in(entry.path() / "idVendor");
        std::string vendor;
        if (in && std::getline(in, vendor) && vendor == want)
            ++count;
    }
    return count;
#else
    (void)vendorId;
    return -1;
#endif
}

} // namespace controllers
