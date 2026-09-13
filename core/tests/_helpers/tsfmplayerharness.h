#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Emulator;
class Z80;

/// TurboSound FM player harness (TSFM implementation plan, P0).
///
/// Pokes the TFM Music Compiler 1.12 player from testdata/sound/tsfm/TSFM-EL.TAP
/// into Pentagon RAM and runs it exactly as on real hardware: entering at 25000
/// executes init(HL = tune) once, then the `ei / halt` main loop calls the
/// per-frame routine on every interrupt. All #FFFD/#BFFFD writes are captured
/// via the Z80 bus trace hook, so later phases (P4/P5/P8) can compare device
/// behaviour against recorded player traffic and detect hangs.
///
/// Entry points, memory layout and the observed port protocol are recorded in
/// docs/inprogress/2026-09-10-turbosound-fm/verification/player-entry-points.md
class TsfmPlayerHarness
{
public:
    /// Entry points inside the `_tsfmplaye` block (loaded at 25000)
    static constexpr uint16_t PLAYER_BASE = 25000;   // 0x61A8: ld hl,tune; call init; ei; halt loop
    static constexpr uint16_t ENTRY_INIT = 25003;    // 0x61CC: init trampoline, HL = tune address
    static constexpr uint16_t ENTRY_FRAME = 25007;   // 0x61CF: per-frame routine trampoline
    static constexpr uint16_t ENTRY_STOP = 25010;    // 0x61D2: mute sequence trampoline

    /// Block load addresses from the TAP headers
    static constexpr uint16_t TUNE_BASE = 32768;     // 0x8000: tune blocks load here (last, wins the overlap)
    static constexpr uint16_t LNXDATA_BASE = 31000;  // 0x7918: loader/graphics block, tail overlapped by the tune

    /// One captured Z80 port write
    struct PortWrite
    {
        uint16_t port;
        uint8_t value;
    };

    /// Load `_tsfmplaye`, `lnxdata` and tune number tuneIndex (0-based, skipping
    /// the non-"TFMcom1.12" terminator block) from TSFM-EL.TAP; poke them in the
    /// hardware load order (player, lnxdata, tune - the tune overwrites the
    /// lnxdata tail at 0x8000, exactly as the MB03+ loader leaves memory);
    /// install the port tracer; enter the player main loop.
    /// Requires a freshly created PENTAGON emulator (all target ranges are RAM
    /// in the default paging).
    /// @param errorMessage optional failure description
    /// @return false when the TAP is missing or the requested blocks are malformed
    bool Setup(Emulator* emulator, size_t tuneIndex, std::string* errorMessage = nullptr);

    /// Run frameCount frames through the emulator main loop; the player advances
    /// one frame per interrupt. Updates all traffic statistics.
    void RunFrames(int frameCount);

    /// Detach the port tracer (restores any previous hook). Called by TearDown.
    void Detach();

    // --- Traffic statistics (updated by RunFrames; valid after Setup) ---

    /// Every captured #FFFD/#BFFFD write, in execution order
    const std::vector<PortWrite>& GetWrites() const { return _writes; }

    /// Number of writes captured during each RunFrames(1) step, in frame order
    const std::vector<uint32_t>& GetPerFrameWriteCounts() const { return _perFrameCounts; }

    uint64_t GetWriteCount() const { return _writes.size(); }
    uint64_t GetChip0WriteCount() const { return _chip0Writes; }    // writes to #FFFD
    uint64_t GetChip1WriteCount() const { return _chip1Writes; }    // writes to #BFFD

    /// Times the given control word (0xF8..0xFF) was written to #FFFD
    uint64_t GetControlWordCount(uint8_t controlWord) const;

    /// Consecutive frames ending without any TS port write. Stays 0 while the
    /// player produces output; a growing value means the player hung or the
    /// tune ended. Init runs during the first frame, so frame 0 is not counted.
    uint32_t GetFramesSinceLastWrite() const { return _framesSinceLastWrite; }

    /// Simple FNV-1a hash over the captured write sequence - lets tests compare
    /// traffic across emulator instances without holding two full sequences
    uint64_t GetTrafficHash() const;

    /// Human-readable tune identification ("03 DJ Tepp" header name)
    const std::string& GetTuneName() const { return _tuneName; }

private:
    void PokeBlock(uint16_t address, const std::vector<uint8_t>& bytes);
    void OnPortWrite(uint16_t port, uint8_t value);

    Emulator* _emulator = nullptr;
    Z80* _z80 = nullptr;
    std::string _tuneName;

    std::vector<PortWrite> _writes;
    std::vector<uint32_t> _perFrameCounts;
    uint64_t _chip0Writes = 0;
    uint64_t _chip1Writes = 0;
    uint64_t _controlWordCounts[8] = {};  // indexed by controlWord & 7 (0xF8..0xFF)
    uint32_t _framesSinceLastWrite = 0;
    size_t _frameStartWriteCount = 0;

    /// Previously installed hook (restored by Detach)
    std::function<void(char, uint16_t, uint8_t)> _previousHook;
    bool _hookInstalled = false;
};
