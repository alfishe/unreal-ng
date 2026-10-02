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
