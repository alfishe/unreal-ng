#include "ipclogsink.h"
#include "common/spsc_ringbuffer.h"

namespace monitoring {

IPCLogSink::IPCLogSink(emu::SPSCRingBuffer* ringBuffer)
    : _ringBuffer(ringBuffer)
{
}

void IPCLogSink::write(uint8_t module, uint16_t submodule,
                       uint8_t level, const char* text, uint32_t len)
{
    if (!_ringBuffer)
        return;

    // Encode module+submodule as category_id for companion to decode
    // High 16 bits = module (PlatformModulesEnum, 0-12)
    // Low 16 bits = submodule bitmask
    uint32_t categoryId = (static_cast<uint32_t>(module) << 16) | submodule;

    // Map LoggerLevel (1=Trace..5=Error) to SPSC level (0=Trace..4=Error)
    uint8_t spscLevel = (level > 0) ? level - 1 : 0;

    _ringBuffer->try_write(categoryId, spscLevel, text, len);
}

}  // namespace monitoring
