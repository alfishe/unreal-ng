#pragma once

/// @file winthread.h
/// @brief The name of a backend thread in a debugger and in crash dumps. SetThreadDescription (Windows 10 1607) is
/// looked up at run time, so the library still loads on an older Windows. Include after <windows.h>.

namespace controllers
{

inline void NameThisThread(const wchar_t* name)
{
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto setDescription = reinterpret_cast<SetThreadDescriptionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "SetThreadDescription")));
    if (setDescription != nullptr)
        setDescription(GetCurrentThread(), name);
}

}  // namespace controllers
