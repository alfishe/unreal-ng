#include "monitoringmanager.h"

#include <cstring>

#include "common/modulelogger.h"
#include "common/spsc_ringbuffer.h"
#include "common/emulog_registry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/video/screen.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/memory/memory.h"
#include "emulator/cpu/opcode_profiler.h"

// Verify LOGGER_MODULE_COUNT (manifest.h, used by companion) stays in sync with MODULE_COUNT (platform.h)
static_assert(monitoring::LOGGER_MODULE_COUNT == MODULE_COUNT,
              "LOGGER_MODULE_COUNT in manifest.h must equal MODULE_COUNT in platform.h");

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace monitoring;

/// region <ModuleLogger definitions>
static const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_CORE;
static constexpr uint16_t _SUBMODULE = PlatformCoreSubmodulesEnum::SUBMODULE_CORE_GENERIC;
/// endregion </ModuleLogger definitions>

// =============================================================================
// Construction / Destruction
// =============================================================================

MonitoringManager::MonitoringManager(EmulatorContext* context)
    : _context(context)
    , _logger(context ? context->pModuleLogger : nullptr)
{
}

MonitoringManager::~MonitoringManager()
{
    shutdown();
}

// =============================================================================
// Initialization
// =============================================================================

bool MonitoringManager::initialize()
{
    if (_initialized)
        return true;

    if (!_context || !_context->pCore)
    {
        MLOGWARNING("MonitoringManager::initialize() - context or core not available");
        return false;
    }

    // Generate unique names
    std::string shmName = generateShmName();
    std::string notifierName = shmName;  // Use same base for notifier

    // Calculate total SHM size for initial sections
    uint64_t totalSize = calculateInitialShmSize();

    // Create shared memory region
    if (!_shmRegion.create(shmName, static_cast<size_t>(totalSize)))
    {
        MLOGERROR("MonitoringManager: Failed to create shared memory region '%s' (%llu bytes)",
                  shmName.c_str(), static_cast<unsigned long long>(totalSize));
        return false;
    }

    // Write manifest header
    writeManifestHeader(totalSize);

    // Register initial sections
    _z80StateSection = registerSection(SectionType::CHIP_STATE_Z80, "z80_regs",
                                        sizeof(Z80StateSnapshot), SECTION_ENABLED);

    _controlSection = registerSection(SectionType::CONTROL_RING, "control",
                                       sizeof(ControlBlock),
                                       SECTION_ENABLED | SECTION_BIDIRECTIONAL);

    // Initialize control block
    auto* controlData = static_cast<ControlBlock*>(
        getSectionData(_shmRegion.data(), _controlSection.descriptor));
    if (controlData)
    {
        controlData->write_idx.store(0, std::memory_order_relaxed);
        controlData->read_idx.store(0, std::memory_order_relaxed);
        controlData->response_epoch.store(0, std::memory_order_relaxed);
        for (auto& cmd : controlData->commands)
        {
            cmd.type.store(static_cast<uint8_t>(ControlCommandType::NONE), std::memory_order_relaxed);
            cmd.processed.store(true, std::memory_order_relaxed);
        }
    }

    // Register LOG_STREAM section (SPSC ring buffer)
    uint64_t logStreamTotalSize = emu::SPSCRingBuffer::totalMemoryRequired(LOG_STREAM_DEFAULT_SIZE);
    _logStreamSection = registerSection(SectionType::LOG_STREAM, "log_stream",
                                        logStreamTotalSize,
                                        SECTION_ENABLED | SECTION_RING_BUFFER);

    // Initialize SPSC ring buffer within the section's data area
    void* logStreamData = getSectionData(_shmRegion.data(), _logStreamSection.descriptor);
    if (logStreamData)
    {
        _logRingBuffer = emu::SPSCRingBuffer::create(logStreamData, LOG_STREAM_DEFAULT_SIZE);

        // Wire ring buffer into the global LoggerRegistry
        emu::log::LoggerRegistry::instance().setRingBuffer(_logRingBuffer);

        // Install IPC log sink into ModuleLogger for real-time output
        _ipcLogSink = std::make_unique<monitoring::IPCLogSink>(_logRingBuffer);
        if (_context && _context->pModuleLogger)
        {
            _context->pModuleLogger->AddSink(_ipcLogSink.get());
        }
    }

    // Register AY-8910 state sections (2 slots for TurboSound dual-AY)
    _ayStateSections[0] = registerSection(SectionType::CHIP_STATE_AY, "ay0_regs",
                                           sizeof(AYStateSnapshot), SECTION_ENABLED);
    _ayStateSections[1] = registerSection(SectionType::CHIP_STATE_AY, "ay1_regs",
                                           sizeof(AYStateSnapshot), SECTION_ENABLED);

    // Register WD1793 FDC state section
    _fdcStateSection = registerSection(SectionType::CHIP_STATE_FDC, "fdc_state",
                                        sizeof(FDCStateSnapshot), SECTION_ENABLED);

    // Register INPUT_STATE section (bidirectional)
    _inputStateSection = registerSection(SectionType::INPUT_STATE, "input",
                                          sizeof(InputStateSnapshot),
                                          SECTION_ENABLED | SECTION_BIDIRECTIONAL);

    // Register FRAMEBUFFER section (allocate for worst-case mode)
    _framebufferSection = registerSection(SectionType::FRAMEBUFFER, "framebuffer",
                                           FRAMEBUFFER_MAX_SIZE, SECTION_ENABLED);

    // Initialize framebuffer header
    auto* fbHeader = static_cast<FramebufferSectionHeader*>(
        getSectionData(_shmRegion.data(), _framebufferSection.descriptor));
    if (fbHeader)
    {
        std::memset(fbHeader, 0, sizeof(FramebufferSectionHeader));
        fbHeader->pixel_format = 0;  // RGBA8888
        fbHeader->buffer_offset = sizeof(FramebufferSectionHeader);
    }

    // Register AUDIO_BUFFER section
    uint32_t audioSectionSize = sizeof(AudioBufferSectionHeader) + 3528;  // header + PCM data
    _audioBufferSection = registerSection(SectionType::AUDIO_BUFFER, "audio",
                                           audioSectionSize, SECTION_ENABLED);

    // Initialize audio header
    auto* audioHeader = static_cast<AudioBufferSectionHeader*>(
        getSectionData(_shmRegion.data(), _audioBufferSection.descriptor));
    if (audioHeader)
    {
        audioHeader->sampling_rate = 44100;
        audioHeader->channels = 2;
        audioHeader->bits_per_sample = 16;
        audioHeader->samples_per_frame = 882;
        audioHeader->buffer_size = 3528;
    }

    // Register HEATMAP_Z80 section (768KB of R/W/X counters for Z80 address space)
    _heatmapZ80Section = registerSection(SectionType::HEATMAP_Z80, "heatmap_z80",
                                          HEATMAP_Z80_DATA_SIZE, SECTION_ENABLED);

    // Initialize heatmap Z80 header
    auto* heatZ80Header = static_cast<HeatmapZ80Header*>(
        getSectionData(_shmRegion.data(), _heatmapZ80Section.descriptor));
    if (heatZ80Header)
    {
        heatZ80Header->address_count = 65536;
        heatZ80Header->session_active = 0;
        std::memset(heatZ80Header->reserved, 0, sizeof(heatZ80Header->reserved));
    }

    // Register HEATMAP_PAGES section (~4KB of per-page aggregated counters)
    _heatmapPagesSection = registerSection(SectionType::HEATMAP_PAGES, "heatmap_pages",
                                            HEATMAP_PAGES_DATA_SIZE, SECTION_ENABLED);

    // Initialize heatmap pages header
    auto* heatPagesHeader = static_cast<HeatmapPagesHeader*>(
        getSectionData(_shmRegion.data(), _heatmapPagesSection.descriptor));
    if (heatPagesHeader)
    {
        heatPagesHeader->page_count = HEATMAP_PAGE_COUNT;
        heatPagesHeader->session_active = 0;
        std::memset(heatPagesHeader->reserved, 0, sizeof(heatPagesHeader->reserved));
    }

    // Register HEATMAP_PHYS section (~6MB of per-address counters for active pages)
    _heatmapPhysSection = registerSection(SectionType::HEATMAP_PHYS, "heatmap_phys",
                                           HEATMAP_PHYS_DATA_SIZE, SECTION_ENABLED);

    // Initialize heatmap phys header
    auto* heatPhysHeader = static_cast<HeatmapPhysHeader*>(
        getSectionData(_shmRegion.data(), _heatmapPhysSection.descriptor));
    if (heatPhysHeader)
    {
        heatPhysHeader->active_page_count = 0;
        heatPhysHeader->max_pages = HEATMAP_PHYS_MAX_PAGES;
        heatPhysHeader->session_active = 0;
        std::memset(heatPhysHeader->reserved, 0, sizeof(heatPhysHeader->reserved));
    }

    // Register OPCODE_TRACE section (header + 1024 trace entries)
    _opcodeTraceSection = registerSection(SectionType::OPCODE_TRACE, "opcode_trace",
                                           OPCODE_TRACE_DATA_SIZE, SECTION_ENABLED);

    // Initialize opcode trace header
    auto* opcTraceHeader = static_cast<OpcodeTraceHeader*>(
        getSectionData(_shmRegion.data(), _opcodeTraceSection.descriptor));
    if (opcTraceHeader)
    {
        std::memset(opcTraceHeader, 0, sizeof(OpcodeTraceHeader));
        opcTraceHeader->max_entries = OPCODE_TRACE_SNAPSHOT_COUNT;
    }

    // Register CALL_TRACE section (header + 512 call trace entries)
    _callTraceSection = registerSection(SectionType::CALL_TRACE, "call_trace",
                                         CALL_TRACE_DATA_SIZE, SECTION_ENABLED);

    // Initialize call trace header
    auto* callTraceHeader = static_cast<CallTraceHeader*>(
        getSectionData(_shmRegion.data(), _callTraceSection.descriptor));
    if (callTraceHeader)
    {
        std::memset(callTraceHeader, 0, sizeof(CallTraceHeader));
        callTraceHeader->max_entries = CALL_TRACE_SNAPSHOT_COUNT;
    }

    // Register LOGGER_STATE section (compact logger configuration snapshot)
    _loggerStateSection = registerSection(SectionType::LOGGER_STATE, "logger_state",
                                           sizeof(LoggerStateSnapshot), SECTION_ENABLED);

    // Register MEMORY_PAGES section (full RAM/ROM page snapshot)
    _memoryPagesSection = registerSection(SectionType::MEMORY_PAGES, "memory_pages",
                                           MEMORY_PAGES_DATA_SIZE, SECTION_ENABLED);

    // Create frame notifier
    _frameNotifier.create(notifierName);

    // Set running state
    _manifest->flags = MANIFEST_RUNNING;
    _manifest->global_epoch.store(0, std::memory_order_release);
    _manifest->frame_counter.store(0, std::memory_order_release);
    _manifest->layout_epoch.store(0, std::memory_order_release);

    _initialized = true;

    MLOGINFO("MonitoringManager initialized: SHM='%s' (%llu bytes), %u sections",
             shmName.c_str(), static_cast<unsigned long long>(totalSize), _manifest->section_count);

    return true;
}

void MonitoringManager::shutdown()
{
    if (!_initialized)
        return;

    // Mark as stopped
    if (_manifest)
    {
        _manifest->flags = MANIFEST_STOPPED;
        _manifest->global_epoch.fetch_add(1, std::memory_order_release);
    }

    // Disconnect IPC log sink from ModuleLogger
    if (_ipcLogSink && _context && _context->pModuleLogger)
    {
        _context->pModuleLogger->RemoveSink(_ipcLogSink.get());
    }
    _ipcLogSink.reset();

    // Disconnect log ring buffer from the registry
    emu::log::LoggerRegistry::instance().setRingBuffer(nullptr);
    emu::log::LoggerRegistry::instance().disableAll();
    _logRingBuffer = nullptr;

    _frameNotifier.destroy();
    _sections.clear();
    _z80StateSection = {};
    _controlSection = {};
    _logStreamSection = {};
    _ayStateSections[0] = {};
    _ayStateSections[1] = {};
    _fdcStateSection = {};
    _inputStateSection = {};
    _framebufferSection = {};
    _audioBufferSection = {};
    _heatmapZ80Section = {};
    _heatmapPagesSection = {};
    _heatmapPhysSection = {};
    _opcodeTraceSection = {};
    _callTraceSection = {};
    _loggerStateSection = {};
    _manifest = nullptr;
    _shmRegion.close();
    _initialized = false;

    MLOGINFO("MonitoringManager shut down");
}

// =============================================================================
// Frame End — Hot Path
// =============================================================================

void MonitoringManager::onFrameEnd()
{
    if (!_initialized || !_manifest)
        return;

    // 1. Snapshot all state sections (epoch-protected)
    snapshotZ80State();
    snapshotAYState();
    snapshotFDCState();
    snapshotInputState();
    snapshotFramebuffer();
    snapshotAudioBuffer();
    snapshotHeatmapZ80();
    snapshotHeatmapPages();
    snapshotHeatmapPhys();
    snapshotOpcodeTrace();
    snapshotCallTrace();
    snapshotLoggerState();
    snapshotMemoryPages();

    // 2. Bump global epoch and frame counter
    _manifest->global_epoch.fetch_add(1, std::memory_order_release);
    _manifest->frame_counter.fetch_add(1, std::memory_order_release);

    // 3. Signal any waiting observers
    signalObservers();

    // 4. Poll control commands (cold path, ~20ns)
    processControlCommands();
}

// =============================================================================
// Z80 State Snapshot
// =============================================================================

void MonitoringManager::snapshotZ80State()
{
    if (!_z80StateSection.descriptor || !_context->pCore)
        return;

    auto* desc = _z80StateSection.descriptor;
    auto* snapshot = static_cast<Z80StateSnapshot*>(
        getSectionData(_shmRegion.data(), desc));
    if (!snapshot)
        return;

    Z80* z80 = _context->pCore->GetZ80();
    if (!z80)
        return;

    // Signal UPDATING to observers
    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Copy register state into flat snapshot
    snapshot->af = z80->af;
    snapshot->bc = z80->bc;
    snapshot->de = z80->de;
    snapshot->hl = z80->hl;

    snapshot->af_alt = z80->alt.af;
    snapshot->bc_alt = z80->alt.bc;
    snapshot->de_alt = z80->alt.de;
    snapshot->hl_alt = z80->alt.hl;

    snapshot->ix = z80->ix;
    snapshot->iy = z80->iy;
    snapshot->sp = z80->sp;
    snapshot->pc = z80->pc;

    snapshot->ir = z80->ir_;
    snapshot->memptr = z80->memptr;

    snapshot->iff1 = z80->iff1;
    snapshot->iff2 = z80->iff2;
    snapshot->im = z80->im;
    snapshot->halted = z80->halted;

    snapshot->t = z80->t;
    snapshot->total_tstates = _context->emulatorState.t_states;

    snapshot->prev_pc = z80->prev_pc;
    snapshot->m1_pc = z80->m1_pc;

    // Decode flags for observer convenience
    uint8_t f = z80->f;
    snapshot->flag_s  = (f >> 7) & 1;
    snapshot->flag_z  = (f >> 6) & 1;
    snapshot->flag_h  = (f >> 4) & 1;
    snapshot->flag_pv = (f >> 2) & 1;
    snapshot->flag_n  = (f >> 1) & 1;
    snapshot->flag_c  = (f >> 0) & 1;

    // Commit with new epoch
    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// AY-8910 State Snapshot
// =============================================================================

void MonitoringManager::snapshotAYState()
{
    if (!_context->pSoundManager)
        return;

    int chipCount = _context->pSoundManager->getAYChipCount();

    for (int chipIdx = 0; chipIdx < 2; chipIdx++)
    {
        auto* desc = _ayStateSections[chipIdx].descriptor;
        if (!desc)
            continue;

        auto* snapshot = static_cast<AYStateSnapshot*>(
            getSectionData(_shmRegion.data(), desc));
        if (!snapshot)
            continue;

        SoundChip_AY8910* ay = (chipIdx < chipCount)
            ? _context->pSoundManager->getAYChip(chipIdx)
            : nullptr;

        desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

        if (ay)
        {
            const uint8_t* regs = ay->getRegisters();
            std::memcpy(snapshot->registers, regs, 16);
            snapshot->current_register = ay->getCurrentRegister();
            snapshot->chip_index = static_cast<uint8_t>(chipIdx);
            snapshot->chip1_selected = _context->pSoundManager->wasTurboSoundChip1Selected() ? 1 : 0;

            // Decode tone periods
            snapshot->tone_period_a = regs[0] | ((regs[1] & 0x0F) << 8);
            snapshot->tone_period_b = regs[2] | ((regs[3] & 0x0F) << 8);
            snapshot->tone_period_c = regs[4] | ((regs[5] & 0x0F) << 8);
            snapshot->noise_period  = regs[6] & 0x1F;
            snapshot->envelope_period = regs[11] | (regs[12] << 8);

            // Decode volumes
            snapshot->volume_a = regs[8] & 0x0F;
            snapshot->volume_b = regs[9] & 0x0F;
            snapshot->volume_c = regs[10] & 0x0F;
            snapshot->envelope_shape = regs[13] & 0x0F;

            // Decode mixer (R7: bits inverted — 0 = enabled)
            uint8_t mixer = regs[7];
            snapshot->tone_enable  = ~mixer & 0x07;
            snapshot->noise_enable = (~mixer >> 3) & 0x07;

            // Copy per-register activity counters for companion indicators
            std::memcpy(snapshot->reg_write_counts, ay->getRegWriteCounts(), 16);
            snapshot->reg_read_mask = ay->getRegReadMask();
        }
        else
        {
            // No chip at this index — zero out the snapshot
            std::memset(snapshot, 0, sizeof(AYStateSnapshot));
            snapshot->chip_index = static_cast<uint8_t>(chipIdx);
            snapshot->chip1_selected = 0;
        }

        uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
        desc->epoch.store(epoch, std::memory_order_release);
    }
}

// =============================================================================
// WD1793 FDC State Snapshot
// =============================================================================

void MonitoringManager::snapshotFDCState()
{
    if (!_fdcStateSection.descriptor || !_context->pBetaDisk)
        return;

    auto* desc = _fdcStateSection.descriptor;
    auto* snapshot = static_cast<FDCStateSnapshot*>(
        getSectionData(_shmRegion.data(), desc));
    if (!snapshot)
        return;

    const WD1793* fdc = _context->pBetaDisk;

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    const WD93State& wdState = fdc->getState();
    uint8_t status = fdc->getStatusRegister();

    snapshot->command_register = wdState.registers.commandRegister;
    snapshot->status_register  = status;
    snapshot->track_register   = fdc->getTrackRegister();
    snapshot->sector_register  = fdc->getSectorRegister();
    snapshot->data_register    = fdc->getDataRegister();
    snapshot->selected_drive   = static_cast<uint8_t>(wdState.registers.commandRegister & 0x03);
    snapshot->side             = 0;
    snapshot->fsm_state        = 0;
    snapshot->beta128_status   = fdc->getBeta128Status();
    snapshot->last_command     = static_cast<uint8_t>(fdc->getLastDecodedCommand());
    snapshot->intrq            = 0;
    snapshot->drq              = 0;

    // Error flags from status register
    snapshot->error_lost_data   = (status & 0x04) ? 1 : 0;
    snapshot->error_crc         = (status & 0x08) ? 1 : 0;
    snapshot->error_not_found   = (status & 0x10) ? 1 : 0;
    snapshot->error_write_fault = (status & 0x20) ? 1 : 0;

    // Disk inserted bitmask
    snapshot->disk_inserted = 0;
    for (int i = 0; i < 4; i++)
    {
        if (_context->coreState.diskDrives[i] &&
            _context->coreState.diskDrives[i]->isDiskInserted())
        {
            snapshot->disk_inserted |= (1 << i);
        }
    }

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Input State Snapshot
// =============================================================================

void MonitoringManager::snapshotInputState()
{
    if (!_inputStateSection.descriptor || !_context->pKeyboard)
        return;

    auto* desc = _inputStateSection.descriptor;
    auto* snapshot = static_cast<InputStateSnapshot*>(
        getSectionData(_shmRegion.data(), desc));
    if (!snapshot)
        return;

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Copy keyboard matrix state
    std::memcpy(snapshot->keyboard_matrix, _context->pKeyboard->getKeyboardMatrix(), 8);

    // Kempston joystick: placeholder (no dedicated accessor yet)
    snapshot->kempston_joystick = 0;

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Framebuffer Snapshot
// =============================================================================

void MonitoringManager::snapshotFramebuffer()
{
    if (!_framebufferSection.descriptor || !_context->pScreen)
        return;

    auto* desc = _framebufferSection.descriptor;
    auto* fbHeader = static_cast<FramebufferSectionHeader*>(
        getSectionData(_shmRegion.data(), desc));
    if (!fbHeader)
        return;

    FramebufferDescriptor& fbDesc = _context->pScreen->GetFramebufferDescriptor();
    if (!fbDesc.memoryBuffer || fbDesc.memoryBufferSize == 0)
        return;

    // Check if framebuffer fits in allocated space
    uint32_t pixelDataSize = static_cast<uint32_t>(fbDesc.memoryBufferSize);
    uint32_t totalNeeded = sizeof(FramebufferSectionHeader) + pixelDataSize;
    if (totalNeeded > desc->length)
        return;  // Mode changed to something larger than allocation — skip

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    fbHeader->width = fbDesc.width;
    fbHeader->height = fbDesc.height;
    fbHeader->video_mode = static_cast<uint8_t>(fbDesc.videoMode);
    fbHeader->pixel_format = 0;  // RGBA8888
    fbHeader->buffer_size = pixelDataSize;
    fbHeader->buffer_offset = sizeof(FramebufferSectionHeader);

    // Copy pixel data after header
    char* pixelDst = reinterpret_cast<char*>(fbHeader) + sizeof(FramebufferSectionHeader);
    std::memcpy(pixelDst, fbDesc.memoryBuffer, pixelDataSize);

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Audio Buffer Snapshot
// =============================================================================

void MonitoringManager::snapshotAudioBuffer()
{
    if (!_audioBufferSection.descriptor || !_context->pSoundManager)
        return;

    auto* desc = _audioBufferSection.descriptor;
    auto* audioHeader = static_cast<AudioBufferSectionHeader*>(
        getSectionData(_shmRegion.data(), desc));
    if (!audioHeader)
        return;

    const AudioFrameDescriptor& audioDesc = _context->pSoundManager->getAudioBufferDescriptor();

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Copy PCM data after header
    char* pcmDst = reinterpret_cast<char*>(audioHeader) + sizeof(AudioBufferSectionHeader);
    std::memcpy(pcmDst, audioDesc.memoryBuffer, audioDesc.memoryBufferSizeInBytes);

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Z80 Address Space Heatmap Snapshot
// =============================================================================

void MonitoringManager::snapshotHeatmapZ80()
{
    if (!_heatmapZ80Section.descriptor || !_context->pMemory)
        return;

    MemoryAccessTracker& tracker = _context->pMemory->GetAccessTracker();

    auto* desc = _heatmapZ80Section.descriptor;
    auto* header = static_cast<HeatmapZ80Header*>(
        getSectionData(_shmRegion.data(), desc));
    if (!header)
        return;

    const uint32_t* readPtr = tracker.GetZ80ReadCountersPtr();
    const uint32_t* writePtr = tracker.GetZ80WriteCountersPtr();
    const uint32_t* execPtr = tracker.GetZ80ExecuteCountersPtr();

    bool sessionActive = tracker.IsMemoryCapturing();
    header->session_active = sessionActive ? 1 : 0;

    // Only copy counter data if there's active data (pointers are non-null when allocated)
    if (!readPtr && !writePtr && !execPtr)
        return;

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Counter arrays follow the header
    auto* data = reinterpret_cast<char*>(header) + sizeof(HeatmapZ80Header);
    constexpr size_t arraySize = 65536 * sizeof(uint32_t);  // 256KB per array

    if (readPtr)
        std::memcpy(data, readPtr, arraySize);
    else
        std::memset(data, 0, arraySize);

    if (writePtr)
        std::memcpy(data + arraySize, writePtr, arraySize);
    else
        std::memset(data + arraySize, 0, arraySize);

    if (execPtr)
        std::memcpy(data + 2 * arraySize, execPtr, arraySize);
    else
        std::memset(data + 2 * arraySize, 0, arraySize);

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Page-Level Heatmap Snapshot
// =============================================================================

void MonitoringManager::snapshotHeatmapPages()
{
    if (!_heatmapPagesSection.descriptor || !_context->pMemory)
        return;

    MemoryAccessTracker& tracker = _context->pMemory->GetAccessTracker();

    auto* desc = _heatmapPagesSection.descriptor;
    auto* header = static_cast<HeatmapPagesHeader*>(
        getSectionData(_shmRegion.data(), desc));
    if (!header)
        return;

    bool sessionActive = tracker.IsMemoryCapturing();
    header->session_active = sessionActive ? 1 : 0;

    if (!sessionActive)
        return;

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    auto* data = reinterpret_cast<char*>(header) + sizeof(HeatmapPagesHeader);
    constexpr size_t arraySize = HEATMAP_PAGE_COUNT * sizeof(uint32_t);

    auto* readCounters = reinterpret_cast<uint32_t*>(data);
    auto* writeCounters = reinterpret_cast<uint32_t*>(data + arraySize);
    auto* execCounters = reinterpret_cast<uint32_t*>(data + 2 * arraySize);

    for (uint16_t i = 0; i < HEATMAP_PAGE_COUNT; i++)
    {
        readCounters[i] = tracker.GetPageReadAccessCount(i);
        writeCounters[i] = tracker.GetPageWriteAccessCount(i);
        execCounters[i] = tracker.GetPageExecuteAccessCount(i);
    }

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Physical Page Heatmap Snapshot
// =============================================================================

void MonitoringManager::snapshotHeatmapPhys()
{
    if (!_heatmapPhysSection.descriptor || !_context->pMemory)
        return;

    MemoryAccessTracker& tracker = _context->pMemory->GetAccessTracker();

    auto* desc = _heatmapPhysSection.descriptor;
    auto* header = static_cast<HeatmapPhysHeader*>(
        getSectionData(_shmRegion.data(), desc));
    if (!header)
        return;

    bool sessionActive = tracker.IsMemoryCapturing();
    header->session_active = sessionActive ? 1 : 0;

    if (!sessionActive)
        return;

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Get physical counter pointers
    const uint32_t* physRead = tracker.GetPhysReadCountersPtr();
    const uint32_t* physWrite = tracker.GetPhysWriteCountersPtr();
    const uint32_t* physExec = tracker.GetPhysExecuteCountersPtr();

    if (!physRead || !physWrite || !physExec)
    {
        header->active_page_count = 0;
        uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
        desc->epoch.store(epoch, std::memory_order_release);
        return;
    }

    // Collect active pages (non-zero counters), up to max
    constexpr uint32_t PAGE_SIZE = 16384;
    uint16_t activeCount = 0;
    auto* entryBase = reinterpret_cast<uint8_t*>(header) + sizeof(HeatmapPhysHeader);

    for (uint16_t page = 0; page < HEATMAP_PAGE_COUNT && activeCount < HEATMAP_PHYS_MAX_PAGES; page++)
    {
        // Check page-level activity via aggregated counters
        if (tracker.GetPageTotalAccessCount(page) == 0)
            continue;

        // Get page info
        PageInfo info = tracker.GetPageInfo(page);

        // Write descriptor
        auto* entry = entryBase + activeCount * HEATMAP_PHYS_PAGE_ENTRY_SIZE;
        auto* pageDesc = reinterpret_cast<HeatmapPhysPageDescriptor*>(entry);
        pageDesc->abs_page_index = page;
        pageDesc->page_type = info.pageType;
        pageDesc->logical_page_num = info.logicalPageNum;
        pageDesc->is_mapped = info.isCurrentlyMapped ? 1 : 0;
        pageDesc->mapped_bank = info.mappedToBank;
        std::memset(pageDesc->reserved, 0, sizeof(pageDesc->reserved));

        // Write per-address counters for this page
        auto* counters = entry + sizeof(HeatmapPhysPageDescriptor);
        size_t offset = static_cast<size_t>(page) * PAGE_SIZE;

        std::memcpy(counters,
                    physRead + offset, PAGE_SIZE * sizeof(uint32_t));
        std::memcpy(counters + PAGE_SIZE * sizeof(uint32_t),
                    physWrite + offset, PAGE_SIZE * sizeof(uint32_t));
        std::memcpy(counters + 2 * PAGE_SIZE * sizeof(uint32_t),
                    physExec + offset, PAGE_SIZE * sizeof(uint32_t));

        activeCount++;
    }

    header->active_page_count = activeCount;
    header->max_pages = HEATMAP_PHYS_MAX_PAGES;

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Opcode Trace Snapshot
// =============================================================================

void MonitoringManager::snapshotOpcodeTrace()
{
    if (!_opcodeTraceSection.descriptor || !_context->pCore)
        return;

    Z80* z80 = _context->pCore->GetZ80();
    if (!z80)
        return;

    OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
    if (!profiler)
        return;

    auto* desc = _opcodeTraceSection.descriptor;
    auto* header = static_cast<OpcodeTraceHeader*>(
        getSectionData(_shmRegion.data(), desc));
    if (!header)
        return;

    ProfilerStatus status = profiler->GetStatus();
    header->session_active = status.capturing ? 1 : 0;
    header->total_executions = profiler->GetTotalExecutions();

    // Only snapshot trace entries if the profiler has data
    if (status.traceSize == 0)
    {
        header->entry_count = 0;
        return;
    }

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Get recent trace entries (newest first)
    uint32_t count = std::min(static_cast<uint32_t>(status.traceSize), OPCODE_TRACE_SNAPSHOT_COUNT);
    std::vector<OpcodeTraceEntry> trace = profiler->GetRecentTrace(count);

    header->entry_count = static_cast<uint32_t>(trace.size());

    // Copy trace entries after header (OpcodeTraceEntry is already 16 bytes POD,
    // matching OpcodeTraceEntryPOD layout exactly)
    auto* entries = reinterpret_cast<OpcodeTraceEntryPOD*>(
        reinterpret_cast<char*>(header) + sizeof(OpcodeTraceHeader));
    for (size_t i = 0; i < trace.size(); i++)
    {
        entries[i].pc = trace[i].pc;
        entries[i].prefix = trace[i].prefix;
        entries[i].opcode = trace[i].opcode;
        entries[i].flags = trace[i].flags;
        entries[i].a = trace[i].a;
        entries[i].reserved = 0;
        entries[i].frame = trace[i].frame;
        entries[i].tState = trace[i].tState;
    }

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Call Trace Snapshot
// =============================================================================

void MonitoringManager::snapshotCallTrace()
{
    if (!_callTraceSection.descriptor || !_context->pMemory)
        return;

    MemoryAccessTracker& tracker = _context->pMemory->GetAccessTracker();
    CallTraceBuffer* callTrace = tracker.GetCallTraceBuffer();
    if (!callTrace)
        return;

    auto* desc = _callTraceSection.descriptor;
    auto* header = static_cast<CallTraceHeader*>(
        getSectionData(_shmRegion.data(), desc));
    if (!header)
        return;

    bool sessionActive = tracker.IsCalltraceCapturing();
    header->session_active = sessionActive ? 1 : 0;
    header->cold_size = callTrace->ColdSize();
    header->hot_size = callTrace->HotSize();

    size_t totalEvents = callTrace->GetCount();
    if (totalEvents == 0)
    {
        header->entry_count = 0;
        return;
    }

    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Get recent cold buffer entries (newest first)
    uint32_t count = std::min(static_cast<uint32_t>(callTrace->ColdSize()), CALL_TRACE_SNAPSHOT_COUNT);
    std::vector<Z80ControlFlowEvent> events = callTrace->GetLatestCold(count);

    header->entry_count = static_cast<uint32_t>(events.size());

    // Convert to flat POD entries
    auto* entries = reinterpret_cast<CallTraceEntryPOD*>(
        reinterpret_cast<char*>(header) + sizeof(CallTraceHeader));
    for (size_t i = 0; i < events.size(); i++)
    {
        const auto& ev = events[i];
        entries[i].m1_pc = ev.m1_pc;
        entries[i].target_addr = ev.target_addr;
        std::memcpy(entries[i].opcode_bytes, ev.opcode_bytes.data(), 4);
        entries[i].opcode_len = ev.opcode_len;
        entries[i].flags = ev.flags;
        entries[i].type = static_cast<uint8_t>(ev.type);
        entries[i].reserved1 = 0;
        entries[i].sp = ev.sp;
        entries[i].stack_top[0] = ev.stack_top[0];
        entries[i].stack_top[1] = ev.stack_top[1];
        entries[i].stack_top[2] = ev.stack_top[2];
        entries[i].loop_count = ev.loop_count;
    }

    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Logger State Snapshot
// =============================================================================

void MonitoringManager::snapshotLoggerState()
{
    if (!_loggerStateSection.descriptor || !_context->pModuleLogger)
        return;

    auto* desc = _loggerStateSection.descriptor;
    auto* snapshot = static_cast<LoggerStateSnapshot*>(
        getSectionData(_shmRegion.data(), desc));
    if (!snapshot)
        return;

    ModuleLogger* logger = _context->pModuleLogger;
    const LoggerSettings& settings = logger->GetSettings();

    // Signal UPDATING to observers
    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Copy module/submodule enable state
    snapshot->module_enable_mask = settings.modules;
    std::memcpy(snapshot->submodule_masks, settings.submodules, sizeof(snapshot->submodule_masks));
    std::memcpy(snapshot->module_levels, settings.moduleLevels, sizeof(snapshot->module_levels));
    snapshot->global_level = static_cast<uint8_t>(logger->GetLevel());
    snapshot->muted = 0;  // muted state not exposed in public API currently
    std::memset(snapshot->reserved, 0, sizeof(snapshot->reserved));

    // Commit with new epoch
    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

void MonitoringManager::snapshotMemoryPages()
{
    if (!_memoryPagesSection.descriptor || !_context->pMemory)
        return;

    auto* desc = _memoryPagesSection.descriptor;
    auto* header = static_cast<MemoryPagesSectionHeader*>(
        getSectionData(_shmRegion.data(), desc));
    if (!header)
        return;

    Memory* mem = _context->pMemory;
    const uint8_t* memBase = mem->GetMemoryBase();
    if (!memBase)
        return;

    // Signal UPDATING to observers
    desc->epoch.store(EPOCH_UPDATING, std::memory_order_release);

    // Write header metadata
    header->page_count = MEMORY_MAX_PAGES;
    header->page_size = MEMORY_PAGE_SIZE;
    header->ram_pages = MAX_RAM_PAGES;
    header->rom_pages = MAX_ROM_PAGES;

    // Current bank mapping
    for (int i = 0; i < 4; i++)
    {
        header->bank_pages[i] = mem->GetPageForBank(static_cast<uint8_t>(i));
    }

    // Copy full page array (RAM+CACHE+MISC+ROM)
    char* pageData = reinterpret_cast<char*>(header) + sizeof(MemoryPagesSectionHeader);
    std::memcpy(pageData, memBase, static_cast<size_t>(MEMORY_MAX_PAGES) * MEMORY_PAGE_SIZE);

    // Commit with new epoch
    uint64_t epoch = _manifest->global_epoch.load(std::memory_order_relaxed) + 1;
    desc->epoch.store(epoch, std::memory_order_release);
}

// =============================================================================
// Control Command Processing
// =============================================================================

void MonitoringManager::processControlCommands()
{
    if (!_controlSection.descriptor)
        return;

    auto* controlBlock = static_cast<ControlBlock*>(
        getSectionData(_shmRegion.data(), _controlSection.descriptor));
    if (!controlBlock)
        return;

    uint32_t readIdx = controlBlock->read_idx.load(std::memory_order_relaxed);
    uint32_t writeIdx = controlBlock->write_idx.load(std::memory_order_acquire);

    while (readIdx != writeIdx)
    {
        auto& cmd = controlBlock->commands[readIdx % CONTROL_CMD_SLOTS];
        auto type = static_cast<ControlCommandType>(cmd.type.load(std::memory_order_acquire));

        if (type == ControlCommandType::NONE || cmd.processed.load(std::memory_order_relaxed))
        {
            readIdx++;
            continue;
        }

        switch (type)
        {
            case ControlCommandType::ENABLE_SECTION:
                if (cmd.section_index < _sections.size() && _sections[cmd.section_index].descriptor)
                {
                    _sections[cmd.section_index].descriptor->flags |= SECTION_ENABLED;
                }
                break;

            case ControlCommandType::DISABLE_SECTION:
                if (cmd.section_index < _sections.size() && _sections[cmd.section_index].descriptor)
                {
                    _sections[cmd.section_index].descriptor->flags &= ~SECTION_ENABLED;
                }
                break;

            case ControlCommandType::PING:
                // Just mark as processed — observer uses this to check liveness
                break;

            case ControlCommandType::SET_LOG_LEVEL:
            {
                // Set minimum log level for a category pattern
                std::string pattern(cmd.pattern, strnlen(cmd.pattern, sizeof(cmd.pattern)));
                auto level = static_cast<emu::log::Level>(cmd.log_level);
                emu::log::LoggerRegistry::instance().setEnabled(pattern, true, level);
                break;
            }

            case ControlCommandType::ENABLE_LOG_CATEGORY:
            {
                std::string pattern(cmd.pattern, strnlen(cmd.pattern, sizeof(cmd.pattern)));
                auto level = cmd.log_level > 0
                    ? static_cast<emu::log::Level>(cmd.log_level)
                    : emu::log::Level::Trace;
                emu::log::LoggerRegistry::instance().setEnabled(pattern, true, level);
                break;
            }

            case ControlCommandType::DISABLE_LOG_CATEGORY:
            {
                std::string pattern(cmd.pattern, strnlen(cmd.pattern, sizeof(cmd.pattern)));
                emu::log::LoggerRegistry::instance().setEnabled(pattern, false);
                break;
            }

            case ControlCommandType::LIST_CATEGORIES:
            {
                // Serialize category list into the control block's response area
                size_t written = emu::log::LoggerRegistry::instance().serializeCategoryList(
                    controlBlock->response_data, sizeof(controlBlock->response_data));
                (void)written;
                controlBlock->response_epoch.fetch_add(1, std::memory_order_release);
                break;
            }

            // ── ModuleLogger commands ──────────────────────────────────

            case ControlCommandType::SET_MODULE_ENABLED:
            {
                if (_context && _context->pModuleLogger)
                {
                    auto module = static_cast<PlatformModulesEnum>(cmd.section_index);
                    uint16_t submodule;
                    std::memcpy(&submodule, cmd.pattern, sizeof(uint16_t));
                    bool enabled = cmd.reserved != 0;

                    if (enabled)
                        _context->pModuleLogger->TurnOnLoggingForModule(module, submodule);
                    else
                        _context->pModuleLogger->TurnOffLoggingForModule(module, submodule);
                }
                break;
            }

            case ControlCommandType::SET_MODULE_LOG_LEVEL:
            {
                if (_context && _context->pModuleLogger)
                {
                    auto level = static_cast<LoggerLevel>(cmd.log_level);
                    if (cmd.section_index == 0)
                    {
                        // Global level
                        _context->pModuleLogger->SetLoggingLevel(level);
                    }
                    else
                    {
                        // Per-module level
                        auto module = static_cast<PlatformModulesEnum>(cmd.section_index);
                        _context->pModuleLogger->SetModuleLogLevel(module, level);
                    }
                }
                break;
            }

            case ControlCommandType::LIST_MODULES:
            {
                if (_context && _context->pModuleLogger)
                {
                    size_t written = _context->pModuleLogger->SerializeSettings(
                        controlBlock->response_data, sizeof(controlBlock->response_data));
                    (void)written;
                    controlBlock->response_epoch.fetch_add(1, std::memory_order_release);
                }
                break;
            }

            default:
                break;
        }

        cmd.processed.store(true, std::memory_order_release);
        readIdx++;
    }

    controlBlock->read_idx.store(readIdx, std::memory_order_release);

    // Stale command protection: if write_idx has advanced beyond read_idx
    // for too many frames, the observer may have crashed. Reset the ring.
    uint32_t finalReadIdx = controlBlock->read_idx.load(std::memory_order_relaxed);
    uint32_t finalWriteIdx = controlBlock->write_idx.load(std::memory_order_relaxed);
    if (finalWriteIdx != finalReadIdx)
    {
        _staleCmdCounter++;
        if (_staleCmdCounter > STALE_COMMAND_FRAMES)
        {
            // Reset: mark all commands as processed and sync indices
            for (auto& cmd : controlBlock->commands)
            {
                cmd.processed.store(true, std::memory_order_relaxed);
            }
            controlBlock->read_idx.store(finalWriteIdx, std::memory_order_release);
            _staleCmdCounter = 0;
        }
    }
    else
    {
        _staleCmdCounter = 0;
    }
}

// =============================================================================
// Observer Signaling
// =============================================================================

void MonitoringManager::signalObservers()
{
    _frameNotifier.signal();
}

// =============================================================================
// SHM Name Generation
// =============================================================================

std::string MonitoringManager::generateShmName() const
{
    std::string instanceId;

    if (_context && _context->pEmulator)
    {
        instanceId = _context->pEmulator->GetUUID().toString();
        // Use last 12 characters (still unique within process)
        if (instanceId.length() > 12)
        {
            instanceId = instanceId.substr(instanceId.length() - 12);
        }
    }
    else
    {
        instanceId = std::to_string(getpid());
    }

#ifdef _WIN32
    return "Local\\unreal_monitor_" + instanceId;
#else
    return "/unreal_monitor_" + instanceId;
#endif
}

// =============================================================================
// SHM Layout Calculation
// =============================================================================

uint64_t MonitoringManager::calculateInitialShmSize() const
{
    uint64_t size = 0;

    // ManifestHeader
    size += sizeof(ManifestHeader);

    // Section descriptors (initial capacity)
    size += INITIAL_MAX_SECTIONS * sizeof(SectionDescriptor);

    // Align data region to 64 bytes
    size = (size + 63) & ~63ULL;

    // Z80 state section
    size += sizeof(Z80StateSnapshot);

    // Control block
    size += sizeof(ControlBlock);

    // LOG_STREAM section: SPSC ring buffer header + data area
    size += emu::SPSCRingBuffer::totalMemoryRequired(LOG_STREAM_DEFAULT_SIZE);

    // AY-8910 state sections (2 chips for TurboSound)
    size += 2 * sizeof(AYStateSnapshot);

    // WD1793 FDC state section
    size += sizeof(FDCStateSnapshot);

    // Input state section
    size += sizeof(InputStateSnapshot);

    // Framebuffer section (worst-case allocation)
    size += FRAMEBUFFER_MAX_SIZE;

    // Audio buffer section (header + PCM data)
    size += sizeof(AudioBufferSectionHeader) + 3528;

    // Z80 heatmap section (768KB)
    size += HEATMAP_Z80_DATA_SIZE;

    // Page-level heatmap section (~4KB)
    size += HEATMAP_PAGES_DATA_SIZE;
    size += HEATMAP_PHYS_DATA_SIZE;

    // Opcode trace section (header + 1024 entries × 16B)
    size += OPCODE_TRACE_DATA_SIZE;

    // Call trace section (header + 512 entries × 32B)
    size += CALL_TRACE_DATA_SIZE;

    // Logger state section (64B)
    size += sizeof(LoggerStateSnapshot);

    // Memory pages section (~5MB)
    size += MEMORY_PAGES_DATA_SIZE;

    // Add padding for alignment between sections (64B per section × ~13 sections)
    size += 1600;

    return size;
}

void MonitoringManager::writeManifestHeader(size_t totalSize)
{
    _manifest = static_cast<ManifestHeader*>(_shmRegion.data());

    _manifest->magic = MANIFEST_MAGIC;
    _manifest->version = MANIFEST_VERSION;
    _manifest->header_size = sizeof(ManifestHeader);
    _manifest->total_size = totalSize;
    _manifest->emulator_pid = static_cast<uint32_t>(getpid());
    _manifest->flags = 0;
    _manifest->section_count = 0;
    _manifest->max_sections = INITIAL_MAX_SECTIONS;
    _manifest->descriptor_size = sizeof(SectionDescriptor);

    // Calculate data region offset (after header + all descriptor slots)
    uint32_t dataOffset = sizeof(ManifestHeader) + INITIAL_MAX_SECTIONS * sizeof(SectionDescriptor);
    dataOffset = (dataOffset + 63) & ~63u;  // Align to 64 bytes
    _manifest->data_region_offset = dataOffset;

    // Initialize atomics
    _manifest->global_epoch.store(0, std::memory_order_relaxed);
    _manifest->frame_counter.store(0, std::memory_order_relaxed);
    _manifest->layout_epoch.store(0, std::memory_order_relaxed);

    std::memset(_manifest->reserved, 0, sizeof(_manifest->reserved));
}

MonitoringManager::SectionRuntime MonitoringManager::registerSection(
    SectionType type, const char* name, uint64_t dataSize, uint16_t flags)
{
    if (!_manifest || _manifest->section_count >= _manifest->max_sections)
    {
        return {};
    }

    uint16_t index = _manifest->section_count;

    // Get descriptor pointer
    auto* desc = getDescriptor(_shmRegion.data(), _manifest, index);
    if (!desc)
    {
        // section_count hasn't been incremented yet; manually calculate position
        auto* base = static_cast<char*>(_shmRegion.data());
        desc = reinterpret_cast<SectionDescriptor*>(
            base + _manifest->header_size + index * _manifest->descriptor_size);
    }

    // Calculate data offset: after the current data region end
    uint32_t dataOffset = _manifest->data_region_offset;
    for (uint16_t i = 0; i < index; i++)
    {
        auto* existing = getDescriptor(_shmRegion.data(), _manifest, i);
        if (existing && existing->offset > 0)
        {
            uint32_t end = existing->offset + existing->length;
            end = (end + 63) & ~63u;  // Align next section
            if (end > dataOffset)
                dataOffset = end;
        }
    }
    dataOffset = (dataOffset + 63) & ~63u;

    // Write descriptor
    desc->type = type;
    desc->flags = flags;
    desc->offset = dataOffset;
    desc->length = static_cast<uint32_t>(dataSize);
    desc->reserved1 = 0;
    desc->epoch.store(0, std::memory_order_relaxed);

    std::memset(desc->name, 0, sizeof(desc->name));
    std::strncpy(desc->name, name, sizeof(desc->name) - 1);
    std::memset(desc->shm_name, 0, sizeof(desc->shm_name));

    // Increment section count
    _manifest->section_count = index + 1;
    _manifest->layout_epoch.fetch_add(1, std::memory_order_release);

    // Track in runtime list
    SectionRuntime runtime;
    runtime.descriptor = desc;
    runtime.index = index;
    _sections.push_back(runtime);

    return runtime;
}
