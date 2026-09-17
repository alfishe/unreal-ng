#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opl4/opl4.h"
#include "opl4/wavememory.h"

#include "common/modulelogger.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/audio.h"

class EmulatorContext;

/// Tier A TTD blob header (TTD integration 4.2): explicit fields only, no
/// implicit padding - blobs are copied and hashed byte-wise. The chip state
/// (the library's POD blob, fixed after Configure) follows the header
/// verbatim; streams, meters and render state are Tier C and never captured.
struct MoonSoundTTDHeader
{
    uint8_t magic[4];         // 'M','S','N','D'
    uint16_t layoutVersion;   // SoundChip_Moonsound::kTtdLayoutVersion
    uint16_t headerSize;      // sizeof(MoonSoundTTDHeader)
    uint8_t fmLatch[2];       // guest-visible FM address latches (TTD 7.2)
    uint8_t waveLatch;        // guest-visible wave address latch (TTD 7.2)
    uint8_t fmBank;           // guest-visible FM bank of the last address write (v2)
    uint16_t reserved1;
    uint16_t reserved2;
    uint64_t tstateOrigin;    // T-state axis origin (TTD 7.2)
    uint64_t lastChipTime;    // monotonic clamp memory
    uint64_t romHash;         // wave ROM content hash (D6; 0 = no image)
};

static_assert(sizeof(MoonSoundTTDHeader) == 40, "MoonSoundTTDHeader must stay 40 bytes");
static_assert(offsetof(MoonSoundTTDHeader, romHash) + sizeof(MoonSoundTTDHeader::romHash)
                  == sizeof(MoonSoundTTDHeader),
              "MoonSoundTTDHeader has implicit trailing padding - resize reserved[]");

/// ZXM-MoonSound (YMF278B / OPL4) expansion card - synthesis device.
///
/// Ports (integration design 2.1, full 16-bit decode per D4):
///   #C4  write  FM register address, bank 1
///   #C4  read   FM status (BUSY)
///   #C5  write  FM data, bank 1
///   #C6  write  FM register address, bank 2
///   #C7  write  FM data, bank 2
///   #7E  write  wave register address
///   #7F  write  wave register data
///   #7F  read   wave register data
///
/// Shared bus (2.2 revision): the design's conservative "exclusive claim"
/// reading does not hold in this code base. The model decode maps the card's
/// raw addresses onto other devices (A0=0 ports -> ULA #FE family, #C5/#C7 ->
/// #7FFD paging, #7F -> Beta-128 FDC data), so an exclusive registration
/// either steals the port from them (#7F killed TR-DOS reads in testing) or
/// never fires at all. A real card full-decodes and shares the bus: the device
/// registers as a full-decode observer tapped at the Z80 I/O funnel with the
/// raw port. Partial-decode devices still observe MoonSound cycles and vice
/// versa. READ priority defaults to the legacy device (R6: enabling MoonSound
/// must not alter the FDC data register or keyboard reads on the shared
/// addresses - empirically, wired-AND zeroed TR-DOS sector reads through
/// #007F). Resolved with the card sources (MoonService v0.3a by the card
/// author, 2016): the card DOES drive #7F over the FDC mirror once the guest
/// arms OPL4 NEW (FM2 reg 05) - the service reads the device ID and the wave
/// RAM window at #7F immediately after that write. portDeviceClaimsRead()
/// encodes exactly that arming rule, so TR-DOS traffic before any MoonSound
/// software runs stays byte-identical (NEW never set).
///
/// Time base (3.2): libopl4 needs ONE monotonic T-state axis for the whole
/// session, while the host z80->t is a per-frame counter the core rebases
/// every frame. The device therefore keeps an absolute origin that advances
/// by one frame duration at each frame start; absolute chip time is
///   _tstateOrigin + AudioTstate(z80->t) * HostSpeedMultiplier()
/// and every value handed to the library is clamped monotonic - a machine
/// switch or hard reset mid-session must never move chip time backwards.
class SoundChip_Moonsound : public PortDevice, public ttd::TTDSerializable
{
public:
    // The card's fixed I/O addresses (2.1). CPLD decodes A0..A7 only; the two
    // FM data ports are equivalent — the bank is the one selected by the most
    // recent address-port write, not a property of the data port itself.
    static constexpr uint16_t PORT_FM_ADDR1  = 0x00C4;  // FM addr latch, selects bank 1 / FM status read
    static constexpr uint16_t PORT_FM_DATA1  = 0x00C5;  // FM data write (bank of last addr write)
    static constexpr uint16_t PORT_FM_ADDR2  = 0x00C6;  // FM addr latch, selects bank 2
    static constexpr uint16_t PORT_FM_DATA2  = 0x00C7;  // FM data write (same as #C5)
    static constexpr uint16_t PORT_WAVE_ADDR = 0x007E;  // wave register addr latch (NEW2-gated)
    static constexpr uint16_t PORT_WAVE_DATA = 0x007F;  // wave register data write / read (NEW2-gated)

    SoundChip_Moonsound() = delete;
    explicit SoundChip_Moonsound(EmulatorContext* context, size_t coreRate = 44100);
    virtual ~SoundChip_Moonsound();

    // Registry buffers (one frame of interleaved stereo int16 per source, D5)
    int16_t* getFmBuffer() { return _fmBuffer; }
    const int16_t* getFmBuffer() const { return _fmBuffer; }
    int16_t* getPcmBuffer() { return _pcmBuffer; }
    const int16_t* getPcmBuffer() const { return _pcmBuffer; }

    // Channel taps (4.1): per-channel sources stay OUT of AudioSourceType -
    // the UI reaches the 18 FM + 24 PCM taps through these accessors.
    size_t channelCount(opl4::ChannelGroup group) const;
    void setChannelMute(opl4::ChannelGroup group, size_t index, bool mute);
    float channelPeak(opl4::ChannelGroup group, size_t index) const;

    // Frame lifecycle
    void reset();
    void handleFrameStart();

    /// @param expectedSamples exact per-frame sample count from the
    ///        SoundManager accumulator (same contract as Covox).
    void handleFrameEnd(size_t expectedSamples);

    /// Turbo/no-output mode (3.3): the synthesis core keeps running (D3),
    /// only the render stage is skipped and pending delivery audio is
    /// discarded.
    void setSynthesisSuppressed(bool suppressed);
    bool isSynthesisSuppressed() const { return _synthesisSuppressed; }

    /// Live core-rate change request. The library fixes its output rate at
    /// Configure() time, so the request is only recorded here; the render
    /// stage re-design is part of the render path work and must not reset
    /// chip state on a live reroute.
    void setCoreRate(size_t coreRate);
    size_t getCoreRate() const { return _coreRate; }

    // PortDevice interface (exact 16-bit match, D4)
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    /// Armed-card read claim (shared bus, see the class comment): the card
    /// drives #7F over the Beta-128 FDC mirror only while OPL4 NEW is armed
    /// (FM2 reg 05 bit 0) - the MoonService-verified rule. Every other port
    /// keeps the legacy-priority read (R6).
    bool portDeviceClaimsRead(uint16_t port) override;

    /// Bus attachment (the AY/TurboSound device pattern): the card owns its
    /// own bus interface - the six low-byte full-decode claims live with the
    /// PORT_* constants, not in the SoundManager router. SoundManager only
    /// routes the lifecycle call.
    bool attachToPorts(PortDecoder* decoder);
    void detachFromPorts();

    // Library access for tests / diagnostics (CUT pattern)
    opl4::Opl4& chip() { return _opl4; }
    const opl4::Opl4& chip() const { return _opl4; }
    opl4::WaveMemory& waveMemory() { return _waveMemory; }

    // Bytes of the wave ROM image actually loaded (0 = zero-filled region, D10)
    size_t waveRomLoadedBytes() const { return _waveRomLoadedBytes; }

    // TTDSerializable - Tier A (TTD integration 4.4): fixed POD blob, host
    // header + the library's chip state. Tier B (wave SRAM) is a paged-region
    // mechanism that does not exist yet: until it lands, TTD is explicitly
    // incomplete for this device rather than silently wrong (TTD 13.1).
    static constexpr uint16_t kTtdLayoutVersion = 2;

    /// Stack capacity for hash snapshots. The constructor checks the actual
    /// blob size against it, so a library state growth fails loudly at
    /// construction instead of silently disabling hashing later.
    static constexpr size_t kTtdBlobCapacity = 8192;

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "MoonSound"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::MoonSound; }
    uint64_t TTDHashState() const override;

private:
    /// This frame's duration on the 3.5 MHz T-state axis, in audio time
    /// units (config.frame * HostSpeedMultiplier - the same budget the
    /// SoundManager sample accumulator uses).
    uint64_t frameDuration() const;

    /// Absolute chip time of the current CPU position (3.2 time base).
    uint64_t currentChipTime() const;

    /// Clamp a raw time value monotonic and remember it (machine switches
    /// and hard resets must never walk the core backwards).
    uint64_t monotonicChipTime(uint64_t time);

    /// Current CPU position, clamped monotonic - the value every library
    /// call receives.
    uint64_t chipTimeNow();

    /// Load the configured wave ROM image into the wave memory (D10 / 6.2):
    /// resolve like every other ROM image, warn and zero-fill on miss.
    void loadWaveRom();

    EmulatorContext* _context;
    ModuleLogger* _logger = nullptr;

    // Bus attachment state (attachToPorts/detachFromPorts)
    PortDecoder* _portDecoder = nullptr;
    bool _chipAttachedToPortDecoder = false;

    // Synthesis core + wave memory. The ROM region is loaded from the
    // configured image at construction; a missing image leaves the
    // zero-filled region in place, which is the documented behaviour (D10).
    opl4::WaveMemory _waveMemory;
    opl4::Opl4 _opl4;

    // Wave ROM bytes actually loaded (diagnostics / tests)
    size_t _waveRomLoadedBytes = 0;

    // TTD Tier A: ROM content hash (D6) and the chip-state size fixed at
    // construction (R5: stable for the instance lifetime).
    uint64_t _romHash = 0;
    size_t _ttdChipStateSize = 0;

    // Registry frame buffers
    AudioFrameDescriptor _fmAudioDescriptor;
    int16_t* const _fmBuffer = reinterpret_cast<int16_t*>(_fmAudioDescriptor.memoryBuffer);
    AudioFrameDescriptor _pcmAudioDescriptor;
    int16_t* const _pcmBuffer = reinterpret_cast<int16_t*>(_pcmAudioDescriptor.memoryBuffer);

    // Render scratch (4.2): RenderSplit emits interleaved-stereo floats at
    // int16 scale, hard-bounded by the frames asked for, so one frame of
    // sizing matches the registry buffers.
    std::array<float, static_cast<size_t>(AUDIO_BUFFER_SAMPLES_PER_FRAME)> _fmScratch{};
    std::array<float, static_cast<size_t>(AUDIO_BUFFER_SAMPLES_PER_FRAME)> _pcmScratch{};

    // Guest-visible address latches (TTD replay-critical, 7.1)
    uint8_t _fmLatch[2] = {0, 0};
    // FM bank selected by the most recent address-port write (#C4 -> 0,
    // #C6 -> 1). Card-decode truth (2.1): both data ports #C5/#C7 deliver to
    // THIS bank - the author's own MBPlayer writes bank-2 data through #C5.
    uint8_t _fmBank = 0;
    uint8_t _waveLatch = 0;

    // Monotonic T-state axis (3.2)
    uint64_t _tstateOrigin = 0;
    uint64_t _lastChipTime = 0;

    size_t _coreRate;
    bool _synthesisSuppressed = false;
};
