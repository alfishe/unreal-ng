#include "eve-profile.h"

#ifdef EVE_PROFILE
namespace EveLib
{
ProfileCounters& Profile()
{
    static ProfileCounters counters;
    return counters;
}

void ProfileReset()
{
    Profile() = ProfileCounters{};
}
} // namespace EveLib
#endif
