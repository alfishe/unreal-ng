#pragma once

#include "common/ilogsink.h"

namespace emu { struct SPSCRingBuffer; }

namespace monitoring {

/// IPC log sink — writes ModuleLogger messages to a SPSC ring buffer
/// in shared memory. Installed/removed by MonitoringManager during its lifecycle.
///
/// Encodes module+submodule as a combined category_id:
///   category_id = (module << 16) | submodule
/// This allows the companion app to decode module identity from the ring buffer.
class IPCLogSink : public ILogSink
{
public:
    explicit IPCLogSink(emu::SPSCRingBuffer* ringBuffer);

    void write(uint8_t module, uint16_t submodule,
               uint8_t level, const char* text, uint32_t len) override;

private:
    emu::SPSCRingBuffer* _ringBuffer;
};

}  // namespace monitoring
