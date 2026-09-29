#pragma once

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/zxpoly/zxpolyscreencomposer.h"

#include <array>
#include <bitset>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Emulator;
class EmulatorContext;
class ZXPolyPortInterceptor;
struct ZXPSnapshot;

/// A ZX-Poly machine built from four stock emulator instances of one model
/// (docs/inprogress/2026-09-27-zxpoly/quad-instance-architecture.md,
/// model-agnostic-sync-layer.md).
///
/// Instance 0 is the master: it owns input, sound and the display. Instances
/// 1..3 are slaves. Two phases:
///
///  - Loader (#3D00 unlocked): only the master runs; the slaves are parked in
///    WAIT, as on the real platform after reset. A multiloader streams plane
///    data into them through the IO-mapped window; those writes are collected
///    per slave (overlay).
///  - Locked: set by the #3D00 write with the lock bit (with local reset), or
///    by loading a .zxp. At the lock the slaves become copies of the master
///    with their overlay on top and their own reset-command PC; from then on
///    all four run the same frames and differ only in graphics data.
///
/// Two ways to drive it:
///  - RunFrame(): the caller's thread steps all four, one frame at a time (the
///    "sequential" pipeline, the reference mode for tests);
///  - AttachToMaster(): the master runs as a normal instance (its own loop,
///    pacing, sound, the GUI adopts it); at the end of each of its frames the
///    group runs the slaves' frame and composes the picture into the master's
///    framebuffer. Host keys sent to the master are queued by the group.
class ZXPolyGroup : public Observer
{
public:
    static constexpr size_t MODULES = 4;

    /// What differs between the master and a slave at a frame boundary
    struct Divergence
    {
        bool diverged = false;
        size_t module = 0;
        std::string what;
    };

public:
    explicit ZXPolyGroup(std::string symbolicPrefix = "zxpoly");
    ~ZXPolyGroup();

    ZXPolyGroup(const ZXPolyGroup&) = delete;
    ZXPolyGroup& operator=(const ZXPolyGroup&) = delete;

    /// Creates the four instances of `model` (any creatable model name).
    /// Slave audio is muted; host key input is gated on every member
    bool Create(const std::string& model, std::string* error = nullptr);

    /// Removes the instances from the emulator manager
    void Destroy();

    bool IsCreated() const { return _instances[0] != nullptr; }
    Emulator* GetInstance(size_t module) const { return _instances[module].get(); }
    EmulatorContext* GetContext(size_t module) const;

    /// Loads a .zxp: module i into instance i, then applies #3D00 (video
    /// mode). The group is locked afterwards
    bool LoadZXP(const std::string& path, std::string* error = nullptr);

    /// Loads a ZX-Poly ROM image (.prom: up to four 16K parts, module i gets
    /// part i mod N as its only ROM) and powers the machine on: every CPU at
    /// #0000, #3D00 = 0 - the slaves wait until the ROM releases them
    bool LoadPROM(const std::string& path, std::string* error = nullptr);

    /// Mounts a disk image in drive A of every member and boots it on the
    /// master through TR-DOS (loader phase). The ZX-Poly multiloader on the
    /// disk then fills the slaves and locks the machine
    bool BootDisk(const std::string& path, std::string* error = nullptr);

    /// Loads the machine's media by extension: .zxp snapshot, .prom ROM image,
    /// anything else as a disk image booted through TR-DOS. An empty path
    /// leaves the machine as created (the master runs its ROM, slaves wait)
    bool LoadMedia(const std::string& path, std::string* error = nullptr);

    /// Copies the master's machine state into every slave: CPU registers,
    /// frame position, interrupt state, all RAM pages, paging latches, TR-DOS
    /// session. Plane data is applied on top by the caller afterwards. Locks
    /// the group
    void ReplicateFromMaster();

    /// Keyboard input for the whole machine. Queued and applied to all four
    /// instances at the next frame boundary, so every CPU sees the change at
    /// the same T-state
    void PressKey(ZXKeysEnum key);
    void ReleaseKey(ZXKeysEnum key);

    /// Runs one frame: in the loader phase the master alone (the locking #3D00
    /// write replicates at that instruction), in the locked phase all four
    void RunFrame();

    /// Live mode: hook the group into the master's own frame loop (see the
    /// class comment) and take host keys addressed to the master. Call before
    /// the master is started
    void AttachToMaster();
    void DetachFromMaster();
    std::shared_ptr<Emulator> GetMaster() const { return _instances[0]; }

    /// Live display frame at double resolution: the master's framebuffer
    /// (border included) scaled 2x with the composed 512 x 384 paper on top,
    /// so mode 5 shows its full resolution. Updated at every rendered master
    /// frame; the master's own framebuffer keeps a 1:1 picture for
    /// screenshots and recording
    unsigned GetDisplayWidth() const { return _displayWidth; }
    unsigned GetDisplayHeight() const { return _displayHeight; }
    const uint32_t* GetDisplayBuffer() const { return _displayFront.data(); }

    /// Copies the latest display frame (tear-free); false when dstSize does
    /// not match the current display size
    bool CopyDisplay(uint8_t* dst, size_t dstSize);
    void RunFrames(unsigned frames);

    bool IsLocked() const { return _locked; }

    /// Locked machine: after the master's frame the three slaves run their
    /// frame on worker threads at the same time (they share nothing while
    /// locked). Used only while every slave's device writes are disabled
    /// (R0 D4), so no two slaves drive the master's devices concurrently.
    /// On by default; the results are identical either way
    void SetParallelSlaves(bool parallel) { _parallelSlaves = parallel; }
    bool IsParallelSlaves() const { return _parallelSlaves; }

    /// Compares every slave with the master (locked phase only): port reads
    /// of the last frame (when the check is enabled), then the control state -
    /// PC, SP, I, IM, IFF1, HALT, T-state position, paging. Data registers are
    /// not compared: they legitimately hold plane-specific graphics bytes
    Divergence CheckLockstep() const;

    /// Debug aid: record every IN of every instance per frame and report the
    /// first read where a slave saw a different port, T-state or value than
    /// the master. Slows execution (installs the CPU bus trace hook)
    void EnablePortReadCheck(bool enable);

    /// Debug aid: record the PC of every executed instruction per frame and
    /// report the first instruction where a slave's control flow left the
    /// master's, with the master's preceding instructions
    void EnableInstructionTrace(bool enable);

    /// ZX-Poly video mode (#3D00 D2-D4)
    uint8_t GetVideoMode() const { return static_cast<uint8_t>((_port3D00 >> 2) & 0x07u); }
    void SetVideoMode(uint8_t mode);
    uint8_t GetPort3D00() const { return _port3D00; }

    /// Group TTD (quad-instance-architecture.md §8): four ordinary sessions
    /// started at the same frame, so TTD frame N is the same machine frame on
    /// every module. Group input is journaled into each module's session; the
    /// platform state (#3D00, R0-R3, lock) is kept per frame beside them
    bool StartRecording(std::string* error = nullptr);
    void StopRecording();
    bool IsRecording() const { return _recording; }

    /// TTD frame the group is at (session-relative; the master's position)
    uint64_t GetRecordedPosition() const;

    /// Seek every module to the start of TTD frame `frame` and restore the
    /// platform state of that moment. Recording pauses (the history stays;
    /// ResumeRecording continues from here); a live master is left paused
    bool SeekToFrame(uint64_t frame, std::string* error = nullptr);

    /// Continue recording from the current position: the recorded future
    /// (every module's and the platform history) is dropped - a new branch
    bool ResumeRecording(std::string* error = nullptr);

    /// The group as automation reports it (one source for every surface)
    struct Status
    {
        std::array<std::string, MODULES> memberIds;   // [0] = the master
        bool locked = false;
        bool slavesRunning = false;
        bool parallelSlaves = false;
        uint8_t port3D00 = 0;
        uint8_t videoMode = 0;
        std::array<std::array<uint8_t, 4>, MODULES> registers{};
        Divergence divergence;
    };
    Status GetStatus() const;

    /// Module register R0..R3 as last written
    uint8_t GetModuleRegister(size_t module, size_t reg) const { return _regs[module][reg]; }

    /// Number of bytes the loader wrote into a slave through the IO window
    size_t GetOverlayBytes(size_t module) const;

    /// The 6912-byte screen each module currently displays (#7FFD D3 selects
    /// page 5 or 7)
    const uint8_t* GetScreenMemory(size_t module) const;

    /// Composes the current picture (512 x 384, framebuffer pixel format)
    void Compose(std::vector<uint32_t>& out);

    /// Platform port access from a member's CPU (ZXPolyPortInterceptor).
    /// Return true when the access is consumed
    bool OnPortIn(size_t module, uint16_t port, uint8_t& value);
    bool OnPortOut(size_t module, uint16_t port, uint8_t value);
    void OnPortInResult(size_t module, uint16_t port, uint8_t& value, bool fromFloatingBus);

private:
    std::string _prefix;
    std::array<std::shared_ptr<Emulator>, MODULES> _instances{};
    std::array<std::unique_ptr<ZXPolyPortInterceptor>, MODULES> _interceptors{};

    // Platform state
    uint8_t _port3D00 = 0;
    std::array<std::array<uint8_t, 4>, MODULES> _regs{};
    bool _locked = false;
    bool _lockedThisFrame = false;   // the slaves start mid-frame, at the lock point
    uint64_t _lastMasterFrame = 0;   // a smaller frame counter means the master was reset
    bool _slavesRunning = false;     // unlocked with #3D00 D0 = 1: the slaves execute on their own
    bool _parallelSlaves = true;
    std::array<bool, MODULES> _stopWait{};    // WAIT from the stop address (R2/R3)
    std::array<bool, MODULES> _wasHalted{};   // HALT edge for halt notification

    // Floating-bus reads of the master by (frame << 32 | T): the slaves take
    // the master's value - one video memory decides, as on the real board
    std::unordered_map<uint64_t, uint8_t> _masterFloatingBus;
    bool _attached = false;

    // Loader phase: what the IO window wrote into each slave (-1 = untouched),
    // indexed by RAM page * 16K + offset
    std::array<std::vector<int16_t>, MODULES> _overlay;

    std::mutex _displayMutex;
    std::vector<uint32_t> _displayFront;
    std::vector<uint32_t> _displayBack;
    unsigned _displayWidth = 0;
    unsigned _displayHeight = 0;

    // Host input queued for the next frame boundary (keys and the Kempston
    // mouse), applied to all four members at once
    struct InputOp
    {
        enum Kind : uint8_t { KeyDown, KeyUp, MouseMove, MouseButtons, MouseWheel } kind;
        ZXKeysEnum key = ZXKEY_NONE;
        int a = 0;
        int b = 0;
    };
    std::mutex _keysMutex;
    std::vector<InputOp> _pendingInput;

    struct PortRead
    {
        uint32_t t;
        uint16_t port;
        uint16_t pc;
        uint8_t value;
    };
    bool _portReadCheck = false;
    std::array<std::vector<PortRead>, MODULES> _portReads;

    bool _instructionTrace = false;
    std::array<std::vector<uint16_t>, MODULES> _instructions;

    // Per-line video capture (zxpoly renders a line once the beam passed it):
    // each module copies a paper line from its own screen page when its T
    // passes the end of that line's fetch. Two frames: filling / completed
    struct LineCapture
    {
        std::array<ZXPolyScreenComposer::Lines, 2> lines{};
        std::array<uint64_t, 2> frame{~0ull, ~0ull};
        std::array<std::bitset<192>, 2> captured;
        std::array<unsigned, 2> nextLine{};
        uint64_t currentFrame = ~0ull;
    };
    std::array<LineCapture, MODULES> _capture;

    // Group TTD: the platform state at the start of every recorded frame
    struct PlatformSnapshot
    {
        uint8_t port3D00 = 0;
        std::array<std::array<uint8_t, 4>, MODULES> regs{};
        bool locked = false;
        bool slavesRunning = false;
        std::array<bool, MODULES> stopWait{};
        std::array<bool, MODULES> wasHalted{};
    };
    bool _recording = false;
    std::map<uint64_t, PlatformSnapshot> _platformHistory;
    void SnapshotPlatform();

private:
    void ResetPlatformState();
    void DetectMasterReset();
    void MountMasterDisksOnSlaves();
    void OnMainPortWrite(uint8_t value);
    void CopyMasterState();
    void UpdateIntGates();
    void RaiseModuleInt(size_t module);
    void RaiseModuleNmi(size_t module);
    void OnModuleHalted(size_t module);
    uint16_t StopAddress(size_t module) const;
    void LocalReset(size_t module);
    void AlignSlaveClock(size_t module, unsigned extraT);
    void InstallMasterM1Hook();
    void CatchUpSlaves();
    void ResetSlaveMachines();
    uint8_t ModuleStatus(size_t module) const;
    uint8_t* ModuleRam(size_t module, uint16_t address) const;
    void InstallSlaveM1Hooks();
    void CaptureLines(size_t module);
    void CopyScreenLine(size_t module, unsigned line, ZXPolyScreenComposer::Lines& lines) const;
    void FillMissingLines(size_t module, size_t slot);
    const ZXPolyScreenComposer::Lines* CapturedFrame(size_t module, uint64_t frame);
    bool OnPort7FFDWrite(size_t module, uint8_t value);
    void ApplyPendingInput();
    void AdvanceSlaves();
    void RunSlaveToMasterPosition(size_t module);
    void OnMasterFrameEnd(bool rendered);
    void ComposeIntoMasterFramebuffer();
    void ComposeDisplayFrame();
    void ResizeDisplay(unsigned width, unsigned height);
    void OnHostKeyPressed(int id, Message* message);
    void OnHostKeyReleased(int id, Message* message);
    void QueueHostKey(Message* message, bool pressed);
    void OnHostMouse(int id, Message* message);
    void QueueInput(const InputOp& op);
    void PerformLock();
    uint16_t ResetCommandTarget(size_t module) const;
    size_t WindowOffset(size_t module, uint16_t address) const;
    bool IsTRDOSActive(size_t module) const;
    uint8_t ModuleIdentity(size_t module) const;
};
