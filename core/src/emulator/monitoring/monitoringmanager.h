#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "manifest.h"
#include "ipclogsink.h"
#include "common/shared_memory_region.h"
#include "common/framenotifier.h"

namespace emu { struct SPSCRingBuffer; }

class EmulatorContext;
class ModuleLogger;

/// Manages the real-time monitoring shared memory subsystem.
///
/// Follows the AnalyzerManager pattern: owned by EmulatorContext, constructed
/// with EmulatorContext*, wired into the lifecycle via MainLoop::OnFrameEnd().
///
/// Feature-gated by "monitoring" in FeatureManager. When disabled, the only
/// cost is a single bool check per frame.
class MonitoringManager
{
public:
    explicit MonitoringManager(EmulatorContext* context);
    ~MonitoringManager();

    // Non-copyable
    MonitoringManager(const MonitoringManager&) = delete;
    MonitoringManager& operator=(const MonitoringManager&) = delete;

    /// Initialize the monitoring subsystem: allocate shared memory, write manifest,
    /// register initial sections (Z80 state, control ring).
    /// @return true on success
    bool initialize();

    /// Shut down: unmap shared memory, clean up.
    void shutdown();

    /// Called from MainLoop::OnFrameEnd(). Snapshots state sections, bumps epochs,
    /// signals observers, polls control commands.
    void onFrameEnd();

    /// @return true if monitoring is initialized and active
    bool isEnabled() const { return _initialized; }

    /// @return The shared memory name (for WebAPI to report to clients)
    const std::string& shmName() const { return _shmRegion.name(); }

    /// @return The frame notifier name (for observer tools)
    const std::string& notifierName() const { return _frameNotifier.name(); }

    /// @return Total SHM size in bytes
    size_t shmSize() const { return _shmRegion.size(); }

private:
    EmulatorContext* _context;
    ModuleLogger* _logger = nullptr;
    bool _initialized = false;

    // Shared memory
    SharedMemoryRegion _shmRegion;
    monitoring::ManifestHeader* _manifest = nullptr;

    // Frame notification
    FrameNotifier _frameNotifier;

    // Section management
    struct SectionRuntime
    {
        monitoring::SectionDescriptor* descriptor = nullptr;
        uint16_t index = 0;
    };

    std::vector<SectionRuntime> _sections;
    SectionRuntime _z80StateSection;
    SectionRuntime _controlSection;
    SectionRuntime _logStreamSection;
    SectionRuntime _ayStateSections[2];  // [0] = primary AY, [1] = TurboSound secondary
    SectionRuntime _fdcStateSection;
    SectionRuntime _inputStateSection;
    SectionRuntime _framebufferSection;
    SectionRuntime _audioBufferSection;
    SectionRuntime _heatmapZ80Section;
    SectionRuntime _heatmapPagesSection;
    SectionRuntime _heatmapPhysSection;
    SectionRuntime _opcodeTraceSection;
    SectionRuntime _callTraceSection;
    SectionRuntime _loggerStateSection;
    SectionRuntime _memoryPagesSection;

    // Log stream (SPSC ring buffer within the SHM region)
    emu::SPSCRingBuffer* _logRingBuffer = nullptr;
    std::unique_ptr<monitoring::IPCLogSink> _ipcLogSink;
    static constexpr uint64_t LOG_STREAM_DEFAULT_SIZE = 64ULL * 1024 * 1024;  // 64MB

    // Framebuffer: worst-case allocation (573KB × 2 double-buffer)
    static constexpr uint32_t FRAMEBUFFER_MAX_SIZE = 573 * 1024 * 2 + sizeof(monitoring::FramebufferSectionHeader);

    // Helper: generate unique SHM name from emulator UUID
    std::string generateShmName() const;

    // Layout helpers
    uint64_t calculateInitialShmSize() const;
    void writeManifestHeader(size_t totalSize);
    SectionRuntime registerSection(monitoring::SectionType type, const char* name,
                                    uint64_t dataSize, uint16_t flags = 0);

    // Frame-end operations
    void snapshotZ80State();
    void snapshotAYState();
    void snapshotFDCState();
    void snapshotInputState();
    void snapshotFramebuffer();
    void snapshotAudioBuffer();
    void snapshotHeatmapZ80();
    void snapshotHeatmapPages();
    void snapshotHeatmapPhys();
    void snapshotOpcodeTrace();
    void snapshotCallTrace();
    void snapshotLoggerState();
    void snapshotMemoryPages();
    void processControlCommands();
    void signalObservers();

    // Stale command protection: max frames to tolerate unprocessed commands
    static constexpr uint32_t STALE_COMMAND_FRAMES = 100;  // ~2 seconds at 50Hz
    uint32_t _staleCmdCounter = 0;
};
