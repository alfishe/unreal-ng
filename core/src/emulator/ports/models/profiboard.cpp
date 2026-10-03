#include "stdafx.h"

#include "profiboard.h"

#include "common/stringhelper.h"

namespace
{
    struct SyncPromName
    {
        const char* name;
        ProfiSyncProm prom;
    };

    constexpr SyncPromName kSyncPromNames[] = {
        {"default", ProfiSyncProm::Default},
        {"0a1d", ProfiSyncProm::Vr0a1d},
        {"samx6", ProfiSyncProm::Samx6},
        {"fb0579b6", ProfiSyncProm::Fb0579b6},
        {"v503", ProfiSyncProm::V503},
    };
}

bool ParseProfiSyncProm(const char* text, ProfiSyncProm& out)
{
    if (text == nullptr || text[0] == '\0')
    {
        out = ProfiSyncProm::Default;
        return true;
    }
    for (const SyncPromName& entry : kSyncPromNames)
    {
        if (StringHelper::CompareCaseInsensitive(text, entry.name, strlen(entry.name)) == 0)
        {
            out = entry.prom;
            return true;
        }
    }
    return false;
}

const char* ProfiSyncPromName(ProfiSyncProm prom)
{
    for (const SyncPromName& entry : kSyncPromNames)
    {
        if (entry.prom == prom)
            return entry.name;
    }
    return "default";
}

namespace
{
    struct KeyboardName
    {
        const char* name;
        ProfiKeyboard keyboard;
    };

    constexpr KeyboardName kKeyboardNames[] = {
        {"default", ProfiKeyboard::Default},
        {"matrix", ProfiKeyboard::Matrix},
        {"xt", ProfiKeyboard::Xt},
        {"xttable", ProfiKeyboard::XtTable},
    };

    bool SameTextIgnoringCase(const char* a, const char* b)
    {
        for (; *a && *b; ++a, ++b)
        {
            const char x = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
            const char y = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
            if (x != y)
                return false;
        }
        return *a == '\0' && *b == '\0';
    }
}

bool ParseProfiKeyboard(const char* text, ProfiKeyboard& out)
{
    if (text == nullptr || text[0] == '\0')
    {
        out = ProfiKeyboard::Default;
        return true;
    }
    for (const KeyboardName& entry : kKeyboardNames)
    {
        if (SameTextIgnoringCase(text, entry.name))
        {
            out = entry.keyboard;
            return true;
        }
    }
    return false;
}

const char* ProfiKeyboardName(ProfiKeyboard keyboard)
{
    for (const KeyboardName& entry : kKeyboardNames)
    {
        if (entry.keyboard == keyboard)
            return entry.name;
    }
    return "default";
}

std::function<void(CONFIG&)> ProfiKeyboardOverride(ProfiKeyboard keyboard)
{
    if (keyboard == ProfiKeyboard::Default)
        return {};
    return [keyboard](CONFIG& config) { config.profi_keyboard = static_cast<uint8_t>(keyboard); };
}
