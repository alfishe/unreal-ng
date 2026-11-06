#include "emulator/readoutschanged.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

#include <atomic>

namespace
{
std::atomic<bool> frameReaders{false};
}

void SetFrameReadersWanted(bool wanted)
{
    frameReaders.store(wanted, std::memory_order_relaxed);
}

bool FrameReadersWanted()
{
    return frameReaders.load(std::memory_order_relaxed);
}

void NotifyFramePublished(const EmulatorContext* context)
{
    if (FrameReadersWanted() && context != nullptr && context->pEmulator != nullptr)
        NotifyReadoutsChanged(context->pEmulator->GetId(), kFrameArea);
}

void NotifyReadoutsChanged(const std::string& emulatorId, const std::string& area)
{
    MessageCenter::DefaultMessageCenter().Post(NC_READOUTS_CHANGED, new ReadoutsChangedPayload(emulatorId, area));
}

void NotifyReadoutsChanged(const EmulatorContext* context, const std::string& area)
{
    NotifyReadoutsChanged(context != nullptr && context->pEmulator != nullptr ? context->pEmulator->GetId() : std::string(),
                          area);
}
