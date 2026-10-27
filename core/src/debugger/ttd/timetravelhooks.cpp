#include "timetravelhooks.h"

namespace ttd
{
const char* TTDSessionStateToString(TTDSessionState state)
{
    switch (state)
    {
        case TTDSessionState::Idle:      return "idle";
        case TTDSessionState::Recording: return "recording";
        case TTDSessionState::Detached:  return "detached";
    }
    return "unknown";
}
}  // namespace ttd
